CC = gcc
CXX = g++
INC = -I../module

LIBS =
LD = $(CXX)

CFLAGS = -O2 -g
CXXFLAGS = $(CFLAGS) -std=c++0x

EXTRA_LDFLAGS = 

APPS = sender

all: $(APPS) 

sender: sender.o packet.o network.o process_info.o

.cpp.o:
	$(CXX) $(INC) $(CXXFLAGS) -c -o $@ $^

.c.o:
	$(CC) $(INC) $(CFLAGS) -c -o $@ $^

.o:
	$(LD) -o $@ $^ $(LIBS) $(EXTRA_LDFLAGS)

clean:
	rm -f *.o
	rm -f sender
