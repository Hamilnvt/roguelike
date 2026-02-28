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

/// BEGIN Name Generator

typedef struct {
    const char *text;
    size_t length;
    size_t weight;
    bool can_start;
    bool can_end;
} Chunk;

#define CHUNK(str, w, cs, ce) {(str), sizeof(str) - 1, (w), (cs), (ce)}
#define CAN_START true
#define CAN_END true

static const Chunk vowels[] = {
    CHUNK("a", 50, CAN_START, CAN_END), CHUNK("e", 50, CAN_START, CAN_END), CHUNK("i", 40, CAN_START, CAN_END), 
    CHUNK("o", 40, CAN_START, CAN_END), CHUNK("u", 30, CAN_START, CAN_END), CHUNK("y", 15, CAN_START, CAN_END),

    CHUNK("ae", 10, CAN_START, CAN_END), CHUNK("ai", 10, CAN_START, !CAN_END), CHUNK("ea", 15, CAN_START, CAN_END), 
    CHUNK("ee", 15, CAN_START, CAN_END), CHUNK("ei", 10, CAN_START, CAN_END), CHUNK("ie", 10, CAN_START, CAN_END), 
    CHUNK("oa", 10, CAN_START, CAN_END), CHUNK("oo", 15, CAN_START, CAN_END), CHUNK("ou", 10, CAN_START, !CAN_END),

    CHUNK("eau", 4, CAN_START, CAN_END), CHUNK("ieu", 2, !CAN_START, CAN_END)
};
const size_t vowels_count = sizeof(vowels) / sizeof(vowels[0]);

static const Chunk consonants[] = {
    CHUNK("b", 40, CAN_START, CAN_END), CHUNK("c", 30, CAN_START, CAN_END), CHUNK("d", 40, CAN_START, CAN_END), 
    CHUNK("f", 30, CAN_START, CAN_END), CHUNK("g", 30, CAN_START, CAN_END), CHUNK("h", 30, CAN_START, CAN_END), 
    CHUNK("j", 15, CAN_START, CAN_END), CHUNK("k", 30, CAN_START, CAN_END), CHUNK("l", 50, CAN_START, CAN_END), 
    CHUNK("m", 40, CAN_START, CAN_END), CHUNK("n", 50, CAN_START, CAN_END), CHUNK("p", 30, CAN_START, CAN_END), 
    CHUNK("q", 5,  CAN_START, CAN_END), CHUNK("r", 50, CAN_START, CAN_END), CHUNK("s", 50, CAN_START, CAN_END), 
    CHUNK("t", 50, CAN_START, CAN_END), CHUNK("v", 20, CAN_START, CAN_END), CHUNK("w", 20, CAN_START, CAN_END), 
    CHUNK("x", 10, CAN_START, CAN_END), CHUNK("z", 10, CAN_START, CAN_END),
    
    CHUNK("qu", 20, CAN_START, !CAN_END), CHUNK("ch", 25, CAN_START, CAN_END), CHUNK("sh", 25, CAN_START, CAN_END), 
    CHUNK("th", 25, CAN_START, CAN_END), CHUNK("ph", 15, CAN_START, CAN_END), CHUNK("ck", 15, !CAN_START, CAN_END), 
    CHUNK("ng", 15, !CAN_START, CAN_END), CHUNK("bj", 10, CAN_START, !CAN_END), CHUNK("fj", 8, CAN_START, !CAN_END),
    CHUNK("kv", 8, CAN_START, !CAN_END), CHUNK("sv", 10, CAN_START, !CAN_END), CHUNK("kn", 10, CAN_START, !CAN_END),
    CHUNK("gn", 8, CAN_START, !CAN_END), CHUNK("pf", 10, CAN_START, CAN_END),

    CHUNK("sch", 15, CAN_START, CAN_END), CHUNK("chl", 5, CAN_START, !CAN_END), CHUNK("rst", 5, !CAN_START, CAN_END), 
    CHUNK("rtz", 4, !CAN_START, CAN_END), CHUNK("ndr", 5, !CAN_START, CAN_END), CHUNK("lch", 4, !CAN_START, CAN_END),
    CHUNK("str", 10, CAN_START, !CAN_END), CHUNK("spl", 5, CAN_START, !CAN_END), CHUNK("scr", 5, CAN_START, !CAN_END),

    CHUNK("schm", 5, CAN_START, !CAN_END), CHUNK("schn", 5, CAN_START, !CAN_END), 
    CHUNK("tsch", 5, CAN_START, CAN_END), CHUNK("tzsch", 2, !CAN_START, CAN_END)
};
static const size_t consonants_count = sizeof(consonants) / sizeof(consonants[0]);

void generate_name(char *output_buffer, size_t max_len)
{
    if (max_len <= 1) return;
    size_t min_chunks = 2;
    size_t max_chunks = (max_len - 1) / 2.5;
    if (max_chunks < min_chunks) max_chunks = min_chunks; 
    size_t target_chunks = min_chunks + (rng_generate(ENTITIES_RNG) % (max_chunks - min_chunks + 1));

    if (max_len == 0) return;
    
    bool is_vowel_turn = rng_bernoulli(ENTITIES_RNG, 0.5);
    
    size_t current_len = 0;
    char *write_ptr = output_buffer;
    *write_ptr = '\0'; 
    
    for (size_t i = 0; i < target_chunks; i++) {
        bool is_start = (i == 0);
        bool is_end = (i == target_chunks - 1);
        
        const Chunk *pool = is_vowel_turn ? vowels : consonants;
        size_t pool_size = is_vowel_turn ? vowels_count : consonants_count;
        
        size_t total_weight = 0;
        for (size_t j = 0; j < pool_size; j++) {
            if (is_start && !pool[j].can_start) continue;
            if (is_end && !pool[j].can_end) continue;
            total_weight += pool[j].weight;
        }
        
        if (total_weight == 0) break;
        
        size_t random_roll = rng_generate(ENTITIES_RNG) % total_weight;
        size_t cumulative_weight = 0;
        const Chunk *selected_chunk = NULL;
        
        for (size_t j = 0; j < pool_size; j++) {
            if (is_start && !pool[j].can_start) continue;
            if (is_end && !pool[j].can_end) continue;
            
            cumulative_weight += pool[j].weight;
            if (random_roll < cumulative_weight) {
                selected_chunk = &pool[j];
                break;
            }
        }
        
        if (selected_chunk) {
            if (current_len + selected_chunk->length < max_len) {
                memcpy(write_ptr, selected_chunk->text, selected_chunk->length);
                write_ptr += selected_chunk->length;
                current_len += selected_chunk->length;
                *write_ptr = '\0';
            } else {
                break;
            }
        }
        
        is_vowel_turn = !is_vowel_turn; 
    }
    
    if (current_len > 0) output_buffer[0] = toupper((unsigned char)output_buffer[0]);
}

/// END Name Generator

