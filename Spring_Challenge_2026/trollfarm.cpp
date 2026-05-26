#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_W 64
#define MAX_H 64
#define MAX_NODES (MAX_W * MAX_H)
#define MAX_TREES 512
#define MAX_WORKERS 128
#define MAX_WORKER_ID 512
#define MAX_PATH MAX_NODES
#define COMMAND_SIZE 4096
#define EPSILON 0.000001
#ifndef CHOP_HARVEST_MARGIN
#define CHOP_HARVEST_MARGIN -2
#endif
#ifndef ENABLE_CHOP_RESERVATION
#define ENABLE_CHOP_RESERVATION 1
#endif
#ifndef ENABLE_LIGHT_CHOPPER_FALLBACK
#define ENABLE_LIGHT_CHOPPER_FALLBACK 1
#endif
#ifndef ENABLE_SCORE_CHOP_TIEBREAK
#define ENABLE_SCORE_CHOP_TIEBREAK 1
#endif
typedef enum { PLUM, LEMON, APPLE, BANANA, FRUIT_TYPES } Fruit;
typedef enum { MOVE, HARVEST, PLANT, CHOP, PICK, DROP, MINE, PLAN_NONE } PlanName;
typedef struct {
    int plum, lemon, apple, banana, iron, wood;
} Inventory;
typedef struct {
    Fruit type;
    int x, y, size, health, fruits, cooldown, period;
} Tree;
typedef struct {
    int id, player, x, y;
    int move_speed, carry_capacity, harvest_power, chop_power;
    int carry_plum, carry_lemon, carry_apple, carry_banana, carry_iron, carry_wood;
} Worker;
typedef struct {
    int len;
    int node[MAX_PATH];
} Path;
typedef struct {
    bool present;
    PlanName name;
    int worker_id;
    int type;
    int node;
    int weight;
    int order;
} Plan;
typedef struct {
    int move, carry, harvest, chop;
    int costs[4];
    int turns, remaining_turns;
} Prediction;
struct MapFeatures {
    int camp_manhattan;
    int ready_near;
    int choppable_ready_near;
    bool poor_long_distance_opening;
};
struct TaskScore {
    int points = 0;
    int turns = 99;
    int net_points = -9999;
    double points_per_turn = -9999.0;
    double risk_penalty = 0.0;
};
struct ArenaPressure {
    int my_score;
    int opp_score;
    int diff;
    bool low_score;
    bool behind;
    bool cashout;
    bool no_train;
};
typedef struct {
    int width, height, count;
    bool exists[MAX_NODES];
    int neighbor_count[MAX_NODES];
    int neighbors[MAX_NODES][4];
} Grid;
typedef struct {
    Grid grid;
    int my_camp, opp_camp, distance_between_camps;

    bool grass[MAX_NODES], water[MAX_NODES], iron[MAX_NODES];
    bool wet[MAX_NODES], mining[MAX_NODES], my_nodes[MAX_NODES];
    bool near_camp[MAX_NODES], wet_near_camp[MAX_NODES], near_except_seed[MAX_NODES];
    int grass_list[MAX_NODES], grass_count;
    int water_list[MAX_NODES], water_count;
    int iron_list[MAX_NODES], iron_count;
    int wet_list[MAX_NODES], wet_count;
    int mining_list[MAX_NODES], mining_count;
    int my_node_list[MAX_NODES], my_node_count;
    int near_list[MAX_NODES], near_count;
    int wet_near_list[MAX_NODES], wet_near_count;
    int near_except_seed_list[MAX_NODES], near_except_seed_count;
    int dropoff[MAX_NODES], dropoff_count;
    int opp_dropoff[MAX_NODES], opp_dropoff_count;
    bool opp_corner[MAX_NODES];
    int opp_corner_list[MAX_NODES], opp_corner_count;
    int seed_node;

    Inventory mine_inv, opp_inv;
    Tree trees[MAX_TREES];
    int tree_count, tree_at[MAX_NODES];
    Worker workers[MAX_WORKERS];
    int worker_count, worker_at[MAX_NODES], opp_worker_at[MAX_NODES];
    int helper, inter, chopper;

    Plan plans[MAX_WORKER_ID];
    int plan_order;
    int reserved_chop_nodes[MAX_WORKERS], reserved_chop_workers[MAX_WORKERS], reserved_chop_count;
    char training[96];
    Prediction predictions[8];
    int prediction_count, best_prediction;
    MapFeatures map_features;
    int opening_tree_count;
    int turn;
} Controller;
static const int DIRS[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
static const char *FRUIT_NAME[FRUIT_TYPES] = {"PLUM", "LEMON", "APPLE", "BANANA"};
static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }
static int sq(int n) { return n * n; }
static int ceil_div_int(int n, int d) { return n <= 0 ? 0 : (n + d - 1) / d; }
static int floor_div_int(int n, int d) {
    if (n >= 0) return n / d;
    return -(((-n) + d - 1) / d);
}
static int ceil_positive_double(double value) {
    int whole = (int)value;
    return value > whole + EPSILON ? whole + 1 : whole;
}
static double rounded_hundredths(double value) {
    return ((int)(value * 100.0 + 0.5)) / 100.0;
}
static double pow08(int exponent) {
    double value = 1.0;
    for (int i = 0; i < exponent; i++) value *= 0.8;
    return value;
}
static Fruit fruit_from_name(const char *name) {
    for (int i = 0; i < FRUIT_TYPES; i++) {
        if (strcmp(name, FRUIT_NAME[i]) == 0) return (Fruit)i;
    }
    return BANANA;
}
static int fruit_inventory(const Inventory *inv, Fruit type) {
    if (type == PLUM) return inv->plum;
    if (type == LEMON) return inv->lemon;
    if (type == APPLE) return inv->apple;
    return inv->banana;
}
static bool inventory_has(const Inventory *inv, Fruit type) {
    return fruit_inventory(inv, type) > 0;
}
static bool inventory_any_fruit(const Inventory *inv) {
    return inv->plum > 0 || inv->lemon > 0 || inv->apple > 0 || inv->banana > 0;
}
static int inventory_score(const Inventory *inv) {
    return inv->plum + inv->lemon + inv->apple + inv->banana + 4 * inv->wood;
}
static int type_health_increment(Fruit type) {
    if (type == APPLE) return 3;
    if (type == BANANA) return 1;
    return 2;
}
static int type_health(Fruit type, int size) {
    static const int health[FRUIT_TYPES][5] = {
        {0, 6, 8, 10, 12},
        {0, 6, 8, 10, 12},
        {0, 11, 14, 17, 20},
        {0, 3, 4, 5, 6},
    };
    return health[type][size];
}
static int tree_period(Fruit type, bool wet) {
    static const int dry[FRUIT_TYPES] = {8, 8, 9, 6};
    static const int wet_period[FRUIT_TYPES] = {3, 3, 2, 4};
    return wet ? wet_period[type] : dry[type];
}
static void list_add(bool set[], int list[], int *count, int node) {
    if (!set[node]) {
        set[node] = true;
        list[(*count)++] = node;
    }
}
static int node_of(const Grid *g, int x, int y) { return y * g->width + x; }
static int node_x(const Grid *g, int node) { return node % g->width; }
static int node_y(const Grid *g, int node) { return node / g->width; }
static bool in_bounds(const Grid *g, int x, int y) {
    return x >= 0 && y >= 0 && x < g->width && y < g->height;
}
static bool neighbor_has(const Grid *g, int from, int to) {
    for (int i = 0; i < g->neighbor_count[from]; i++) {
        if (g->neighbors[from][i] == to) return true;
    }
    return false;
}
static void neighbor_add(Grid *g, int from, int to) {
    if (neighbor_has(g, from, to)) return;
    g->neighbors[from][g->neighbor_count[from]++] = to;
}
static void neighbor_remove(Grid *g, int from, int to) {
    for (int i = 0; i < g->neighbor_count[from]; i++) {
        if (g->neighbors[from][i] == to) {
            for (int j = i + 1; j < g->neighbor_count[from]; j++) {
                g->neighbors[from][j - 1] = g->neighbors[from][j];
            }
            g->neighbor_count[from]--;
            return;
        }
    }
}
static void grid_fill(Grid *g, int width, int height) {
    memset(g, 0, sizeof(*g));
    g->width = width;
    g->height = height;
    g->count = width * height;
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            int node = node_of(g, x, y);
            g->exists[node] = true;
            for (int d = 0; d < 4; d++) {
                int nx = x + DIRS[d][0], ny = y + DIRS[d][1];
                if (!in_bounds(g, nx, ny)) continue;
                int neighbor = node_of(g, nx, ny);
                neighbor_add(g, node, neighbor);
                neighbor_add(g, neighbor, node);
            }
        }
    }
}
static void grid_remove_cell(Grid *g, int node) {
    int old[4], count = g->neighbor_count[node];
    for (int i = 0; i < count; i++) old[i] = g->neighbors[node][i];
    for (int i = 0; i < count; i++) neighbor_remove(g, old[i], node);
    g->neighbor_count[node] = 0;
    g->exists[node] = false;
}
static int n4(const Grid *g, int node, int out[4]) {
    int count = 0, x = node_x(g, node), y = node_y(g, node);
    for (int d = 0; d < 4; d++) {
        int nx = x + DIRS[d][0], ny = y + DIRS[d][1];
        if (in_bounds(g, nx, ny)) out[count++] = node_of(g, nx, ny);
    }
    return count;
}
static int manhattan(const Grid *g, int a, int b) {
    return abs(node_x(g, a) - node_x(g, b)) + abs(node_y(g, a) - node_y(g, b));
}
static int middle_distance(const Grid *g, int node) {
    int x = node_x(g, node), y = node_y(g, node);
    int mid_x = g->width / 2;
    int midpoint_row = g->height / 2;
    if (g->width % 2 == 0 && abs(x - (mid_x - 1)) < abs(x - mid_x)) mid_x--;
    if (g->height % 2 == 0 && abs(y - (midpoint_row - 1)) < abs(y - midpoint_row)) midpoint_row--;
    int factor = 10;
    for (int row = midpoint_row; row >= 10; row /= 10) factor *= 10;
    int encoded_mid_x = mid_x * factor + midpoint_row;
    return abs(x - encoded_mid_x) + abs(y);
}
static void sorted_neighbors(const Grid *g, int node, int out[4], int *count) {
    *count = g->neighbor_count[node];
    for (int i = 0; i < *count; i++) out[i] = g->neighbors[node][i];
    for (int i = 1; i < *count; i++) {
        int value = out[i], value_score = middle_distance(g, value), j = i - 1;
        while (j >= 0 && middle_distance(g, out[j]) > value_score) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = value;
    }
}
static Path no_path(void) {
    Path p;
    p.len = 0;
    return p;
}
static Path path_from_parents(int meet, int forward[], int backward[]) {
    Path p = no_path();
    int left[MAX_PATH], left_count = 0;
    for (int at = meet; at >= 0; at = forward[at]) left[left_count++] = at;
    for (int i = left_count - 1; i >= 0; i--) p.node[p.len++] = left[i];
    for (int at = backward[meet]; at >= 0; at = backward[at]) p.node[p.len++] = at;
    return p;
}
static Path grid_shortest_path(const Grid *g, int start, int goal, int excluded) {
    if (start < 0 || goal < 0) return no_path();
    Path same = no_path();
    if (start == goal) {
        same.node[same.len++] = start;
        return same;
    }
    int fq[MAX_NODES], bq[MAX_NODES], fh = 0, ft = 0, bh = 0, bt = 0;
    int fp[MAX_NODES], bp[MAX_NODES];
    for (int i = 0; i < g->count; i++) fp[i] = bp[i] = -2;
    if (excluded >= 0) fp[excluded] = bp[excluded] = -3;
    fp[start] = -1;
    bp[goal] = -1;
    fq[ft++] = start;
    bq[bt++] = goal;
    while (fh < ft && bh < bt) {
        int sides[2] = {0, 1};
        for (int s = 0; s < 2; s++) {
            int current = sides[s] == 0 ? fq[fh++] : bq[bh++];
            int neighbors[4], count = 0;
            sorted_neighbors(g, current, neighbors, &count);
            for (int i = 0; i < count; i++) {
                int n = neighbors[i];
                int *own = sides[s] == 0 ? fp : bp;
                int *other = sides[s] == 0 ? bp : fp;
                if (own[n] != -2) continue;
                own[n] = current;
                if (other[n] != -2) return path_from_parents(n, fp, bp);
                if (sides[s] == 0) fq[ft++] = n;
                else bq[bt++] = n;
            }
            if (s == 0 && fh >= ft) return no_path();
            if (s == 1 && bh >= bt) return no_path();
        }
    }
    return no_path();
}
static bool tree_grown(const Tree *tree) { return tree->size == 4; }
static bool tree_fruit(const Tree *tree) { return tree->fruits >= 1; }
static void tree_apply_turn(Tree *tree) {
    tree->cooldown--;
    if (tree->cooldown > 0) return;
    tree->cooldown = tree->period;
    if (tree->size < 4) {
        tree->size++;
        tree->health += type_health_increment(tree->type);
    }
}
static int tree_chop_turns(const Tree *tree, int chop_speed) {
    if (chop_speed < 1) return 99;
    if (tree->health <= chop_speed) return 1;
    Tree copy = *tree;
    int turns = 1;
    for (;;) {
        copy.health -= chop_speed;
        if (copy.health <= 0) return turns;
        tree_apply_turn(&copy);
        turns++;
    }
}
static bool tree_choppable_for_full_yield(const Tree *tree, int chop_power) {
    if (tree_grown(tree)) return true;
    Tree copy = *tree;
    for (;;) {
        copy.health -= chop_power;
        if (copy.health <= 0) return copy.size == 4;
        tree_apply_turn(&copy);
    }
}
static int tree_turns_till_fruit(const Tree *tree) {
    if (tree_fruit(tree)) return 0;
    return (4 - tree->size) * tree->period + tree->cooldown;
}
static int worker_free_capacity(const Worker *worker) {
    return worker->carry_capacity - worker->carry_plum - worker->carry_lemon - worker->carry_apple -
        worker->carry_banana - worker->carry_iron - worker->carry_wood;
}
static bool worker_full(const Worker *worker) { return worker_free_capacity(worker) <= 0; }
static bool worker_my(const Worker *worker) { return worker->player == 0; }
static bool worker_can_chop(const Worker *worker) { return worker->chop_power > 0; }
static bool worker_can_harvest(const Worker *worker) { return worker->harvest_power > 0; }
static int worker_carrying(const Worker *worker, Fruit type) {
    if (type == PLUM) return worker->carry_plum;
    if (type == LEMON) return worker->carry_lemon;
    if (type == APPLE) return worker->carry_apple;
    return worker->carry_banana;
}
static bool worker_carry_seed(const Worker *worker) {
    return worker->carry_plum > 0 || worker->carry_lemon > 0 || worker->carry_apple > 0 || worker->carry_banana > 0;
}
static Fruit worker_carried_seed(const Worker *worker) {
    if (worker->carry_plum > 0) return PLUM;
    if (worker->carry_lemon > 0) return LEMON;
    if (worker->carry_apple > 0) return APPLE;
    return BANANA;
}
static int worker_mining_turns(const Worker *worker) {
    return ceil_div_int(worker->carry_capacity, worker->chop_power);
}
static int tree_turns_till_fruit_in_hand(const Tree *tree, const Worker *worker, Path path) {
    int distance_penalty = ceil_div_int(max_int(path.len - 1, 0), worker->move_speed);
    if (tree->fruits > 0) return distance_penalty + 1;
    return max_int(distance_penalty, (4 - tree->size) * tree->period + tree->cooldown) + 1;
}
static int tree_turns_till_chop(const Tree *tree, const Worker *worker, Path path) {
    return ceil_div_int(max_int(path.len - 1, 0), worker->move_speed) + tree_chop_turns(tree, worker->chop_power);
}
static int tree_fruits_at_arrival(const Tree *tree, int turns) {
    if (tree->fruits == 3) return 3;
    int turns_to_grow = tree_grown(tree) ? 0 : tree->cooldown + (4 - (tree->size + 1)) * tree->period;
    if (turns < turns_to_grow) return 0;
    int fruiting_turns = turns - turns_to_grow;
    int starting_cd = tree_grown(tree) ? tree->cooldown : tree->period;
    if (starting_cd - fruiting_turns > 0) return tree->fruits;
    fruiting_turns -= starting_cd;
    int fruiting_periods = fruiting_turns / tree->period;
    return min_int(3, tree->fruits + fruiting_periods + 1);
}
static int tree_turns_till_size(const Tree *tree, int desired_size) {
    if (tree->size >= desired_size) return 0;
    int growth_periods = desired_size - tree->size - 1;
    return max_int(growth_periods, 0) * tree->period + tree->cooldown;
}
static bool tree_damaged(const Tree *tree) { return tree->health < type_health(tree->type, tree->size); }
static double tree_average_fruit_yield(const Tree *tree, int distance_to_camp, const Worker *worker) {
    int move_turns = ceil_div_int(max_int(distance_to_camp - 1, 0), worker->move_speed);
    int travel_turns = move_turns * 2 + 1;
    int min_cycle = travel_turns + 1;
    if (min_cycle < tree->period) return 1.0 / tree->period;
    double available = min_cycle / (double)tree->period;
    if (available > 3.0) available = 3.0;
    double carried = available < worker->carry_capacity ? available : worker->carry_capacity;
    double harvest_turns = carried / worker->harvest_power;
    double value = carried / (travel_turns + harvest_turns);
    return rounded_hundredths(value);
}
static int worker_node(const Controller *c, const Worker *worker) { return node_of(&c->grid, worker->x, worker->y); }
static int tree_node(const Controller *c, const Tree *tree) { return node_of(&c->grid, tree->x, tree->y); }
static Path shortest_path(const Controller *c, int from, int to) {
    return grid_shortest_path(&c->grid, from, to, -1);
}
static Path shortest_path_excluding(const Controller *c, int from, int to, int excluded) {
    return grid_shortest_path(&c->grid, from, to, excluded);
}
static bool plan_exists(const Controller *c, int worker_id) {
    return worker_id >= 0 && worker_id < MAX_WORKER_ID && c->plans[worker_id].present;
}
static bool plan_is(const Controller *c, int worker_id, PlanName name) {
    return plan_exists(c, worker_id) && c->plans[worker_id].name == name;
}
static void set_plan(Controller *c, PlanName name, int worker_id, int type, int node) {
    if (worker_id < 0 || worker_id >= MAX_WORKER_ID) return;
    Plan *plan = &c->plans[worker_id];
    if (!plan->present) plan->order = c->plan_order++;
    plan->present = true;
    plan->name = name;
    plan->worker_id = worker_id;
    plan->type = type;
    plan->node = node;
    plan->weight = 0;
}
static Worker *worker_ptr(Controller *c, int index) { return index >= 0 ? &c->workers[index] : NULL; }
static Tree *tree_at_node(Controller *c, int node) { return c->tree_at[node] >= 0 ? &c->trees[c->tree_at[node]] : NULL; }
static int shortest_path_size(const Controller *c, int from, int to);
static int my_worker_count(const Controller *c) {
    int count = 0;
    for (int i = 0; i < c->worker_count; i++) if (worker_my(&c->workers[i])) count++;
    return count;
}
static int opp_worker_count(const Controller *c) {
    int count = 0;
    for (int i = 0; i < c->worker_count; i++) if (!worker_my(&c->workers[i])) count++;
    return count;
}
static int inventory_best_tier(int store, int allowed_max, int existing) {
    if (store >= existing + 9) return min_int(allowed_max, 3);
    if (store >= existing + 4) return min_int(allowed_max, 2);
    if (store >= existing + 1) return 1;
    return 0;
}
static bool best_intermediate_worker(const Controller *c, char out[32]) {
    int existing = my_worker_count(c);
    int plum = inventory_best_tier(c->mine_inv.plum, 3, existing);
    int lemon = inventory_best_tier(c->mine_inv.lemon, 2, existing);
    int apple = inventory_best_tier(c->mine_inv.apple, lemon, existing);
    int iron = inventory_best_tier(c->mine_inv.iron, lemon, existing);
    if (!plum || !lemon || !apple || !iron) return false;
    snprintf(out, 32, "%d %d %d %d", plum, lemon, apple, iron);
    return true;
}
static void worker_cost(const Controller *c, int move, int carry, int harvest, int chop, int costs[4]) {
    int existing = my_worker_count(c);
    costs[PLUM] = existing + sq(move);
    costs[LEMON] = existing + sq(carry);
    costs[APPLE] = existing + sq(harvest);
    costs[3] = existing + sq(chop);
}
static bool can_afford(const Controller *c, const int costs[4]) {
    return c->mine_inv.plum >= costs[PLUM] && c->mine_inv.lemon >= costs[LEMON] &&
        c->mine_inv.apple >= costs[APPLE] && c->mine_inv.iron >= costs[3];
}
static bool use_shortscale(const Controller *c) { return c->wet_near_count == 0; }
static int turns_to_gather(Controller *c, int type, int count);
static int prediction_cost(const Prediction *p) { return p->costs[PLUM] + p->costs[LEMON] + p->costs[APPLE]; }
static int prediction_cycle_length(const Prediction *p) { return p->chop <= 0 ? 999 : ceil_div_int(6, p->chop) + 3 + (p->move < 2 ? 1 : 0); }
static int prediction_chop_cycles(const Prediction *p) { return floor_div_int(p->remaining_turns, prediction_cycle_length(p)); }
static int prediction_chop_points(const Prediction *p) { return prediction_chop_cycles(p) * 4 * p->carry; }
static int prediction_grand_total(const Prediction *p) { return prediction_chop_points(p) - (prediction_cost(p) * 3 + 1) / 2; }
static ArenaPressure arena_pressure(const Controller *c) {
    ArenaPressure p;
    p.my_score = inventory_score(&c->mine_inv);
    p.opp_score = inventory_score(&c->opp_inv);
    p.diff = p.my_score - p.opp_score;
    p.low_score = c->turn >= 160 && p.my_score < 100;
    p.behind = c->turn >= 180 && p.diff < -80;
    p.cashout = c->turn >= 290 || (c->turn >= 260 && (p.low_score || p.behind));
    p.no_train = c->turn >= 225 || p.cashout;
    return p;
}
static int worker_score_load(const Worker *worker) {
    return worker->carry_plum + worker->carry_lemon + worker->carry_apple + worker->carry_banana + 4 * worker->carry_wood;
}
static Prediction predict(Controller *c, int move, int carry, int harvest, int chop) {
    Prediction p;
    memset(&p, 0, sizeof(p));
    p.move = move;
    p.carry = carry;
    p.harvest = harvest;
    p.chop = chop;
    worker_cost(c, move, carry, harvest, chop, p.costs);
    p.turns =
        turns_to_gather(c, PLUM, p.costs[PLUM] - c->mine_inv.plum) +
        turns_to_gather(c, LEMON, p.costs[LEMON] - c->mine_inv.lemon) +
        turns_to_gather(c, APPLE, p.costs[APPLE] - c->mine_inv.apple) +
        turns_to_gather(c, 3, p.costs[3] - c->mine_inv.iron);
    p.remaining_turns = 300 - (c->turn + p.turns);
    return p;
}
static bool node_in_plan_values(const Controller *c, int node) {
    for (int i = 0; i < MAX_WORKER_ID; i++) {
        if (c->plans[i].present && c->plans[i].node == node) return true;
    }
    return false;
}
static int my_worker_node_for_id(const Controller *c, int worker_id) {
    for (int i = 0; i < c->worker_count; i++) {
        if (worker_my(&c->workers[i]) && c->workers[i].id == worker_id) return worker_node(c, &c->workers[i]);
    }
    return -1;
}
static bool node_reserved_by_any_plan(const Controller *c, int node) {
    for (int i = 0; i < MAX_WORKER_ID; i++) {
        if (!c->plans[i].present) continue;
        if (c->plans[i].node == node) return true;
        if (my_worker_node_for_id(c, c->plans[i].worker_id) == node) return true;
    }
    return false;
}
static bool my_worker_on_node_except(const Controller *c, int node, int worker_id) {
    for (int i = 0; i < c->worker_count; i++) {
        const Worker *other = &c->workers[i];
        if (worker_my(other) && other->id != worker_id && worker_node(c, other) == node) return true;
    }
    return false;
}

[[maybe_unused]] static bool node_reserved_for_chop(const Controller *c, int node) {
    for (int i = 0; i < c->reserved_chop_count; i++) {
        if (c->reserved_chop_nodes[i] == node) return true;
    }
    return false;
}
static bool chop_reservation_blocks_harvest(const Controller *c, int node, const Worker *worker) {
#if !ENABLE_CHOP_RESERVATION
    (void)c;
    (void)node;
    (void)worker;
    return false;
#else
    for (int i = 0; i < c->reserved_chop_count; i++) {
        if (c->reserved_chop_nodes[i] != node || c->reserved_chop_workers[i] == worker->id) continue;
        Worker *reserved = NULL;
        for (int w = 0; w < c->worker_count; w++) {
            if (worker_my(&c->workers[w]) && c->workers[w].id == c->reserved_chop_workers[i]) {
                reserved = (Worker *)&c->workers[w];
                break;
            }
        }
        if (!reserved) continue;
        Path reserved_path = shortest_path(c, worker_node(c, reserved), node);
        Path worker_path = shortest_path(c, worker_node(c, worker), node);
        int reserved_turns = ceil_div_int(max_int(reserved_path.len - 1, 0), reserved->move_speed);
        int worker_turns = ceil_div_int(max_int(worker_path.len - 1, 0), worker->move_speed);
        int escape_turns = 1;
        if (worker_node(c, worker) == node) escape_turns = 0;
        else {
            int best_escape = INT_MAX;
            for (int n = 0; n < c->grid.neighbor_count[node]; n++) {
                int next = c->grid.neighbors[node][n];
                if (my_worker_on_node_except(c, next, worker->id) || c->opp_worker_at[next] >= 0) continue;
                Path escape = shortest_path(c, node, next);
                if (escape.len > 0) best_escape = min_int(best_escape, ceil_div_int(escape.len - 1, worker->move_speed));
            }
            if (best_escape != INT_MAX) escape_turns = best_escape;
        }
        int harvest_window = worker_turns + 1 + escape_turns;
        if (reserved_turns <= harvest_window + CHOP_HARVEST_MARGIN) return true;
    }
    return false;
#endif
}
static void reserve_chop_node(Controller *c, int worker_id, int node) {
#if ENABLE_CHOP_RESERVATION
    if (node < 0 || node_reserved_for_chop(c, node) || c->reserved_chop_count >= MAX_WORKERS) return;
    c->reserved_chop_nodes[c->reserved_chop_count] = node;
    c->reserved_chop_workers[c->reserved_chop_count++] = worker_id;
#else
    (void)c;
    (void)worker_id;
    (void)node;
#endif
}
static int closest_by_path(const Controller *c, int from, const int nodes[], int count) {
    int best = -1, best_len = INT_MAX;
    for (int i = 0; i < count; i++) {
        Path p = shortest_path(c, from, nodes[i]);
        if (p.len > 0 && p.len < best_len) {
            best = nodes[i];
            best_len = p.len;
        }
    }
    return best;
}
static int closest_dropoff(const Controller *c, int from) {
    return closest_by_path(c, from, c->dropoff, c->dropoff_count);
}
static Path shortest_path_to_drop(const Controller *c, int from) {
    return shortest_path(c, from, closest_dropoff(c, from));
}
static int closest_mining(const Controller *c, int from) {
    return closest_by_path(c, from, c->mining_list, c->mining_count);
}
static Path shortest_path_to_mining(const Controller *c) {
    Path best = no_path();
    for (int i = 0; i < c->mining_count; i++) {
        for (int j = 0; j < c->dropoff_count; j++) {
            Path p = shortest_path(c, c->dropoff[j], c->mining_list[i]);
            if (p.len > 0 && (best.len == 0 || p.len < best.len)) best = p;
        }
    }
    return best;
}
static int turns_to_drop(const Controller *c, const Worker *worker) {
    if (!worker_full(worker)) return 0;
    return ceil_div_int(max_int(shortest_path_to_drop(c, worker_node(c, worker)).len - 1, 0), worker->move_speed) + 1;
}
static void go_to(Controller *c, Worker *worker, int node) {
    if (node < 0) return;
    Path path = no_path();
    if (path.len == 0) path = shortest_path(c, worker_node(c, worker), node);
    if (path.len == 0) return;
    int step = worker->move_speed < path.len ? worker->move_speed : path.len - 1;
    int naive_node = path.node[step];
    if (node_reserved_by_any_plan(c, naive_node) || c->opp_worker_at[naive_node] >= 0) {
        Path alternate = shortest_path_excluding(c, worker_node(c, worker), node, naive_node);
        if (alternate.len == path.len) {
            int alternate_step = worker->move_speed < alternate.len ? worker->move_speed : alternate.len - 1;
            set_plan(c, MOVE, worker->id, -1, alternate.node[alternate_step]);
            return;
        }
    }
    set_plan(c, MOVE, worker->id, -1, naive_node);
}
static void go_and_harvest(Controller *c, Worker *worker, int node) {
    if (worker_node(c, worker) == node) set_plan(c, HARVEST, worker->id, -1, -1);
    else go_to(c, worker, node);
}
static void go_and_pick(Controller *c, Worker *worker, int node, Fruit type) {
    if (worker_node(c, worker) == node) set_plan(c, PICK, worker->id, type, -1);
    else go_to(c, worker, node);
}
static void go_and_plant(Controller *c, Worker *worker, int node, Fruit type) {
    if (worker_node(c, worker) == node) set_plan(c, PLANT, worker->id, type, -1);
    else go_to(c, worker, node);
}
static void go_and_chop(Controller *c, Worker *worker, int node) {
    Worker *chopper = worker_ptr(c, c->chopper);
    if (chopper && chopper->id == worker->id) reserve_chop_node(c, worker->id, node);
    if (worker_node(c, worker) == node) set_plan(c, CHOP, worker->id, -1, node);
    else go_to(c, worker, node);
}
static void go_and_mine(Controller *c, Worker *worker, int node) {
    if (worker_node(c, worker) == node) set_plan(c, MINE, worker->id, -1, -1);
    else {
        go_to(c, worker, node);
    }
}
static void go_and_drop(Controller *c, Worker *worker, int node) {
    int current = worker_node(c, worker);
    if (node_reserved_by_any_plan(c, node)) {
        Path path = shortest_path(c, current, node);
        if (path.len > 0 && (path.len - 1) / (double)worker->move_speed == 1.0) {
            int alternate[MAX_NODES], alternate_count = 0;
            for (int i = 0; i < c->dropoff_count; i++) {
                if (c->dropoff[i] != node) alternate[alternate_count++] = c->dropoff[i];
            }
            int alternate_dropoff = closest_by_path(c, current, alternate, alternate_count);
            if (alternate_dropoff >= 0) {
                go_to(c, worker, alternate_dropoff);
                return;
            }
        }
    }
    if (current == node) set_plan(c, DROP, worker->id, -1, -1);
    else go_to(c, worker, node);
}
static bool seek_to_chop(Controller *c, Worker *worker, int node) {
    if (worker_full(worker)) go_and_drop(c, worker, closest_dropoff(c, worker_node(c, worker)));
    else go_and_chop(c, worker, node);
    return plan_exists(c, worker->id);
}
static bool gather_iron(Controller *c, Worker *worker) {
    if (worker_full(worker)) go_and_drop(c, worker, closest_dropoff(c, worker_node(c, worker)));
    else go_and_mine(c, worker, closest_mining(c, worker_node(c, worker)));
    return plan_exists(c, worker->id);
}
static bool node_empty_tree(Controller *c, int node) { return node >= 0 && c->tree_at[node] < 0; }
static int score_seed_node(const Controller *c, int node) {
    int neighbors = 0;
    for (int i = 0; i < c->grid.neighbor_count[node]; i++) {
        if (c->near_camp[c->grid.neighbors[node][i]]) neighbors++;
    }
    return neighbors;
}
static bool node_secluded(const Controller *c, int node) {
    return c->distance_between_camps + 10 < shortest_path(c, c->opp_camp, node).len - 1;
}
static void init_near_camp_and_seed(Controller *c) {
    for (int i = 0; i < c->grass_count; i++) {
        int node = c->grass_list[i];
        if (shortest_path(c, c->my_camp, node).len <= 4) {
            list_add(c->near_camp, c->near_list, &c->near_count, node);
            if (c->wet[node]) list_add(c->wet_near_camp, c->wet_near_list, &c->wet_near_count, node);
        }
    }
    int best = -1;
    if (c->wet_near_count) {
        int score_best = INT_MIN, my_best = INT_MAX, opp_best = INT_MIN;
        for (int i = 0; i < c->wet_near_count; i++) {
            int node = c->wet_near_list[i];
            int score = (node_secluded(c, node) ? 1 : 0) + score_seed_node(c, node);
            int mine = shortest_path(c, c->my_camp, node).len;
            int opp = shortest_path(c, c->opp_camp, node).len;
            if (score > score_best || (score == score_best && (mine < my_best || (mine == my_best && opp > opp_best)))) {
                best = node;
                score_best = score;
                my_best = mine;
                opp_best = opp;
            }
        }
    } else {
        int score_best = INT_MIN, my_best = INT_MAX, opp_best = INT_MIN;
        for (int i = 0; i < c->near_count; i++) {
            int node = c->near_list[i];
            int score = score_seed_node(c, node);
            int mine = shortest_path(c, c->my_camp, node).len;
            int opp = shortest_path(c, c->opp_camp, node).len;
            if (score > score_best || (score == score_best && (mine < my_best || (mine == my_best && opp > opp_best)))) {
                best = node;
                score_best = score;
                my_best = mine;
                opp_best = opp;
            }
        }
    }
    c->seed_node = best;
    for (int i = 0; i < c->near_count; i++) {
        if (c->near_list[i] != c->seed_node) list_add(c->near_except_seed, c->near_except_seed_list, &c->near_except_seed_count, c->near_list[i]);
    }
}
static void init_opp_corners(Controller *c) {
    int camp_x = node_x(&c->grid, c->opp_camp);
    int camp_y = node_y(&c->grid, c->opp_camp);
    for (int i = 0; i < c->opp_dropoff_count; i++) {
        int dropoff = c->opp_dropoff[i];
        for (int j = 0; j < c->grid.neighbor_count[dropoff]; j++) {
            int node = c->grid.neighbors[dropoff][j];
            if (node_x(&c->grid, node) == camp_x || node_y(&c->grid, node) == camp_y) continue;
            if (shortest_path(c, c->opp_camp, node).len != 3) continue;
            list_add(c->opp_corner, c->opp_corner_list, &c->opp_corner_count, node);
        }
    }
}
static void controller_init(Controller *c, int width, int height, char field[MAX_H][MAX_W + 4]) {
    memset(c, 0, sizeof(*c));
    c->my_camp = c->opp_camp = c->seed_node = -1;
    c->helper = c->inter = c->chopper = -1;
    grid_fill(&c->grid, width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            char cell = field[y][x];
            int node = node_of(&c->grid, x, y);
            if (cell == '~' || cell == '#' || cell == '+') grid_remove_cell(&c->grid, node);
            if (cell == '.') list_add(c->grass, c->grass_list, &c->grass_count, node);
            if (cell == '~') list_add(c->water, c->water_list, &c->water_count, node);
            if (cell == '+') list_add(c->iron, c->iron_list, &c->iron_count, node);
            if (cell == '0') c->my_camp = node;
            if (cell == '1') c->opp_camp = node;
        }
    }
    c->distance_between_camps = max_int(shortest_path(c, c->my_camp, c->opp_camp).len - 2, 0);
    int around[4], count = n4(&c->grid, c->my_camp, around);
    c->dropoff_count = 0;
    for (int i = 0; i < c->grid.neighbor_count[c->my_camp]; i++) {
        int node = c->grid.neighbors[c->my_camp][i];
        if (node != c->opp_camp) c->dropoff[c->dropoff_count++] = node;
    }
    for (int i = 0; i < count; i++) neighbor_remove(&c->grid, around[i], c->my_camp);
    count = n4(&c->grid, c->opp_camp, around);
    c->opp_dropoff_count = 0;
    for (int i = 0; i < c->grid.neighbor_count[c->opp_camp]; i++) {
        int node = c->grid.neighbors[c->opp_camp][i];
        if (node != c->my_camp) c->opp_dropoff[c->opp_dropoff_count++] = node;
    }
    for (int i = 0; i < count; i++) neighbor_remove(&c->grid, around[i], c->opp_camp);
    for (int i = 0; i < c->grass_count; i++) {
        int node = c->grass_list[i];
        int n[4], n_count = n4(&c->grid, node, n);
        for (int j = 0; j < n_count; j++) {
            if (c->water[n[j]]) list_add(c->wet, c->wet_list, &c->wet_count, node);
            if (c->iron[n[j]]) list_add(c->mining, c->mining_list, &c->mining_count, node);
        }
    }
    init_near_camp_and_seed(c);
    init_opp_corners(c);
    for (int i = 0; i < c->grass_count; i++) {
        int node = c->grass_list[i];
        int my_path = shortest_path(c, c->my_camp, node).len - 1;
        int opp_path = shortest_path(c, c->opp_camp, node).len - 1;
        if (my_path < opp_path && opp_path > c->distance_between_camps) list_add(c->my_nodes, c->my_node_list, &c->my_node_count, node);
    }
}
static int trees_within_camp_count(const Controller *c, Fruit wanted, bool only_type) {
    int count = 0;
    for (int i = 0; i < c->tree_count; i++) {
        int node = tree_node(c, &c->trees[i]);
        if (c->near_camp[node] && (!only_type || c->trees[i].type == wanted)) count++;
    }
    return count;
}
static void update_map_features(Controller *c) {
    MapFeatures f = {};
    f.camp_manhattan = manhattan(&c->grid, c->my_camp, c->opp_camp);
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        int distance = shortest_path_size(c, c->my_camp, node) - 1;
        if (distance < 0 || distance > 6) continue;
        if (tree->fruits > 0) {
            f.ready_near++;
            if (tree->size >= 3 && tree->health > 0) f.choppable_ready_near++;
        }
    }
    f.poor_long_distance_opening = f.camp_manhattan > 10 && f.ready_near <= 2 && f.choppable_ready_near <= 2;
    c->map_features = f;
}
static bool any_grown_tree(const Controller *c) {
    for (int i = 0; i < c->tree_count; i++) if (tree_grown(&c->trees[i])) return true;
    return false;
}
static int shortest_path_size(const Controller *c, int from, int to) {
    return shortest_path(c, from, to).len;
}
static bool plan_chops_tree(const Controller *c, int worker_index, int node);
static int path_turns_for_worker(const Worker *worker, Path path) {
    return ceil_div_int(max_int(path.len - 1, 0), worker->move_speed);
}
static TaskScore make_task_score(int points, int turns, double risk_penalty) {
    TaskScore score;
    score.points = points;
    score.turns = max_int(turns, 1);
    score.risk_penalty = risk_penalty;
    score.net_points = points - (int)(risk_penalty + 0.5);
    score.points_per_turn = score.net_points / (double)score.turns;
    return score;
}
static int tree_size_after_chop(const Tree *tree, int chop_power) {
    if (chop_power < 1) return 0;
    Tree copy = *tree;
    for (;;) {
        copy.health -= chop_power;
        if (copy.health <= 0) return copy.size;
        tree_apply_turn(&copy);
    }
}
static double opponent_tree_risk(const Controller *c, int node, int my_turns) {
    int best_opp = 99;
    for (int i = 0; i < c->worker_count; i++) {
        const Worker *opp = &c->workers[i];
        if (worker_my(opp)) continue;
        Path path = shortest_path(c, worker_node(c, opp), node);
        if (path.len <= 0) continue;
        int opp_turns = path_turns_for_worker(opp, path);
        if (opp_turns < best_opp) best_opp = opp_turns;
    }
    if (best_opp == 99) return 0.0;
    if (best_opp < my_turns) return 3.0;
    if (best_opp == my_turns) return 1.0;
    return 0.0;
}
static TaskScore score_chop_task(Controller *c, Worker *worker, int node) {
    Tree *tree = tree_at_node(c, node);
    if (!tree || worker->chop_power <= 0 || worker_free_capacity(worker) <= 0) return make_task_score(0, 99, 999.0);
    Path to_tree = shortest_path(c, worker_node(c, worker), node);
    if (to_tree.len <= 0) return make_task_score(0, 99, 999.0);
    int move_turns = path_turns_for_worker(worker, to_tree);
    int chop_turns = tree_chop_turns(tree, worker->chop_power);
    int yield_size = tree_size_after_chop(tree, worker->chop_power);
    int carried_wood = min_int(worker_free_capacity(worker), yield_size);
    int points = carried_wood * 4;
    int drop_turns = ceil_div_int(max_int(shortest_path_to_drop(c, node).len - 1, 0), worker->move_speed) + 1;
    int turns = move_turns + chop_turns + drop_turns;
    double risk = opponent_tree_risk(c, node, move_turns + chop_turns);
    if (!tree_grown(tree)) risk += 1.0;
    if (c->near_camp[node]) risk -= 0.5;
    return make_task_score(points, turns, risk);
}
static TaskScore score_harvest_task(Controller *c, Worker *worker, int node) {
    Tree *tree = tree_at_node(c, node);
    if (!tree || worker->harvest_power <= 0 || worker_free_capacity(worker) <= 0) return make_task_score(0, 99, 999.0);
    if (chop_reservation_blocks_harvest(c, node, worker)) return make_task_score(0, 99, 999.0);
    Path path = shortest_path(c, worker_node(c, worker), node);
    if (path.len <= 0) return make_task_score(0, 99, 999.0);
    int move_turns = path_turns_for_worker(worker, path);
    int wait = max_int(0, tree_turns_till_fruit(tree) - move_turns);
    if (wait > 4) return make_task_score(0, 99, 999.0);
    int fruits = tree_fruits_at_arrival(tree, move_turns + wait);
    int points = min_int(worker_free_capacity(worker), min_int(worker->harvest_power, fruits));
    if (points <= 0) return make_task_score(0, 99, 999.0);
    int drop_turns = ceil_div_int(max_int(shortest_path_to_drop(c, node).len - 1, 0), worker->move_speed) + 1;
    double risk = opponent_tree_risk(c, node, move_turns + wait);
    if (c->near_camp[node]) risk -= 0.25;
    if (c->opp_worker_at[node] >= 0) risk -= 0.5;
    return make_task_score(points, move_turns + wait + 1 + drop_turns, risk);
}
static bool plan_chops_tree(const Controller *c, int worker_index, int node) {
    if (worker_index < 0) return false;
    int id = c->workers[worker_index].id;
    return plan_is(c, id, CHOP) && c->plans[id].node == node;
}
static int tree_best_path_metric(Controller *c, Worker *worker, bool grown, int skip_node) {
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if ((grown && !tree_grown(tree)) || node == skip_node) continue;
        int value = shortest_path_size(c, worker_node(c, worker), node);
        if (value > 0 && value < score) {
            best = node;
            score = value;
        }
    }
#if ENABLE_SCORE_CHOP_TIEBREAK
    if (best >= 0 && worker_can_chop(worker) && worker_free_capacity(worker) > 0 && c->turn >= 55) {
        TaskScore baseline = score_chop_task(c, worker, best);
        TaskScore scored_best = baseline;
        int scored_node = best;
        for (int i = 0; i < c->tree_count; i++) {
            Tree *tree = &c->trees[i];
            int node = tree_node(c, tree);
            if ((grown && !tree_grown(tree)) || node == skip_node || node == best) continue;
            if (my_worker_on_node_except(c, node, worker->id)) continue;
            TaskScore candidate = score_chop_task(c, worker, node);
            if (candidate.net_points <= 0 || candidate.risk_penalty > 2.5) continue;
            if (candidate.points_per_turn > scored_best.points_per_turn) {
                scored_best = candidate;
                scored_node = node;
            }
        }
        if (scored_node != best) {
            int extra_turns = scored_best.turns - baseline.turns;
            bool better_total = scored_best.net_points >= baseline.net_points + 4 &&
                scored_best.points_per_turn >= baseline.points_per_turn + 0.05;
            bool better_rate = scored_best.points_per_turn >= baseline.points_per_turn + 0.35 &&
                extra_turns <= (grown ? 5 : 3);
            if (extra_turns <= 8 && (better_total || better_rate)) {
                return scored_node;
            }
        }
    }
#else
    (void)c;
#endif
    return best;
}
static int best_fruit_tree_anywhere(Controller *c, Worker *worker, Fruit type, int max_wait, int *turns_out) {
    int best = -1, best_turns = INT_MAX;
    Worker *helper = worker_ptr(c, c->helper);
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if (tree->type != type) continue;
        if (chop_reservation_blocks_harvest(c, node, worker)) continue;
        if (my_worker_on_node_except(c, node, worker->id)) continue;
        if (helper && plan_is(c, helper->id, HARVEST) && worker_node(c, helper) == node) continue;
        Path path = shortest_path(c, worker_node(c, worker), node);
        int turns = tree_turns_till_fruit_in_hand(tree, worker, path);
        if (turns <= max_wait && turns < best_turns) {
            best = node;
            best_turns = turns;
        }
    }
    if (turns_out) *turns_out = best_turns;
    return best;
}
static int self_harvest_seed(const Controller *c) {
    if (inventory_has(&c->mine_inv, BANANA)) return BANANA;
    if (inventory_has(&c->mine_inv, PLUM)) return PLUM;
    if (inventory_has(&c->mine_inv, LEMON)) return LEMON;
    return -1;
}
static bool handle_planting_at_end_of(Controller *c, Worker *worker, Path path, Fruit type) {
    if (path.len < 2) return false;
    int current = worker_node(c, worker);
    int last = path.node[path.len - 1];
    if (current == last && worker_carrying(worker, type) > 0) set_plan(c, PLANT, worker->id, type, -1);
    else if (worker_carrying(worker, type) > 0) go_to(c, worker, last);
    else if (current == path.node[1] && inventory_has(&c->mine_inv, type)) set_plan(c, PICK, worker->id, type, -1);
    else if (inventory_has(&c->mine_inv, type)) go_to(c, worker, path.node[1]);
    return plan_exists(c, worker->id);
}
static bool ensure_sufficient_growth(Controller *c, Worker *worker, Fruit type) {
    double expected = 0.0;
    for (int i = 0; i < c->near_count; i++) {
        int node = c->near_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || tree->type != type) continue;
        if (type == LEMON) expected += c->wet[node] ? 1.0 / 3.0 : 1.0 / 8.0;
        else expected += c->wet[node] ? 1.0 / 8.0 : 1.0 / 3.0;
    }
    if ((type == LEMON && expected >= 2.0 / 8.0) || (type == PLUM && expected >= 1.0 / 8.0)) return false;
    Path best = no_path();
    double best_score = 1e9;
    int best_opp = INT_MIN;
    for (int i = 0; i < c->wet_near_count; i++) {
        int node = c->wet_near_list[i];
        if (!node_empty_tree(c, node)) continue;
        Path path = shortest_path(c, c->my_camp, node);
        double score = path.len - (node_secluded(c, node) ? 1.1 : 0.0);
        int opp_distance = shortest_path_size(c, c->opp_camp, node);
        double score_diff = score > best_score ? score - best_score : best_score - score;
        bool lemon_tiebreak = type == LEMON && score_diff <= EPSILON && opp_distance > best_opp;
        if (score < best_score - EPSILON || lemon_tiebreak) {
            best = path;
            best_score = score;
            best_opp = opp_distance;
        }
    }
    if (best.len) return handle_planting_at_end_of(c, worker, best, type);
    best = no_path();
    best_score = 1e9;
    for (int i = 0; i < c->near_count; i++) {
        int node = c->near_list[i];
        if (!node_empty_tree(c, node)) continue;
        Path mine = shortest_path(c, c->my_camp, node);
        Path worker_path = shortest_path(c, worker_node(c, worker), node);
        double score = mine.len + worker_path.len;
        if (type == LEMON) score -= shortest_path(c, c->opp_camp, node).len;
        if (score < best_score) {
            best = mine;
            best_score = score;
        }
    }
    return best.len ? handle_planting_at_end_of(c, worker, best, type) : false;
}
static bool harvest_already_stood_on_tree(Controller *c, Worker *worker, bool accept_lemon, bool accept_plum) {
    if (worker_full(worker)) return false;
    Tree *tree = tree_at_node(c, worker_node(c, worker));
    if (!tree || !tree_fruit(tree)) return false;
    if (chop_reservation_blocks_harvest(c, worker_node(c, worker), worker)) return false;
    if (!((accept_lemon && tree->type == LEMON) || (accept_plum && tree->type == PLUM))) return false;
    go_and_harvest(c, worker, worker_node(c, worker));
    return true;
}
static bool gather_initial_fruit(Controller *c, Worker *worker, Fruit type, int max_wait) {
    int current = worker_node(c, worker);
    if (worker_full(worker)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return true;
    }
    Tree *stood = tree_at_node(c, current);
    if (stood && stood->type == type && tree_fruit(stood)) {
        if (chop_reservation_blocks_harvest(c, current, worker)) return false;
        set_plan(c, HARVEST, worker->id, -1, -1);
        return true;
    }
    int best = -1, best_turns = INT_MAX;
    Worker *helper = worker_ptr(c, c->helper);
    for (int i = 0; i < c->near_count; i++) {
        int node = c->near_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || tree->type != type) continue;
        if (chop_reservation_blocks_harvest(c, node, worker)) continue;
        if (my_worker_on_node_except(c, node, worker->id)) continue;
        if (helper && plan_is(c, helper->id, HARVEST) && worker_node(c, helper) == node) continue;
        Path p = shortest_path(c, current, node);
        int turns = tree_turns_till_fruit_in_hand(tree, worker, p);
        if (turns <= max_wait && turns < best_turns) {
            best = node;
            best_turns = turns;
        }
    }
    if (best < 0) return false;
    go_to(c, worker, best);
    return true;
}
static bool gather_anywhere_fruit(Controller *c, Worker *worker, Fruit type, int max_wait) {
    int current = worker_node(c, worker);
    if (worker_full(worker)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return true;
    }
    Tree *stood = tree_at_node(c, current);
    if (stood && stood->type == type && tree_fruit(stood)) {
        if (chop_reservation_blocks_harvest(c, current, worker)) return false;
        set_plan(c, HARVEST, worker->id, -1, -1);
        return true;
    }
    int turns = 0, best = best_fruit_tree_anywhere(c, worker, type, max_wait, &turns);
    if (best < 0) return false;
    go_to(c, worker, best);
    return true;
}
static bool gather_and_plant(Controller *c, Worker *worker, Fruit type) {
    if (arena_pressure(c).cashout) return false;
    if (worker_carrying(worker, type) > 0) {
    int best = -1;
    double best_score = 1e9;
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        if (!node_empty_tree(c, node)) continue;
        if (shortest_path_size(c, c->opp_camp, node) < 5) continue;
        double score = shortest_path_size(c, worker_node(c, worker), node) +
            shortest_path_size(c, c->my_camp, node) - (c->wet[node] ? 0.5 : 0.0);
            if (score < best_score) {
                best = node;
                best_score = score;
            }
        }
        if (best >= 0) go_and_plant(c, worker, best, type);
        else go_and_drop(c, worker, closest_dropoff(c, worker_node(c, worker)));
        return plan_exists(c, worker->id);
    }
    int best = -1, best_score = INT_MAX, best_opp = INT_MIN;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        if (tree->type != type) continue;
        int node = tree_node(c, tree);
        int score = tree_turns_till_fruit_in_hand(tree, worker, shortest_path(c, worker_node(c, worker), node));
        int opp_score = shortest_path_size(c, c->opp_camp, node);
        if (score < best_score || (score == best_score && opp_score > best_opp)) {
            best = node;
            best_score = score;
            best_opp = opp_score;
        }
    }
    if (best < 0) return false;
    go_and_harvest(c, worker, best);
    return true;
}
static bool seek_to_plant_carried_banana(Controller *c, Worker *worker) {
    if (arena_pressure(c).cashout) return false;
    if (worker->carry_banana <= 0) return false;
    if (node_empty_tree(c, c->seed_node)) {
        go_and_plant(c, worker, c->seed_node, BANANA);
        return true;
    }
    int best = -1;
    double best_score = 1e9;
    int best_opp = INT_MIN;
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        if (!node_empty_tree(c, node)) continue;
        double score = shortest_path_size(c, worker_node(c, worker), node) +
            shortest_path_size(c, c->my_camp, node) +
            shortest_path_size(c, c->seed_node, node) - (c->wet[node] ? 0.5 : 0.0);
        int opp_score = shortest_path_size(c, c->opp_camp, node);
        double score_diff = score > best_score ? score - best_score : best_score - score;
        if (score < best_score - EPSILON || (score_diff <= EPSILON && opp_score > best_opp)) {
            best = node;
            best_score = score;
            best_opp = opp_score;
        }
    }
    if (best >= 0) go_and_plant(c, worker, best, BANANA);
    return plan_exists(c, worker->id);
}
static bool seek_to_plant_banana(Controller *c, Worker *worker) {
    if (arena_pressure(c).cashout) return false;
    if (seek_to_plant_carried_banana(c, worker)) return true;
    int current = worker_node(c, worker);
    if (worker_full(worker)) return false;
    Tree *stood = tree_at_node(c, current);
    if (stood && stood->type == BANANA && tree_fruit(stood)) {
        if (chop_reservation_blocks_harvest(c, current, worker)) return false;
        go_and_harvest(c, worker, current);
        return true;
    }
    Tree *seed = tree_at_node(c, c->seed_node);
    if (seed && seed->type == BANANA &&
        tree_turns_till_fruit_in_hand(seed, worker, shortest_path(c, current, c->seed_node)) < 5) {
        if (chop_reservation_blocks_harvest(c, c->seed_node, worker)) return false;
        go_and_harvest(c, worker, c->seed_node);
        return true;
    }
    if (c->mine_inv.banana > 0) {
        go_and_pick(c, worker, closest_dropoff(c, current), BANANA);
        return true;
    }
    int best = -1, best_score = INT_MAX;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        if (tree->type != BANANA) continue;
        int node = tree_node(c, tree);
        if (chop_reservation_blocks_harvest(c, node, worker)) continue;
        int score = tree_turns_till_fruit_in_hand(tree, worker, shortest_path(c, current, node));
        if (score < best_score) {
            best = node;
            best_score = score;
        }
    }
    if (best >= 0) go_and_harvest(c, worker, best);
    return plan_exists(c, worker->id);
}
static bool enemy_in_near_camp(const Controller *c) {
    for (int i = 0; i < c->worker_count; i++) {
        if (!worker_my(&c->workers[i]) && c->near_camp[worker_node(c, &c->workers[i])]) return true;
    }
    return false;
}
static bool seek_to_self_plant(Controller *c, Worker *worker) {
    if (arena_pressure(c).cashout) return false;
    if (enemy_in_near_camp(c)) return false;
    int current = worker_node(c, worker);
    if (worker_carry_seed(worker)) {
        int best = -1, camp_score = INT_MAX, worker_score = INT_MAX;
        for (int i = 0; i < c->my_node_count; i++) {
            int node = c->my_node_list[i];
            if (!node_empty_tree(c, node)) continue;
            int camp = shortest_path_size(c, c->my_camp, node);
            int mine = shortest_path_size(c, current, node);
            if (camp < camp_score || (camp == camp_score && mine < worker_score)) {
                best = node;
                camp_score = camp;
                worker_score = mine;
            }
        }
        if (best >= 0) go_and_plant(c, worker, best, worker_carried_seed(worker));
        return plan_exists(c, worker->id);
    }
    if (tree_at_node(c, current)) {
        go_and_chop(c, worker, current);
        return true;
    }
    int seed = self_harvest_seed(c);
    if (seed < 0) return false;
    int candidates[MAX_NODES], candidate_count = 0;
    for (int i = 0; i < c->dropoff_count; i++) {
        int node = c->dropoff[i];
        if (!c->my_nodes[node] || node_in_plan_values(c, node)) continue;
        candidates[candidate_count++] = node;
    }
    int pickup = candidate_count ? closest_by_path(c, current, candidates, candidate_count) :
        closest_by_path(c, current, c->dropoff, c->dropoff_count);
    go_and_pick(c, worker, pickup, (Fruit)seed);
    return plan_exists(c, worker->id);
}
static bool chase_opp_corner(Controller *c, Worker *worker) {
    if (!inventory_any_fruit(&c->opp_inv) || !c->opp_corner_count) return false;
    int current = worker_node(c, worker);
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->opp_corner_count; i++) {
        int corner = c->opp_corner_list[i];
        int value = shortest_path_size(c, current, corner) + shortest_path_size(c, c->my_camp, corner);
        if (value < score) {
            best = corner;
            score = value;
        }
    }
    if (best < 0) return false;
    go_to(c, worker, best);
    return true;
}
static int turns_till_own_lemon_tree(Controller *c) {
    Worker *helper = worker_ptr(c, c->helper);
    if (!helper) return 300;
    int best = INT_MAX;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if (tree->type != LEMON || !c->near_camp[node]) continue;
        int value = tree_turns_till_fruit_in_hand(tree, helper, shortest_path(c, worker_node(c, helper), node));
        if (value < best) best = value;
    }
    if (best != INT_MAX) return best;
    if (c->mine_inv.lemon > 0) {
        int node = c->wet_near_count ? c->wet_near_list[0] : c->dropoff[0];
        int period = tree_period(LEMON, c->wet_near_count > 0);
        Tree tree = {LEMON, node_x(&c->grid, node), node_y(&c->grid, node), 1, 8, 0, period, period};
        return tree_turns_till_fruit_in_hand(&tree, helper, shortest_path(c, worker_node(c, helper), node));
    }
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        if (tree->type != LEMON) continue;
        int tree_node_value = tree_node(c, tree);
        int seed_turns = tree_turns_till_fruit_in_hand(tree, helper, shortest_path(c, worker_node(c, helper), tree_node_value));
        int get_back = shortest_path_size(c, tree_node_value, c->my_camp) - 2;
        int node = c->wet_near_count ? c->wet_near_list[0] : c->dropoff[0];
        int period = tree_period(LEMON, c->wet_near_count > 0);
        Tree growth = {LEMON, node_x(&c->grid, node), node_y(&c->grid, node), 1, 8, 0, period, period};
        int new_growth = tree_turns_till_fruit_in_hand(&growth, helper, shortest_path(c, worker_node(c, helper), node));
        int value = seed_turns + get_back + new_growth;
        if (value < best) best = value;
    }
    return best == INT_MAX ? 300 : best;
}
static int turns_till_chopper(Controller *c) {
    Path mining = shortest_path_to_mining(c);
    return -min_int(c->mine_inv.plum - 5, 0) * 5 -
        min_int(c->mine_inv.lemon - 17, 0) * 5 -
        min_int(c->mine_inv.apple - 1, 0) * 5 -
        min_int(c->mine_inv.iron - 10, 0) * mining.len * 2;
}
static double likelihood_opp_scales(Controller *c) {
    int lemon_distance = 99;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        if (tree->type != LEMON) continue;
        int distance = shortest_path_size(c, c->opp_camp, tree_node(c, tree)) - 2;
        if (distance < lemon_distance) lemon_distance = distance;
    }
    return c->turn > 16 && lemon_distance > 5 ? 0.0 : 1.0;
}
static bool no_way_to_scale_to_chopper(Controller *c) {
    if (c->chopper < 0 && c->mine_inv.lemon < 4 && turns_till_own_lemon_tree(c) + c->turn > 50) return true;
    int grown_count = 0;
    for (int i = 0; i < c->tree_count; i++) if (tree_grown(&c->trees[i])) grown_count++;
    return grown_count <= 4 && c->turn > 80;
}
static void sort_worker_indexes(const Controller *c, int idx[], int count) {
    for (int i = 1; i < count; i++) {
        int value = idx[i], j = i - 1;
        while (j >= 0) {
            Worker a = c->workers[idx[j]], b = c->workers[value];
            bool after = a.move_speed < b.move_speed ||
                (a.move_speed == b.move_speed && (a.carry_capacity < b.carry_capacity ||
                (a.carry_capacity == b.carry_capacity && a.chop_power < b.chop_power)));
            if (!after) break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = value;
    }
}
static void sort_desc_double(double values[], int count) {
    for (int i = 1; i < count; i++) {
        double value = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] < value) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }
}
static int turns_to_gather(Controller *c, int type, int count) {
    if (count <= 0) return 0;
    int indexes[MAX_WORKERS], worker_count = 0;
    if (type == 3) {
        for (int i = 0; i < c->worker_count; i++) {
            if (worker_my(&c->workers[i]) && worker_can_chop(&c->workers[i])) indexes[worker_count++] = i;
        }
        sort_worker_indexes(c, indexes, worker_count);
        double yields = 0.0;
        Path mining = shortest_path_to_mining(c);
        for (int i = 0; i < worker_count; i++) {
            Worker *worker = &c->workers[indexes[i]];
            int mining_cycle = ceil_div_int(max_int(mining.len - 1, 0), worker->move_speed) * 2 + 1 + worker_mining_turns(worker);
            yields += (worker->carry_capacity / (double)mining_cycle) * pow08(i);
        }
        return yields <= EPSILON ? 300 : ceil_positive_double(count / yields);
    }
    Fruit fruit = (Fruit)type;
    for (int i = 0; i < c->worker_count; i++) {
        if (worker_my(&c->workers[i]) && worker_can_harvest(&c->workers[i])) indexes[worker_count++] = i;
    }
    sort_worker_indexes(c, indexes, worker_count);
    double total_yields = 0.0;
    int penalty = 0;
    for (int i = 0; i < worker_count; i++) {
        Worker *worker = &c->workers[indexes[i]];
        double tree_yields[MAX_TREES];
        int tree_yield_count = 0;
        for (int t = 0; t < c->tree_count; t++) {
            Tree *tree = &c->trees[t];
            if (tree->type != fruit) continue;
            int distance = shortest_path_size(c, c->my_camp, tree_node(c, tree)) - 1;
            tree_yields[tree_yield_count++] = tree_average_fruit_yield(tree, distance, worker);
        }
        sort_desc_double(tree_yields, tree_yield_count);
        if (i < tree_yield_count) {
            total_yields += tree_yields[i] * pow08(i);
        } else if (inventory_has(&c->mine_inv, fruit) && !use_shortscale(c)) {
            penalty = c->wet_near_count ? 15 : 26;
            total_yields += (c->wet_near_count ? 1.0 / 5.0 : 1.0 / 9.0) * pow08(i);
        }
    }
    return total_yields <= EPSILON ? 300 : ceil_positive_double(count / total_yields) + penalty;
}
static void init_predictions(Controller *c) {
    c->prediction_count = 0;
    c->best_prediction = -1;
    if (c->chopper >= 0) return;
    if (c->turn >= 240) return;
    int variants[8][4], variant_count = 0;
    if (use_shortscale(c)) {
        int move = inventory_best_tier(c->mine_inv.plum, 3, 1);
        int carry = inventory_best_tier(c->mine_inv.lemon, 3, 1);
        int chop = inventory_best_tier(c->mine_inv.iron, 3, 1);
        int raw[8][4] = {{move, carry, 0, chop}, {move + 1, carry, 0, chop},
            {move, carry + 1, 0, chop}, {move, carry, 0, chop + 1},
            {move + 1, carry + 1, 0, chop}, {move + 1, carry, 0, chop + 1},
            {move, carry + 1, 0, chop + 1}, {move + 1, carry + 1, 0, chop + 1}};
        for (int i = 0; i < 8; i++) if (raw[i][1] <= 3) {
            memcpy(variants[variant_count++], raw[i], sizeof(raw[i]));
        }
    } else {
        int raw[4][4] = {{2, 4, 0, 3}, {2, 4, 0, 2}, {2, 3, 0, 3}, {2, 3, 0, 2}};
        for (int i = 0; i < 4; i++) memcpy(variants[variant_count++], raw[i], sizeof(raw[i]));
    }
    Prediction fallback_prediction = {};
    bool has_fallback_prediction = false;
    for (int i = 0; i < variant_count; i++) {
        Prediction prediction = predict(c, variants[i][0], variants[i][1], variants[i][2], variants[i][3]);
        bool valid_prediction = prediction.move > 0 && prediction.carry > 0 && prediction.chop > 0;
        if (valid_prediction && (!has_fallback_prediction ||
            prediction_grand_total(&prediction) > prediction_grand_total(&fallback_prediction) ||
            (prediction_grand_total(&prediction) == prediction_grand_total(&fallback_prediction) &&
             prediction.turns < fallback_prediction.turns))) {
            fallback_prediction = prediction;
            has_fallback_prediction = true;
        }
        int cycle = prediction_cycle_length(&prediction);
        if (valid_prediction &&
            prediction.remaining_turns >= cycle * 2 && prediction_grand_total(&prediction) > 0) {
            c->predictions[c->prediction_count++] = prediction;
        }
    }
    if (c->prediction_count == 0 && has_fallback_prediction && c->turn < 120 &&
        c->opening_tree_count <= 12 && c->map_features.ready_near == 0) {
        c->predictions[c->prediction_count++] = fallback_prediction;
    }
    for (int i = 0; i < c->prediction_count; i++) {
        if (c->best_prediction < 0 ||
            prediction_grand_total(&c->predictions[i]) > prediction_grand_total(&c->predictions[c->best_prediction]) ||
            (prediction_grand_total(&c->predictions[i]) == prediction_grand_total(&c->predictions[c->best_prediction]) &&
             c->predictions[i].turns < c->predictions[c->best_prediction].turns)) {
            c->best_prediction = i;
        }
    }
}
static bool contest_value_positive(Controller *c, Worker *worker, Tree *tree, int node, int reach, int fell, Worker *opp);
static bool chop_wars(Controller *c, Worker *worker) {
    if (!worker_can_chop(worker)) return false;
    int enemy[MAX_WORKERS], enemy_count = 0;
    for (int i = 0; i < c->worker_count; i++) {
        Worker *opp = &c->workers[i];
        Tree *tree = tree_at_node(c, worker_node(c, opp));
        if (!worker_my(opp) && worker_can_chop(opp) && tree && tree_damaged(tree)) enemy[enemy_count++] = i;
    }
    if (!enemy_count) return false;
    int current = worker_node(c, worker);
    Tree *worker_tree = tree_at_node(c, current);
    if (worker_tree && c->opp_worker_at[current] >= 0 && tree_damaged(worker_tree)) {
        bool contest_followup = false;
        Worker *opp = &c->workers[c->opp_worker_at[current]];
        if (c->turn > 135 && worker->chop_power <= opp->chop_power && neighbor_has(&c->grid, c->my_camp, current) &&
            node_y(&c->grid, c->my_camp) == 0 && !tree_fruit(worker_tree)) return false;
        if (neighbor_has(&c->grid, c->my_camp, current) && worker_full(worker)) {
            go_and_drop(c, worker, current);
            return true;
        }
        if (neighbor_has(&c->grid, c->my_camp, current)) {
            if (tree_chop_turns(worker_tree, opp->chop_power) >= 3 && tree_fruit(worker_tree)) {
                go_and_harvest(c, worker, current);
                return true;
            }
        } else {
            if (worker->chop_power > opp->chop_power) go_and_chop(c, worker, current);
            else {
                int fell = tree_chop_turns(worker_tree, opp->chop_power);
                if (!contest_value_positive(c, worker, worker_tree, current, 0, fell, opp)) return false;
                go_to(c, worker, current);
                contest_followup = c->map_features.camp_manhattan == 2 ||
                    (c->map_features.camp_manhattan >= 6 && c->map_features.camp_manhattan <= 9);
            }
            if (plan_exists(c, worker->id) && !contest_followup) return true;
        }
    }
    int best = -1, best_tree_size = INT_MIN;
    for (int i = 0; i < enemy_count; i++) {
        Worker *opp = &c->workers[enemy[i]];
        int node = worker_node(c, opp);
        Tree *tree = tree_at_node(c, node);
        if (node == current && c->turn > 135 && node_y(&c->grid, c->my_camp) == 0) continue;
        Path path = shortest_path(c, current, node);
        int reach = ceil_div_int(max_int(path.len - 1, 0), worker->move_speed) + turns_to_drop(c, worker) * 2;
        int fell = tree_chop_turns(tree, opp->chop_power);
        bool intercept = c->tree_count > 2 ? (fell < 5 && reach <= 3 && reach + 1 <= fell) :
            (fell < 6 && reach <= 5 && reach + 1 <= fell);
        if (intercept && !contest_value_positive(c, worker, tree, node, reach, fell, opp)) intercept = false;
        if (intercept && tree->size > best_tree_size) {
            best = node;
            best_tree_size = tree->size;
        }
    }
    if (best < 0) return false;
    if (worker_full(worker)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
    } else {
        go_and_chop(c, worker, best);
    }
    return plan_exists(c, worker->id);
}
static bool harvest_closest_harvestable(Controller *c, Worker *worker, int except_node) {
    if (worker_free_capacity(worker) <= 0 || worker->harvest_power <= 0) return false;
    int nearest[10], nearest_count = 0, dist[10];
    int current = worker_node(c, worker);
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if (node == except_node || node == c->seed_node || tree->size <= 1 || plan_chops_tree(c, c->chopper, node)) continue;
        if (chop_reservation_blocks_harvest(c, node, worker)) continue;
        if (my_worker_on_node_except(c, node, worker->id)) continue;
        int value = manhattan(&c->grid, current, node);
        int pos = nearest_count;
        if (pos > 9) pos = 9;
        while (pos > 0 && dist[pos - 1] > value) {
            if (pos < 10) {
                nearest[pos] = nearest[pos - 1];
                dist[pos] = dist[pos - 1];
            }
            pos--;
        }
        if (pos < 10) {
            nearest[pos] = i;
            dist[pos] = value;
            if (nearest_count < 10) nearest_count++;
        }
    }
    int best = -1, best_path = INT_MAX, best_period = INT_MAX;
    for (int i = 0; i < nearest_count; i++) {
        Tree *tree = &c->trees[nearest[i]];
        int node = tree_node(c, tree);
        Path path = shortest_path(c, current, node);
        int reach = ceil_div_int(max_int(path.len - 1, 0), worker->move_speed);
        if (tree_fruits_at_arrival(tree, reach) < worker_free_capacity(worker)) continue;
        if (path.len < best_path || (path.len == best_path && tree->period < best_period)) {
            best = node;
            best_path = path.len;
            best_period = tree->period;
        }
    }
    if (best < 0) return false;
    go_and_harvest(c, worker, best);
    return true;
}
static int neighbor_empty_closest_to_camp(Controller *c, int node) {
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->grid.neighbor_count[node]; i++) {
        int next = c->grid.neighbors[node][i];
        if (!node_empty_tree(c, next)) continue;
        if (my_worker_on_node_except(c, next, -1) || c->opp_worker_at[next] >= 0 || node_reserved_by_any_plan(c, next)) continue;
        int value = shortest_path_size(c, c->my_camp, next);
        if (value < score) {
            best = next;
            score = value;
        }
    }
    return best;
}
static int neighbor_tree_growest(Controller *c, int node) {
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->grid.neighbor_count[node]; i++) {
        int next = c->grid.neighbors[node][i];
        Tree *tree = tree_at_node(c, next);
        if (!tree) continue;
        int value = tree_turns_till_size(tree, 4);
        if (value < score) {
            best = next;
            score = value;
        }
    }
    return best;
}
static int neighbor_banana_soon(Controller *c, int node) {
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->grid.neighbor_count[node]; i++) {
        int next = c->grid.neighbors[node][i];
        Tree *tree = tree_at_node(c, next);
        if (!tree || tree->type != BANANA || tree_turns_till_fruit(tree) > 1) continue;
        int value = shortest_path_size(c, c->seed_node, next);
        if (value < score) {
            best = next;
            score = value;
        }
    }
    return best;
}
static int max_opp_carry(const Controller *c) {
    int best = 0;
    for (int i = 0; i < c->worker_count; i++) {
        const Worker *worker = &c->workers[i];
        if (!worker_my(worker) && worker->carry_capacity > best) best = worker->carry_capacity;
    }
    return best;
}
static int opponent_tree_target(Controller *c, Worker *worker, Fruit type, bool wet_only, int max_opp_dist) {
    int best = -1, best_opp = INT_MAX, best_me = INT_MAX;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if (tree->type != type || (wet_only && !c->wet[node])) continue;
        int opp = shortest_path_size(c, c->opp_camp, node) - 1;
        if (opp < 0 || (max_opp_dist >= 0 && opp > max_opp_dist)) continue;
        int me = shortest_path_size(c, worker_node(c, worker), node);
        if (me <= 0) continue;
        if (opp < best_opp || (opp == best_opp && me < best_me)) {
            best = node;
            best_opp = opp;
            best_me = me;
        }
    }
    return best;
}
static bool chop_opponent_tree(Controller *c, Worker *worker, Fruit type, bool wet_only, int max_opp_dist) {
    if (!worker_can_chop(worker)) return false;
    int node = opponent_tree_target(c, worker, type, wet_only, max_opp_dist);
    if (node < 0) return false;
    go_and_chop(c, worker, node);
    return true;
}
[[maybe_unused]] static bool opportunistic_harvest(Controller *c, Worker *worker, int except_node, double min_rate, int min_points) {
    if (!worker_can_harvest(worker) || worker_free_capacity(worker) <= 0) return false;
    int best_node = -1;
    TaskScore best = make_task_score(0, 99, 99.0);
    for (int i = 0; i < c->tree_count; i++) {
        int node = tree_node(c, &c->trees[i]);
        if (node == except_node || node == c->seed_node) continue;
        if (plan_chops_tree(c, c->chopper, node)) continue;
        if (my_worker_on_node_except(c, node, worker->id)) continue;
        TaskScore score = score_harvest_task(c, worker, node);
        if (score.net_points < min_points || score.points_per_turn < min_rate) continue;
        if (score.points_per_turn > best.points_per_turn ||
            (score.points_per_turn == best.points_per_turn && score.net_points > best.net_points)) {
            best = score;
            best_node = node;
        }
    }
    if (best_node < 0) return false;
    go_and_harvest(c, worker, best_node);
    return plan_exists(c, worker->id);
}
static TaskScore best_cashout_task(Controller *c, Worker *worker, int *node_out, PlanName *name_out) {
    TaskScore best = make_task_score(0, 99, 99.0);
    *node_out = -1;
    *name_out = PLAN_NONE;
    int limit = c->turn >= 240 ? 30 : 20;
    for (int i = 0; i < c->tree_count; i++) {
        int node = tree_node(c, &c->trees[i]);
        if (my_worker_on_node_except(c, node, worker->id)) continue;
        TaskScore s = score_harvest_task(c, worker, node);
        if (s.net_points > 0 && s.turns <= limit &&
            (s.points_per_turn > best.points_per_turn || (s.points_per_turn == best.points_per_turn && s.net_points > best.net_points))) {
            best = s;
            *node_out = node;
            *name_out = HARVEST;
        }
    }
    if (worker_can_chop(worker)) {
        for (int i = 0; i < c->tree_count; i++) {
            int node = tree_node(c, &c->trees[i]);
            if (my_worker_on_node_except(c, node, worker->id)) continue;
            TaskScore s = score_chop_task(c, worker, node);
            if (s.net_points >= 4 && s.turns <= limit + 4 && s.risk_penalty <= 3.0 &&
                (s.points_per_turn > best.points_per_turn + 0.05 || s.net_points >= best.net_points + 4)) {
                best = s;
                *node_out = node;
                *name_out = CHOP;
            }
        }
    }
    return best;
}
static bool cashout_worker(Controller *c, Worker *worker, bool force) {
    ArenaPressure a = arena_pressure(c);
    int current = worker_node(c, worker);
    int load = worker_score_load(worker);
    bool active = force || a.cashout;
    if (!active) return false;
    if (load > 0) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return true;
    }
    if (worker_full(worker)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return true;
    }
    Tree *stood = tree_at_node(c, current);
    if (stood && tree_fruit(stood) && worker_can_harvest(worker) && !chop_reservation_blocks_harvest(c, current, worker)) {
        go_and_harvest(c, worker, current);
        return true;
    }
    int node = -1;
    PlanName name = PLAN_NONE;
    TaskScore best = best_cashout_task(c, worker, &node, &name);
    if (best.net_points <= 0 || best.points_per_turn < 0.18) return false;
    if (name == HARVEST) go_and_harvest(c, worker, node);
    else if (name == CHOP) go_and_chop(c, worker, node);
    return plan_exists(c, worker->id);
}
static bool contest_value_positive(Controller *c, Worker *worker, Tree *tree, int node, int reach, int fell, Worker *opp) {
    ArenaPressure a = arena_pressure(c);
    if (!a.cashout) return true;
    if (!tree || reach > fell) return false;
    if (a.cashout && worker->chop_power <= opp->chop_power && !tree_fruit(tree)) return false;
    if (tree_fruit(tree) && worker_can_harvest(worker) && worker_free_capacity(worker) > 0) {
        TaskScore h = score_harvest_task(c, worker, node);
        if (h.net_points > 0 && h.turns <= 4) return true;
    }
    TaskScore chop = score_chop_task(c, worker, node);
    int cash_node = -1;
    PlanName cash_name = PLAN_NONE;
    TaskScore cash = best_cashout_task(c, worker, &cash_node, &cash_name);
    double cash_rate = cash.net_points > 0 ? cash.points_per_turn : 0.15;
    if (a.low_score && chop.turns > 12) return false;
    return chop.net_points >= 4 && (chop.points_per_turn + 0.05 >= cash_rate || chop.net_points >= cash.net_points + 4);
}
static void organize_chopping(Controller *c, Worker *worker) {
    int current = worker_node(c, worker);
    if (cashout_worker(c, worker, arena_pressure(c).cashout)) return;
    if ((any_grown_tree(c) ? worker->carry_wood > 0 : worker_full(worker)) || (worker_full(worker) && worker->carry_iron > 0)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return;
    }
    if (chop_wars(c, worker)) return;
    if (c->inter < 0 && worker->carry_capacity > max_opp_carry(c)) {
        if (chop_opponent_tree(c, worker, LEMON, true, 3)) return;
        if (chop_opponent_tree(c, worker, LEMON, false, 3)) return;
    }
    if (c->turn > 287) {
        int best = -1, best_score = INT_MAX;
        for (int pass = 0; pass < 2 && best < 0; pass++) {
            for (int i = 0; i < c->near_count; i++) {
                int node = c->near_list[i];
                Tree *tree = tree_at_node(c, node);
                if (!tree || !tree_grown(tree) || ((pass == 0) != (tree->type == BANANA))) continue;
                int score = shortest_path_size(c, current, node);
                if (score < best_score) {
                    best = node;
                    best_score = score;
                }
            }
        }
        if (best >= 0) {
            go_and_chop(c, worker, best);
            return;
        }
    }
    Tree *seed_tree = tree_at_node(c, c->seed_node);
    if (seed_tree && seed_tree->type != BANANA) {
        go_and_chop(c, worker, c->seed_node);
        return;
    }
    int best = -1, best_score = INT_MAX;
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || tree->type != BANANA || !tree_choppable_for_full_yield(tree, worker->chop_power)) continue;
        int score = shortest_path_size(c, current, node);
        if (score < best_score) {
            best = node;
            best_score = score;
        }
    }
    if (best >= 0) {
        go_and_chop(c, worker, best);
        return;
    }
    best = -1;
    best_score = INT_MAX;
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || !tree_grown(tree) || !neighbor_has(&c->grid, c->seed_node, node)) continue;
        int score = shortest_path_size(c, current, node);
        if (score < best_score) {
            best = node;
            best_score = score;
        }
    }
    if (best >= 0) {
        go_and_chop(c, worker, best);
        return;
    }
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || !tree_grown(tree)) continue;
        int score = shortest_path_size(c, current, node);
        if (score < best_score) {
            best = node;
            best_score = score;
        }
    }
    if (best >= 0) {
        go_and_chop(c, worker, best);
        return;
    }
    best = -1;
    best_score = INT_MAX;
    for (int i = 0; i < c->near_except_seed_count; i++) {
        int node = c->near_except_seed_list[i];
        Tree *tree = tree_at_node(c, node);
        if (!tree || tree_turns_till_size(tree, 4) > 2) continue;
        int score = tree_turns_till_size(tree, 4);
        if (score < best_score) {
            best = node;
            best_score = score;
        }
    }
    if (best >= 0) {
        if (current != best) go_to(c, worker, best);
        return;
    }
    best = tree_best_path_metric(c, worker, true, c->seed_node);
    if (best >= 0) {
        go_and_chop(c, worker, best);
        return;
    }
    best = tree_best_path_metric(c, worker, false, -1);
    if (best >= 0) {
        go_and_chop(c, worker, best);
        return;
    }
    go_and_chop(c, worker, closest_by_path(c, current, c->opp_dropoff, c->opp_dropoff_count));
}
static int first_tree_chop_order(Controller *c, Worker *worker, Fruit type, int skip_node) {
    int best = -1, score = INT_MAX;
    for (int i = 0; i < c->tree_count; i++) {
        Tree *tree = &c->trees[i];
        int node = tree_node(c, tree);
        if (tree->type != type || node == skip_node) continue;
        int value = tree_turns_till_chop(tree, worker, shortest_path(c, worker_node(c, worker), node));
        if (value < score) {
            best = node;
            score = value;
        }
    }
    return best;
}
static bool training_is_zero_harvest(const Controller *c);
static void scaling_helper(Controller *c, Worker *worker, bool regular) {
    Prediction *best = c->best_prediction >= 0 ? &c->predictions[c->best_prediction] : NULL;
    if (!best) return;
    bool need_lemon = c->mine_inv.lemon < best->costs[LEMON];
    bool need_plum = c->mine_inv.plum < best->costs[PLUM];
    bool need_apple = c->mine_inv.apple < best->costs[APPLE];
    bool need_iron = !arena_pressure(c).no_train && c->mine_inv.iron < best->costs[3];
    if (regular && seek_to_plant_carried_banana(c, worker)) return;
    if (regular && c->turn < 40 && likelihood_opp_scales(c) > 0.5 && need_lemon && ensure_sufficient_growth(c, worker, LEMON)) return;
    if (regular && c->turn < 40 && likelihood_opp_scales(c) > 0.5 && need_plum && ensure_sufficient_growth(c, worker, PLUM)) return;
    if (harvest_already_stood_on_tree(c, worker, need_lemon, need_plum)) return;
    if (need_lemon && gather_initial_fruit(c, worker, LEMON, 5)) return;
    if (need_plum && gather_initial_fruit(c, worker, PLUM, 5)) return;
    if (need_apple && gather_initial_fruit(c, worker, APPLE, 2)) return;
    if (need_iron && c->inter < 0 && gather_iron(c, worker)) return;
    if (!need_lemon && !need_plum && need_iron && gather_iron(c, worker)) return;
    if (c->inter >= 0 && turns_till_chopper(c) < 15 && seek_to_plant_banana(c, worker)) return;
    if (need_lemon && gather_initial_fruit(c, worker, LEMON, 10)) return;
    if (need_plum && gather_initial_fruit(c, worker, PLUM, 10)) return;
    if (need_apple && gather_anywhere_fruit(c, worker, APPLE, 10)) return;
    if (need_lemon && gather_anywhere_fruit(c, worker, LEMON, 10)) return;
    if (need_plum && gather_anywhere_fruit(c, worker, PLUM, 10)) return;
    if (need_iron && gather_iron(c, worker)) return;
    if (regular) harvest_closest_harvestable(c, worker, -1);
}
static void organize_helper(Controller *c, Worker *worker) {
    if (!worker) return;
    int current = worker_node(c, worker);
    if (cashout_worker(c, worker, false)) return;
    if (worker->carry_wood > 0 || (worker_full(worker) && worker->carry_iron > 0)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return;
    }
    if (chop_wars(c, worker)) return;
    if (inventory_score(&c->mine_inv) > inventory_score(&c->opp_inv) + 40 && c->tree_count < 6) {
        int tree = tree_best_path_metric(c, worker, false, -1);
        if (tree >= 0) {
            seek_to_chop(c, worker, tree);
        }
        if (plan_exists(c, worker->id)) return;
    }
    if (use_shortscale(c) && c->chopper < 0 && c->best_prediction >= 0) {
        scaling_helper(c, worker, false);
        if (plan_exists(c, worker->id)) return;
    }
    if (c->chopper < 0 && c->best_prediction >= 0 && !training_is_zero_harvest(c)) {
        scaling_helper(c, worker, true);
        if (plan_exists(c, worker->id)) return;
    }
    if (!any_grown_tree(c)) {
        seek_to_self_plant(c, worker);
        if (plan_exists(c, worker->id)) return;
        if (chase_opp_corner(c, worker)) return;
    }
    if (worker_full(worker) && worker->carry_banana == 0) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return;
    }
    if (node_in_plan_values(c, current)) {
        if (worker_full(worker) && worker->carry_banana > 0) {
            int plantable = neighbor_empty_closest_to_camp(c, current);
            if (plantable >= 0) {
                go_and_plant(c, worker, plantable, BANANA);
                return;
            }
            int nearby_tree = neighbor_tree_growest(c, current);
            if (nearby_tree >= 0) {
                go_and_chop(c, worker, nearby_tree);
                return;
            }
        }
        int banana = neighbor_banana_soon(c, current);
        if (banana >= 0) {
            go_and_harvest(c, worker, banana);
            return;
        }
        int nearby_tree = neighbor_tree_growest(c, current);
        if (nearby_tree >= 0) {
            go_and_chop(c, worker, nearby_tree);
            return;
        }
        int nearby_empty = neighbor_empty_closest_to_camp(c, current);
        if (nearby_empty >= 0) {
            go_and_chop(c, worker, nearby_empty);
            return;
        }
    }
    seek_to_plant_banana(c, worker);
}
static void organize_intermediate_scaling(Controller *c, Worker *worker) {
    Prediction *best = c->best_prediction >= 0 ? &c->predictions[c->best_prediction] : NULL;
    if (!best) return;
    bool need_iron = !arena_pressure(c).no_train && c->mine_inv.iron < best->costs[3];
    if (c->mine_inv.lemon == 0 && trees_within_camp_count(c, LEMON, true) == 0 && gather_and_plant(c, worker, LEMON)) return;
    if (c->mine_inv.plum == 0 && trees_within_camp_count(c, PLUM, true) == 0 && gather_and_plant(c, worker, PLUM)) return;
    if (c->mine_inv.lemon < 2 && gather_initial_fruit(c, worker, LEMON, 1)) return;
    if (c->mine_inv.plum < 2 && gather_initial_fruit(c, worker, PLUM, 1)) return;
    if (c->mine_inv.lemon < 2 && gather_initial_fruit(c, worker, LEMON, 2)) return;
    if (c->mine_inv.plum < 2 && gather_initial_fruit(c, worker, PLUM, 2)) return;
    if (c->mine_inv.lemon < 6 && gather_initial_fruit(c, worker, LEMON, 1)) return;
    if (c->mine_inv.plum < 6 && gather_initial_fruit(c, worker, PLUM, 1)) return;
    if (c->mine_inv.lemon < 6 && gather_initial_fruit(c, worker, LEMON, 2)) return;
    if (c->mine_inv.plum < 6 && gather_initial_fruit(c, worker, PLUM, 2)) return;
    if (c->mine_inv.lemon < best->costs[LEMON] && gather_initial_fruit(c, worker, LEMON, 1)) return;
    if (c->mine_inv.plum < best->costs[PLUM] && gather_initial_fruit(c, worker, PLUM, 1)) return;
    if (c->mine_inv.lemon < best->costs[LEMON] && gather_anywhere_fruit(c, worker, LEMON, 2)) return;
    if (c->mine_inv.plum < best->costs[PLUM] && gather_anywhere_fruit(c, worker, PLUM, 2)) return;
    if (c->mine_inv.apple < best->costs[APPLE] && gather_anywhere_fruit(c, worker, APPLE, 30)) return;
    if (need_iron) gather_iron(c, worker);
}
static void organize_intermediate(Controller *c, Worker *worker) {
    if (!worker) return;
    int current = worker_node(c, worker);
    if (cashout_worker(c, worker, false)) return;
    if (node_in_plan_values(c, current)) {
        if (worker_full(worker) && neighbor_has(&c->grid, c->my_camp, current)) {
            int alternate[MAX_NODES], alternate_count = 0;
            for (int i = 0; i < c->dropoff_count; i++) {
                if (c->dropoff[i] != current) alternate[alternate_count++] = c->dropoff[i];
            }
            int alternate_dropoff = closest_by_path(c, current, alternate, alternate_count);
            if (alternate_dropoff < 0) {
                if (c->grid.neighbor_count[current]) go_to(c, worker, c->grid.neighbors[current][0]);
            } else {
                go_and_drop(c, worker, alternate_dropoff);
            }
            return;
        }
        Tree *stood = tree_at_node(c, current);
        if (!worker_full(worker) && stood && tree_fruit(stood)) {
            harvest_closest_harvestable(c, worker, current);
            return;
        }
    }
    if (worker->carry_wood > 0 || (worker_full(worker) && worker->carry_iron > 0)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return;
    }
    if (c->chopper < 0 && chop_wars(c, worker)) return;
    if (my_worker_count(c) > opp_worker_count(c)) {
        if (chop_opponent_tree(c, worker, LEMON, true, 3)) return;
        if (chop_opponent_tree(c, worker, LEMON, false, 3)) return;
    }
    if (inventory_score(&c->mine_inv) > inventory_score(&c->opp_inv) + 40 && c->tree_count < 6) {
        int tree = tree_best_path_metric(c, worker, false, -1);
        if (tree >= 0) seek_to_chop(c, worker, tree);
        if (plan_exists(c, worker->id)) return;
    }
    if (no_way_to_scale_to_chopper(c)) {
        Worker *helper = worker_ptr(c, c->helper);
        int skip = -1;
        if (helper) {
            Tree *tree = tree_at_node(c, worker_node(c, helper));
            if (tree) skip = tree_node(c, tree);
        }
        Fruit order[4] = {BANANA, PLUM, LEMON, APPLE};
        for (int i = 0; i < 4; i++) {
            int node = first_tree_chop_order(c, worker, order[i], skip);
            if (node >= 0) {
                seek_to_chop(c, worker, node);
                return;
            }
        }
    }
    if (c->chopper < 0 && c->best_prediction >= 0 && !training_is_zero_harvest(c)) {
        organize_intermediate_scaling(c, worker);
        if (plan_exists(c, worker->id)) return;
    }
    if (worker_full(worker)) {
        go_and_drop(c, worker, closest_dropoff(c, current));
        return;
    }
    if (chop_opponent_tree(c, worker, LEMON, false, -1)) return;
    if (chop_opponent_tree(c, worker, BANANA, false, 2)) return;
    if (chop_opponent_tree(c, worker, PLUM, false, -1)) return;
    if (opportunistic_harvest(c, worker, -1, c->turn >= 220 ? 0.12 : 0.18, 1)) return;
    if (harvest_closest_harvestable(c, worker, -1)) return;
    if (!any_grown_tree(c)) {
        seek_to_self_plant(c, worker);
        if (plan_exists(c, worker->id)) return;
        if (chase_opp_corner(c, worker)) return;
    }
}
static bool training_is_zero_harvest(const Controller *c) {
    int move = 0, carry = 0, harvest = -1, chop = 0;
    return sscanf(c->training, "TRAIN %d %d %d %d", &move, &carry, &harvest, &chop) == 4 && harvest == 0;
}
static void classify_my_workers(Controller *c) {
    int indexes[MAX_WORKERS], count = 0;
    c->helper = c->inter = c->chopper = -1;
    for (int i = 0; i < c->worker_count; i++) if (worker_my(&c->workers[i])) indexes[count++] = i;
    for (int i = 1; i < count; i++) {
        int value = indexes[i], j = i - 1;
        while (j >= 0 && c->workers[indexes[j]].id > c->workers[value].id) {
            indexes[j + 1] = indexes[j];
            j--;
        }
        indexes[j + 1] = value;
    }
    if (count == 3) {
        c->helper = indexes[0];
        c->inter = indexes[1];
        c->chopper = indexes[2];
        return;
    }
    for (int i = 0; i < count; i++) {
        Worker *worker = &c->workers[indexes[i]];
        if (i == 0) c->helper = indexes[i];
        else if ((use_shortscale(c) && worker->id > 1) ||
            (worker->harvest_power == 0 && worker->carry_capacity >= 2 && worker->move_speed >= 2 && worker->chop_power >= 2)) {
            c->chopper = indexes[i];
        } else {
            c->inter = indexes[i];
        }
    }
}
static bool read_inventory(Inventory *inv) {
    return scanf("%d %d %d %d %d %d", &inv->plum, &inv->lemon, &inv->apple, &inv->banana, &inv->iron, &inv->wood) == 6;
}
static bool controller_read_turn(Controller *c) {
    int tree_count = 0, worker_count = 0;
    if (scanf("%d", &c->mine_inv.plum) != 1) return false;
    if (scanf("%d %d %d %d %d", &c->mine_inv.lemon, &c->mine_inv.apple, &c->mine_inv.banana, &c->mine_inv.iron, &c->mine_inv.wood) != 5) return false;
    if (!read_inventory(&c->opp_inv)) return false;
    if (scanf("%d", &tree_count) != 1) return false;
    c->tree_count = 0;
    c->worker_count = 0;
    c->helper = c->inter = c->chopper = -1;
    for (int i = 0; i < c->grid.count; i++) c->tree_at[i] = c->worker_at[i] = c->opp_worker_at[i] = -1;
    for (int i = 0; i < tree_count; i++) {
        char type_name[24];
        Tree tree;
        if (scanf("%23s %d %d %d %d %d %d", type_name, &tree.x, &tree.y, &tree.size, &tree.health, &tree.fruits, &tree.cooldown) != 7) return false;
        tree.type = fruit_from_name(type_name);
        int node = node_of(&c->grid, tree.x, tree.y);
        tree.period = tree_period(tree.type, c->wet[node]);
        c->trees[c->tree_count] = tree;
        c->tree_at[node] = c->tree_count++;
    }
    if (scanf("%d", &worker_count) != 1) return false;
    for (int i = 0; i < worker_count; i++) {
        Worker worker;
        if (scanf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d",
            &worker.id, &worker.player, &worker.x, &worker.y,
            &worker.move_speed, &worker.carry_capacity, &worker.harvest_power, &worker.chop_power,
            &worker.carry_plum, &worker.carry_lemon, &worker.carry_apple, &worker.carry_banana,
            &worker.carry_iron, &worker.carry_wood) != 14) return false;
        int index = c->worker_count++;
        c->workers[index] = worker;
        int node = worker_node(c, &c->workers[index]);
        if (worker_my(&c->workers[index])) {
            c->worker_at[node] = index;
        } else {
            c->opp_worker_at[node] = index;
        }
    }
    classify_my_workers(c);
    return true;
}
static void reset_turn_output(Controller *c) {
    memset(c->plans, 0, sizeof(c->plans));
    c->plan_order = 0;
    c->reserved_chop_count = 0;
    c->training[0] = '\0';
}
static void set_chopper_training(Controller *c, const Prediction *prediction) {
    snprintf(c->training, sizeof(c->training), "TRAIN %d %d %d %d",
        prediction->move, prediction->carry, prediction->harvest, prediction->chop);
}
static bool poor_long_distance_opening(const Controller *c) {
    return c->map_features.poor_long_distance_opening;
}
static bool set_light_chopper_fallback(Controller *c) {
#if ENABLE_LIGHT_CHOPPER_FALLBACK
    if (!poor_long_distance_opening(c)) return false;
    int costs[4];
    worker_cost(c, 2, 3, 0, 2, costs);
    if (!can_afford(c, costs)) return false;
    snprintf(c->training, sizeof(c->training), "TRAIN 2 3 0 2");
    return true;
#else
    (void)c;
    return false;
#endif
}
static void controller_call(Controller *c) {
    reset_turn_output(c);
    update_map_features(c);
    if (c->turn == 1) c->opening_tree_count = c->tree_count;
    init_predictions(c);
    bool can_train_prediction = c->best_prediction >= 0 &&
        !arena_pressure(c).no_train &&
        can_afford(c, c->predictions[c->best_prediction].costs);
    if (can_train_prediction && c->turn > 100) {
        Prediction *p = &c->predictions[c->best_prediction];
        can_train_prediction = p->turns <= 25 && prediction_chop_cycles(p) >= 3 && prediction_grand_total(p) > 0;
    }
    if (c->turn <= 1 && !can_train_prediction && set_light_chopper_fallback(c)) {
        
    } else if (c->turn <= 1 && !use_shortscale(c) && turns_till_chopper(c) > 65) {
        char worker[32];
        if (best_intermediate_worker(c, worker)) snprintf(c->training, sizeof(c->training), "TRAIN %s", worker);
    }
    if (!c->training[0] && c->chopper < 0 && can_train_prediction) {
        set_chopper_training(c, &c->predictions[c->best_prediction]);
    }
    Worker *chopper = worker_ptr(c, c->chopper);
    Worker *helper = worker_ptr(c, c->helper);
    Worker *inter = worker_ptr(c, c->inter);
    if (chopper && !plan_exists(c, chopper->id)) organize_chopping(c, chopper);
    if (helper && !plan_exists(c, helper->id)) organize_helper(c, helper);
    if (inter && !plan_exists(c, inter->id)) organize_intermediate(c, inter);
}
static const char *plan_name(PlanName name) {
    if (name == MOVE) return "MOVE";
    if (name == HARVEST) return "HARVEST";
    if (name == PLANT) return "PLANT";
    if (name == CHOP) return "CHOP";
    if (name == PICK) return "PICK";
    if (name == DROP) return "DROP";
    if (name == MINE) return "MINE";
    return "WAIT";
}
static void append_text(char *out, size_t size, bool *has_part, const char *text) {
    size_t used = strlen(out);
    snprintf(out + used, size > used ? size - used : 0, "%s%s", *has_part ? "; " : "", text);
    *has_part = true;
}
static void plan_command(const Controller *c, const Plan *plan, char out[128]) {
    if (plan->name == MOVE) {
        snprintf(out, 128, "MOVE %d %d %d", plan->worker_id, node_x(&c->grid, plan->node), node_y(&c->grid, plan->node));
    } else if (plan->name == PICK || plan->name == PLANT) {
        snprintf(out, 128, "%s %d %s", plan_name(plan->name), plan->worker_id, FRUIT_NAME[plan->type]);
    } else {
        snprintf(out, 128, "%s %d", plan_name(plan->name), plan->worker_id);
    }
}
static void controller_print_result(Controller *c) {
    char result[COMMAND_SIZE] = "";
    bool has_part = false;
    if (c->training[0]) append_text(result, sizeof(result), &has_part, c->training);
    int order[MAX_WORKER_ID], count = 0;
    for (int i = 0; i < MAX_WORKER_ID; i++) if (c->plans[i].present) order[count++] = i;
    for (int i = 1; i < count; i++) {
        int value = order[i], j = i - 1;
        while (j >= 0 && (c->plans[order[j]].weight < c->plans[value].weight ||
            (c->plans[order[j]].weight == c->plans[value].weight && c->plans[order[j]].order > c->plans[value].order))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = value;
    }
    for (int i = 0; i < count; i++) {
        char command[128];
        plan_command(c, &c->plans[order[i]], command);
        append_text(result, sizeof(result), &has_part, command);
    }
    puts(has_part ? result : "WAIT");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int width = 0, height = 0;
    char field[MAX_H][MAX_W + 4];
    if (scanf("%d %d", &width, &height) != 2) return 0;
    for (int y = 0; y < height; y++) {
        if (scanf("%67s", field[y]) != 1) return 0;
    }
    Controller controller;
    controller_init(&controller, width, height, field);
    for (controller.turn = 1; controller_read_turn(&controller); controller.turn++) {
        controller_call(&controller);
        controller_print_result(&controller);
    }
    return 0;
}
