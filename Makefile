CFLAGS=-O2 -fopenmp
CXXFLAGS=$(CFLAGS) -std=c++0x
LIBS = -fopenmp
LD = $(CXX)


APPS = hello exercise1 textreader

all: $(APPS) 

hello: hello.o

exercise1: exercise1.o

textreader: textreader.o

.cpp.o:
	$(CXX) $(INC) $(CXXFLAGS) -c -o $@ $^

.c.o:
	$(CC) $(INC) $(CFLAGS) -c -o $@ $^

.o:
	$(LD) -o $@ $^ $(LIBS) $(EXTRA_LDFLAGS)

clean:
	rm -f *.o hello

