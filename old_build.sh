#!/usr/bin/bash

set -e

clear

gcc   -o old_roguelike       old_main.c -lncurses -lm -Wall -Wextra -Werror -Wswitch-enum -Wno-discarded-qualifiers -ggdb
clang -o old_clang_roguelike old_main.c -lncurses -lm -Wall -Wextra -Werror -Wswitch-enum -Wno-unused-function -ggdb
