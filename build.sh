#!/usr/bin/bash

clear

gcc -o roguelike main.c -lncurses -lm -Wall -Wextra -Werror -Wno-switch -Wno-discarded-qualifiers -ggdb
