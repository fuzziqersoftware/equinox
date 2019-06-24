# equinox

equinox is an interpreter and JIT compiler for esoteric programming languages.

Like nemesys, this is just for fun. Don't use it for any serious business purpose. Then again, if you're using esoteric languages for any serious business purpose, maybe you have other problems...

## Building

- Build and install phosg (https://github.com/fuzziqersoftware/phosg)
- Build and install libamd64 (https://github.com/fuzziqersoftware/libamd64)
- `make`

If it doesn't work on your system, let me know. It should build and run on all recent OS X versions.

## Modes and options

Usually you can just run `./equinox <filename>` and it should be able to figure out which language you want to use. If it infers incorrectly, you can give the appropriate language option (e.g. `--befunge`) to override its guess.

equinox will use the JIT compiler for the appropriate language if it's available. If it's not available or if you specify `--interpret`, equinox will use the interpreter for that language.

When using the JIT compiler, the option `--show-assembly` will cause all the generated code to be printed to stderr. This is pretty useful when debugging issues with the compiled code or the compiler itself.

## Languages

Currently equinox supports three languages to varying degrees. Suggestions for other esoteric languages to support are welcome.

### Brainfuck

To force interpreting/compiling the input program as Brainfuck, use the `--brainfuck` option.

The Brainfuck implementation is fully working and correct in both interpret and compile modes. The compiler implements a few optimizations for some common patterns, making the compiled code much faster than a naive translation to assembly. You can disable complex optimizations or all optimizations by using `--optimize-level=1` or `--optimize-level=0` respectively.

For memory-hungry programs, you might want to increase the `--memory-expansion-size` option (default 64KB). This controls how many more bytes are allocated when the program moves past the end of its currently-allocated array.

### Funge-98

To force interpreting/compiling the input program as Funge-98, use the `--befunge` option.

The Funge-98 JIT implementation is mostly working, but the interpreter is incomplete. The Mycology tests fail pretty early in the interpreter because the 'k' opcode isn't implemented; they fail much later in the JIT for other reasons that I haven't gotten around to fixing yet. There's also a known inefficiency in the JIT: each cell will be compiled multiple times depending on how many different directions it's entered from (among other factors), so the code buffer can get quite large.

Use `--dimensions` to choose between Unefunge (1), Befunge (2; default), and Trefunge (3).

To start a Funge-98 program in single-step debugging mode, use the `--single-step` option. Alternatively, you can use `--breakpoint=X[,Y[,Z]]` (depending on the number of dimensions) to enter single-step debugging mode when execution reaches that cell.

### Malbolge

To force interpreting/compiling the input program as Malbolge, use the `--malbolge` option.

Malbolge runs only in interpret mode. There are no language-specific options. There's definitely some kind of unfixed critical bug in the interpreter; Hello World works but 99 Bottles hangs forever. That's on my TODO list.
