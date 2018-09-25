#include "worker.h"
#include <algorithm>

using namespace std;

#if 0
// определить по имени файла "<base_name>_<num>(<num_of_blocks>)" количество блоков данных в нем
// (просто количество блоков участвует в формировании имени выходного файла)
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
	else fileBlkCnt = 1;  // если <num_of_blocks> отсутствует (такие файлы получаются после пресортировки) 
	return fileBlkCnt;
}
#endif

// основная worker-функция
int WorkerClass::Work()
{
	int elemCount;
	state = wsSearch;
	printf("Worker %d started\n", ID);

	while (state != wsIdle)  // статус wsIdle выставляется, когда потоку больше нечего делать
	{
		if (gParams->inFile.state & bsDeleted)  // статус bsDeleted выставляется inFile когда его обработка завершена
		{
			state = wsMerge;                        // если пресортировка inFile завершена - запуск задачи слияния пресортированных файлов
			printf("Worker %d merging...\n", ID);
			elemCount = JobMerge();
			printf("Worker %d merged %d elements\n", ID, elemCount);
			if (elemCount > 0)
			{
				totalSorted += elemCount;
				gParams->cvProduced.notify_all();   // сформирован новый промежуточный файл - сигнализировать, вдруг кому-то нужен
			}
			else if (elemCount == 0) state = wsIdle;
		}
		else
		{
			state = wsSort;                         // если inFile еще не до конца обработан - запуск задачи пресортировки
			printf("Worker %d sorting...\n", ID);
			elemCount = JobPreSort();
			printf("Worker %d presorted %d elements\n", ID, elemCount);
			if (elemCount > 0)
			{
				totalPreSorted += elemCount;
				gParams->cvProduced.notify_all();   // сформирован новый промежуточный файл - сигнализировать
			}
		}
	}
	printf("Worker %d finished: %d elements presorted, %d elements merged\n", ID, totalPreSorted, totalSorted);
	return totalSorted;
}

// задача предварительной сортировки:
// из заранее открытого на чтение вхожного файла читается очередная порция (buflen элементов), сортируется, записывается в промежуточный файл
// информация о сформированном файле сохраняется в очередной элемент files[] (первичное заполнение files[])
// название файла "<inFile_name>_<fileIdx>" <fileIdx> - индекс в массиве files[]
// return: количество элементов в выходном файле
int WorkerClass::JobPreSort()
{
	bool jobAct = true;
	FileInfo *fileSrcInfo = &gParams->inFile;
	int fileIdx;

	gParams->mtxFiles.lock();

	// если входной файл все еще помечен как обрабатывающийся - считать очередную порцию данных
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
		jobAct = false;  // если входной файл уже не обрабатывается (типа обработан уже) - завершить задачу

	gParams->mtxFiles.unlock();
	if (!jobAct) return 0;

	sort(buffer, buffer + datalen);

	FileInfo *fileDstInfo = NULL;
	gParams->mtxFiles.lock();

	// поиск места в files[] для сохранения информации о выходном промежуточном файле
	for (fileIdx = 0; (fileIdx < gParams->numOfFiles) && (fileDstInfo == NULL); fileIdx++)
	{
		if (files[fileIdx].state == bsNone || files[fileIdx].state == bsDeleted)
			fileDstInfo = &files[fileIdx];
	}

	if (fileDstInfo) fileDstInfo->state = bsWriting;
	gParams->mtxFiles.unlock();

	// формирование имени выходного файла
	if (datalen == fileSrcInfo->length)    
	{   // если вдруг весь inFile влез в один блок сортировки - то сразу получили выходной файл
		outFileName = gParams->outFile.name;
	}
	else
	{
		char fileSuffix[0x20];
		sprintf(fileSuffix, "_%04X", fileIdx);
		outFileName = fileSrcInfo->name + fileSuffix;
	}
	
	// запись результата в выходной файл
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
	fileDstInfo->blocks = 1;
	fileDstInfo->fp = NULL;
	fpOut = NULL;
	printf("Worker %d produced: %s\n", ID, outFileName.c_str());

	return datalen;
}


// задача слияния:
// из files[] выбираются 2 пресортированных промежуточных файла с состоянием bsToMerge, проводится сортировка слиянием
// информация о сформированном файле сохраняется в элемент files[] первого входного файла
// элемент files[] второго входного файла помечается bsDeleted и больше никогда не используется
// имя выходного файла - такое же, как имя первого входного файла, но с другим значением в скобках (а в скобках там <num_of_blocks>)
// return: количество элементов в выходном файле
int WorkerClass::JobMerge()
{
	FileInfo *fileSrc1Info = NULL;   // информация о 1 найденном входном файле
	FileInfo *fileSrc2Info = NULL;   // информация о 2 найденном входном файле
	int filesWorkingCount = 0;       // счетчик элементов files[], с которыми вообще еще ведется работа (если <2 - то слиянию уже делать нечего, все обработано)

    // цикл - до тех пор, пока в files[] не будет найдено 2 входных файла
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
		
		// если файлов для обработки не осталось потому что все обработано - выйти
		if (filesWorkingCount <= 1) return 0;
		
		// если 2 файла для обработки не найдены, но не все еще обработаны - ожидать на cvProduced
		if (fileSrc2Info == NULL)
		{
			if (fileSrc1Info) fileSrc1Info->state = bsToMerge;
			gParams->cvProduced.wait(lock);
		}

	} while (fileSrc2Info == NULL);	
    // два входных файла найдены

	// формирование имени выходного файла
	// для проверки, не он ли будет основным выходным файлом, используем сравнение длин с требуемой 
	// (можно было бы проверять filesWorkingCount==2, но тогда надо не выходить из цикла поиска do-while до конца files[])
	if (fileSrc1Info->length + fileSrc2Info->length == gParams->inFile.length)
		outFileName = gParams->outFile.name;
	else
		outFileName = fileSrc1Info->name.substr(0, fileSrc1Info->name.find_last_of('(')) + 
		'(' + to_string(fileSrc1Info->blocks + fileSrc2Info->blocks) + ')';
		//'(' + to_string(GetFileBlkCntFromName(fileSrc1Info->name) + GetFileBlkCntFromName(fileSrc2Info->name)) + ')';

	// подготовка файлов и проведение слияния (MergeFiles())
	fileSrc1Info->fp = fopen(fileSrc1Info->name.c_str(), "rb");
	fileSrc2Info->fp = fopen(fileSrc2Info->name.c_str(), "rb");
	fpOut = fopen(outFileName.c_str(), "wb");
	int datalen = MergeFiles(fileSrc1Info->fp, fileSrc2Info->fp, fpOut);
	fclose(fpOut); fpOut = NULL;
	fclose(fileSrc1Info->fp);
	fclose(fileSrc2Info->fp);

	// корректировка записей в files[], удаление входных файлов
	printf("Worker %d merged: %s(%d) + %s(%d) -> %s(%d)\n", ID,
		fileSrc1Info->name.c_str(), fileSrc1Info->length,
		fileSrc2Info->name.c_str(), fileSrc2Info->length,
		outFileName.c_str(), datalen);
	gParams->mtxFiles.lock();
	fileSrc1Info->fp = NULL;
	fileSrc2Info->fp = NULL; 
	if (gParams->deleteInterFiles)
	{
		remove(fileSrc1Info->name.c_str());
		remove(fileSrc2Info->name.c_str());
	}
	fileSrc1Info->name = outFileName;
	fileSrc1Info->length = datalen;
	fileSrc1Info->blocks += fileSrc2Info->blocks;
	fileSrc1Info->state = bsToMerge;
	fileSrc2Info->length = 0;
	fileSrc2Info->blocks = 0;
	fileSrc2Info->state = bsDeleted;
	gParams->mtxFiles.unlock();

	return datalen;
}


// слияние fpIn1 + fpIn2 -> fpOut
// первая половина buffer worker'а выделяется для формирования результата (bufDst[2*sublen]), вторая - для входных данных
// вторая половина также разделяется пополам - данные каждого из двух входных файлов (bufSrc1[sublen] и bufSrc2[sublen])
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
		// обмен между буфером и файлами
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

		// непосредственно слияние с сортировкой
		if (ptrSrc1 >= 0)
			for (unsigned int min = (ptrSrc2 >= 0) ? bufSrc2[ptrSrc2] : 0;
			((bufSrc1[ptrSrc1] <= min) || (ptrSrc2 < 0)) && (ptrSrc1 < lenSrc1) && (ptrDst < lenDst);
				bufDst[ptrDst++] = bufSrc1[ptrSrc1++]);
		if (ptrSrc2 >= 0)
			for (unsigned int min = (ptrSrc1 >= 0) ? bufSrc1[ptrSrc1] : 0;
			((bufSrc2[ptrSrc2] <= min) || (ptrSrc1 < 0)) && (ptrSrc2 < lenSrc2) && (ptrDst < lenDst);
				bufDst[ptrDst++] = bufSrc2[ptrSrc2++]);

	} while (ptrSrc1 >= 0 || ptrSrc2 >= 0);

	// дозапись остатка
	if (ptrDst > 0)
	{
		fwrite(bufDst, sizeof(SortData_t), ptrDst, fpOut);
		totalDone += ptrDst;
	}

	return totalDone;
}


