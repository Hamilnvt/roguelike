#!/usr/bin/bash

clear

gcc -o roguelike main.c -lncurses -lm -Wall -Wextra -Werror -Wswitch-enum -Wno-discarded-qualifiers -ggdb
