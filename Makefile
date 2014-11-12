CC=gcc
OBJECTS=opc.o
CFLAGS=-O3 -s -Wall -Wno-deprecated-declarations
EXECUTABLES=opc

all: opc

opc: $(OBJECTS)
	g++ $(LDFLAGS) -o opc $^

install: opc
	cp opc /usr/bin/opc

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
