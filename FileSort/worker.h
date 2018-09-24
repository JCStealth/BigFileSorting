#ifndef _WORKER_H
#define _WORKER_H

#include "structs.h"

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
	GlobalParams *gParams;
	SortData_t *buffer;
	int buflen;
	int datalen;
	FileInfo *files;

	int GetFileBlkCntFromName(std::string fileName);
	int JobPreSort();
	int JobMerge();
	int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut);

public:
	int ID;
	WorkerStates state;

	std::thread thd;
	std::mutex mtx;

	std::string  outFileName;
	FILE   *fpOut;

	int Work();

	WorkerClass(int id, GlobalParams *globParams, int startThread = 1)
	{
		ID = id;
		gParams = globParams;
		state = wsIdle;
		buffer = gParams->buffer + ID * gParams->memBlockSize;
		buflen = gParams->memBlockSize;
		files = gParams->files;
		datalen = 0;
		fpOut = NULL;
		if(startThread) thd = std::thread(&WorkerClass::Work, this);
	};
	//~WorkerClass();

};


#endif

