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

// 0 - ok, 1 - only generate input file, 2 - only calculate checksum of file, <0 - error
int ParseCommandLine(int argc, char *argv[], GlobalParams *gParams)
{
	bool outFileSet = false;
	bool inFileGenerate = false;
	bool checkSorted = false;
	for (int arg = 1; arg < argc; arg++)
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
				if (!outFileSet) gParams->outFile.name = gParams->inFile.name + "_sort";
				break;
			case 'o':
				gParams->outFile.name = argv[++arg];
				outFileSet = true;
				break;
			case 'd':
				gParams->deleteInterFiles = false;
				break;
			case 'c':
				checkSorted = true;
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
			gParams->inFile.name = argv[arg];
			if (!outFileSet) gParams->outFile.name = gParams->inFile.name + "_sort";
		}
	}

	if (checkSorted) return 2;
	if (inFileGenerate) return 1;
	return 0;
}

int main(int argc, char *argv[])
{

	int res;
	
	// глобальные параметры, загрузка default-значений
	GlobalParams gParams;	
	gParams.numOfWorkers = MAX_WORKERS;
	gParams.memTotalSize = MAX_OPERATE_MEMORY;
	gParams.inFile.name = FILENAME_IN;
	gParams.outFile.name = FILENAME_OUT;

	// загрузка в gParams значений из командной строки
	res = ParseCommandLine(argc, argv, &gParams);
	if (res < 0) return res;

    // если задана генерация входного файла - генерировать его и выйти
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

	// calculate checksum and exit
	// checksum calculated as XOR of all values in a file specified
	if (res == 2)
	{
		FILE *fp = fopen(gParams.inFile.name.c_str(), "rb");
		if (fp == NULL)
		{
			fprintf(stderr, "Input file not found: %s\n", gParams.inFile.name.c_str());
			return -1;
		}
		SortData_t lastVal = 0;
		SortData_t chkSum = 0;
		int i;
		int cntBlock = 0;
		int datalen = 0;
		int cntSortMiss = 0;
		int firstMissOffset = -1;
		gParams.buffer = new SortData_t[gParams.memTotalSize];
		SortData_t *buffer = gParams.buffer;
		do
		{
			datalen = fread(buffer, sizeof(gParams.buffer[0]), gParams.memTotalSize, fp);
			if (cntBlock == 0) chkSum = buffer[0];
			if (datalen > 0)
			{
				for (i = cntBlock ? -1 : 0; i < datalen - 1; i++)
				{
					if ( ((i < 0) ? lastVal : buffer[i]) > buffer[i + 1])
					{
						if (firstMissOffset < 0) firstMissOffset = (cntBlock * gParams.memTotalSize) + i;
						cntSortMiss++;
					}
					chkSum ^= buffer[i+1];
				}
				lastVal = buffer[i++];
			}
			//if ((datalen < gParams.memTotalSize) && (datalen >= 0)) fSorted = true;
			if (datalen == gParams.memTotalSize) cntBlock++;
		} while (datalen == gParams.memTotalSize);

		int lastElemIdx = cntBlock*gParams.memTotalSize + i;
		printf("File %s (%d elements [%d*%d+%d]) csum=%08X %s\n", gParams.inFile.name.c_str(), lastElemIdx, 
			cntBlock, gParams.memTotalSize, i,
			chkSum, (firstMissOffset < 0) ? "[SORTED]" : "[UNSORTED]");
		if (firstMissOffset >= 0)
			printf("first sort miss: idx=%d, total misses: %d\n", firstMissOffset, cntSortMiss);


		/*if (fSorted)
			printf("File %s (%d elements) is sorted; csum=%08X\n", gParams.inFile.name.c_str(), lastElemIdx + 1, chkSum);
		else
			printf("File %s unsorted: [%d]=%08X [%d]=%08X\n", gParams.inFile.name.c_str(), 
				lastElemIdx, (i < 0) ? lastVal : buffer[i], 
				lastElemIdx + 1, buffer[i + 1]);*/

		//delete[] gParams.buffer;
		//gParams.buffer = NULL;

		fclose(fp);
		return (firstMissOffset < 0) ? 0 : 1;
	}


	// дозаполнение gParams
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

	WorkerClass mainWorker(0, &gParams, 0);  // main-worker
	WorkerClass **workers = NULL;            // другие workers

	// запуск worker в других потоках
	if (gParams.numOfWorkers > 1)
	{
		workers = new WorkerClass*[gParams.numOfWorkers];
		workers[0] = &mainWorker;
		for (int i = 1; i < gParams.numOfWorkers; i++)
			workers[i] = new WorkerClass(i, &gParams);
	}
	
	// запуск worker в этом потоке
	mainWorker.Work();

	// ожидание (если надо) других потоков и освобождение ресурсов
    // (выходной файл формируется последним отработавшим потоком)
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



