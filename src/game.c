#include "game.h"

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
        case 'i'          : return ALT_i;
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
        case CTRL('D'): return CTRL_ALT_D;
        case CTRL('E'): return CTRL_ALT_E;
        case CTRL('K'): return CTRL_ALT_K;
        case CTRL('J'): return CTRL_ALT_J;
        case CTRL('H'): return CTRL_ALT_H;
        case CTRL('L'): return CTRL_ALT_L;

        default: return ESC;
    }
}

_Noreturn void quit(void)
{
    save_data();
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
        case KEY_UP:
            if (game.map.enabled) {
                if (game.map.index > 0) {
                    game.map.index--;
                    if (game.map.index < game.map.offset) game.map.offset = game.map.index;
                }
            } else player_move(DIRECTION_UP);
            break;

        case 's':
        case KEY_DOWN: 
            if (game.map.enabled) {
                if (game.map.index < game.data.map.count-1) {
                    game.map.index++;
                    if (game.map.index >= (game.map.offset + win_main.height - 2))
                        game.map.offset++;
                }
            } else player_move(DIRECTION_DOWN);
            break;

        case 'a':
        case KEY_LEFT: player_move(DIRECTION_LEFT); break;

        case 'd':
        case KEY_RIGHT: player_move(DIRECTION_RIGHT); break;

        case 'e':
            player_equip_all();
            break;

        case CTRL('E'):
            spawn_random_entity(CURRENT_ROOM);
            break;

        case CTRL('I'):
            game.showing_general_info = !game.showing_general_info;
            game.showing_help = false;
            game.show_entities_info.enabled = false;
            break;

        case CTRL('H'):
            game.showing_help = !game.showing_help;
            game.showing_general_info = false;
            game.show_entities_info.enabled = false;
            break;

        case ENTER: check_player_look_direction(); break;

        case 'l': game.looking = !game.looking; break;

        case 'm':
            game.map.enabled = !game.map.enabled;
            game.map.index = CURRENT_ROOM->index;
            game.map.offset = CURRENT_ROOM->index - ((win_main.height-1)/2-1);
            break;

        case CTRL('S'): save_data(); break;

        case 'q': quit();

        case ESC:
            if (game.show_entities_info.enabled) {
                game.show_entities_info.enabled = false;
                game.show_entities_info.index = 0;
            }
            break;

        case 'c':
            if (game.map.enabled) game.map.show_current_connections = !game.map.show_current_connections;
            break;

        case 't':
            game.showing_tooltips = !game.showing_tooltips;
            if (game.showing_tooltips) write_message("Show tooltips");
            else write_message("Hide tooltips");
            break;

        default:
            if (isprint(key)) log_this("Unprocessed key '%c'", key);
            else log_this("Unprocessed key %d", key);
    }
}

void game_init(void)
{
    signal(SIGWINCH, handle_sigwinch);
    ncurses_init();
    create_windows();

    if (!load_data()) {
        write_message("Creating new save file...");
        init_data();
        save_data();
    } else write_message("Save loaded!");
}

void add_message(const char *message)
{
    if (game.messages.lines[game.messages.head]) free(game.messages.lines[game.messages.head]);

    game.messages.lines[game.messages.head] = strdup(message);
    game.messages.head = (game.messages.head + 1) % MAX_MESSAGES;

    if (game.messages.count < MAX_MESSAGES) game.messages.count++;
}

void write_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(game.messages.buffer, 0, sizeof(game.messages.buffer));
    vsnprintf(game.messages.buffer, sizeof(game.messages.buffer), fmt, ap);
    va_end(ap);
    log_this("> %s", game.messages.buffer);
    add_message(game.messages.buffer);
}

void write_string_to_message(String string) { write_message(S_FMT, S_ARG(string)); }
