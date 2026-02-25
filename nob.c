#define NOB_IMPLEMENTATION
#include "nob.h"

#define SRC_DIR "src"
#define INCLUDE_DIR "include"
#define BUILD_DIR "build"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!mkdir_if_not_exists(BUILD_DIR)) return 1;

    File_Paths src_files = {0};
    if (!read_entire_dir(SRC_DIR, &src_files)) return 1;

    File_Paths include_files = {0};
    File_Paths headers = {0};
    if (read_entire_dir(INCLUDE_DIR, &include_files)) {
        for (size_t i = 0; i < include_files.count; ++i) {
            if (sv_end_with(sv_from_cstr(include_files.items[i]), ".h")) {
                da_append(&headers, temp_sprintf(INCLUDE_DIR"/%s", include_files.items[i]));
            }
        }
    }

    Cmd cmd = {0};
    File_Paths gcc_objs = {0};
    File_Paths clang_objs = {0};

    for (size_t i = 0; i < src_files.count; ++i) {
        const char *file_name = src_files.items[i];
        if (!sv_end_with(sv_from_cstr(file_name), ".c")) continue;

        const char *input_path = temp_sprintf(SRC_DIR"/%s", file_name);
        
        const char *base_name = temp_strndup(file_name, strlen(file_name) - 2);

        const char *gcc_obj = temp_sprintf(BUILD_DIR"/%s.gcc.o", base_name);
        const char *clang_obj = temp_sprintf(BUILD_DIR"/%s.clang.o", base_name);

        da_append(&gcc_objs, gcc_obj);
        da_append(&clang_objs, clang_obj);

        File_Paths deps = {0};
        da_append(&deps, input_path);
        da_append_many(&deps, headers.items, headers.count);

        if (needs_rebuild(gcc_obj, deps.items, deps.count)) {
            cmd_append(&cmd, "gcc", "-c", "-o", gcc_obj, input_path);
            cmd_append(&cmd, "-I./"INCLUDE_DIR, "-Wall", "-Wextra", "-Werror", "-Wswitch-enum", 
                             "-Wno-discarded-qualifiers", "-Wno-comment", "-ggdb");
            if (!cmd_run(&cmd)) return 1;
        }

        if (needs_rebuild(clang_obj, deps.items, deps.count)) {
            cmd_append(&cmd, "clang", "-c", "-o", clang_obj, input_path);
            cmd_append(&cmd, "-I./"INCLUDE_DIR, "-Wall", "-Wextra", "-Werror", "-Wswitch-enum", 
                             "-Wno-unused-function", "-ggdb");
            if (!cmd_run(&cmd)) return 1;
        }

        da_free(deps);
    }

    const char *gcc_exe = BUILD_DIR"/roguelike";
    if (needs_rebuild(gcc_exe, gcc_objs.items, gcc_objs.count)) {
        cmd_append(&cmd, "gcc", "-o", gcc_exe);
        da_append_many(&cmd, gcc_objs.items, gcc_objs.count);
        cmd_append(&cmd, "-lncurses", "-lm");
        if (!cmd_run(&cmd)) return 1;
    }

    const char *clang_exe = BUILD_DIR"/clang_roguelike";
    if (needs_rebuild(clang_exe, clang_objs.items, clang_objs.count)) {
        cmd_append(&cmd, "clang", "-o", clang_exe);
        da_append_many(&cmd, clang_objs.items, clang_objs.count);
        cmd_append(&cmd, "-lncurses", "-lm");
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}
