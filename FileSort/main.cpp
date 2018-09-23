#define _CRT_SECURE_NO_WARNINGS true

#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <algorithm>        // sort()
#include <stdlib.h>         // rand()

#include "structs.h"
#include "worker.h"

#define FILENAME_IN  "unsorted.dmp"
#define FILENAME_OUT "sorted.dmp"
#define GENERATE_FILE
#define INPUT_FILE_LENGTH (1024*1024)  // for input file generation: number of elements generated
#define MAX_OPERATE_MEMORY (1024*64*10)
#define MAX_WORKERS 10

using namespace std;


/*FileInfo *gFiles = NULL;
int numOfFiles = 0;
int numOfElems = 0;
mutex gFilesMtx;
condition_variable gFilesProduced;*/

int main()
{
	int res;
	GlobalParams gParams;

#ifdef GENERATE_FILE
	{
		FILE *fp = fopen(FILENAME_IN, "wb");
		for (int i = 0; i < INPUT_FILE_LENGTH; i++)
		{
			unsigned int rndnum = 0;
			for (int k = 0; k < 4; k++, rndnum = (rndnum << 8) | (rand() & 0xFF));
			fwrite(&rndnum, sizeof(rndnum), 1, fp);
		}
		fclose(fp);
	}
#endif

	gParams.numOfWorkers = MAX_WORKERS;	
	gParams.memTotalSize = MAX_OPERATE_MEMORY;
	gParams.inFile.name = FILENAME_IN;
	gParams.outFile.name = FILENAME_OUT;
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

	WorkerClass **workers = new WorkerClass* [gParams.numOfWorkers];
		
	for (int i = 0; i < gParams.numOfWorkers; i++)
	{
		workers[i] = new WorkerClass(i, &gParams);// +memBlockSize * i, memBlockSize, gFiles);
	}

	for (int i = 0; i < gParams.numOfWorkers; i++)
	{
		workers[i]->thd.join();
		delete workers[i];
	}
	
	delete[] workers;
	return 0;

}



