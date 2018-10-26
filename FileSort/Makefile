CC=g++
CFLAGS=-pthread -std=c++11
PROGNAME=filesort
SOURCES=main.cpp worker.cpp
HEADERS=worker.h structs.h
OBJECTS=$(SOURCES:.cpp=.o)

all: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm *.o
	rm $(PROGNAME)
