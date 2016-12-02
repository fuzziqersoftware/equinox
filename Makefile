CC=gcc
OBJECTS=opc.o
CFLAGS=-O3 -Wall -Werror -Wno-deprecated-declarations
EXECUTABLES=opc

all: opc

opc: $(OBJECTS)
	g++ $(LDFLAGS) -o opc $^

install: opc
	cp opc /usr/bin/opc

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
