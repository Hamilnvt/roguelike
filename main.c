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
    RANK_KING,
    ENTITY_RANKS_COUNT
} EntityRank;

char entity_rank_char(EntityRank r)
{
    switch (r)
    {
    case RANK_1:    return '1';
    case RANK_2:    return '2';
    case RANK_3:    return '3';
    case RANK_KING: return 'K';

    case ENTITY_RANKS_COUNT:
    default: print_error_and_exit("Unreachable entity rank %u", r);
    }
}

typedef struct Cell Cell;
typedef void (* Effect) (Cell *self, Cell *cells, size_t width, size_t height);

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

//   0 1 2 3
// 0 @     -
// 1     1 -
// 2   K   -
// 3 - - - -

typedef enum
{
    CELL_BLANK = 0,
    CELL_PLAYER,
    CELL_ENTITY,
    CELL_WALL,
    CELL_DOOR
} CellType;

typedef struct
{
    Cursor pos;
    bool destructible;
} Wall;

typedef struct Room Room;
typedef struct
{
    Cursor pos;
    bool openable;
    bool open;
    Room *to;
} Door;

typedef struct Cell
{
    CellType type;
    Cursor pos;
    union {
        Entity *entity;
        Wall *wall;
        Door *door;
    };
} Cell;

char cell_char(Cell cell)
{
    switch (cell.type)
    {
    case CELL_BLANK:  return ' ';
    case CELL_PLAYER: return '@';
    case CELL_ENTITY: return entity_rank_char(cell.entity->rank);
    case CELL_WALL:   return '#';
    case CELL_DOOR:   return 'O';
    default: print_error_and_exit("Unreachable cell type %u in cell_char", cell.type);
    }
}

static inline Cell make_cell_entity(Entity *e)
{
    return (Cell){
        .type = CELL_ENTITY,
        .entity = e,
        .pos = e->pos
    };
}

static inline Cell make_cell_entity_random_at(size_t x, size_t y)
{
    Entity *e = malloc(sizeof(Entity));
    memcpy(e->name, "Entity", 6); // TODO: random name based on rank
    e->pos = (Cursor){x, y};
    // TODO:
       // > if an entity spawns on top of another entity it triggers some event:
       //   - on player: ambush
       //   - on another entity: combat
       // > maybe it's better to have a fixed grid rather than a dynamic array, so that I can easily check if the slot is already occupied
       //   - I can then create rooms with walls and whatnot (screen is then a graphical representation of the grid)
       //   - If slot is, say, -1 then it's not possible to spawn an entity there, try again...
    e->rank = rand() % ENTITY_RANKS_COUNT;
    e->level = rand() % (10*(e->rank+1));
    e->chance = rand() % (100*(e->rank+1));
    e->attack = rand() % (100*(e->rank+1));

    return make_cell_entity(e);
}

static inline Cell make_cell_entity_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (rand() % (x_high - x_low)) + x_low;
    size_t y = (rand() % (y_high - y_low)) + y_low;
    return make_cell_entity_random_at(x, y);
}

static inline Cell make_cell_wall(Wall *w)
{
    return (Cell){
        .type = CELL_WALL,
        .wall = w,
        .pos = w->pos
    };
}

static inline Cell make_cell_wall_random_at(size_t x, size_t y)
{
    Wall *w = malloc(sizeof(Wall));
    w->pos = (Cursor){x, y};
    w->destructible = rand()%2;
    return make_cell_wall(w);
}

static inline Cell make_cell_wall_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (rand() % (x_high - x_low)) + x_low;
    size_t y = (rand() % (y_high - y_low)) + y_low;
    return make_cell_wall_random_at(x, y);
}

static inline Cell make_cell_door(Door *d)
{
    return (Cell){
        .type = CELL_DOOR,
        .door = d,
        .pos = d->pos
    };
}

static inline Cell make_cell_door_random_at(size_t x, size_t y)
{
    Door *d = malloc(sizeof(Door));
    d->pos = (Cursor){x, y};
    d->openable = true;
    d->open = rand()%2;
    d->to = NULL; // TODO
    return make_cell_door(d);
}

static inline Cell make_cell_door_random(size_t x_low, size_t x_high, size_t y_low, size_t y_high)
{
    size_t x = (rand() % (x_high - x_low)) + x_low;
    size_t y = (rand() % (y_high - y_low)) + y_low;
    return make_cell_door_random_at(x, y);
}

typedef struct Room
{
    Cell *cells;
    size_t width;
    size_t height;
} Room;

Room generate_room(size_t width, size_t height)
{
    Room room = {
        .cells = malloc(sizeof(Cell)*width*height),
        .width = width,
        .height = height
    };
    int doors_count = (rand() % 2) + 1;
    int entities_count = (rand() % 10) + 1;
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            size_t index = y*width + x;
            if (x == 0 || y == 0 || x == width-1 || y == height-1) {
                room.cells[index] = make_cell_wall_random_at(x, y);
            } else if (doors_count > 0) {
                room.cells[index] = make_cell_door_random_at(x, y);
                doors_count--;
            } else if (entities_count > 0) {
                room.cells[index] = make_cell_entity_random_at(x, y);
                entities_count--;
            }
        }
    }
    return room;
}

typedef struct
{
    Room *items;
    size_t count;
    size_t capacity;
} Rooms;

typedef struct
{
    Entity *self;
    size_t xp;
} Player;

typedef struct
{
    Player player;
    float total_time;
    Rooms rooms;
} Data;

typedef struct
{
    Data data;
    Room *current_room;
    float save_timer;
    char message[256];
} Game;
static Game game = {0};

static inline Cursor *get_player_pos(void) { return &game.data.player.self->pos; }

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

void spawn_random_entity(void)
{
    int tries = 10;
    size_t x;
    size_t y;
    while (tries > 0) {
        x = rand() % game.current_room->width;
        y = rand() % game.current_room->height;
        Cell cell = game.current_room->cells[y*game.current_room->width + x];
        if (cell.type == CELL_BLANK || cell.type == CELL_ENTITY) {
            make_cell_entity_random_at(x, y);
            return;
        } else tries--;
    }
}

void save_entity(FILE *f, Entity *e)
{
    // Name
    fwrite(e->name, sizeof(char), sizeof(e->name), f);
    
    // POD
    fwrite(&e->pos,    sizeof(Cursor),     1, f);
    fwrite(&e->rank,   sizeof(EntityRank), 1, f);
    fwrite(&e->level,  sizeof(size_t),     1, f);
    fwrite(&e->chance, sizeof(int),        1, f);
    fwrite(&e->attack, sizeof(int),        1, f);
    
    // Effects TODO
    // 3. IMPORTANT: Do NOT write the 'effects' struct directly.
    // It contains pointers. For now, we skip it.
}

#define SAVE_FILEPATH "./save.bin"
bool save_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "wb");    
    if (!save_file) return false;

    // Player
    save_entity(save_file, game.data.player.self);
    fwrite(&game.data.player.xp, sizeof(size_t), 1, save_file);

    // POD
    fwrite(&game.data.total_time, sizeof(float), 1, save_file);

    // Rooms TODO
    //fwrite(&game.data.entities.count, sizeof(size_t), 1, save_file);
    //da_foreach (game.data.entities, Entity*, e)
    //    save_entity(save_file, *e);

    fclose(save_file);
    write_message("saved");
    return true;
}

bool load_entity(FILE  *f, Entity *e)
{
    // Name
    if (fread(e->name, sizeof(char), sizeof(e->name), f) != sizeof(e->name)) return false;

    // POD
    if (fread(&e->pos,    sizeof(Cursor),     1, f) != 1) return false;
    log_this("Loaded entity position (%zu, %zu)", e->pos.x, e->pos.y);
    if (fread(&e->rank,   sizeof(EntityRank), 1, f) != 1) return false;
    if (fread(&e->level,  sizeof(size_t),     1, f) != 1) return false;
    if (fread(&e->chance, sizeof(int),        1, f) != 1) return false;
    if (fread(&e->attack, sizeof(int),        1, f) != 1) return false;

    // Effects TODO
    // Reset pointers to NULL to prevent crashes
    e->effects.items = NULL;
    e->effects.count = 0;
    e->effects.capacity = 0;

    return true;
}

bool load_game_data(void)
{
    FILE *save_file = fopen(SAVE_FILEPATH, "rb");    
    if (!save_file) {
        if (errno == ENOENT) return save_game_data();
        else return false;
    }

    // Player
    if (!load_entity(save_file, game.data.player.self)) goto fail;
    log_this("it was player's position");
    if (fread(&game.data.player.xp, sizeof(size_t), 1, save_file) != 1) goto fail;

    // POD
    if (fread(&game.data.total_time, sizeof(float), 1, save_file) != 1) goto fail;

    // Rooms TODO
    //da_clear(&game.data.entities);
    //size_t count = 0;
    //if (fread(&count, sizeof(size_t), 1, save_file) != 1) goto fail;
    //if (count > 0) {
    //    game.data.entities.count = count;
    //    game.data.entities.capacity = count;
    //    game.data.entities.items = malloc(count * sizeof(Entity));
    //    da_foreach (game.data.entities, Entity*, e)
    //        if (!load_entity(save_file, *e)) goto fail;
    //}

    fclose(save_file);
    return true;

fail:
    fclose(save_file);
    return false;
}

#define SAVE_TIME_INTERVAL 15.f
void advance_save_timer(float dt)
{
    game.save_timer += dt;
    if (game.save_timer >= SAVE_TIME_INTERVAL) {
        game.save_timer = 0.f;
        if (!save_game_data()) print_error_and_exit("Could not save game");
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
    TAB       = 9,
    ENTER     = 13,
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
    CTRL_ALT_E,
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
    get_screen_size();
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
    log_this("Program received signal %d: %s", sig, strsignal(sig));
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
        case CTRL('E'): return CTRL_ALT_E;
        case CTRL('K'): return CTRL_ALT_K;
        case CTRL('J'): return CTRL_ALT_J;
        case CTRL('H'): return CTRL_ALT_H;
        case CTRL('L'): return CTRL_ALT_L;

        default: return ESC;
    }
}

void update_window_main(void) {
    box(win_main.win, 0, 0);

    for (size_t i = 0; i < game.current_room->width*game.current_room->height; i++) {
        Cell cell = game.current_room->cells[i];
        mvwaddch(win_main.win, cell.pos.y+1, cell.pos.x+1, cell_char(cell));
    }

    mvwprintw(win_main.win, 0, 0, "Player '%s'", game.data.player.self->name);
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
    //load_game_data(); // TODO rimetti

    // TODO: poi verranno caricati dal file di salvataggio
    {
        game.data.player = (Player){0};

        Entity *e = malloc(sizeof(Entity));
        memcpy(e->name, "Adventurer", 10);
        size_t player_x = 20;
        size_t player_y = 20;
        e->pos = (Cursor){player_x, player_y};
        e->rank = RANK_1;
        e->level = 1;
        e->chance = 50;
        e->attack = 10;
        e->effects = (Effects){0}; 
        game.data.player.self = e;

        Room initial_room = generate_room(win_main.width-2, win_main.height-2);
        size_t player_index = player_y*initial_room.width + player_x;
        initial_room.cells[player_index] = (Cell){ .type = CELL_PLAYER, .pos = e->pos }; // TODO: devo controllare che non ci fosse qualcos'altro e fare una free altrimenti leako un po' di memoria
        da_push(&game.data.rooms, initial_room);
        game.current_room = &game.data.rooms.items[0];
    }
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
    if (player_pos->y > 1) player_pos->y--;
}
static inline void move_cursor_down(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->y < screen_rows-2) player_pos->y++;
}
static inline void move_cursor_left(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->x > 1) player_pos->x--;
}
static inline void move_cursor_right(void)
{
    Cursor *player_pos = get_player_pos();
    if (player_pos->x < screen_cols-2) player_pos->x++;
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

        case CTRL('E'):
            spawn_random_entity();
            break;

        case CTRL_ALT_E:
            // TODO: I have to free all the entities
            write_message("TODO: clear all entities");
            break;

        case CTRL('S'):
            if (!save_game_data()) print_error_and_exit("Could not save game");
            break;

        case CTRL('Q'):
            if (can_quit()) quit();
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

    srand(time(NULL));
    signal(SIGWINCH, handle_sigwinch);
    ncurses_init();
    initialize_colors();
    create_windows();
    game_init();

    //memcpy(game.data.player.self->name, "Hamilnvt", 8);

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

        //advance_save_timer(dt); // TODO rimetti
    }

    return 0;
}
