CXX=g++
OBJECTS=Main.o Assembler/AMD64Assembler.o Assembler/CodeBuffer.o Languages/Brainfuck.o Languages/Befunge.o Languages/BefungeInterpreter.o Languages/BefungeJITCompiler.o Languages/MalbolgeInterpreter.o
CXXFLAGS=-g -I/usr/local/include -std=c++14 -O0 -Wall -Werror -Wno-deprecated-declarations
LDFLAGS=-g -L/usr/local/lib -lphosg

all: opc

opc: $(OBJECTS)
	g++ $(LDFLAGS) -o opc $^

clean:
	-rm -f *.o Assembler/*.o Languages/*.o opc

.PHONY: clean
