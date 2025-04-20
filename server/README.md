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

The communication between threads is done via a pipe that gets initialized when
a new connection is created. The thread polls the pipe for information, if no
new information is received in a hot second then the global state is checked, if
the server or the connection is closing down we then proceed to end the main
thread loop and clean all resources associated with it (like the file descriptor
or the memory arenas).

Finally, all synchronization is done via mutexes. Each individual item on the
global state has a mutex associated with it.

For example, each ChatHistory has it's own mutex, every time we need to add a
message to it we lock the mutex and unlock it once the operation is done:

ChatHistory struct:
https://github.com/ElrohirGT/CHAD/blob/5953d85f212ecc7f9bd32f5372fa910497d05b99/lib/lib.c#L723-L744

ChatHistory usage:
https://github.com/ElrohirGT/CHAD/blob/5953d85f212ecc7f9bd32f5372fa910497d05b99/server/src/main.c#L563-L569

The same applies to other global state, like the `ActiveUsers` linked list, or
the `chats` hashmap.

This way our server can handle multiple chats being modified concurrently, since
each thread only needs to lock the chat it want's to append a chat to, instead
of the complete list of all chats.

### Creation of a connection with all it's information:

https://github.com/ElrohirGT/CHAD/blob/ee96684fbf7053ebf8babd5d78dd1bfe2349e67e/server/src/main.c#L906

### Message passing to the appropriate thread:

https://github.com/ElrohirGT/CHAD/blob/ee96684fbf7053ebf8babd5d78dd1bfe2349e67e/server/src/main.c#L1096
