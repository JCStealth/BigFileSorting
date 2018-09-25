#ifndef _STRUCTS_H
#define _STRUCTS_H
#define _CRT_SECURE_NO_WARNINGS true

#include <string>
#include <mutex>
#include <condition_variable>

enum BlockStates
{
	bsNone    = 0x0000,        // �� ����������
	bsToSort  = 0x0001,        // ����� � ��������������� ����������
	bsToMerge = 0x0002,        // ����� � �������
	bsDeleted = 0x0004,        // ������, ����� ��������� ���������
	bsReading = 0x0010,        // ������������ ��� ������� ������ ��� ���������
	bsWriting = 0x0020,        // �����������
	bsWorking = (bsToSort | bsToMerge | bsReading | bsWriting),
};

typedef unsigned int SortData_t;

// ������� ������ ������
struct FileInfo
{
	std::string name;       // ��� �����
	FILE *fp;               
	unsigned int state;     // ��������� ����� (BlockStates)
	int length;             // ����� ����� [���������]
	int blocks;             // ���������� ������ ���������� � ����� (���� ���������� - ��������� ������ ��������������� ���������� WorkerClass::JobPreSort())
	FileInfo() { name = ""; fp = NULL; state = bsNone; length = 0; blocks = 0; };
};

// ��������� � ����������� �����������
struct GlobalParams
{
	FileInfo  inFile;        // �������� ������� ����
	FileInfo  outFile;       // �������� �������� ����
	FileInfo *files;         // ������������� �����

	std::mutex mtxFiles;                 // ������: ������ ������ (files)
	std::condition_variable cvProduced;  // ������ � ���������� ������ ����� ������
	
	int inFileSize;                 // ������ �������� ����� � ������ (���, �� ������ ������)
	int numOfFiles;                 // ���������� ������������� ������ (files[])
	int numOfWorkers;               // ���������� worker'��
	int memTotalSize;               // ����� ������ ��� ����������� ������ [���������]
	int memBlockSize;               // ������ ��� ������ worker'� [���������]
	SortData_t *buffer;             // ��������� �� ��� ������
	
	bool deleteInterFiles;          // ������� ������������ ������������� ����� (false �������� ��� �������)

	// �����������
	GlobalParams() { 
		inFileSize = 0;
		files = NULL;
		buffer = NULL;
		numOfFiles = 0; 
		numOfWorkers = 0; 
		memTotalSize = 0; 
		memBlockSize = 0;
		deleteInterFiles = true;
	};

	// ����������: ������� �������� �����, ���������� ������
	~GlobalParams() {
		if (inFile.fp) fclose(inFile.fp);
		if (outFile.fp) fclose(outFile.fp);
		if (files)	{
			for (int i = 0; i < numOfFiles; i++)
				if (files[i].fp) fclose(files[i].fp);
			delete[] files;
		}
		if (buffer) delete[] buffer;
	}

	// ���������� ���������� ����������:
	// �� �������������� ������������ ���������� ��������� ��� ���������
	int SetValues()
	{
		// ������� worker'� ���������� ���������� ���������� ������ ��� memBlockSize ���������
		if (numOfWorkers <= 0) return -1;
		memBlockSize = (memTotalSize) / sizeof(SortData_t) / numOfWorkers;        
		if (memBlockSize <= 0) return -2;
		buffer = new SortData_t[memBlockSize * numOfWorkers];  // � ����� ����� ������������ ������� ������ ������, ��� ������ � ��������� (�����, ���� ����� � �������)
		if (buffer == NULL) return -3;
		
		// numOfFiles - ���������� ������� � files[] - ����� ���������� ������ ��������������� ���������� � �������� �����
		// ����� ���������� ��������������� ����������, ��� �������, ���������� ������ �����������
		inFile.length = inFileSize / sizeof(SortData_t);
		inFile.blocks = inFile.length / memBlockSize;
		if (inFile.length % memBlockSize) inFile.blocks++;
		numOfFiles = inFile.blocks;
		files = new FileInfo[numOfFiles];
		if (files == NULL) return -4;
		
		// ������� �������� �������� ���� � ��������� ��� ������
		inFile.state = bsToSort;
		inFile.fp = fopen(inFile.name.c_str(), "rb");
		if (inFile.fp == NULL) return -4;

		return 0;
	}
};

#endif
