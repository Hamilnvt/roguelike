/* Ideas
 * - name: Dead Kings
 * - features:
 *   > the monsters you fight are divided into an hierarchy (at the top lie the Dead Kings)
 *      - each rank has some properties (from lesser to stronger + some other stuff
 *      - if a monster kills you it can rank up
 *      - if you kill a monster you can decide what to do with him
 *          > reclute it
 *          > kill it
 *          > downrank it
*/

#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <termios.h>
#include <ncurses.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "dynamic_arrays.h"

#define DEBUG true

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

#define BOOL_AS_CSTR(value) ((value) ? "true" : "false")

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} Strings;

typedef struct
{
    size_t x;
    size_t y;
} Cursor;

typedef enum
{
    RANK_1,
    RANK_2,
    RANK_3,
    RANK_KING
} EntityRank;

typedef struct Entity Entity;
typedef void (* Effect) (Entity *self, Entity *entities, size_t entities_count);

typedef struct
{
    Effect *items; 
    size_t count;
    size_t capacity;
} Effects;

#define ENTITY_NAME_MAX_LEN 31
typedef struct Entity
{
    char name[ENTITY_NAME_MAX_LEN + 1];
    Cursor pos;
    EntityRank rank;
    size_t level;
    int chance;
    int attack;
    Effects effects; 
} Entity;

typedef struct
{
    Entity *items;
    size_t count;
    size_t capacity;
} Entities;

typedef struct
{
    Entity self;
    int xp;
} Player;

typedef struct
{
    Player player;
    float total_time;
    Entities entities;
} Data;

typedef struct
{
    Data data;
    float save_timer;
    char message[256];
} Game;
static Game game = {0};

static inline Cursor *get_player_pos(void) { return &game.data.player.self.pos; }

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

const char *logpath = "./log.txt";
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

void write_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(game.message, 0, sizeof(game.message));
    vsnprintf(game.message, sizeof(game.message), fmt, ap);
    va_end(ap);
    log_this("MESSAGE: %s", game.message);
}

#define da_read_from_file(da, Type, file)                \
    do {                                                 \
        size_t count;                                    \
        fread(&count, sizeof(size_t), 1, (file));        \
        (da)->items = malloc(count * sizeof(Type));      \
        (da)->count = count;                             \
        (da)->capacity = count;                          \
        fread((da)->items, sizeof(Type), count, (file)); \
    } while (0)

#define da_write_to_file(da, Type, file)                         \
    do {                                                         \
        fwrite(&(da)->count, sizeof(size_t), 1, (file));         \
        fwrite(&(da)->items, sizeof(Type), (da)->count, (file)); \
    } while (0)

#define SAVE_FILEPATH "./save.bin"
bool save_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "wb");    
    if (!save_file) return false;

    fwrite(&game.data.player, sizeof(Player), 1, save_file);
    fwrite(&game.data.total_time, sizeof(float), 1, save_file);
    da_write_to_file(&game.data.entities, Entity, save_file);

    fclose(save_file);
    write_message("saved");
    return true;
}

bool load_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "rb");    
    if (!save_file) {
        if (errno == ENOENT) save_game_data();
        else return false;
    }

    fread(&game.data.player, sizeof(Player), 1, save_file);
    fread(&game.data.total_time, sizeof(float), 1, save_file);
    da_read_from_file(&game.data.entities, Entity, save_file);

    fclose(save_file);
    return true;
}

#define SAVE_TIME_INTERVAL 15.f
void advance_save_timer(float dt)
{
    game.save_timer += dt;
    if (game.save_timer >= SAVE_TIME_INTERVAL) {
        game.save_timer = 0.f;
        save_game_data();
    }
}

float get_time_in_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float)ts.tv_sec + ((float)ts.tv_nsec / 1e9);
}

/* Colors */
typedef enum
{
    R_COLOR_BACKGROUND = 10,
    R_COLOR_FOREGROUND,
    R_COLOR_YELLOW,
    R_COLOR_RED,
    R_COLOR_BLUE
} R_Color;

typedef enum
{
    KEY_NULL  = 0,
    CTRL_H    = 8,
    TAB       = 9,
    CTRL_J    = 10,
    CTRL_K    = 11,
    CTRL_L    = 12,
    ENTER     = 13,
    CTRL_M    = 13,
    CTRL_N    = 14,
    CTRL_P    = 16,
    CTRL_Q    = 17,
    CTRL_S    = 19,
    ESC       = 27,

    ALT_0     = 1000,
    ALT_1,
    ALT_2,
    ALT_3,
    ALT_4,
    ALT_5,
    ALT_6,
    ALT_7,
    ALT_8,
    ALT_9, // NOTE: ALT_digit sequences must be consecutive

    ALT_k,
    ALT_j,
    ALT_h,
    ALT_l,

    ALT_c,
    ALT_C,
    ALT_K,
    ALT_J,
    ALT_H,
    ALT_L,

    ALT_m,
    ALT_n,
    ALT_p,

    CTRL_ALT_C,
    CTRL_ALT_K,
    CTRL_ALT_J,
    CTRL_ALT_H,
    CTRL_ALT_L,

    ALT_BACKSPACE,
    ALT_COLON,
} Key;

typedef struct
{
    WINDOW *win;
    size_t height;
    size_t width;
    size_t start_y;
    size_t start_x;
} Window;

static Window win_main = {0};

/* Pairs */
typedef enum
{
    R_PAIR = 1,
    R_PAIR_INV,
} ColorPair;

/* Windows */
static size_t screen_rows;
static size_t screen_cols;
static inline void get_screen_size(void) { getmaxyx(stdscr, screen_rows, screen_cols); }

Window create_window(int h, int w, int y, int x, int color_pair)
{
    Window win = {0};
    win.win = newwin(h, w, y, x);
    win.height = h;
    win.width = w;
    win.start_y = y;
    win.start_x = x;
    if (has_colors() && can_change_color()) wbkgd(win.win, COLOR_PAIR(color_pair));
    return win;
}

void create_windows(void)
{
    win_main = create_window(screen_rows, screen_cols, 0, 0, R_PAIR);
}

void destroy_windows(void)
{
    delwin(win_main.win);
}

void ncurses_end(void)
{
    // TODO:
    // - restore original colors
    // - restore original terminal options (maybe not needed)
    curs_set(1);
    clear();
    refresh();
    endwin();
    log_this("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

void cleanup_on_terminating_signal(int sig)
{
    log_this("Program received signal %d", sig);
    ncurses_end();
    exit(1);
}

void ncurses_init(void)
{
    log_this("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

    initscr();

    curs_set(0);
    raw();
    noecho();
    nonl();
    nodelay(stdscr, TRUE);
    set_escdelay(25);
    keypad(stdscr, TRUE);

    signal(SIGINT, cleanup_on_terminating_signal);
    signal(SIGTERM, cleanup_on_terminating_signal);
    signal(SIGSEGV, cleanup_on_terminating_signal);
}

#define COLOR_VALUE_TO_NCURSES(value) ((value*1000)/255)
#define RGB_TO_NCURSES(r, g, b) COLOR_VALUE_TO_NCURSES(r), COLOR_VALUE_TO_NCURSES(g), COLOR_VALUE_TO_NCURSES(b)

void initialize_colors(void)
{
    if (has_colors()) {
        start_color();
        if (can_change_color()) {
            init_color(R_COLOR_BACKGROUND, RGB_TO_NCURSES(18, 18, 18));
            init_color(R_COLOR_FOREGROUND, RGB_TO_NCURSES(150, 200, 150));
            init_color(R_COLOR_YELLOW, RGB_TO_NCURSES(178, 181, 0));
            init_color(R_COLOR_RED, RGB_TO_NCURSES(150, 20, 20));
            init_color(R_COLOR_BLUE, RGB_TO_NCURSES(20, 20, 150));

            init_pair(R_PAIR,     R_COLOR_FOREGROUND, R_COLOR_BACKGROUND);
            init_pair(R_PAIR_INV, R_COLOR_BACKGROUND, R_COLOR_FOREGROUND);
        } else {
            use_default_colors();
        }
    }
}

int read_key()
{
    int c = getch();
    if (c != ESC) return c;

    int first = getch();
    if (first == ERR) return ESC;

    if (first == '[') { // ESC-[-X sequence
        int second = getch();
        if (second == ERR) return ESC;
        log_this("Read ESC-[-%c sequence", first);

        return ESC; // TODO: togli

        switch (second) { default: return ESC; }
    }

    switch (first) { // ALT-X sequence
        case '0'          : return ALT_0;
        case '1'          : return ALT_1;
        case '2'          : return ALT_2;
        case '3'          : return ALT_3;
        case '4'          : return ALT_4;
        case '5'          : return ALT_5;
        case '6'          : return ALT_6;
        case '7'          : return ALT_7;
        case '8'          : return ALT_8;
        case '9'          : return ALT_9;

        case 'c'          : return ALT_c;
        case 'C'          : return ALT_C;
        case 'k'          : return ALT_k;
        case 'K'          : return ALT_K;
        case 'j'          : return ALT_j;
        case 'J'          : return ALT_J;
        case 'h'          : return ALT_h;
        case 'H'          : return ALT_H;
        case 'l'          : return ALT_l;
        case 'L'          : return ALT_L;
        case 'm'          : return ALT_m;
        case 'n'          : return ALT_n;
        case 'p'          : return ALT_p;
        case KEY_BACKSPACE: return ALT_BACKSPACE;
        case ':'          : return ALT_COLON;

        case CTRL('C'): return CTRL_ALT_C;
        case CTRL('K'): return CTRL_ALT_K;
        case CTRL('J'): return CTRL_ALT_J;
        case CTRL('H'): return CTRL_ALT_H;
        case CTRL('L'): return CTRL_ALT_L;

        default: return ESC;
    }
}

void update_window_main(void) {
    box(win_main.win, 0, 0);
    Cursor *player_pos = get_player_pos();
    mvwaddch(win_main.win, player_pos->y, player_pos->x, '@');
    mvwprintw(win_main.win, 0, 0, "Player '%s'", game.data.player.self.name);
    if (strlen(game.message)) mvwprintw(win_main.win, win_main.height-2, 0, "Message: %s", game.message);
    mvwprintw(win_main.win, win_main.height-1, 0, "Total time: %.3f", game.data.total_time);
}

#define update_window(window_name)           \
    do {                                     \
        werase(win_##window_name.win);       \
        update_window_##window_name();       \
        wnoutrefresh(win_##window_name.win); \
    } while (0)

void update_windows(void)
{
    update_window(main);
}

void update_cursor(void)
{
    Cursor *player_pos = get_player_pos();
    size_t cy = player_pos->y;
    size_t cx = player_pos->x;
    WINDOW *win = win_main.win;

    wmove(win, cy, cx);
    wnoutrefresh(win);
}

void handle_sigwinch(int signo)
{
    (void)signo;
    get_screen_size();
    destroy_windows();
    create_windows();
    
    Cursor *player_pos = get_player_pos();
    if (player_pos->y >= win_main.height) player_pos->y = win_main.height - 1;
    if (player_pos->x >= win_main.width)  player_pos->x = win_main.width - 1;
}

void game_init()
{
    load_game_data();
    get_screen_size();
    signal(SIGWINCH, handle_sigwinch);
}

void itoa(int n, char *buf)
{
    if (n == 0) {
        buf[0] = '0';
        return;
    }
    char tmp[64] = {0};
    int i = 0;
    while (n > 0) {
        tmp[i] = n % 10 + '0';
        i++;
        n /= 10;
    }
    int len = strlen(tmp);
    for (int i = 0; i < len; i++) buf[i] = tmp[len-i-1];
}

static inline void move_cursor_up(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->y > 0) player_pos->y--;
}
static inline void move_cursor_down(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->y < screen_rows-1) player_pos->y++;
}
static inline void move_cursor_left(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->x > 0) player_pos->x--;
}
static inline void move_cursor_right(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->x < screen_cols-1) player_pos->x++;
}

bool can_quit(void) { return true; }

_Noreturn void quit(void)
{
    ncurses_end();
    exit(0);
}

void process_pressed_key(void)
{
    int key = read_key();
    if (key == ERR) return;

    switch (key)
    {
        case 'w':
        case KEY_UP: move_cursor_up(); break;

        case 's':
        case KEY_DOWN: move_cursor_down(); break;

        case 'a':
        case KEY_LEFT: move_cursor_left(); break;

        case 'd':
        case KEY_RIGHT: move_cursor_right(); break;

        case CTRL_Q:
            if (can_quit()) quit();
            break;

        case CTRL_S:
            save_game_data();
            break;

        //case ALT_0:
        //case ALT_1:
        //case ALT_2:
        //case ALT_3:
        //case ALT_4:
        //case ALT_5:
        //case ALT_6:
        //case ALT_7:
        //case ALT_8:
        //case ALT_9:
        //case KEY_UP:
        //case ALT_k:
        //case KEY_DOWN:
        //case ALT_j:
        //case KEY_LEFT:
        //case ALT_h:
        //case KEY_RIGHT:
        //case ALT_l:
        //case ALT_K:
        //case ALT_J:
        //case ALT_H:
        //case ALT_L:
        //case ALT_m:
        //case ALT_p:
        //case ALT_n:
        //case ALT_c:
        //case ALT_C:
        //case CTRL_ALT_C:
        //case CTRL_K:
        //case CTRL_J:
        //case CTRL_H:
        //case CTRL_L:
        //case KEY_PPAGE:
        //case CTRL_ALT_K:
        //case KEY_NPAGE:
        //case CTRL_ALT_J:
        //case ALT_COLON:
        //case ALT_BACKSPACE:
        //case TAB:
        //case KEY_BTAB:
        //case ENTER:
        //case KEY_BACKSPACE:
        //case ESC:

        default:
            if (isprint(key)) log_this("Unprocessed key '%c'", key);
            else log_this("Unprocessed key %d", key);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ncurses_init();
    game_init();
    initialize_colors();
    create_windows();

    float current_time = get_time_in_seconds();
    float last_time = current_time;
    float dt = 0.f;

    while (true) {
        current_time = get_time_in_seconds();
        dt = current_time - last_time;
        last_time = current_time;
        game.data.total_time += dt;

        process_pressed_key();
        update_windows();
        update_cursor();
        doupdate();

        advance_save_timer(dt);
    }

    return 0;
}
