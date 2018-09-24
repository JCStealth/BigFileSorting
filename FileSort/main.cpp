#define _CRT_SECURE_NO_WARNINGS true

#include <thread>
#include <mutex>
#include <stdlib.h>         // rand()

#include "structs.h"
#include "worker.h"

#define FILENAME_IN  "unsorted.dmp"
#define FILENAME_OUT "sorted.dmp"
//#define INPUT_FILE_LENGTH (1024*1024)  // for input file generation: number of elements generated
#define MAX_OPERATE_MEMORY (1024*64*10)
#define MAX_WORKERS 10

using namespace std;

// 0 - ok, 1 - only generate input file, <0 - error
int ParseCommandLine(int argc, char *argv[], GlobalParams *gParams)
{
	int arg = 1;
	bool outFileSet = false;
	bool inFileGenerate = false;
	while (arg < argc)
	{

		int paramInt;
		char *pArgKey = argv[arg];
		if (pArgKey[0] == '-')
		{
			if (arg >= argc - 1)
			{
				fprintf(stderr, "Wrong input parameters\n");
				return -1;
			}
			switch (pArgKey[1])
			{
			case 'w':
				paramInt = atoi(argv[++arg]);
				if (paramInt > 0) gParams->numOfWorkers = paramInt;
				else printf("Bad number of workers: %s, using default: %d\n", argv[arg], gParams->numOfWorkers);
				break;
			case 'm':
				paramInt = atoi(argv[++arg]);
				if (paramInt > 0) gParams->memTotalSize = paramInt;
				else printf("Bad memory size: %s, using default: %d\n", argv[arg], gParams->memTotalSize);
				break;
			case 'i':
				gParams->inFile.name = argv[++arg];
				if (!outFileSet) gParams->inFile.name = gParams->inFile.name + "_sort";
				break;
			case 'o':
				gParams->outFile.name = argv[++arg];
				outFileSet = true;
				break;
			case 'g':
				if (arg <= argc - 2)
				{
					paramInt = atoi(argv[arg + 2]);
					if (paramInt > 0)
					{
						gParams->inFile.length = paramInt;
						gParams->inFile.name = argv[++arg];
						inFileGenerate = true;
						arg++;
					}
					else printf("Bad number of elements in file to generate: %s; generation ignored\n", argv[arg + 2]);
				}
				else printf("Too few parameters for generate input file: <filename> <num_of_elems>; generation ignored\n");
				break;
			default:
				fprintf(stderr, "Unknown input parameter: %s\n", argv[arg]);
				return -2;
				break;
			}
		}
		else
		{
			gParams->inFile.name = argv[++arg];
			if (!outFileSet) gParams->inFile.name = gParams->inFile.name + "_sort";
		}
	}

	return inFileGenerate ? 1 : 0;

}

int main(int argc, char *argv[])
{

	int res;
	GlobalParams gParams;
	gParams.numOfWorkers = MAX_WORKERS;
	gParams.memTotalSize = MAX_OPERATE_MEMORY;
	gParams.inFile.name = FILENAME_IN;
	gParams.outFile.name = FILENAME_OUT;

	res = ParseCommandLine(argc, argv, &gParams);
	if (res < 0) return res;

    if(res == 1)
	{
		FILE *fp = fopen(gParams.inFile.name.c_str(), "wb");
		for (int i = 0; i < gParams.inFile.length; i++)
		{
			unsigned int rndnum = 0;
			for (int k = 0; k < 4; k++, rndnum = (rndnum << 8) | (rand() & 0xFF));
			fwrite(&rndnum, sizeof(rndnum), 1, fp);
		}
		fclose(fp);
		return 1;
	}

	{
		struct stat statIn;
		stat(gParams.inFile.name.c_str(), &statIn);
		gParams.inFileSize = statIn.st_size;
	}
	res = gParams.SetValues();
	if (res < 0)
	{
		fprintf(stderr, "Error input parameters (%d)\n", res);
		return -1;
	}

	WorkerClass mainWorker(0, &gParams, 0);
	WorkerClass **workers = NULL;

	if (gParams.numOfWorkers > 1)
	{
		workers = new WorkerClass*[gParams.numOfWorkers];
		workers[0] = &mainWorker;
		for (int i = 1; i < gParams.numOfWorkers; i++)
			workers[i] = new WorkerClass(i, &gParams);
	}
	
	mainWorker.Work();

	if (gParams.numOfWorkers > 1)
	{
		for (int i = 1; i < gParams.numOfWorkers; i++)
		{
			workers[i]->thd.join();
			delete workers[i];
		}
		delete[] workers;
	}
	
	return 0;

}



