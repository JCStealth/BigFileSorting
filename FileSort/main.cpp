#define _CRT_SECURE_NO_WARNINGS true

#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <vector>
#include <algorithm>        // sort()
#include <stdlib.h>         // rand()
#define FILENAME_IN  "unsorted.dmp"
#define FILENAME_OUT "sorted.dmp"
//#define GENERATE_FILE
#define INPUT_FILE_LENGTH (1024*1024)  // for input file generation: number of elements generated
#define MAX_OPERATE_MEMORY (1024*64)
#define MAX_WORKERS 10
#define __WIN32

#ifdef __WIN32
#include <Windows.h>
#endif

using namespace std;

enum BlockStates
{
    mbsNone         = 0x0000,
	mbsLoaded       = 0x0001,
	mbsAssigned     = 0x0002,
	mbsSorting      = 0x0004,
	mbsSorted       = 0x0008,
	mbsMerging      = 0x0010,
	mbsMerged       = 0x0020,
    mbsGenerating   = 0x0040,
	mbsOpenedRead   = 0x0100,
	mbsOpenedWrite  = 0x0200,
	mbsDone         = 0x1000,
    mbsDeleted      = 0x8000
};

typedef unsigned int SortData_t;
/*struct MemBlockInfo
{
	SortData_t *ptr;
	int  size;
	int  worker;
	unsigned int state;
	MemBlockInfo() { ptr = NULL; size = 0; worker = -1; state = mbsNone; };

	int min;    // current not merged value
	int idx;    // current not merged index
};*/

struct FileInfo
{
	string name;
	FILE *fp;
	unsigned int state;
	mutex mtx;
	int offset;
	int length;
	FileInfo() { name = ""; fp = NULL; state = mbsNone; offset = 0; length = 0; };
};

enum WorkerStates
{
    wsIdle,
	wsSorting,
	wsSorted,
	wsMerging,
	wsMerged,
};

struct WorkerInfo
{
	int id;
	HANDLE  thrd;
	//MemBlockInfo *memBlock;
	SortData_t *buffer;
	int buflen;
	WorkerStates state;
	thread thd;
	mutex mtx;
	condition_variable cv;
	
	string  outFileName;
	FILE   *fpOut;

	WorkerInfo() { id = -1; thrd = NULL; buffer = NULL; buflen = 0; state = wsIdle; fpOut = NULL; };
};

int gCounter = 0;
DWORD WINAPI ThreadMain(LPVOID lpParameters)
{
	WorkerInfo *info = reinterpret_cast <WorkerInfo*> (lpParameters);
	info->state = wsSorting;

	for (int i = 0; i < 10; i++)
	{
		gCounter++;
		printf("Worker %d[%X] step %d: %d\n", info->id, info->thrd, i, gCounter);
		Sleep(400);
	}
	printf("Worker %d exit\n", info->id);
	info->state = wsSorted;
	ExitThread(0);
}

void SortBlock(SortData_t *arr, int n, int id)
{
	char filename[0x100];

	sort(arr, arr + n);
	sprintf(filename, "%s_s0_%d", FILENAME_OUT, id);
	FILE *fp = fopen(filename, "wb");
	fwrite(arr, sizeof(SortData_t), n, fp);
	fclose(fp);
}

int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut, WorkerInfo *info)
{
	int sublen = info->buflen / 4;
	SortData_t *bufDst = info->buffer;
	SortData_t *bufSrc1 = info->buffer + 2 * sublen;
	SortData_t *bufSrc2 = info->buffer + 3 * sublen;
	int lenDst = sublen*2, lenSrc1 = 0, lenSrc2 = 0;
	int ptrDst = 0, ptrSrc1 = sublen, ptrSrc2 = sublen;
	FILE *fpOut = fopen("tmp.dmp", "wb");
	int totalDone = 0;

	do
	{
		if (ptrSrc1 >= lenSrc1)
		{
			lenSrc1 = fread(bufSrc1, sizeof(SortData_t), sublen, fpIn1);
			if (lenSrc1 <= 0) ptrSrc1 = -1; else ptrSrc1 = 0;
		}
		if (ptrSrc2 >= lenSrc2)
		{
			lenSrc2 = fread(bufSrc2, sizeof(SortData_t), sublen, fpIn2);
			if (lenSrc2 <= 0) ptrSrc2 = -1; else ptrSrc2 = 0;
		}
		if (ptrDst >= lenDst)
		{
			fwrite(bufDst, sizeof(SortData_t), ptrDst, fpOut);
			totalDone += ptrDst;
			ptrDst = 0;
		}
		
		if(ptrSrc1 >= 0)
		  for (int min = (ptrSrc2 >= 0) ? bufSrc2[ptrSrc2] : 0; 
			 ((bufSrc1[ptrSrc1] < min) || (ptrSrc2 < 0)) && (ptrSrc1 < lenSrc1) && (ptrDst < lenDst); 
			 bufDst[ptrDst++] = bufSrc1[ptrSrc1++]);
		if(ptrSrc2 >= 0)
		  for (int min = (ptrSrc1 >= 0) ? bufSrc1[ptrSrc1] : 0; 
			 ((bufSrc2[ptrSrc2] < min) || (ptrSrc1 < 0))&& (ptrSrc2 < lenSrc2) && (ptrDst < lenDst); 
			 bufDst[ptrDst++] = bufSrc2[ptrSrc2++]);

	} while (ptrSrc1 >= 0 || ptrSrc2 >= 0);

	if (ptrDst > 0)
	{
		fwrite(bufDst, sizeof(SortData_t), ptrDst, fpOut);
		totalDone += ptrDst;
	}

	fclose(fpOut);
	return totalDone;
}

int JobMerge(WorkerInfo *info)
{
	

}

#define RAND_MAX 65536
FileInfo gInputFile;
FileInfo *gFiles = NULL;
int numOfFiles = 0;

int main()
{
	gInputFile.name = FILENAME_IN;
	gInputFile.state = mbsNone;
#ifdef GENERATE_FILE
	{
	
		gInputFile.mtx.lock();
		gInputFile.fp = fopen(FILENAME_IN, "wb");
		gInputFile.state |= (mbsGenerating | mbsOpenedWrite);
		gInputFile.mtx.unlock();
		for (int i = 0; i < INPUT_FILE_LENGTH; i++)
		{
			unsigned int rndnum = (rand() << 16) + rand();
			fwrite(&rndnum, sizeof(rndnum), 1, gInputFile.fp);
		}
		gInputFile.mtx.lock();
		fclose(gInputFile.fp);
		gInputFile.state = mbsNone;
		gInputFile.mtx.unlock();
	}
#endif

	int numOfWorkers = MAX_WORKERS;
	
	int memBlockSize = (MAX_OPERATE_MEMORY) / sizeof(SortData_t) / numOfWorkers;
	SortData_t *buffer = new SortData_t[memBlockSize * numOfWorkers];
	if (buffer == NULL) return -1;

	WorkerInfo *workers = new WorkerInfo[numOfWorkers];
	
	struct stat statIn;
	stat(FILENAME_IN, &statIn);
	int numOfElems = statIn.st_size / sizeof(SortData_t);
	int numOfFiles = numOfElems / memBlockSize;
	FileInfo *files = new FileInfo[numOfFiles];

	FILE *fp = fopen(FILENAME_IN, "rb");
	fread(buffer, sizeof(SortData_t), memBlockSize * numOfWorkers, fp);
	fclose(fp);

	for (int i = 0; i < numOfWorkers; i++)
	{
		workers[i].id = i;
		workers[i].buffer = buffer + memBlockSize * i;
		workers[i].buflen = memBlockSize;
		if (i == 0)
		{
			char filename[0x100];
			sprintf(filename, "%s_srt%d", FILENAME_OUT, i);
			FILE *fpIn1 = fopen(filename, "rb");
			sprintf(filename, "%s_srt%d", FILENAME_OUT, i+1);
			FILE *fpIn2 = fopen(filename, "rb");
			MergeFiles(fpIn1, fpIn2, &workers[i]);
			fclose(fpIn1);
			fclose(fpIn2);
		}
		/*workers[i].thrd = CreateThread(NULL, 0, ThreadMain, &workers[i], 0, NULL);
		if (workers[i].thrd == NULL)
			printf("Error creating thread %d\n", i);*/
		workers[i].thd = thread(SortBlock, /*reinterpret_cast<unsigned int*> (memblocks[i].ptr)*/workers[i].buffer, workers[i].buflen, i);
	}

	for (int i = 0; i < numOfWorkers; i++) workers[i].thd.join();

	
	delete[] buffer;
	delete[] workers;
	delete[] files;
	return 0;

}



