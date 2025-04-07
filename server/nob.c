#include <stdio.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "../deps/nob.h/nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER "src/"
#define LIBWEBSOCKET_BUILD_FOLDER "../deps/libwebsockets/build/"

#define HELP

bool args_contains(int argc, char **argv, const char *arg, int arg_length) {
  if (argc <= 1) {
    return false;
  }

  for (int i = 0; i < argc; i++) {
    if (memcmp(argv[i], arg, arg_length) == 0) {
      return true;
    }
  }

  return false;
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  if (args_contains(argc, argv, "-h", 2)) {
    fprintf(stderr, "Usage: nob [-compiler options]\n"
                    "Compiler options:\n"
                    "  -v: Compiler with verbosity enabled.\n"
                    "  -r: Rebuild dependencies.\n"
                    "  -h: Display help menu.\n");
    return 1;
  }

  bool should_rebuild_deps = false;
  if (args_contains(argc, argv, "-r", 2)) {
    nob_log(NOB_WARNING, "Will rebuild dependencies!");
    should_rebuild_deps = true;
  }

  bool compile_wit_verbosity = false;
  if (args_contains(argc, argv, "-v", 2)) {
    nob_log(NOB_WARNING, "Will compile with verbosity!");
    compile_wit_verbosity = true;
  }

  Cmd cmd = {0};

  if (!mkdir_if_not_exists(BUILD_FOLDER))
    return 1;

  if (should_rebuild_deps) {
    nob_cmd_append(&cmd, "bash", "-c", "rm -rf ../deps/libwebsockets/build/");
    if (!nob_cmd_run_sync_and_reset(&cmd)) {
      nob_log(NOB_WARNING, "Failed to remove libwebsockets build directory! "
                           "Was this a mistake?");
    }

    if (!mkdir_if_not_exists("../deps/libwebsockets/build/"))
      return 1;

    nob_cmd_append(&cmd, "bash", "-c",
                   "cd ../deps/libwebsockets/build/ && cmake .. && make");
    if (!nob_cmd_run_sync_and_reset(&cmd))
      return 1;
  }

  String_Builder sb = {0};
  const char *compiler = "clang ";
  if (compile_wit_verbosity) {
    compiler = "clang -v ";
  }

  sb_append_cstr(&sb, compiler);
  sb_append_cstr(&sb, "-Wall -Wextra ");
  sb_append_cstr(&sb, "-I\"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER "include "
                      "-I\"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER
                      "include/libwebsockets "
                      "-I\"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER
                      "include/libwebsockets/abstract "
                      "-I\"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER
                      "include/libwebsockets/abstract/protocols "
                      "-I\"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER
                      "include/libwebsockets/abstract/transports "
                      "-L$(realpath \"$PWD\"/" LIBWEBSOCKET_BUILD_FOLDER "lib) "
                      "-l:libwebsockets.so "
                      "-o build/main -flto " SRC_FOLDER "main.c");

  nob_cmd_append(&cmd, "bash", "-c", sb.items);
  if (!nob_cmd_run_sync_and_reset(&cmd))
    return 1;
  return 0;
}
