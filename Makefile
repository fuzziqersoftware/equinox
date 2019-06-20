CXX=g++
OBJECTS=Main.o Languages/BrainfuckInterpreter.o Languages/BrainfuckJITCompiler.o Languages/Befunge.o Languages/BefungeInterpreter.o Languages/BefungeJITCompiler.o Languages/MalbolgeInterpreter.o
CXXFLAGS=-g -I/usr/local/include -I/opt/local/include -std=c++14 -O0 -Wall -Werror -Wno-deprecated-declarations
LDFLAGS=-g -L/usr/local/lib -L/opt/local/lib -lphosg -lamd64

all: equinox

equinox: $(OBJECTS)
	g++ $(LDFLAGS) -o equinox $^

clean:
	-rm -f *.o Assembler/*.o Languages/*.o equinox

.PHONY: clean
