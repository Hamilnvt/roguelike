#!/usr/bin/bash

set -e

clear

gcc   -o roguelike       main.c -lncurses -lm -Wall -Wextra -Werror -Wswitch-enum -Wno-discarded-qualifiers -ggdb
clang -o clang_roguelike main.c -lncurses -lm -Wall -Wextra -Werror -Wswitch-enum -Wno-unused-function -ggdb
