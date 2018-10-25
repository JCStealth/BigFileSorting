#ifndef _STRUCTS_H
#define _STRUCTS_H
#define _CRT_SECURE_NO_WARNINGS true

#include <string>
#include <mutex>
#include <condition_variable>

enum BlockStates
{
	bsNone    = 0x0000,        // не определено
	bsToSort  = 0x0001,        // готов к предварительной сортировке
	bsToMerge = 0x0002,        // готов к слиянию
	bsDeleted = 0x0004,        // удален, любая обработка запрещена
	bsReading = 0x0010,        // используется как входные данные для обработки
	bsWriting = 0x0020,        // формируется
	bsWorking = (bsToSort | bsToMerge | bsReading | bsWriting),
};

typedef unsigned int SortData_t;

// элемент списка файлов
struct FileInfo
{
	std::string name;       // имя файла
	FILE *fp;               
	unsigned int state;     // состояние файла (BlockStates)
	int length;             // длина файла [элементов]
	int blocks;             // количество блоков сортировки в файле (блок сортировки - результат задачи предварительной сортировки WorkerClass::JobPreSort())
	FileInfo() { name = ""; fp = NULL; state = bsNone; length = 0; blocks = 0; };
};

// структура с глобальными параметрами
struct GlobalParams
{
	FileInfo  inFile;        // основной входной файл
	FileInfo  outFile;       // основной выходной файл
	FileInfo *files;         // промежуточные файлы

	std::mutex mtxFiles;                 // ресурс: список файлов (files)
	std::condition_variable cvProduced;  // сигнал о готовности нового блока данных
	
	int inFileSize;                 // размер входного файла в байтах (так, на всякий случай)
	int numOfFiles;                 // количество промежуточных файлов (files[])
	int numOfWorkers;               // количество worker'ов
	int memTotalSize;               // всего памяти под сортируемые данные [элементов]
	int memBlockSize;               // памяти для одного worker'а [элементов]
	SortData_t *buffer;             // указатель на эту память
	
	bool deleteInterFiles;          // удалять отработанные промежуточные файлы (false помогает при отладке)

	// конструктор
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

	// деструктор: закрыть открытые файлы, освободить память
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

	// подготовка глобальных параметров:
	// по предварительно установленым параметрам заполнить все остальные
	int SetValues()
	{
		// каждому worker'у выделяется одинаковое количество памяти под memBlockSize элементов
		if (numOfWorkers <= 0) return -1;
		memBlockSize = (memTotalSize) / sizeof(SortData_t) / numOfWorkers;        
		if (memBlockSize <= 0) return -2;
		buffer = new SortData_t[memBlockSize * numOfWorkers];  // в итоге можем использовать немного меньше памяти, чем задано в параметре (пусть, зато проще в отладке)
		if (buffer == NULL) return -3;
		
		// numOfFiles - количество записей в files[] - равно количеству блоков предварительной сортировки в исходном файле
		// после проведения предварительной сортировки, при слиянии, количество файлов уменьшается
		inFile.length = inFileSize / sizeof(SortData_t);
		inFile.blocks = inFile.length / memBlockSize;
		if (inFile.length % memBlockSize) inFile.blocks++;
		numOfFiles = inFile.blocks;
		files = new FileInfo[numOfFiles];
		if (files == NULL) return -4;
		
		// открыть основной исходный файл и присвоить ему статус
		inFile.state = bsToSort;
		inFile.fp = fopen(inFile.name.c_str(), "rb");
		if (inFile.fp == NULL) return -4;

		return 0;
	}
};

#endif
