#ifndef _WORKER_H
#define _WORKER_H

#include "structs.h"

enum WorkerStates
{
	wsIdle,      // нет задачи
	wsSort,      // предварительна€ сортировка: считываетс€ очередна€ часть входного файла, сортируетс€, записываетс€ в выходной файл
	wsMerge,     // сли€ние: два предварительно сортированных входных файла объедин€ютс€ сортировкой сли€нием в один
	wsSearch,    // поиск файла, требующего обработки
};

// класс потока обработки (worker)
class WorkerClass
{
private:
	GlobalParams *gParams;          // указатель на глобальные параметры
	SortData_t *buffer;             // пам€ть, отведенна€ worker'у
	int buflen;
	int datalen;                    // длина обрабатываемых данных (<= buflen)
	FileInfo *files;                // gParams->files

	//int GetFileBlkCntFromName(std::string fileName);        // определить по имени файла количество блоков данных в нем
	int JobPreSort();                                       // задача предварительной сортировки
	int JobMerge();                                         // задача сли€ни€
	int MergeFiles(FILE *fpIn1, FILE *fpIn2, FILE *fpOut);  // сли€ние двух сортированных файлов

public:
	int ID;              
	WorkerStates state;  

	int totalPreSorted;             // счетчик элементов, которым worker провел пресортировку
	int totalSorted;                // счетчик элементов, которым worker провел сли€ние

	std::thread thd;

	// выходной файл дл€ текущей задачи worker'а
	std::string  outFileName;
	FILE   *fpOut; 

	// main-функци€ worker'а
	int Work();

	// конструктор
	// IN: id - идентификатор worker'а; сквозна€, [1..numOfWorkers) (0 зарезервирован дл€ worker-0, запускаемого из основного потока)
    // IN: globParams - указатель на предварительно заполненные входные параметры
	// IN: startThread - =0 - создание объекта без запуска нового потока (используетс€ worker-0)
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

