# CHAD Server

**IMPORTANT!** Remember to enter the nix development shell using:

```bash
# REMEMBER! From within the repository root!
nix develop
```

## Getting started

For the client we use cmake to compile the client and the UI. All you need to do is create the build directory

```bash
mkdir build
cd build
```

In this directory is where the client is compiled and executable file is generated

## Building the project

To build the project follow the nexts steps:

```bash
cmake ..
cmake --build .
```
Also instead of using **cmake --build .**, you can use the following command:

```bash
make
```

## Running the project

With the project build now the executable is created in the build directory and just have to run this command:

```bash
# Be sure you are in the build directory or specify it in the command when runnung the project
./chad_client
```
