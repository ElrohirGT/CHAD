#include <stdio.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "./deps/nob/nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER "src/"
#define DEPS_FOLDER "deps/"
#define MONGOOSE_DIR DEPS_FOLDER "mongoose/"

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
                    "  -h: Display help menu.\n");
    return 1;
  }

  bool compile_with_verbosity = false;
  if (args_contains(argc, argv, "-v", 2)) {
    nob_log(NOB_WARNING, "Will compile with verbosity!");
    compile_with_verbosity = true;
  }

  Cmd cmd = {0};

  if (!mkdir_if_not_exists(BUILD_FOLDER))
    return 1;

  String_Builder sb = {0};
  const char *compiler = "clang ";
  if (compile_with_verbosity) {
    compiler = "clang -v ";
  }

  sb_append_cstr(&sb, compiler);
  sb_append_cstr(&sb, "-Wall -Werror -fuse-ld=lld ");
  sb_append_cstr(&sb, "-o build/main " SRC_FOLDER "main.c");

  nob_cmd_append(&cmd, "bash", "-c", sb.items);
  if (!nob_cmd_run_sync_and_reset(&cmd))
    return 1;
  return 0;
}
