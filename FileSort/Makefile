EXECUTABLE=filesort
SRCDIR=src
HDRDIR=include
TMPDIR=tmp
CC=g++
CFLAGS=-pthread -std=c++11 -I $(HDRDIR)
SOURCES=$(wildcard $(SRCDIR)/*.cpp)
#main.cpp worker.cpp
#HEADERS=$(wildcard $(HDRDIR)/*.h)
#worker.h structs.h
#OBJECTS=$(SOURCES:.cpp=.o)
OBJECTS=$(patsubst $(SRCDIR)/%.cpp, $(TMPDIR)/%.o, $(SOURCES))
DEPENDHDRS=$(patsubst $(SRCDIR)/%.cpp, $(TMPDIR)/%.d, $(SOURCES))

all: $(EXECUTABLE)

$(EXECUTABLE): $(DEPENDHDRS) $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

$(DEPENDHDRS): $(TMPDIR)/%.d : $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) $< -MM -MF $@

include $(DEPENDHDRS)
#.cpp.o:
#%.o: %.cpp
$(OBJECTS): $(TMPDIR)/%.o : $(SRCDIR)/%.cpp
	$(CC) -c $(CFLAGS) -I $(HDRDIR) $< -o $@

#$(OBJECTS): tmptgt

.PHONY : clean all #tmptgt

clean:
#	if [ -fe $(TMPDIR)/*.o ] 
#	then
#	  rm $(TMPDIR)/*.o
#	fi
#	if [ -fe $(TMPDIR)/*.d ] 
#	then
#	  rm $(TMPDIR)/*.d
#	fi
#	if [ -fe $(EXECUTABLE) ] 
#	then
#	  rm $(EXECUTABLE)
#	fi
	rm -f $(TMPDIR)/*.o
	rm -f $(TMPDIR)/*.d
	rm -f $(EXECUTABLE)

tmptgt:
	@echo TEMP TARGET: $(OBJECTS) $(SOURCES) $(HEADERS) $(DEPENDHDRS)
