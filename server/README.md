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
# Execute `./nob -h` to get a list of all available commands!
./nob
```

`./nob` detects changes on itself! So if you ever need to change the `./nob.c`
file you won't need to recompile it manually! `./nob` takes care of that for
you.

## Running the project

Just run the binary! If you want to use the special flags to modify something
you can just use the `-h` flag to see the help menu!

```bash
# This commands assumes you're inside `./server`
./build/main
# If you want to modify the port or binding address just run it with the -h flag to see the options!
# ./build/main -h
```
