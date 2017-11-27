CXX=g++
OBJECTS=opc.o AMD64Assembler.o CodeBuffer.o
CXXFLAGS=-I/usr/local/include -std=c++14 -O3 -Wall -Werror -Wno-deprecated-declarations
LDFLAGS=-L/usr/local/lib -lphosg

all: opc

opc: $(OBJECTS)
	g++ $(LDFLAGS) -o opc $^

clean:
	-rm -f *.o opc

.PHONY: clean
