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

## Multithreading

Last time you checked on us the server didn't run with various threads. Now it
creates a thread for each connection it receives and manages all race conditions
so no data is ever overwritten by a client sending a message "at the same time"
as another client. Keep reading to see how we implemented it.

First, the connection lifecycle loop:
https://github.com/ElrohirGT/CHAD/blob/ee96684fbf7053ebf8babd5d78dd1bfe2349e67e/server/src/main.c#L897

Mongoose executes this function every time it receives a new event on any
connection, we use an if statement to check which type of event it is, and
either create a connection or pass the message to the appropriate thread to
handle it.

### Creation of a connection with all it's information:

https://github.com/ElrohirGT/CHAD/blob/ee96684fbf7053ebf8babd5d78dd1bfe2349e67e/server/src/main.c#L906

### Message passing to the appropriate thread:

https://github.com/ElrohirGT/CHAD/blob/ee96684fbf7053ebf8babd5d78dd1bfe2349e67e/server/src/main.c#L1096
