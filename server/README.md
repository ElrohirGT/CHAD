# CHAD Server

**IMPORTANT!** Remember to enter the nix development shell using:

```bash
# REMEMBER! From within the repository root!
nix develop
```

## Getting started

All you need to do to get started is compile the `./nob.c` file:

```bash
cc nob.c -o nob
```

This file acts as the build system for the entire project!

## Building the project

To build the project simply execute:

```bash
# This rebuilds the dependencies every time!
# Execute `./nob -h` to get a list of all available commands!
./nob -d
```

`./nob` detects changes on itself! So if you ever need to change the `./nob.c`
file you won't need to recompile it manually! `./nob` takes care of that for
you.

## Running the project

Since the projects dinamically links to the libwebsocket build inside `./deps`,
then it can't find the library when you try to execute the binary! There's
solutions to this of course. On Linux you can run:

```bash
# This commands assumes you're inside `./server`
LD_PRELOAD=../deps/libwebsockets/build/lib/libwebsockets.so.19 ./build/main
```
