#ifndef _WORKER_H
#define _WORKER_H

#include "structs.h"

enum WorkerStates
{
	wsIdle,      // ��� ������
	wsSort,      // ��������������� ����������: ����������� ��������� ����� �������� �����, �����������, ������������ � �������� ����
	wsMerge,     // �������: ��� �������������� ������������� ������� ����� ������������ ����������� �������� � ����
	wsSearch,    // ����� �����, ���������� ���������
};

// ����� ������ ��������� (worker)
class WorkerClass
{
private:
	GlobalParams *gParams;          // ��������� �� ���������� ���������
	SortData_t *buffer;             // ������, ���������� worker'�
	int buflen;
	int datalen;                    // ����� �������������� ������ (<= buflen)
	FileInfo *files;                // gParams->files

	//int GetFileBlkCntFromName(std::string fileName);        // ���������� �� ����� ����� ���������� ������ ������ � ���
	int JobPreSort();                                       // ������ ��������������� ����������
	int JobMerge();                                         // ������ �������
	int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut);  // ������� ���� ������������� ������

public:
	int ID;              
	WorkerStates state;  

	int totalPreSorted;             // ������� ���������, ������� worker ������ �������������
	int totalSorted;                // ������� ���������, ������� worker ������ �������

	std::thread thd;

	// �������� ���� ��� ������� ������ worker'�
	std::string  outFileName;
	FILE   *fpOut; 

	// main-������� worker'�
	int Work();

	// �����������
	// IN: id - ������������� worker'�; ��������, [1..numOfWorkers) (0 �������������� ��� worker-0, ������������ �� ��������� ������)
    // IN: globParams - ��������� �� �������������� ����������� ������� ���������
	// IN: startThread - =0 - �������� ������� ��� ������� ������ ������ (������������ worker-0)
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
		totalPreSorted = 0;
		totalSorted = 0;
		if(startThread) thd = std::thread(&WorkerClass::Work, this);
	};
	//~WorkerClass();

};


#endif

