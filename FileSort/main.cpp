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
#define GENERATE_FILE
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
	bsNone    = 0x0000,
	bsToSort  = 0x0001,
	bsToMerge = 0x0002,
	bsDeleted = 0x0004,
	bsDone    = 0x0008,
	bsReading = 0x0010,
	bsWriting = 0x0020,
};

/*enum BlockStates
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
};*/

typedef unsigned int SortData_t;

struct FileInfo
{
	string name;
	FILE *fp;
	unsigned int state;
	mutex mtx;
	int offset;
	int length;
	FileInfo() { name = ""; fp = NULL; state = bsNone; offset = 0; length = 0; };
};

enum WorkerStates
{
    wsIdle,
	wsSorting,
	wsSorted,
	wsMerging,
	wsMerged,
};


class WorkerClass
{
public:
	int ID;
	SortData_t *buffer;
	int buflen;
	int datalen;
	WorkerStates state;

	HANDLE  thrd;
	thread thd;
	mutex mtx;
	condition_variable cv;
	
	FileInfo *files;

	string  outFileName;
	FILE   *fpOut;

	int Work();
	int JobPreSort();
	int JobMerge();
	int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut);

	WorkerClass(int id, SortData_t *memBuffer, int memBufLen, FileInfo *fileArray)
	{
		ID = id; 
		state = wsIdle;
		thrd = NULL; 
		buffer = memBuffer; 
		buflen = memBufLen;
		datalen = 0;
		fpOut = NULL;
		thd = thread(Work());
	};
	~WorkerClass();

};


#define RAND_MAX 65536
FileInfo *gFiles = NULL;
int numOfFiles = 0;
mutex gFilesMtx;


/*int gCounter = 0;
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
}*/

/*void SortBlock(SortData_t *arr, int n, int id)
{
	char filename[0x100];

	sort(arr, arr + n);
	sprintf(filename, "%s_s0_%d", FILENAME_OUT, id);
	FILE *fp = fopen(filename, "wb");
	fwrite(arr, sizeof(SortData_t), n, fp);
	fclose(fp);
}*/

int WorkerClass::Work()
{
	if (files[0].state & bsDeleted)
	{
		JobMerge();
	}
	else
	{
		JobPreSort();
	}
}

int WorkerClass::JobPreSort()
{
	bool jobStop = false;
	FileInfo *fileSrcInfo = &files[0];
	gFilesMtx.lock();
	if (fileSrcInfo->state & bsToSort)
	{
		datalen = fread(buffer, sizeof(buffer[0]), buflen, fileSrcInfo->fp);
		if (datalen < buflen)
		{
			fileSrcInfo->state = bsDeleted;
			fclose(fileSrcInfo->fp);
			jobStop = true;
		}
	}
	gFilesMtx.unlock();
	if (jobStop) return 0;

	sort(buffer, buffer + buflen);

	FileInfo *fileDstInfo = NULL;
	gFilesMtx.lock();  	

	for (int f = 1; (f < numOfFiles) && (fileDstInfo == NULL); f++)
	{
		if (files[f].state == bsNone || files[f].state == bsDeleted)
			fileDstInfo = &files[f];
	}

	fileDstInfo->state = bsWriting;
	gFilesMtx.unlock();

	char fileSuffix[0x20];
	sprintf(fileSuffix, "_w%d", ID);
	fileDstInfo->name = fileSrcInfo->name + fileSuffix;
	fileDstInfo->fp = fopen(fileDstInfo->name.c_str(), "w");
	fwrite(buffer, sizeof(buffer[0]), datalen, fileDstInfo->fp);
	fclose(fileDstInfo->fp);

	fileDstInfo->state = bsToMerge;
	fileDstInfo->length = datalen;
	fileDstInfo->fp = NULL;

	// CV - signal that new file is ready
	return datalen;
}

int WorkerClass::JobMerge()
{
	FileInfo *fileSrc1Info = NULL;
	FileInfo *fileSrc2Info = NULL;
	FileInfo *fileDstInfo = NULL;
	gFilesMtx.lock();

	for (int f = 1; (f < numOfFiles) && (fileSrc1Info == NULL || fileSrc2Info == NULL); f++)
	{
		if (files[f].state & bsToMerge)
		{
			if (fileSrc1Info == NULL) fileSrc1Info = &files[f];
			else fileSrc2Info = &files[f];
			files[f].state = bsReading;
		}
	}
	if (fileSrc2Info == NULL)
	{
		fileSrc1Info->state = bsToMerge;
	}
	gFilesMtx.unlock();

	if (fileSrc1Info == NULL || fileSrc2Info == NULL) return -1;

	char fileName[0x100];
	sprintf(fileName, "%s-%s_w%d", fileSrc1Info->name.c_str(), strrchr(fileSrc2Info->name.c_str(),'w'), ID);

	outFileName = fileName;
	fpOut = fopen(outFileName.c_str(), "w");
	int datalen = MergeFiles(fileSrc1Info->fp, fileSrc2Info->fp, fpOut);
	
	gFilesMtx.lock();
	fileSrc1Info->name = outFileName;
	fileSrc1Info->length = datalen;
	fileSrc1Info->state = bsToMerge;
	fileSrc2Info->length = 0;
	fileSrc2Info->state = bsDeleted;
	gFilesMtx.unlock();

}


int WorkerClass::MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut)
{
	int sublen = buflen / 4;
	SortData_t *bufDst = buffer;
	SortData_t *bufSrc1 = buffer + 2 * sublen;
	SortData_t *bufSrc2 = buffer + 3 * sublen;
	int lenDst = sublen*2, lenSrc1 = 0, lenSrc2 = 0;
	int ptrDst = 0, ptrSrc1 = sublen, ptrSrc2 = sublen;
	//FILE *fpOut = fopen("tmp.dmp", "wb");
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

	//fclose(fpOut);
	return totalDone;
}


int main()
{
	FileInfo fileUnsorted;
	fileUnsorted.name = FILENAME_IN;
	fileUnsorted.state = bsNone;
#ifdef GENERATE_FILE
	{
	
		fileUnsorted.mtx.lock();
		fileUnsorted.fp = fopen(FILENAME_IN, "wb");
		fileUnsorted.state = bsWriting;
		fileUnsorted.mtx.unlock();
		for (int i = 0; i < INPUT_FILE_LENGTH; i++)
		{
			unsigned int rndnum = (rand() << 16) + rand();
			fwrite(&rndnum, sizeof(rndnum), 1, fileUnsorted.fp);
		}
		fileUnsorted.mtx.lock();
		fclose(fileUnsorted.fp);
		fileUnsorted.state = bsNone;
		fileUnsorted.mtx.unlock();
	}
#endif

	int numOfWorkers = MAX_WORKERS;
	
	int memBlockSize = (MAX_OPERATE_MEMORY) / sizeof(SortData_t) / numOfWorkers;
	SortData_t *buffer = new SortData_t[memBlockSize * numOfWorkers];
	if (buffer == NULL) return -1;

	WorkerClass **workers = new WorkerClass* [numOfWorkers];
	
	struct stat statIn;
	stat(FILENAME_IN, &statIn);
	int numOfElems = statIn.st_size / sizeof(SortData_t);
	int numOfFiles = numOfElems / memBlockSize;
	FileInfo *files = new FileInfo[numOfFiles];
	
	//for (int f = 1; f < numOfFiles; f++)
	
	files[0].name = fileUnsorted.name;
	files[0].state = bsToSort;
	files[0].length = numOfElems;
	files[0].fp = NULL;

	/*FILE *fp = fopen(FILENAME_IN, "rb");
	fread(buffer, sizeof(SortData_t), memBlockSize * numOfWorkers, fp);
	fclose(fp);*/

	for (int i = 0; i < numOfWorkers; i++)
	{
		//workers[i].ID = i;
		//workers[i].buffer = buffer + memBlockSize * i;
		//workers[i].buflen = memBlockSize;
		workers[i] = new WorkerClass(i, buffer + memBlockSize * i, memBlockSize, gFiles);
		/* (i == 0)
		{
			char filename[0x100];
			sprintf(filename, "%s_srt%d", FILENAME_OUT, i);
			FILE *fpIn1 = fopen(filename, "rb");
			sprintf(filename, "%s_srt%d", FILENAME_OUT, i+1);
			FILE *fpIn2 = fopen(filename, "rb");
			//MergeFiles(fpIn1, fpIn2, &workers[i]);
			fclose(fpIn1);
			fclose(fpIn2);
		}*/
		/*workers[i].thrd = CreateThread(NULL, 0, ThreadMain, &workers[i], 0, NULL);
		if (workers[i].thrd == NULL)
			printf("Error creating thread %d\n", i);*/
		//workers[i].thd = thread(SortBlock, /*reinterpret_cast<unsigned int*> (memblocks[i].ptr)*/workers[i].buffer, workers[i].buflen, i);
	}

	for (int i = 0; i < numOfWorkers; i++)
	{
		workers[i]->thd.join();
		delete workers[i];
	}
	
	delete[] buffer;
	delete[] workers;
	delete[] files;
	return 0;

}



