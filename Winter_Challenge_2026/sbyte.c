#define _POSIX_C_SOURCE 200809L
#pragma GCC optimize("Ofast,unroll-loops,omit-frame-pointer,inline")
#pragma GCC option("arch=native", "tune=native", "no-zero-upper")
#pragma GCC target("movbe,aes,pclmul,avx,avx2,f16c,fma,sse3,ssse3,sse4.1,sse4.2,rdrnd,popcnt,bmi,bmi2,lzcnt")

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

/* --- CONSTANTS --- */

static const float BS_EXP_FACTOR = 0.99f;
#define BS_WIDTH       100
#define BS_MAX_DEPTH   50
#define BS_MAX_TIME    30000

typedef int16_t Pos;

#define MAX_MAP_WIDTH  45
#define MAX_MAP_HEIGHT 30

#define MAP_PADDING    2
#define MAX_WIDTH      (MAX_MAP_WIDTH  + 2 * MAP_PADDING)
#define MAX_HEIGHT     (MAX_MAP_HEIGHT + 2 * MAP_PADDING)
#define MAX_CELL_COUNT (MAX_WIDTH * MAX_HEIGHT)

#define MAX_ENERGY_COUNT 100

/* --- MAP PROPERTIES --- */

typedef struct {
    int width;
    int height;
    int my_id;
    int opp_id;
} MapProperties;

static MapProperties map_properties;

/* --- POS --- */

#define NORTH_POS_OFFSET ((Pos)(-MAX_WIDTH))
#define WEST_POS_OFFSET  ((Pos)(-1))
#define EAST_POS_OFFSET  ((Pos)(1))
#define SOUTH_POS_OFFSET ((Pos)(MAX_WIDTH))

static inline Pos get_pos(int x, int y)             { return (Pos)(y * MAX_WIDTH + x); }
static inline Pos get_pos_from_map_coord(int x, int y) { return (Pos)((y + MAP_PADDING) * MAX_WIDTH + (x + MAP_PADDING)); }

static inline int get_x(Pos pos) { return pos % MAX_WIDTH; }
static inline int get_y(Pos pos) { return pos / MAX_WIDTH; }
static inline int get_map_x(Pos pos) { return get_x(pos) - MAP_PADDING; }
static inline int get_map_y(Pos pos) { return get_y(pos) - MAP_PADDING; }

/* --- SNAKE --- */

#define MAX_SNAKE_SIZE         100
#define MAX_SNAKE_COUNT        8
#define MAX_PLAYER_SNAKE_COUNT (MAX_SNAKE_COUNT / 2)

#define DIR_NORTH ((uint8_t)0)
#define DIR_SOUTH ((uint8_t)1)
#define DIR_WEST  ((uint8_t)2)
#define DIR_EAST  ((uint8_t)3)
#define DIR_ARRAY_SIZE (MAX_SNAKE_SIZE - 1)

static const Pos DIR_TO_OFFSET[4] = {
    NORTH_POS_OFFSET, SOUTH_POS_OFFSET, WEST_POS_OFFSET, EAST_POS_OFFSET
};

static inline uint8_t pos_offset_to_dir(Pos offset)
{
    if (offset == NORTH_POS_OFFSET) return DIR_NORTH;
    if (offset == SOUTH_POS_OFFSET) return DIR_SOUTH;
    if (offset == WEST_POS_OFFSET)  return DIR_WEST;
    return DIR_EAST;
}

typedef struct {
    int id;
    int player_id;
    Pos head_pos;
    Pos tail_pos;
    int body_length;
    uint8_t directions[DIR_ARRAY_SIZE];
} Snake;

static inline int    get_snake_id(Snake *snake)          { return snake->id; }
static inline int    get_snake_player_id(Snake *snake)   { return snake->player_id; }
static inline uint8_t get_direction(Snake *snake, int i) { return snake->directions[i]; }
static inline void   set_direction(Snake *snake, int i, uint8_t dir) { snake->directions[i] = dir; }
static inline Pos    get_snake_head_pos(Snake *snake)    { return snake->head_pos; }
static inline int    get_snake_body_length(Snake *snake) { return snake->body_length; }
static inline void   set_snake_body_length(Snake *snake, int length) { snake->body_length = length; }

static Pos get_snake_body_pos(Snake *snake, int index)
{
    Pos pos = snake->head_pos;
    for (int i = 0; i < index; i++)
        pos += DIR_TO_OFFSET[get_direction(snake, i)];
    return pos;
}

static void set_snake_body_pos(Snake *snake, int index, Pos new_pos)
{
    if (index == 0)
    {
        if (snake->body_length > 1)
        {
            Pos old_pos1 = snake->head_pos + DIR_TO_OFFSET[get_direction(snake, 0)];
            snake->head_pos = new_pos;
            set_direction(snake, 0, pos_offset_to_dir(old_pos1 - new_pos));
        }
        else
        {
            snake->head_pos = new_pos;
            snake->tail_pos = new_pos;
        }
    }
    else
    {
        Pos prev_pos = snake->head_pos;
        for (int i = 0; i < index - 1; i++)
            prev_pos += DIR_TO_OFFSET[get_direction(snake, i)];

        Pos old_pos_at_index = prev_pos + DIR_TO_OFFSET[get_direction(snake, index - 1)];

        set_direction(snake, index - 1, pos_offset_to_dir(new_pos - prev_pos));

        if (index < snake->body_length - 1)
        {
            Pos old_next_pos = old_pos_at_index + DIR_TO_OFFSET[get_direction(snake, index)];
            set_direction(snake, index, pos_offset_to_dir(old_next_pos - new_pos));
        }

        if (index == snake->body_length - 1)
            snake->tail_pos = new_pos;
    }
}

static void add_body_pos(Snake *snake, Pos pos)
{
    if (snake->body_length == 0)
    {
        snake->head_pos = pos;
    }
    else
    {
        set_direction(snake, snake->body_length - 1, pos_offset_to_dir(pos - snake->tail_pos));
    }
    snake->tail_pos = pos;
    snake->body_length++;
}

static void remove_snake_head(Snake *snake)
{
    snake->head_pos += DIR_TO_OFFSET[snake->directions[0]];
    int remaining = snake->body_length - 2;
    if (remaining > 0)
        memmove(&snake->directions[0], &snake->directions[1], remaining);
    snake->body_length--;
}

static inline void reset_snake_length(Snake *snake) { snake->body_length = 0; }

static void shift_snake_body(Snake *snake, Pos offset)
{
    snake->head_pos += offset;
    snake->tail_pos += offset;
}

static inline void reconstruct_body(Snake *snake, Pos *out)
{
    out[0] = snake->head_pos;
    for (int i = 1; i < snake->body_length; i++)
        out[i] = out[i - 1] + DIR_TO_OFFSET[get_direction(snake, i - 1)];
}

static void initialize_snake(Snake *snake, int snake_id, int player_id)
{
    snake->id = snake_id;
    snake->player_id = player_id;
    memset(snake->directions, 0, DIR_ARRAY_SIZE);
    snake->head_pos = 0;
    snake->tail_pos = 0;
    snake->body_length = 3;
}

/* --- ACTION / MOVE --- */

typedef struct {
    int snake_id;
    Pos dst_pos;
} Move;

static inline int  get_move_snake_id(Move *move)    { return move->snake_id; }
static inline Pos  get_move_dst_pos(Move *move)      { return move->dst_pos; }
static inline void set_move_snake_id(Move *move, int id)  { move->snake_id = id; }
static inline void set_move_dst_pos(Move *move, Pos pos)  { move->dst_pos = pos; }

/* 3^4 = 81 */
#define MAX_PLAYER_MOVE_SETS 81

typedef struct {
    Move moves[MAX_SNAKE_COUNT];
    int  move_count;
} MoveSet;

static inline Move *get_moveset_move(MoveSet *moveset, int i)      { return &moveset->moves[i]; }
static inline int   get_moveset_move_count(MoveSet *moveset)        { return moveset->move_count; }
static inline void  set_moveset_move(MoveSet *moveset, Move *move, int i) { moveset->moves[i] = *move; }
static inline void  set_moveset_move_count(MoveSet *moveset, int c) { moveset->move_count = c; }

static void print_moveset(MoveSet moveset)
{
    fprintf(stderr, "MoveSet (move_count=%d) : ", moveset.move_count);
    for (int i = 0; i < moveset.move_count; i++)
        fprintf(stderr, "S=%d: %d %d | ", moveset.moves[i].snake_id,
                get_map_x(moveset.moves[i].dst_pos), get_map_y(moveset.moves[i].dst_pos));
    fprintf(stderr, "\n");
}

/* --- STATE --- */

typedef uint8_t CellType;

#define CELL_EMPTY    ((CellType)8)
#define CELL_PLATFORM ((CellType)9)
#define CELL_ENERGY   ((CellType)10)

typedef struct {
    int   turn;
    bool  game_ended;
    int   player_losses[2];
    int   energy_count;

    CellType cells[MAX_CELL_COUNT];
    Snake    snakes[MAX_SNAKE_COUNT];

    int player_alive_snake_count[2];
    int player_alive_snake_ids[2][MAX_PLAYER_SNAKE_COUNT];

    int alive_snake_count;
    int alive_snake_ids[MAX_SNAKE_COUNT];

    float heuristic;
    float heuristic_depth_weight;
    float state_evaluation;

    MoveSet first_depth_moveset;
} State;

static const size_t SIZE_OF_STATE = sizeof(State);
static const size_t SIZE_OF_INT   = sizeof(int);

static inline int      get_turn(State *state)                          { return state->turn; }
static inline bool     is_game_ended(State *state)                     { return state->game_ended; }
static inline int      get_player_losses(State *state, int pid)        { return state->player_losses[pid]; }
static inline int      get_energy_count(State *state)                  { return state->energy_count; }
static inline CellType get_cell(State *state, Pos pos)                 { return state->cells[pos]; }
static inline Snake   *get_snake(State *state, int snake_id)           { return &state->snakes[snake_id]; }
static inline int      get_player_alive_snake_count(State *s, int pid) { return s->player_alive_snake_count[pid]; }
static inline int      get_player_alive_snake_id(State *s, int pid, int i) { return s->player_alive_snake_ids[pid][i]; }
static inline int      get_alive_snake_count(State *state)             { return state->alive_snake_count; }
static inline int      get_alive_snake_id(State *state, int i)         { return state->alive_snake_ids[i]; }
static inline float    get_heuristic(State *state)                     { return state->heuristic; }
static inline MoveSet *get_first_depth_moveset(State *state)           { return &state->first_depth_moveset; }

static inline void set_turn(State *state, int turn)                         { state->turn = turn; }
static inline void set_game_ended(State *state)                             { state->game_ended = true; }
static inline void set_player_losses(State *s, int pid, int v)              { s->player_losses[pid] = v; }
static inline void add_player_loss(State *s, int pid, int v)                { s->player_losses[pid] += v; }
static inline void set_energy_count(State *state, int count)                { state->energy_count = count; }
static inline void set_cell(State *state, Pos pos, CellType val)            { state->cells[pos] = val; }
static inline void set_heuristic(State *state, float h)                     { state->heuristic = h; }
static inline void set_first_depth_moveset(State *state, MoveSet *moveset)  { state->first_depth_moveset = *moveset; }

static void initialize_cells(State *state)
{
    for (int i = 0; i < MAX_CELL_COUNT; i++)
        state->cells[i] = CELL_EMPTY;
}

static void initialize_snake_data(State *state, int snake_id, int player_id)
{
    initialize_snake(get_snake(state, snake_id), snake_id, player_id);
}

static void reset_alive_snake_count(State *state)
{
    state->alive_snake_count = 0;
    state->player_alive_snake_count[0] = 0;
    state->player_alive_snake_count[1] = 0;
}

static void add_player_alive_snake_id(State *state, int player_id, int snake_id)
{
    state->alive_snake_ids[state->alive_snake_count++] = snake_id;
    state->player_alive_snake_ids[player_id][state->player_alive_snake_count[player_id]++] = snake_id;
}

static void remove_snake_from_alive_snake_ids(State *state, int snake_id, int player_id)
{
    for (int i = 0; i < get_alive_snake_count(state); i++)
    {
        if (get_alive_snake_id(state, i) == snake_id)
        {
            if (i + 1 < get_alive_snake_count(state))
                memmove(&state->alive_snake_ids[i], &state->alive_snake_ids[i + 1],
                        SIZE_OF_INT * (get_alive_snake_count(state) - i - 1));
            state->alive_snake_count--;
            break;
        }
    }

    for (int i = 0; i < get_player_alive_snake_count(state, player_id); i++)
    {
        if (get_player_alive_snake_id(state, player_id, i) == snake_id)
        {
            if (i + 1 < get_player_alive_snake_count(state, player_id))
                memmove(&state->player_alive_snake_ids[player_id][i],
                        &state->player_alive_snake_ids[player_id][i + 1],
                        SIZE_OF_INT * (get_player_alive_snake_count(state, player_id) - i - 1));
            state->player_alive_snake_count[player_id]--;
            return;
        }
    }
}

static inline void remove_energy(State *state) { state->energy_count--; }

/* --- TIMING --- */

static inline long timespec_diff_us(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000L +
           (end->tv_nsec - start->tv_nsec) / 1000L;
}

static int revert_last_move_time  = 0;
static int revert_last_move_count = 0;
static void revert_last_move(State *last_state, State *state)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    memcpy(state, last_state, SIZE_OF_STATE);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    revert_last_move_time += (int)timespec_diff_us(&t0, &t1);
    revert_last_move_count++;
}

/* forward declaration */
static int count_player_points(State *state, int player_id);

/* --- TOOL FUNCTIONS --- */

static inline int  get_opponent_id(int player_id) { return 1 - player_id; }

static inline bool is_north_cell_out_of_bounds(Pos pos) { return get_y(pos) == 0; }
static inline bool is_west_cell_out_of_bounds(Pos pos)  { return get_x(pos) == 0; }
static inline bool is_east_cell_out_of_bounds(Pos pos)  { return get_x(pos) == MAX_WIDTH - 1; }
static inline bool is_south_cell_out_of_bounds(Pos pos) { return get_y(pos) == MAX_HEIGHT - 1; }

static inline Pos get_north_pos(Pos pos) { return pos + NORTH_POS_OFFSET; }
static inline Pos get_west_pos(Pos pos)  { return pos + WEST_POS_OFFSET; }
static inline Pos get_east_pos(Pos pos)  { return pos + EAST_POS_OFFSET; }
static inline Pos get_south_pos(Pos pos) { return pos + SOUTH_POS_OFFSET; }

static void print_map(State *state, const char *title)
{
    if (title && title[0] != '\0')
        fprintf(stderr, "(dPoint=%d) %s\n",
                count_player_points(state, map_properties.my_id) -
                count_player_points(state, map_properties.opp_id),
                title);

    for (int y = 0; y < map_properties.height; y++)
    {
        for (int x = 0; x < map_properties.width; x++)
        {
            Pos pos = get_pos_from_map_coord(x, y);
            CellType cell = get_cell(state, pos);
            if (cell == CELL_EMPTY)
                fprintf(stderr, "\xe2\xac\x9c");        /* ⬜ */
            else if (cell == CELL_PLATFORM)
                fprintf(stderr, "\xf0\x9f\x9f\xa6");   /* 🟦 */
            else if (cell == CELL_ENERGY)
                fprintf(stderr, "\xf0\x9f\x9f\xa8");   /* 🟨 */
            else
            {
                Snake *snake = get_snake(state, (int)cell);
                if (get_snake_player_id(snake) == map_properties.my_id)
                    fprintf(stderr, "\xf0\x9f\x90\x8d"); /* 🐍 */
                else
                    fprintf(stderr, "\xf0\x9f\x90\x89"); /* 🐉 */
            }
        }
        fprintf(stderr, "\n");
    }
}

/* --- FIRST TURN COMPUTING --- */

typedef struct {
    int distance;
    Pos energy_pos;
} BFSDistanceToEnergy;

static BFSDistanceToEnergy cells_to_energy_lookup_table[MAX_CELL_COUNT][MAX_ENERGY_COUNT];

static Pos initial_energies[MAX_ENERGY_COUNT];
static int initial_energy_count = 0;

static BFSDistanceToEnergy lookup_initial_bfs_distance_to_closest_energy(State *state, Pos snake_head_pos)
{
    for (int e = 0; e < MAX_ENERGY_COUNT; e++)
    {
        BFSDistanceToEnergy bfsd = cells_to_energy_lookup_table[snake_head_pos][e];
        if (get_cell(state, bfsd.energy_pos) == CELL_ENERGY)
            return bfsd;
    }
    BFSDistanceToEnergy none = {-1, -1};
    return none;
}

static int compare_bfs_distance(const void *a, const void *b)
{
    return ((const BFSDistanceToEnergy *)a)->distance -
           ((const BFSDistanceToEnergy *)b)->distance;
}

typedef struct { Pos pos; int dist; } BFSQueueEntry;
static BFSQueueEntry bfs_queue_buf[MAX_CELL_COUNT];

static void create_bfs_cells_to_energy_distance_lookup_table(State *state)
{
    for (int ei = 0; ei < initial_energy_count; ei++)
    {
        Pos energy_pos = initial_energies[ei];

        for (int i = 0; i < MAX_CELL_COUNT; i++)
        {
            cells_to_energy_lookup_table[i][ei].distance   = MAX_WIDTH + MAX_HEIGHT;
            cells_to_energy_lookup_table[i][ei].energy_pos = energy_pos;
        }

        bool visited[MAX_CELL_COUNT];
        memset(visited, 0, sizeof(visited));

        int q_head = 0, q_tail = 0;
        visited[energy_pos] = true;
        cells_to_energy_lookup_table[energy_pos][ei].distance = 0;
        bfs_queue_buf[q_tail].pos  = energy_pos;
        bfs_queue_buf[q_tail].dist = 0;
        q_tail++;

        while (q_head < q_tail)
        {
            Pos pos  = bfs_queue_buf[q_head].pos;
            int dist = bfs_queue_buf[q_head].dist;
            q_head++;

            Pos neighbors[4] = {
                get_north_pos(pos), get_west_pos(pos),
                get_east_pos(pos),  get_south_pos(pos)
            };
            bool oob[4] = {
                is_north_cell_out_of_bounds(pos), is_west_cell_out_of_bounds(pos),
                is_east_cell_out_of_bounds(pos),  is_south_cell_out_of_bounds(pos)
            };

            for (int i = 0; i < 4; i++)
            {
                if (oob[i] || visited[neighbors[i]])
                    continue;
                if (get_cell(state, neighbors[i]) == CELL_PLATFORM)
                    continue;

                visited[neighbors[i]] = true;
                cells_to_energy_lookup_table[neighbors[i]][ei].distance   = dist + 1;
                cells_to_energy_lookup_table[neighbors[i]][ei].energy_pos = energy_pos;
                bfs_queue_buf[q_tail].pos  = neighbors[i];
                bfs_queue_buf[q_tail].dist = dist + 1;
                q_tail++;
            }
        }
    }

    for (int cell = 0; cell < MAX_CELL_COUNT; cell++)
        qsort(cells_to_energy_lookup_table[cell], initial_energy_count,
              sizeof(BFSDistanceToEnergy), compare_bfs_distance);
}

static void create_lookup_tables(State *state)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    create_bfs_cells_to_energy_distance_lookup_table(state);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "Lookup tables created in %d ms\n",
            (int)(timespec_diff_us(&t0, &t1) / 1000));
}

/* --- GAME PHYSICS - GENERATION --- */

static inline bool is_cell_solid(CellType cell, int snake_id)
{
    return cell != (CellType)snake_id && cell != CELL_EMPTY;
}

static int generate_snake_moves(State *state, Snake *snake, Pos moves[3])
{
    Pos head_pos = get_snake_head_pos(snake);

    Pos neighbors[4] = {
        get_west_pos(head_pos), get_east_pos(head_pos),
        get_north_pos(head_pos), get_south_pos(head_pos)
    };
    bool neighbor_out_of_bounds[4] = {
        is_west_cell_out_of_bounds(head_pos), is_east_cell_out_of_bounds(head_pos),
        is_north_cell_out_of_bounds(head_pos), is_south_cell_out_of_bounds(head_pos)
    };

    int move_count = 0;
    for (int i = 0; i < 4; i++)
    {
        if (!neighbor_out_of_bounds[i] &&
            get_cell(state, neighbors[i]) != CELL_PLATFORM &&
            neighbors[i] != get_snake_body_pos(snake, 1))
        {
            moves[move_count++] = neighbors[i];
        }
    }
    return move_count;
}

static int generate_player_movesets_time  = 0;
static int generate_player_movesets_count = 0;
static int generate_player_movesets(State *state, int player_id, MoveSet movesets[MAX_PLAYER_MOVE_SETS])
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int snake_count = get_player_alive_snake_count(state, player_id);
    int snake_ids[MAX_PLAYER_SNAKE_COUNT];
    int snake_move_counts[MAX_PLAYER_SNAKE_COUNT];
    Pos snake_moves[MAX_PLAYER_SNAKE_COUNT][3];

    for (int i = 0; i < snake_count; i++)
    {
        int snake_id = get_player_alive_snake_id(state, player_id, i);
        Snake *snake = get_snake(state, snake_id);
        snake_ids[i] = snake_id;
        snake_move_counts[i] = generate_snake_moves(state, snake, snake_moves[i]);
    }

    int snake_move_indices[MAX_PLAYER_SNAKE_COUNT];
    for (int i = 0; i < snake_count; i++)
        snake_move_indices[i] = 0;

    int moveset_count = 0;
    while (moveset_count < MAX_PLAYER_MOVE_SETS)
    {
        MoveSet moveset;
        moveset.move_count = snake_count;

        for (int i = 0; i < snake_count; i++)
        {
            Move *move = get_moveset_move(&moveset, i);
            set_move_snake_id(move, snake_ids[i]);
            set_move_dst_pos(move, snake_moves[i][snake_move_indices[i]]);
        }

        movesets[moveset_count++] = moveset;

        int carry = 1;
        for (int i = snake_count - 1; i >= 0 && carry; i--)
        {
            snake_move_indices[i]++;
            if (snake_move_indices[i] >= snake_move_counts[i])
                snake_move_indices[i] = 0;
            else
                carry = 0;
        }
        if (carry) break;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    generate_player_movesets_time += (int)timespec_diff_us(&t0, &t1);
    generate_player_movesets_count++;
    return moveset_count;
}

/* --- GAME PHYSICS - APPLICATION --- */

static MoveSet merge_movesets(MoveSet *moveset1, MoveSet *moveset2)
{
    MoveSet merged;
    int c1 = get_moveset_move_count(moveset1);
    int c2 = get_moveset_move_count(moveset2);

    for (int i = 0; i < c1; i++)
        set_moveset_move(&merged, get_moveset_move(moveset1, i), i);
    for (int i = 0; i < c2; i++)
        set_moveset_move(&merged, get_moveset_move(moveset2, i), c1 + i);

    set_moveset_move_count(&merged, c1 + c2);
    return merged;
}

static void apply_move(State *state, Snake *snake, Move *move,
                       Pos eaten_energies[MAX_SNAKE_COUNT], int *eaten_energy_count)
{
    Pos new_head_pos = get_move_dst_pos(move);

    bool eating = (get_cell(state, new_head_pos) == CELL_ENERGY);
    if (eating)
    {
        eaten_energies[(*eaten_energy_count)++] = new_head_pos;
        set_snake_body_length(snake, get_snake_body_length(snake) + 1);
    }
    else
    {
        set_cell(state, snake->tail_pos, CELL_EMPTY);
        snake->tail_pos -= DIR_TO_OFFSET[snake->directions[snake->body_length - 2]];
    }

    int dirs_to_shift = snake->body_length - 2;
    if (dirs_to_shift > 0)
        memmove(&snake->directions[1], &snake->directions[0], dirs_to_shift);

    Pos old_head = snake->head_pos;
    snake->directions[0] = pos_offset_to_dir(old_head - new_head_pos);
    snake->head_pos = new_head_pos;
}

static void apply_all_moves(State *state, MoveSet *moveset,
                             Pos eaten_energies[MAX_SNAKE_COUNT], int *eaten_energy_count)
{
    for (int i = 0; i < get_moveset_move_count(moveset); i++)
    {
        Move *move  = get_moveset_move(moveset, i);
        Snake *snake = get_snake(state, get_move_snake_id(move));
        apply_move(state, snake, move, eaten_energies, eaten_energy_count);
    }
}

static void remove_eaten_energies(State *state, Pos eaten_energies[MAX_SNAKE_COUNT], int eaten_energy_count)
{
    for (int i = 0; i < eaten_energy_count; i++)
    {
        set_cell(state, eaten_energies[i], CELL_EMPTY);
        remove_energy(state);
    }
}

static bool is_snake_in_moveset(MoveSet *moveset, int snake_id)
{
    for (int i = 0; i < get_moveset_move_count(moveset); i++)
        if (get_move_snake_id(get_moveset_move(moveset, i)) == snake_id)
            return true;
    return false;
}

static int find_snake_collisions(State *state, Snake *colliding_snakes[MAX_SNAKE_COUNT], MoveSet *moveset)
{
    int colliding_snake_count = 0;
    int alive_count = get_alive_snake_count(state);

    for (int snake_index = 0; snake_index < alive_count; snake_index++)
    {
        int snake_id = get_alive_snake_id(state, snake_index);
        Snake *snake = get_snake(state, snake_id);
        Pos snake_head_pos = get_snake_head_pos(snake);

        if (!is_snake_in_moveset(moveset, snake_id))
            continue;

        bool collide = false;
        CellType head_cell = get_cell(state, snake_head_pos);

        if (head_cell == CELL_PLATFORM)
        {
            collide = true;
        }
        else if (head_cell < MAX_SNAKE_COUNT)
        {
            collide = true;
        }
        else
        {
            for (int s2 = 0; s2 < alive_count && !collide; s2++)
            {
                if (s2 == snake_index) continue;
                if (get_snake_head_pos(get_snake(state, get_alive_snake_id(state, s2))) == snake_head_pos)
                    collide = true;
            }
        }

        if (collide)
            colliding_snakes[colliding_snake_count++] = snake;
        else
            set_cell(state, snake_head_pos, (CellType)snake_id);
    }

    return colliding_snake_count;
}

static void apply_snake_collisions(State *state, Snake *colliding_snakes[MAX_SNAKE_COUNT], int colliding_snake_count)
{
    for (int i = 0; i < colliding_snake_count; i++)
    {
        Snake *snake = colliding_snakes[i];
        if (get_snake_body_length(snake) - 1 < 3)
        {
            remove_snake_from_alive_snake_ids(state, get_snake_id(snake), get_snake_player_id(snake));
            add_player_loss(state, get_snake_player_id(snake), 3);
        }
        else
        {
            remove_snake_head(snake);
            add_player_loss(state, get_snake_player_id(snake), 1);
        }
    }
}

static void kill_snake_immediately(State *state, Snake *snake)
{
    Pos pos = snake->head_pos;
    for (int i = 0; i < get_snake_body_length(snake); i++)
    {
        set_cell(state, pos, CELL_EMPTY);
        if (i < snake->body_length - 1)
            pos += DIR_TO_OFFSET[get_direction(snake, i)];
    }
    remove_snake_from_alive_snake_ids(state, get_snake_id(snake), get_snake_player_id(snake));
}

static bool apply_snake_gravity(State *state, Snake *snake)
{
    int snake_id = get_snake_id(snake);
    int snake_body_length = get_snake_body_length(snake);

    Pos body_cache[MAX_SNAKE_SIZE];
    reconstruct_body(snake, body_cache);

    bool gravity_applied = false;
    int y = 0;
    while (y++ < MAX_HEIGHT)
    {
        int min_y = MAX_HEIGHT;

        for (int i = 0; i < snake_body_length; i++)
        {
            if (is_south_cell_out_of_bounds(body_cache[i]))
            {
                kill_snake_immediately(state, snake);
                return true;
            }
        }

        bool blocked = false;
        for (int i = 0; i < snake_body_length; i++)
        {
            Pos pos_below = get_south_pos(body_cache[i]);
            CellType cell_below = get_cell(state, pos_below);

            if (is_cell_solid(cell_below, (CellType)snake_id))
            {
                blocked = true;
                break;
            }

            int gy = get_y(pos_below);
            if (gy < min_y) min_y = gy;
        }

        if (blocked) break;

        for (int i = 0; i < snake_body_length; i++)
            set_cell(state, body_cache[i], CELL_EMPTY);

        if (min_y >= map_properties.height + MAP_PADDING)
        {
            kill_snake_immediately(state, snake);
            return true;
        }

        shift_snake_body(snake, SOUTH_POS_OFFSET);

        for (int i = 0; i < snake_body_length; i++)
        {
            body_cache[i] += SOUTH_POS_OFFSET;
            set_cell(state, body_cache[i], (CellType)snake_id);
        }

        gravity_applied = true;
    }

    return gravity_applied;
}

static int apply_gravity_time  = 0;
static int apply_gravity_count = 0;
static void apply_gravity(State *state)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int gravity_finalized_count = 0;
    int snake_index = 0;

    while (gravity_finalized_count < get_alive_snake_count(state))
    {
        int snake_id = get_alive_snake_id(state, snake_index);
        Snake *snake = get_snake(state, snake_id);

        bool gravity_applied = apply_snake_gravity(state, snake);
        if (gravity_applied)
            gravity_finalized_count = 0;
        else
            gravity_finalized_count++;

        if (++snake_index >= get_alive_snake_count(state))
            snake_index = 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    apply_gravity_time += (int)timespec_diff_us(&t0, &t1);
    apply_gravity_count++;
}

static int apply_moveset_time  = 0;
static int apply_moveset_count = 0;
static void apply_moveset(State *state, MoveSet *moveset)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    set_turn(state, get_turn(state) + 1);

    Pos eaten_energies[MAX_SNAKE_COUNT];
    int eaten_energies_count = 0;
    apply_all_moves(state, moveset, eaten_energies, &eaten_energies_count);

    remove_eaten_energies(state, eaten_energies, eaten_energies_count);

    Snake *colliding_snakes[MAX_SNAKE_COUNT];
    int colliding_snake_count = find_snake_collisions(state, colliding_snakes, moveset);

    apply_snake_collisions(state, colliding_snakes, colliding_snake_count);

    apply_gravity(state);

    if (get_turn(state) == 200 ||
        get_player_alive_snake_count(state, map_properties.my_id) == 0 ||
        get_player_alive_snake_count(state, map_properties.opp_id) == 0 ||
        get_energy_count(state) == 0)
        set_game_ended(state);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    apply_moveset_time += (int)timespec_diff_us(&t0, &t1);
    apply_moveset_count++;
}

/* --- DECISION MAKING --- */

static int count_player_points(State *state, int player_id)
{
    int points = 0;
    for (int i = 0; i < get_player_alive_snake_count(state, player_id); i++)
    {
        int snake_id = get_player_alive_snake_id(state, player_id, i);
        Snake *snake = get_snake(state, snake_id);
        points += get_snake_body_length(snake);
    }
    return points;
}

static int encode_lexicographic_priority(int a, int b, int b_max)
{
    return a * b_max + b;
}

static int evaluate_end_states(State *state, int player_id, int player_points, int opponent_points)
{
    bool player_win   = false;
    bool opponent_win = false;

    if (get_turn(state) == 200 || get_energy_count(state) == 0)
    {
        if (player_points > opponent_points)
            player_win = true;
        else if (player_points < opponent_points)
            opponent_win = true;
        else if (get_player_losses(state, player_id) > get_player_losses(state, get_opponent_id(player_id)))
            opponent_win = true;
        else if (get_player_losses(state, player_id) < get_player_losses(state, get_opponent_id(player_id)))
            player_win = true;
    }
    else if (opponent_points == 0)
        player_win = true;
    else if (player_points == 0)
        opponent_win = true;
    else if (get_player_losses(state, player_id) > get_player_losses(state, get_opponent_id(player_id)))
        opponent_win = true;
    else if (get_player_losses(state, player_id) < get_player_losses(state, get_opponent_id(player_id)))
        player_win = true;

    if (player_win)
        return 1000 + encode_lexicographic_priority(player_points, (201 - get_turn(state)), 200);
    if (opponent_win)
        return -21000 + encode_lexicographic_priority(player_points, get_turn(state), 200);
    return 0;
}

static int   evaluate_state_time  = 0;
static int   evaluate_state_count = 0;
static float evaluate_state(State *state, int player_id)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int player_points, opponent_points;
    if (player_id == map_properties.my_id)
    {
        player_points   = count_player_points(state, map_properties.my_id);
        opponent_points = count_player_points(state, map_properties.opp_id);
    }
    else
    {
        player_points   = count_player_points(state, map_properties.opp_id);
        opponent_points = count_player_points(state, map_properties.my_id);
    }

    if (is_game_ended(state))
        return (float)evaluate_end_states(state, player_id, player_points, opponent_points);

    float dist_sum        = 0.0f;
    float platform_bonuses = 0.0f;

    for (int i = 0; i < get_player_alive_snake_count(state, player_id); i++)
    {
        int snake_id = get_player_alive_snake_id(state, player_id, i);
        Snake *snake = get_snake(state, snake_id);
        Pos snake_head_pos = get_snake_head_pos(snake);

        BFSDistanceToEnergy closest_energy =
            lookup_initial_bfs_distance_to_closest_energy(state, snake_head_pos);

        if (closest_energy.distance == -1)
            dist_sum += MAX_MAP_WIDTH + MAX_MAP_HEIGHT;
        else
            dist_sum += closest_energy.distance;

        Pos body_cache[MAX_SNAKE_SIZE];
        reconstruct_body(snake, body_cache);
        int snake_len = get_snake_body_length(snake);

        for (int bi = 0; bi < snake_len; bi++)
        {
            Pos bp = body_cache[bi];
            if (get_x(bp) < MAP_PADDING ||
                get_x(bp) >= MAX_WIDTH - MAP_PADDING ||
                get_y(bp) < MAP_PADDING ||
                get_y(bp) >= MAX_HEIGHT - MAP_PADDING)
            {
                dist_sum += 1;
            }
        }

        for (int bi = 0; bi < snake_len; bi++)
        {
            Pos bp = body_cache[bi];
            if (is_south_cell_out_of_bounds(bp)) continue;

            Pos cell_under = get_south_pos(bp);
            if (get_cell(state, cell_under) == CELL_PLATFORM)
            {
                platform_bonuses += 1.5f * (snake_len - bi) / (float)snake_len;
                break;
            }
        }
    }

    dist_sum -= platform_bonuses;
    float dist_score = (dist_sum != 0.0f) ? (1.0f / (2.0f * dist_sum)) : 0.0f;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    evaluate_state_time += (int)timespec_diff_us(&t0, &t1);
    evaluate_state_count++;

    return (float)(player_points - opponent_points) + dist_score;
}

static int     choose_best_player_moveset_time  = 0;
static int     choose_best_player_moveset_count = 0;
static MoveSet choose_best_player_moveset(State *state, int player_id, MoveSet *previous_player_moveset)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    State next_state;

    MoveSet movesets[MAX_PLAYER_MOVE_SETS];
    int moveset_count = generate_player_movesets(state, player_id, movesets);

    MoveSet best_moveset;
    memset(&best_moveset, 0, sizeof(best_moveset));
    float best_evaluation = -100000.0f;

    for (int i = 0; i < moveset_count; i++)
    {
        revert_last_move(state, &next_state);

        MoveSet turn_moveset = merge_movesets(previous_player_moveset, &movesets[i]);
        apply_moveset(&next_state, &turn_moveset);

        float evaluation = evaluate_state(&next_state, player_id);
        if (best_evaluation < evaluation)
        {
            best_evaluation = evaluation;
            best_moveset = movesets[i];
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    choose_best_player_moveset_time += (int)timespec_diff_us(&t0, &t1);
    choose_best_player_moveset_count++;
    return best_moveset;
}

static void print_marks(State *state, MoveSet best_moveset)
{
    for (int i = 0; i < get_moveset_move_count(&best_moveset); i++)
    {
        Move *move = get_moveset_move(&best_moveset, i);
        int snake_id = get_move_snake_id(move);
        Snake *snake = get_snake(state, snake_id);

        BFSDistanceToEnergy closest_energy =
            lookup_initial_bfs_distance_to_closest_energy(state, get_snake_head_pos(snake));

        if (closest_energy.distance != -1)
            printf("MARK %d %d;",
                   get_map_x(closest_energy.energy_pos),
                   get_map_y(closest_energy.energy_pos));
    }
}

/* --- ALGORITHM - BEAM SEARCH --- */

static int   beam_search_depth               = 0;
static int   beam_search_execution_count     = 0;
static int   beam_search_visited_states_count = 0;
static float beam_search_sum_states_visited  = 0.0f;
static float beam_search_average_states_visited = 0.0f;

static bool has_exceeded_time_limit(struct timespec *start, int maximum_microseconds)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    return timespec_diff_us(start, &current) >= maximum_microseconds;
}

/* --- MIN-HEAP for beam search candidates (top = worst = min heuristic) --- */

typedef struct {
    State data[BS_WIDTH + 1];
    int   size;
} StateHeap;

static void heap_push(StateHeap *h, State *s)
{
    h->data[h->size] = *s;
    int i = h->size++;
    while (i > 0)
    {
        int parent = (i - 1) / 2;
        if (h->data[parent].heuristic > h->data[i].heuristic)
        {
            State tmp = h->data[parent];
            h->data[parent] = h->data[i];
            h->data[i] = tmp;
            i = parent;
        }
        else break;
    }
}

static void heap_pop(StateHeap *h)
{
    h->data[0] = h->data[--h->size];
    int i = 0;
    while (1)
    {
        int left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left  < h->size && h->data[left].heuristic  < h->data[smallest].heuristic) smallest = left;
        if (right < h->size && h->data[right].heuristic < h->data[smallest].heuristic) smallest = right;
        if (smallest == i) break;
        State tmp = h->data[i];
        h->data[i] = h->data[smallest];
        h->data[smallest] = tmp;
        i = smallest;
    }
}

/* --- STATE VECTOR for beam search states --- */

typedef struct {
    State data[BS_WIDTH];
    int   size;
} StateVec;

static int compare_state_heuristic_desc(const void *a, const void *b)
{
    float ha = ((const State *)a)->heuristic;
    float hb = ((const State *)b)->heuristic;
    if (hb > ha) return  1;
    if (hb < ha) return -1;
    return 0;
}

static int move_candidates_in_beam_states_time  = 0;
static int move_candidates_in_beam_states_count = 0;
static void move_candidates_in_beam_states(StateVec *beam_search_states, StateHeap *beam_search_candidates)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    beam_search_states->size = 0;
    while (beam_search_candidates->size > 0)
    {
        beam_search_states->data[beam_search_states->size++] = beam_search_candidates->data[0];
        heap_pop(beam_search_candidates);
    }

    qsort(beam_search_states->data, beam_search_states->size, sizeof(State), compare_state_heuristic_desc);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    move_candidates_in_beam_states_time += (int)timespec_diff_us(&t0, &t1);
    move_candidates_in_beam_states_count++;
}

static int consider_state_to_be_candidate_time  = 0;
static int consider_state_to_be_candidate_count = 0;
static void consider_state_to_be_candidate(State *state, MoveSet *last_moveset,
                                            StateHeap *beam_search_candidates, int beam_width)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    beam_search_visited_states_count++;

    if (beam_search_candidates->size < beam_width)
    {
        if (beam_search_depth == 1)
            set_first_depth_moveset(state, last_moveset);
        heap_push(beam_search_candidates, state);
    }
    else if (get_heuristic(state) > beam_search_candidates->data[0].heuristic)
    {
        heap_pop(beam_search_candidates);
        heap_push(beam_search_candidates, state);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    consider_state_to_be_candidate_time += (int)timespec_diff_us(&t0, &t1);
    consider_state_to_be_candidate_count++;
}

static int find_candidates_among_state_children_time  = 0;
static int find_candidates_among_state_children_count = 0;
static void find_candidates_among_state_children(State *state, int player_id,
                                                  StateHeap *beam_search_candidates, int beam_width)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    MoveSet turn_beginning_moveset;
    memset(&turn_beginning_moveset, 0, sizeof(turn_beginning_moveset));
    set_moveset_move_count(&turn_beginning_moveset, 0);

    MoveSet opponent_moveset = choose_best_player_moveset(state, get_opponent_id(player_id), &turn_beginning_moveset);

    MoveSet ally_movesets[MAX_PLAYER_MOVE_SETS];
    int ally_moveset_count = generate_player_movesets(state, player_id, ally_movesets);

    State next_state;
    memcpy(&next_state, state, SIZE_OF_STATE);

    float next_state_heuristic_weight = state->heuristic_depth_weight * BS_EXP_FACTOR;

    for (int i = 0; i < ally_moveset_count; i++)
    {
        revert_last_move(state, &next_state);

        MoveSet turn_moveset = merge_movesets(&opponent_moveset, &ally_movesets[i]);
        apply_moveset(&next_state, &turn_moveset);

        next_state.heuristic_depth_weight = next_state_heuristic_weight;

        next_state.state_evaluation = evaluate_state(&next_state, player_id);
        float delta_h = next_state.state_evaluation - state->state_evaluation;

        set_heuristic(&next_state, get_heuristic(state) + next_state.heuristic_depth_weight * delta_h);

        consider_state_to_be_candidate(&next_state, &ally_movesets[i], beam_search_candidates, beam_width);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    find_candidates_among_state_children_time += (int)timespec_diff_us(&t0, &t1);
    find_candidates_among_state_children_count++;
}

static State *get_best_candidate(StateHeap *beam_search_candidates)
{
    while (beam_search_candidates->size > 1)
        heap_pop(beam_search_candidates);
    return &beam_search_candidates->data[0];
}

static MoveSet beam_search(State *initial_state, int player_id, int depth_max,
                            int beam_width, int maximum_microseconds, struct timespec *start_turn_chrono)
{
    fprintf(stderr, "Starting beam_search: player_id=%d, depth_max=%d, beam_width=%d, max_time_us=%d\n",
            player_id, depth_max, beam_width, maximum_microseconds);
    beam_search_visited_states_count = 0;
    beam_search_depth = 1;

    StateHeap beam_search_candidates;
    beam_search_candidates.size = 0;

    initial_state->heuristic_depth_weight = 1.0f;
    initial_state->state_evaluation = evaluate_state(initial_state, player_id);
    set_heuristic(initial_state, 0.0f);

    find_candidates_among_state_children(initial_state, player_id, &beam_search_candidates, beam_width);

    StateVec beam_search_states;
    beam_search_states.size = 0;

    while (!has_exceeded_time_limit(start_turn_chrono, maximum_microseconds) &&
           beam_search_depth < depth_max)
    {
        beam_search_depth++;
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            fprintf(stderr, "Start beam search depth %d (%ld ym remaining)\n", beam_search_depth,
                    (long)maximum_microseconds - timespec_diff_us(start_turn_chrono, &now));
        }

        move_candidates_in_beam_states(&beam_search_states, &beam_search_candidates);

        for (int si = 0; si < beam_search_states.size; si++)
        {
            State *st = &beam_search_states.data[si];

            if (is_game_ended(st))
            {
                consider_state_to_be_candidate(st, get_first_depth_moveset(st),
                                               &beam_search_candidates, beam_width);
                continue;
            }

            find_candidates_among_state_children(st, player_id, &beam_search_candidates, beam_width);

            if (has_exceeded_time_limit(start_turn_chrono, maximum_microseconds))
            {
                fprintf(stderr, "Time limit exceeded during moveset generation. beam_search_states.size=%d, beam_search_candidates.size=%d\n",
                        beam_search_states.size, beam_search_candidates.size);

                State *best_candidate = get_best_candidate(&beam_search_candidates);

                if (get_heuristic(&beam_search_states.data[0]) > get_heuristic(best_candidate))
                {
                    fprintf(stderr, "Previous beam search state is better than best candidate: h=%f\n",
                            get_heuristic(&beam_search_states.data[0]));
                    return *get_first_depth_moveset(&beam_search_states.data[0]);
                }
                else
                {
                    fprintf(stderr, "Best candidate is better than initial state: h=%f\n",
                            get_heuristic(best_candidate));
                    return *get_first_depth_moveset(best_candidate);
                }
            }
        }
    }

    State *best_candidate = get_best_candidate(&beam_search_candidates);
    return *get_first_depth_moveset(best_candidate);
}

/* --- PARSING --- */

static void parse_initial_inputs(State *state)
{
    scanf("%d", &map_properties.my_id);
    map_properties.opp_id = get_opponent_id(map_properties.my_id);

    scanf("%d %d", &map_properties.width, &map_properties.height);

    initialize_cells(state);

    /* consume newline */
    {
        char c;
        while ((c = (char)getchar()) != '\n' && c != EOF);
    }

    for (int y = 0; y < map_properties.height; y++)
    {
        char row[MAX_MAP_WIDTH + 2];
        if (fgets(row, sizeof(row), stdin) == NULL) break;

        for (int x = 0; x < map_properties.width; x++)
        {
            Pos pos = get_pos_from_map_coord(x, y);
            if (row[x] == '.')
                set_cell(state, pos, CELL_EMPTY);
            else if (row[x] == '#')
                set_cell(state, pos, CELL_PLATFORM);
        }
    }

    int snakebots_per_player;
    scanf("%d", &snakebots_per_player);

    reset_alive_snake_count(state);

    int snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++)
    {
        scanf("%d", &snakebot_id);
        initialize_snake_data(state, snakebot_id, map_properties.my_id);
        add_player_alive_snake_id(state, map_properties.my_id, snakebot_id);
    }
    for (int i = 0; i < snakebots_per_player; i++)
    {
        scanf("%d", &snakebot_id);
        initialize_snake_data(state, snakebot_id, map_properties.opp_id);
        add_player_alive_snake_id(state, map_properties.opp_id, snakebot_id);
    }
}

static bool parse_pos_from_segment(const char *segment, Pos *pos)
{
    const char *comma = strchr(segment, ',');
    if (!comma) return false;
    int x = atoi(segment);
    int y = atoi(comma + 1);

    if (x < -MAP_PADDING || x >= map_properties.width  + MAP_PADDING ||
        y < -MAP_PADDING || y >= map_properties.height + MAP_PADDING)
        return false;

    *pos = get_pos_from_map_coord(x, y);
    return true;
}

static void parse_snakebot(State *state, Snake *snake, int snakebotId, const char *bodyStr)
{
    reset_snake_length(snake);

    char buf[MAX_SNAKE_SIZE * 10];
    strncpy(buf, bodyStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;
    char *colon;

    while ((colon = strchr(p, ':')) != NULL)
    {
        *colon = '\0';
        Pos body_pos;
        if (parse_pos_from_segment(p, &body_pos))
        {
            add_body_pos(snake, body_pos);
            set_cell(state, body_pos, (CellType)snakebotId);
        }
        p = colon + 1;
    }

    /* last segment */
    Pos body_pos;
    if (parse_pos_from_segment(p, &body_pos))
    {
        add_body_pos(snake, body_pos);
        set_cell(state, body_pos, (CellType)snakebotId);
    }
}

static void parse_turn_inputs(State *state)
{
    int energy_count;
    scanf("%d", &energy_count);
    initial_energy_count = energy_count;

    for (int i = 0; i < energy_count; i++)
    {
        int x, y;
        scanf("%d %d", &x, &y);
        Pos pos = get_pos_from_map_coord(x, y);
        initial_energies[i] = pos;
        set_cell(state, pos, CELL_ENERGY);
    }
    set_energy_count(state, energy_count);

    reset_alive_snake_count(state);

    int snakebotCount;
    scanf("%d", &snakebotCount);
    for (int i = 0; i < snakebotCount; i++)
    {
        int snakebotId;
        char bodyStr[MAX_SNAKE_SIZE * 10];
        scanf("%d %s", &snakebotId, bodyStr);

        Snake *snake = get_snake(state, snakebotId);
        int player_id = get_snake_player_id(snake);
        add_player_alive_snake_id(state, player_id, snakebotId);

        parse_snakebot(state, snake, snakebotId, bodyStr);
    }
}

/* --- MAIN LOOP --- */

static void signal_handler(int sig)
{
    const char *signal_name = NULL;
    switch (sig)
    {
    case SIGSEGV: signal_name = "SIGSEGV (Segmentation Fault)"; break;
    case SIGABRT: signal_name = "SIGABRT (Abort)"; break;
    case SIGFPE:  signal_name = "SIGFPE (Floating Point Exception)"; break;
    default:      signal_name = "Unknown signal"; break;
    }
    fprintf(stderr, "\n[ERROR] Caught signal: %s (%d)\n", signal_name, sig);
    exit(EXIT_FAILURE);
}

int main(void)
{
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGFPE,  signal_handler);

    State initial_state;
    memset(&initial_state, 0, SIZE_OF_STATE);
    parse_initial_inputs(&initial_state);

    fprintf(stderr, "State size: %zu bytes\n",   SIZE_OF_STATE);
    fprintf(stderr, "Snake size: %zu bytes\n",   sizeof(Snake));
    fprintf(stderr, "MoveSet size: %zu bytes\n", sizeof(MoveSet));
    fprintf(stderr, "Move size: %zu bytes\n",    sizeof(Move));

    State state;
    memcpy(&state, &initial_state, SIZE_OF_STATE);

    int turn = 0;
    while (1)
    {
        int player_losses   = get_player_losses(&state, map_properties.my_id);
        int opponent_losses = get_player_losses(&state, map_properties.opp_id);

        memcpy(&state, &initial_state, SIZE_OF_STATE);

        set_turn(&state, turn);
        set_player_losses(&state, map_properties.my_id,  player_losses);
        set_player_losses(&state, map_properties.opp_id, opponent_losses);

        parse_turn_inputs(&state);

        struct timespec start_turn_chrono;
        clock_gettime(CLOCK_MONOTONIC, &start_turn_chrono);

        if (turn == 0)
            create_lookup_tables(&state);

        MoveSet best_moveset = beam_search(&state, map_properties.my_id,
                                           BS_MAX_DEPTH, BS_WIDTH, BS_MAX_TIME,
                                           &start_turn_chrono);

        beam_search_execution_count++;
        beam_search_sum_states_visited += beam_search_visited_states_count;
        beam_search_average_states_visited = beam_search_sum_states_visited / (float)beam_search_execution_count;

        fprintf(stderr, "\nMax depth reached: %d\n", beam_search_depth);
        fprintf(stderr, "\nStates visited this turn: %d\n", beam_search_visited_states_count);
        fprintf(stderr, "Avg visited states: %d\n", (int)beam_search_average_states_visited);

        print_marks(&state, best_moveset);

        for (int i = 0; i < get_moveset_move_count(&best_moveset); i++)
        {
            Move *move = get_moveset_move(&best_moveset, i);
            Pos dir = get_move_dst_pos(move);

            int snake_id = get_move_snake_id(move);
            Snake *snake = get_snake(&state, snake_id);
            Pos snake_head = get_snake_head_pos(snake);

            int dir_offset = dir - snake_head;

            if (dir_offset == NORTH_POS_OFFSET)
                printf("%d UP", snake_id);
            else if (dir_offset == SOUTH_POS_OFFSET)
                printf("%d DOWN", snake_id);
            else if (dir_offset == WEST_POS_OFFSET)
                printf("%d LEFT", snake_id);
            else
                printf("%d RIGHT", snake_id);

            if (i != get_player_alive_snake_count(&state, map_properties.my_id) - 1)
                printf(";");
        }

        printf("\n");
        fflush(stdout);

        struct timespec end_turn_chrono;
        clock_gettime(CLOCK_MONOTONIC, &end_turn_chrono);
        fprintf(stderr, "Time elapsed after response: %d ys\n",
                (int)timespec_diff_us(&start_turn_chrono, &end_turn_chrono));
        turn++;
    }

    return 0;
}
