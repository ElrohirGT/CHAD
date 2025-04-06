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
./nob
```

`./nob` detects changes on itself! So if you ever need to change the `./nob.c`
file you won't need to recompile it manually! `./nob` takes care of that for
you.
