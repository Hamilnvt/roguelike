#include "game.h"

bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }

bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

const char *bool_to_string(bool value) { return value ? "true" : "false"; }

size_t index_at(size_t x, size_t y, size_t width) { return y*width + x; }

float get_time_in_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float)ts.tv_sec + ((float)ts.tv_nsec / 1e9);
}

char sign_as_char(int value) { return value >= 0 ? '+' : '-'; }

_Noreturn void print_error_and_exit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    clear();
    printw("ERROR: ");
    vw_printw(stdscr, fmt, ap);
    refresh();
    nodelay(stdscr, FALSE);
    getch();
    exit(1);
}

static const char *logpath = "./log.txt";
void log_this(char *format, ...)
{
    if (!DEBUG) return;

    FILE *logfile = fopen(logpath, "a");
    if (logfile == NULL) {
        print_error_and_exit("Could not open log file at `%s`\n", logpath);
    }
    va_list fmt; 

    va_start(fmt, format);
    vfprintf(logfile, format, fmt);
    fprintf(logfile, "\n");

    va_end(fmt);
    fclose(logfile);
}

