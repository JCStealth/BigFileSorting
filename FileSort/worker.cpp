#include "worker.h"
#include <algorithm>

using namespace std;

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

	while (state != wsIdle)
	{
		if (gParams->inFile.state & bsDeleted)
		{
			state = wsMerge;
			printf("Worker %d merging...\n", ID);
			elemCount = JobMerge();
			printf("Worker %d merged %d elements\n", ID, elemCount);
			if (elemCount > 0)
			{
				totalSorted += elemCount;
				gParams->cvProduced.notify_all();
			}
			else if (elemCount == 0) state = wsIdle;
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
				gParams->cvProduced.notify_all();    // signal that new file is ready
			}
		}
	}
	printf("Worker %d finished: %d elements presorted, %d elements merged\n", ID, totalPreSorted, totalSorted);
	return totalSorted;
}

int WorkerClass::JobPreSort()
{
	bool jobAct = true;
	FileInfo *fileSrcInfo = &gParams->inFile;
	int fileIdx;

	gParams->mtxFiles.lock();

	if (fileSrcInfo->state & bsToSort)
	{
		int fileSrcPtr = ftell(fileSrcInfo->fp);
		datalen = fread(buffer, sizeof(buffer[0]), buflen, fileSrcInfo->fp);
		if (datalen >= 0)
			printf("Worker %d read from %s: %d..%d\n", ID, fileSrcInfo->name.c_str(), fileSrcPtr, fileSrcPtr + (datalen * sizeof(buffer[0])) - 1);
		else printf("Worker %d read from %s error %d\n", ID, fileSrcInfo->name.c_str(), errno);
		if (datalen < buflen)
		{
			fileSrcInfo->state = bsDeleted;
			fclose(fileSrcInfo->fp); fileSrcInfo->fp = NULL;
			if (datalen == 0) jobAct = false;
		}
	}
	else
		jobAct = false;

	gParams->mtxFiles.unlock();
	if (!jobAct) return 0;

	sort(buffer, buffer + buflen);

	FileInfo *fileDstInfo = NULL;
	gParams->mtxFiles.lock();

	for (fileIdx = 0; (fileIdx < gParams->numOfFiles) && (fileDstInfo == NULL); fileIdx++)
	{
		if (files[fileIdx].state == bsNone || files[fileIdx].state == bsDeleted)
			fileDstInfo = &files[fileIdx];
	}

	if (fileDstInfo) fileDstInfo->state = bsWriting;
	gParams->mtxFiles.unlock();

	char fileSuffix[0x20];
	sprintf(fileSuffix, "_%04X", fileIdx);
	outFileName = fileSrcInfo->name + fileSuffix;
	fpOut = fopen(outFileName.c_str(), "wb");
	if (fpOut == NULL)
	{
		fprintf(stderr, "Worker %d cannot create file %s (error %d)\n", ID, outFileName.c_str(), errno);
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

	//gParams->mtxFiles.lock();
	do
	{
		fileSrc1Info = NULL;
		fileSrc2Info = NULL;
		filesWorkingCount = 0;

		unique_lock <mutex> lock(gParams->mtxFiles);

		for (int f = 0; (f < gParams->numOfFiles) && (fileSrc2Info == NULL); f++)
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
			gParams->cvProduced.wait(lock);
		}

	} while (fileSrc2Info == NULL);
	
	//gParams->mtxFiles.unlock();

	if (fileSrc1Info->length + fileSrc2Info->length == gParams->inFile.length)
		outFileName = gParams->outFile.name;
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
	gParams->mtxFiles.lock();
	fileSrc1Info->fp = NULL; remove(fileSrc1Info->name.c_str());
	fileSrc2Info->fp = NULL; remove(fileSrc2Info->name.c_str());
	fileSrc1Info->name = outFileName;
	fileSrc1Info->length = datalen;
	fileSrc1Info->state = bsToMerge;
	fileSrc2Info->length = 0;
	fileSrc2Info->state = bsDeleted;
	gParams->mtxFiles.unlock();

	return datalen;
}


int WorkerClass::MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut)
{
	int sublen = buflen / 4;
	SortData_t *bufDst = buffer;
	SortData_t *bufSrc1 = buffer + 2 * sublen;
	SortData_t *bufSrc2 = buffer + 3 * sublen;
	int lenDst = sublen * 2, lenSrc1 = 0, lenSrc2 = 0;
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

		if (ptrSrc1 >= 0)
			for (unsigned int min = (ptrSrc2 >= 0) ? bufSrc2[ptrSrc2] : 0;
			((bufSrc1[ptrSrc1] <= min) || (ptrSrc2 < 0)) && (ptrSrc1 < lenSrc1) && (ptrDst < lenDst);
				bufDst[ptrDst++] = bufSrc1[ptrSrc1++]);
		if (ptrSrc2 >= 0)
			for (unsigned int min = (ptrSrc1 >= 0) ? bufSrc1[ptrSrc1] : 0;
			((bufSrc2[ptrSrc2] <= min) || (ptrSrc1 < 0)) && (ptrSrc2 < lenSrc2) && (ptrDst < lenDst);
				bufDst[ptrDst++] = bufSrc2[ptrSrc2++]);

	} while (ptrSrc1 >= 0 || ptrSrc2 >= 0);

	if (ptrDst > 0)
	{
		fwrite(bufDst, sizeof(SortData_t), ptrDst, fpOut);
		totalDone += ptrDst;
	}

	return totalDone;
}


