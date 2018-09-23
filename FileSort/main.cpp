#define _CRT_SECURE_NO_WARNINGS true

#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <algorithm>        // sort()
#include <stdlib.h>         // rand()
#define FILENAME_IN  "unsorted.dmp"
#define FILENAME_OUT "sorted.dmp"
#define GENERATE_FILE
#define INPUT_FILE_LENGTH (1024*1024)  // for input file generation: number of elements generated
#define MAX_OPERATE_MEMORY (1024*64*10)
#define MAX_WORKERS 10

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
	bsWorking = (bsToSort|bsToMerge|bsReading|bsWriting),
};

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
	wsSort,
	wsMerge,
	wsSearch,
};


class WorkerClass
{
private:
	int GetFileBlkCntFromName(string fileName);

public:
	int ID;
	SortData_t *buffer;
	int buflen;
	int datalen;
	WorkerStates state;

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
		buffer = memBuffer; 
		buflen = memBufLen;
		datalen = 0;
		fpOut = NULL;
		files = fileArray;
		thd = thread(&WorkerClass::Work, this);
	};
	//~WorkerClass();

};


#define RAND_MAX 65536*1024
FileInfo *gFiles = NULL;
int numOfFiles = 0;
int numOfElems = 0;
mutex gFilesMtx;
condition_variable gFilesProduced;


int WorkerClass::GetFileBlkCntFromName(string fileName)
{
	int fileBlkCntIdx = 0;
	int fileBlkCnt = 0;
	fileBlkCntIdx = fileName.find_last_of('(');
	if (fileBlkCntIdx > 0)
	{
		string::size_type endSymIdx = 0;
		string fileBlkCntStr = fileName.substr(fileBlkCntIdx + 1);
		fileBlkCnt = stoi(fileBlkCntStr, &endSymIdx);
		if (fileBlkCntStr.substr(endSymIdx, 1) != ")")
			printf("Worker %d: error in file name: %s\n", ID, fileName.c_str());
	}
	else fileBlkCnt = 1;
	return fileBlkCnt;
}


int WorkerClass::Work()
{
	int totalPreSorted = 0;
	int totalSorted = 0;
	int elemCount;
	state = wsSearch;
	printf("Worker %d started\n", ID);

    while(state != wsIdle)
	{
		if (files[0].state & bsDeleted)
		{
			state = wsMerge;
			printf("Worker %d merging...\n", ID);
			elemCount = JobMerge();
			printf("Worker %d merged %d elements\n", ID, elemCount);
			if (elemCount > 0)
			{
				totalSorted += elemCount;
				gFilesProduced.notify_all();
			}
			else if(elemCount == 0) state = wsIdle;
		}
		else
		{
			state = wsSort;
			printf("Worker %d sorting...\n", ID);
			elemCount = JobPreSort();
			printf("Worker %d presorted %d elements\n", ID, elemCount);
			if (elemCount > 0)
			{
				totalPreSorted += elemCount;
				gFilesProduced.notify_all();    // signal that new file is ready
			}
		}
	}
	printf("Worker %d finished: %d elements presorted, %d elements merged\n", ID, totalPreSorted, totalSorted);
	return totalSorted;
}

int WorkerClass::JobPreSort()
{
	bool jobAct = true;
	FileInfo *fileSrcInfo = &files[0];
	int fileIdx;

	gFilesMtx.lock();
	//fileSrcInfo->fp = fopen(fileSrcInfo->name.c_str(), "rb");
	if (fileSrcInfo->state & bsToSort)
	{
		int fileSrcPtr = ftell(fileSrcInfo->fp);
		datalen = fread(buffer, sizeof(buffer[0]), buflen, fileSrcInfo->fp);
		if (datalen >= 0)
			printf("Worker %d read from %s: %d..%d\n", ID, files[0].name.c_str(), fileSrcPtr, fileSrcPtr + (datalen * sizeof(buffer[0])) - 1);
		else printf("Worker %d read from %s error %d\n", ID, files[0].name.c_str(), errno);
		if (datalen < buflen)
		{
			fileSrcInfo->state = bsDeleted;
			fclose(fileSrcInfo->fp); fileSrcInfo->fp = NULL;
			if (datalen == 0) jobAct = false;
		}
	}
	else
		jobAct = false;
	//fclose(fileSrcInfo->fp);
    //fileSrcInfo->fp = NULL;
	gFilesMtx.unlock();
	if (!jobAct) return 0;

	sort(buffer, buffer + buflen);

	FileInfo *fileDstInfo = NULL;
	gFilesMtx.lock();  	

	for (fileIdx = 1; (fileIdx < numOfFiles) && (fileDstInfo == NULL); fileIdx++)
	{
		if (files[fileIdx].state == bsNone || files[fileIdx].state == bsDeleted)
			fileDstInfo = &files[fileIdx];
	}

	if (fileDstInfo) fileDstInfo->state = bsWriting;
	gFilesMtx.unlock();

	char fileSuffix[0x20];
	sprintf(fileSuffix, "_%04X", fileIdx);
	outFileName = fileSrcInfo->name + fileSuffix;
	fpOut = fopen(outFileName.c_str(), "wb");
	int a = errno;
	if (fpOut == NULL)
	{
		fprintf(stderr, "Worker %d cannot create file %s (error %d)\n", ID, outFileName.c_str(),errno);
		return -1;
	}
	fileDstInfo->name = outFileName;
	fileDstInfo->fp = fpOut;
	fwrite(buffer, sizeof(buffer[0]), datalen, fileDstInfo->fp);
	fclose(fileDstInfo->fp);

	fileDstInfo->state = bsToMerge;
	fileDstInfo->length = datalen;
	fileDstInfo->fp = NULL;
	fpOut = NULL;
	printf("Worker %d produced: %s\n", ID, outFileName.c_str());

	return datalen;
}

int WorkerClass::JobMerge()
{
	FileInfo *fileSrc1Info = NULL;
	FileInfo *fileSrc2Info = NULL;
	int filesWorkingCount = 0;
	
	//gFilesMtx.lock();
	do
	{
		fileSrc1Info = NULL;
		fileSrc2Info = NULL;
		filesWorkingCount = 0;

		unique_lock <mutex> lock(gFilesMtx);

		for (int f = 1; (f < numOfFiles) && (fileSrc2Info == NULL); f++)
		{
			if (files[f].state & bsWorking) filesWorkingCount++;
			if (files[f].state & bsToMerge)
			{
				if (fileSrc1Info == NULL) fileSrc1Info = &files[f];
				else fileSrc2Info = &files[f];
				files[f].state = bsReading;
			}
		}
		if (filesWorkingCount <= 1) return 0;
		if (fileSrc2Info == NULL)
		{
			if (fileSrc1Info) fileSrc1Info->state = bsToMerge;
			gFilesProduced.wait(lock);
		}

	} while (fileSrc2Info == NULL);

	//gFilesMtx.unlock();

	if (fileSrc1Info->length + fileSrc2Info->length == files[0].length)
		outFileName = FILENAME_OUT;
	else
	  outFileName = fileSrc1Info->name.substr(0, fileSrc1Info->name.find_last_of('(')) + '(' +
		to_string(GetFileBlkCntFromName(fileSrc1Info->name) + GetFileBlkCntFromName(fileSrc2Info->name)) + ')';
	
	fileSrc1Info->fp = fopen(fileSrc1Info->name.c_str(), "rb");
	fileSrc2Info->fp = fopen(fileSrc2Info->name.c_str(), "rb");
	fpOut = fopen(outFileName.c_str(), "wb");
	int datalen = MergeFiles(fileSrc1Info->fp, fileSrc2Info->fp, fpOut);    
	fclose(fpOut); fpOut = NULL;
	fclose(fileSrc1Info->fp); 
	fclose(fileSrc2Info->fp); 

	printf("Worker %d merged: %s(%d) + %s(%d) -> %s(%d)\n", ID,
		fileSrc1Info->name.c_str(), fileSrc1Info->length,
		fileSrc2Info->name.c_str(), fileSrc2Info->length,
		outFileName.c_str(), datalen);
	gFilesMtx.lock();
	fileSrc1Info->fp = NULL; remove(fileSrc1Info->name.c_str()); 
	fileSrc2Info->fp = NULL; remove(fileSrc2Info->name.c_str());
	fileSrc1Info->name = outFileName;
	fileSrc1Info->length = datalen;
	fileSrc1Info->state = bsToMerge;
	fileSrc2Info->length = 0;
	fileSrc2Info->state = bsDeleted;
	gFilesMtx.unlock();
	
	return datalen;    
}


int WorkerClass::MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut)
{
	int sublen = buflen / 4;
	SortData_t *bufDst = buffer;
	SortData_t *bufSrc1 = buffer + 2 * sublen;
	SortData_t *bufSrc2 = buffer + 3 * sublen;
	int lenDst = sublen*2, lenSrc1 = 0, lenSrc2 = 0;
	int ptrDst = 0, ptrSrc1 = sublen, ptrSrc2 = sublen;
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
		  for (unsigned int min = (ptrSrc2 >= 0) ? bufSrc2[ptrSrc2] : 0; 
			 ((bufSrc1[ptrSrc1] <= min) || (ptrSrc2 < 0)) && (ptrSrc1 < lenSrc1) && (ptrDst < lenDst); 
			 bufDst[ptrDst++] = bufSrc1[ptrSrc1++]);
		if(ptrSrc2 >= 0)
		  for (unsigned int min = (ptrSrc1 >= 0) ? bufSrc1[ptrSrc1] : 0; 
			 ((bufSrc2[ptrSrc2] <= min) || (ptrSrc1 < 0))&& (ptrSrc2 < lenSrc2) && (ptrDst < lenDst); 
			 bufDst[ptrDst++] = bufSrc2[ptrSrc2++]);

	} while (ptrSrc1 >= 0 || ptrSrc2 >= 0);

	if (ptrDst > 0)
	{
		fwrite(bufDst, sizeof(SortData_t), ptrDst, fpOut);
		totalDone += ptrDst;
	}

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
			unsigned int rndnum = 0;
			for (int k = 0; k < 4; k++, rndnum = (rndnum << 8) | (rand() & 0xFF));
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
	numOfElems = statIn.st_size / sizeof(SortData_t);	
	numOfFiles = numOfElems / memBlockSize + 1;   // +1 - additional source file placed to gFiles[0] 
	if (numOfElems % memBlockSize) numOfFiles++;
	gFiles = new FileInfo[numOfFiles];
	
	FileInfo *fileSrc = &gFiles[0];
	fileSrc->name = fileUnsorted.name;
	fileSrc->state = bsToSort;
	fileSrc->length = numOfElems;
	
	fileSrc->fp = fopen(fileSrc->name.c_str(),"rb");

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
	delete[] gFiles;
	return 0;

}



