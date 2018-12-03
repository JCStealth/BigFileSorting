#ifndef _WORKER_H
#define _WORKER_H

#include "structs.h"
#include <thread>

enum WorkerStates
{
	wsIdle,      // нет задачи
	wsSort,      // предварительная сортировка: считывается очередная часть входного файла, сортируется, записывается в выходной файл
	wsMerge,     // слияние: два предварительно сортированных входных файла объединяются сортировкой слиянием в один
	wsSearch,    // поиск файла, требующего обработки
};

// класс потока обработки (worker)
class WorkerClass
{
private:
	GlobalParams *gParams;          // указатель на глобальные параметры
	SortData_t *buffer;             // память, отведенная worker'у
	int buflen;
	int datalen;                    // длина обрабатываемых данных (<= buflen)
	FileInfo *files;                // gParams->files
	int dbgLevel;
	FILE *dbgFile;

	//int GetFileBlkCntFromName(std::string fileName);        // определить по имени файла количество блоков данных в нем
	int JobPreSort();                                       // задача предварительной сортировки
	int JobMerge();                                         // задача слияния
	int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut);  // слияние двух сортированных файлов

public:
	int ID;              
	WorkerStates state;  

	int totalPreSorted;             // счетчик элементов, которым worker провел пресортировку
	int totalSorted;                // счетчик элементов, которым worker провел слияние

	std::thread thd;

	// выходной файл для текущей задачи worker'а
	std::string  outFileName;
	FILE   *fpOut; 

	// main-функция worker'а
	int Work();

	// для отладочного вывода
	void Trace(int level, const char *fmt, ...);

	// конструктор
	// IN: id - идентификатор worker'а; сквозная, [1..numOfWorkers) (0 зарезервирован для worker-0, запускаемого из основного потока)
    // IN: globParams - указатель на предварительно заполненные входные параметры
	// IN: startThread - =0 - создание объекта без запуска нового потока (используется worker-0)
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
		dbgLevel = globParams->dbgLevel;
		dbgFile = globParams->dbgFile;
		if(startThread) thd = std::thread(&WorkerClass::Work, this);
	};
	//~WorkerClass();

};


#endif

