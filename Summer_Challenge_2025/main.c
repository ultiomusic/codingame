#undef _GLIBCXX_DEBUG                // disable run-time bound checking, etc
#pragma GCC optimize("Ofast,inline") // Ofast = O3,fast-math,allow-store-data-races,no-protect-parens

#pragma GCC target("bmi,bmi2,lzcnt,popcnt")                      // bit manipulation
#pragma GCC target("movbe")                                      // byte swap
#pragma GCC target("aes,pclmul,rdrnd")                           // encryption
#pragma GCC target("avx,avx2,f16c,fma,sse2,sse3,ssse3,sse4.1,sse4.2") // SIMD
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

#define MAX_WIDTH  20
#define MAX_HEIGHT 20
#define MAX_AGENTS 10
#define MAX_PLAYERS 2
#define MAX_MOVES_PER_AGENT 5
#define MAX_SHOOTS_PER_AGENT 5
#define MAX_BOMB_PER_AGENT 5
#define MAX_COMMANDS_PER_AGENT 55
#define MAX_COMMANDS_PLAYER_ME     512
#define MAX_COMMANDS_PLAYER_ENEMIE 512
#define MAX_COMMANDS (MAX_COMMANDS_PLAYER_ME + MAX_COMMANDS_PLAYER_ENEMIE)
#define MAX_SIMULATIONS 4096

typedef enum {
    CMD_SHOOT,
    CMD_THROW,
    CMD_HUNKER
} ActionType;
typedef struct {
    int target_x_or_id, target_y;
    float score;
} AgentAction;
typedef struct {
    int mv_x, mv_y;
    ActionType action_type;
    int target_x_or_id, target_y;
    float score;
} AgentCommand;

typedef struct {
    float score;
    int my_cmds_index;
    int op_cmds_index;
} SimulationResult;

typedef struct {
    int bfs_enemy_distances[MAX_AGENTS][MAX_HEIGHT][MAX_WIDTH];
    AgentAction moves[MAX_AGENTS][MAX_MOVES_PER_AGENT];
    int move_counts[MAX_AGENTS];
    AgentAction shoots[MAX_AGENTS][MAX_SHOOTS_PER_AGENT];
    int shoot_counts[MAX_AGENTS];
    AgentAction bombs[MAX_AGENTS][MAX_BOMB_PER_AGENT];
    int bomb_counts[MAX_AGENTS];
    AgentCommand agent_commands[MAX_AGENTS][MAX_COMMANDS_PER_AGENT];
    int agent_command_counts[MAX_AGENTS];
    AgentCommand player_commands[MAX_PLAYERS][MAX_COMMANDS][MAX_AGENTS];
    int player_command_count[MAX_PLAYERS];
    SimulationResult simulation_results[MAX_SIMULATIONS];
    int simulation_count;
} GameOutput;

typedef struct {
    int id;
    int player_id;
    int shoot_cooldown;
    int optimal_range;
    int soaking_power;
    int splash_bombs;
} AgentInfo;
typedef struct {
    int agent_count;
    int agent_start_index;
    int agent_stop_index;
} PlayerAgentInfo;

typedef struct {
    int x, y;
    int type;
} Tile;

typedef struct {
    int id;
    int x, y;
    int cooldown;
    int splash_bombs;
    int wetness;
    int alive;
} AgentState;

typedef struct {
    int width, height;
    Tile map[MAX_HEIGHT][MAX_WIDTH];
} MapInfo;

typedef struct {
    const int my_player_id;
    const int agent_info_count;
    AgentInfo agent_info[MAX_AGENTS];
    PlayerAgentInfo player_info[MAX_PLAYERS];
    MapInfo map;
} GameConstants;

typedef struct {
    AgentState agents[MAX_AGENTS];
    int agent_count_do_not_use;
    int my_agent_count_do_not_use;
} GameState;

typedef struct {
    GameConstants consts;
    GameState state;
    GameOutput output;
} GameInfo;

GameInfo game = {0};

static clock_t gCPUStart;
#define CPU_RESET        (gCPUStart = clock())
#define CPU_MS_USED      (((double)(clock() - gCPUStart)) * 1000.0 / CLOCKS_PER_SEC)
#define CPU_BREAK(val)   if (CPU_MS_USED > (val)) break;
#define ERROR(text) {fprintf(stderr,"ERROR:%s",text);fflush(stderr);exit(1);}
#define ERROR_INT(text,val) {fprintf(stderr,"ERROR:%s:%d",text,val);fflush(stderr);exit(1);}

void debug_stats() {
    fprintf(stderr, "\n=== STATS ===\n");

    for (int a = 0; a < MAX_AGENTS; ++a) {
        if (!game.state.agents[a].alive) continue;
        fprintf(stderr, "Agent %d - Commands: %d actions[%d]\n", a+1, game.output.agent_command_counts[a],game.output.agent_command_counts[a]-game.output.move_counts[a] );
    }

    for (int p = 0; p < MAX_PLAYERS; ++p) {
        fprintf(stderr, "Player %d - PlayerCommands: %d\n", p, game.output.player_command_count[p]);
    }

    // Simulations
    fprintf(stderr, "Simulations: %d\n", game.output.simulation_count);
    fprintf(stderr, "=============\n");
}


int controlled_score_gain_if_agent_moves_to(int agent_id, int nx, int ny) {
    int my_gain = 0;
    int enemy_gain = 0;

    for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue;

            int d_my = INT_MAX;
            int d_en = INT_MAX;

            for (int i = game.consts.player_info[game.consts.my_player_id].agent_start_index;
                 i <= game.consts.player_info[game.consts.my_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = (i == agent_id) ? nx : game.state.agents[i].x;
                int ay = (i == agent_id) ? ny : game.state.agents[i].y;
                int d = abs(x - ax) + abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_my) d_my = d;
            }

            for (int i = game.consts.player_info[!game.consts.my_player_id].agent_start_index;
                 i <= game.consts.player_info[!game.consts.my_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = game.state.agents[i].x;
                int ay = game.state.agents[i].y;
                // int ax = (i == agent_id) ? nx : game.state.agents[i].x;
                // int ay = (i == agent_id) ? ny : game.state.agents[i].y;
                int d = abs(x - ax) + abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_en) d_en = d;
            }

            if (d_my < d_en) my_gain++;
            else if (d_en < d_my) enemy_gain++;
        }
    }

    return my_gain - enemy_gain;
}

void read_game_inputs_init() {
    int my_id, agent_info_count;
    scanf("%d", &my_id);
    scanf("%d", &agent_info_count);

    *(int*)&game.consts.my_player_id = my_id;
    *(int*)&game.consts.agent_info_count = agent_info_count;

    for (int i = 0; i < agent_info_count; i++) {
        scanf("%d%d%d%d%d%d",
              &game.consts.agent_info[i].id,
              &game.consts.agent_info[i].player_id,
              &game.consts.agent_info[i].shoot_cooldown,
              &game.consts.agent_info[i].optimal_range,
              &game.consts.agent_info[i].soaking_power,
              &game.consts.agent_info[i].splash_bombs);
    }

    for (int p = 0; p < MAX_PLAYERS; ++p) {
        game.consts.player_info[p].agent_count = 0;
        game.consts.player_info[p].agent_start_index = -1;
        game.consts.player_info[p].agent_stop_index = -1;
    }

    for (int i = 0; i < game.consts.agent_info_count; ++i) {
        int player = game.consts.agent_info[i].player_id;
        if (game.consts.player_info[player].agent_count == 0) {
            game.consts.player_info[player].agent_start_index = i;
        }
        game.consts.player_info[player].agent_count++;
        game.consts.player_info[player].agent_stop_index = i;
    }
    scanf("%d%d", &game.consts.map.width, &game.consts.map.height);
    for (int i = 0; i < game.consts.map.height * game.consts.map.width; i++) {
        int x, y, tile_type;
        scanf("%d%d%d", &x, &y, &tile_type);
        game.consts.map.map[y][x] = (Tile){x, y, tile_type};
    }
}

void read_game_inputs_cycle() {    
    for (int i = 0; i < MAX_AGENTS; i++) {
        game.state.agents[i].alive = 0;
    }
    scanf("%d", &game.state.agent_count_do_not_use);
    int agent_id,agent_x,agent_y,agent_cooldown,agent_splash_bombs,agent_wetness;
    for (int i = 0; i < game.state.agent_count_do_not_use; i++) {
        scanf("%d%d%d%d%d%d",
              &agent_id,
              &agent_x,
              &agent_y,
              &agent_cooldown,
              &agent_splash_bombs,
              &agent_wetness);
        // index start at 1
        agent_id = agent_id -1;
        game.state.agents[agent_id] = (AgentState){agent_id,agent_x,agent_y,agent_cooldown,agent_splash_bombs,agent_wetness,1};
    }

    scanf("%d", &game.state.my_agent_count_do_not_use);
    CPU_RESET;
}

void precompute_bfs_distances() {
    static const int dirs[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};

    for (int k = 0; k < MAX_AGENTS; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;

        int eid = enemy->id;
        int visited[MAX_HEIGHT][MAX_WIDTH] = {0};
        int dist[MAX_HEIGHT][MAX_WIDTH] = {0};

        int queue_x[MAX_WIDTH * MAX_HEIGHT];
        int queue_y[MAX_WIDTH * MAX_HEIGHT];
        int front = 0, back = 0;

        visited[enemy->y][enemy->x] = 1;
        dist[enemy->y][enemy->x] = 0;
        queue_x[back] = enemy->x;
        queue_y[back++] = enemy->y;

        while (front < back) {
            int x = queue_x[front];
            int y = queue_y[front++];
            for (int d = 0; d < 4; d++) {
                int nx = x + dirs[d][0];
                int ny = y + dirs[d][1];
                if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height)
                    continue;
                if (game.consts.map.map[ny][nx].type > 0) continue;
                if (visited[ny][nx]) continue;

                visited[ny][nx] = 1;
                dist[ny][nx] = dist[y][x] + 1;

                queue_x[back] = nx;
                queue_y[back++] = ny;
            }
        }

        for (int y = 0; y < game.consts.map.height; y++) {
            for (int x = 0; x < game.consts.map.width; x++) {
                if (!visited[y][x])
                    game.output.bfs_enemy_distances[eid][y][x] = 9999;
                else
                    game.output.bfs_enemy_distances[eid][y][x] = dist[y][x];
            }
        }
    }
}




void compute_best_agents_moves(int agent_id) {
    static const int dirs[5][2] = {
        {0, 0},   // stay in place
        {-1, 0},  // left
        {1, 0},   // right
        {0, -1},  // up
        {0, 1}    // down
    };

    AgentState* agent_state = &game.state.agents[agent_id];
    AgentInfo* agent_info   = &game.consts.agent_info[agent_id];

    game.output.move_counts[agent_id] = 0;

    int my_player_id = agent_info->player_id;
    int enemy_player_id = !my_player_id;

    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    int ally_start = game.consts.player_info[my_player_id].agent_start_index;
    int ally_stop  = game.consts.player_info[my_player_id].agent_stop_index;

    bool danger = false;
    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;
        if (enemy->splash_bombs <= 0) continue;

        int dist = abs(enemy->x - agent_state->x) + abs(enemy->y - agent_state->y);
        if (dist <= 7) {
            danger = true;
            break;
        }
    }

    for (int d = 0; d < 5; d++) {
        int nx = agent_state->x + dirs[d][0];
        int ny = agent_state->y + dirs[d][1];

        if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) continue;
        if (game.consts.map.map[ny][nx].type > 0) continue;

        int min_dist_to_enemy = 9999;
        for (int k = enemy_start; k <= enemy_stop; k++) {
            AgentState* op_state = &game.state.agents[k];
            if (!op_state->alive) continue;

            int dist = game.output.bfs_enemy_distances[op_state->id][ny][nx];
            if (dist < min_dist_to_enemy) min_dist_to_enemy = dist;
        }

        float penalty = 0.0f;
        if (danger) {
            for (int a = ally_start; a <= ally_stop; a++) {
                if (a == agent_id) continue;
                AgentState* ally = &game.state.agents[a];
                if (!ally->alive) continue;

                int dist_ally = abs(ally->x - nx) + abs(ally->y - ny);
                if (dist_ally < 3) {
                    penalty += 20.0f;
                }
            }
        }

        int gain = controlled_score_gain_if_agent_moves_to(agent_id, nx, ny);
        //if(my_player_id != agent_info->player_id) gain = -gain;
        
        float score = (float)gain  - penalty;

        if(agent_info->optimal_range <  min_dist_to_enemy)
        {
            score -= min_dist_to_enemy*10.f;
        }

        AgentAction action = {
            .target_x_or_id = nx,
            .target_y = ny,
            .score = score
        };

        if (game.output.move_counts[agent_id] < MAX_MOVES_PER_AGENT) {
            game.output.moves[agent_id][game.output.move_counts[agent_id]++] = action;
        }
    }

    for (int m = 0; m < game.output.move_counts[agent_id] - 1; m++) {
        for (int n = m + 1; n < game.output.move_counts[agent_id]; n++) {
            if (game.output.moves[agent_id][n].score > game.output.moves[agent_id][m].score) {
                AgentAction tmp = game.output.moves[agent_id][m];
                game.output.moves[agent_id][m] = game.output.moves[agent_id][n];
                game.output.moves[agent_id][n] = tmp;
            }
        }
    }
}


void compute_best_agents_shoot(int agent_id,int new_shooter_x,int new_shooter_y) {

    AgentState* shooter_state = &game.state.agents[agent_id];
    AgentInfo* shooter_info   = &game.consts.agent_info[agent_id];
    AgentAction * output_list = &game.output.shoots[agent_id][0];

    game.output.shoot_counts[agent_id] = 0;
    if (shooter_state->cooldown > 0) return;

    int my_player_id = shooter_info->player_id;
    int enemy_player_id = !my_player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    int shoots_count = 0;

    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;
        
        int dx = abs(enemy->x - new_shooter_x);
        int dy = abs(enemy->y - new_shooter_y);
        int dist = dx + dy;

        int max_range = 2 * shooter_info->optimal_range;

        if (dist > max_range) continue;

        float optimal_bonus = (dist <= shooter_info->optimal_range)
            ? 1.5f
            : 1.0f;

        float score = enemy->wetness * optimal_bonus - dist * 2;

        AgentAction shoot = {
            .target_x_or_id = k,
            .score = score
        };
        output_list[shoots_count++] = shoot;
    }

    for (int m = 0; m < shoots_count - 1; m++) {
        for (int n = m + 1; n < shoots_count; n++) {
            if (output_list[n].score > output_list[m].score) {
                AgentAction tmp = output_list[m];
                output_list[m] = output_list[n];
                output_list[n] = tmp;
            }
        }
    }
    game.output.shoot_counts[agent_id] = shoots_count;
    
}

bool is_blocked(int x, int y,int thrower_x, int thrower_y) {
    if (x < 0 || x >= game.consts.map.width || y < 0 || y >= game.consts.map.height) return true;
    if (game.consts.map.map[y][x].type > 0) return true;
    if (x == thrower_x && y == thrower_y) return true;
    return false;
}

int count_penalties(int cx, int cy,  int thrower_x, int thrower_y) {
    int penalty = 0;
    for (int ox = -1; ox <= 1; ++ox) {
        for (int oy = -1; oy <= 1; ++oy) {
            int nx = cx + ox;
            int ny = cy + oy;
            if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) {
                penalty++;
            } else if ((game.consts.map.map[ny][nx].type > 0) || (nx == thrower_x && ny == thrower_y)) {
                penalty++;
            }
        }
    }
    return penalty;
}

bool zone_touche_allie(int cx, int cy, int player_id, AgentState agents[]) {
    for (int a = game.consts.player_info[player_id].agent_start_index;
         a <= game.consts.player_info[player_id].agent_stop_index; ++a) {
        AgentState* ally = &agents[a];
        if (!ally->alive) continue;
        if (abs(ally->x - cx) <= 1 && abs(ally->y - cy) <= 1) {
            return true;
        }
    }
    return false;
}


void compute_best_agents_bomb(int agent_id, int new_thrower_x, int new_thrower_y) {
    game.output.bomb_counts[agent_id] = 0;

    AgentState* thrower_state = &game.state.agents[agent_id];
    AgentInfo* thrower_info   = &game.consts.agent_info[agent_id];
    if (!thrower_state->alive || thrower_state->splash_bombs <= 0) return;

    int my_player_id = thrower_info->player_id;
    int enemy_player_id = !my_player_id;

    AgentState* agents = game.state.agents;

    for (int k = game.consts.player_info[enemy_player_id].agent_start_index;
         k <= game.consts.player_info[enemy_player_id].agent_stop_index; ++k) {

        AgentState* enemy = &agents[k];
        if (!enemy->alive) continue;

        int ex = enemy->x;
        int ey = enemy->y;

        int dxs[5] = {0, -1, 1, 0, 0};
        int dys[5] = {0, 0, 0, -1, 1};

        for (int d = 0; d < 5; ++d) {
            int tx = ex + dxs[d];
            int ty = ey + dys[d];

            if (d > 0) {
                int ox = ex - dxs[d];
                int oy = ey - dys[d];
                if (!is_blocked(ox, oy, new_thrower_x, new_thrower_y)) continue;
            }

            int dist = abs(tx - new_thrower_x) + abs(ty - new_thrower_y);
            if (dist > 4) continue;

            if (zone_touche_allie(tx, ty, my_player_id, agents)) continue;

            // Score
            int score = 100 - enemy->wetness - count_penalties(tx, ty, new_thrower_x, new_thrower_y);

            if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
                game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] = (AgentAction){
                    .target_x_or_id = tx,
                    .target_y = ty,
                    .score = score
                };
            }
        }
    }
    int count = game.output.bomb_counts[agent_id];
    for (int m = 0; m < count - 1; m++) {
        for (int n = m + 1; n < count; n++) {
            if (game.output.bombs[agent_id][n].score > game.output.bombs[agent_id][m].score) {
                AgentAction tmp = game.output.bombs[agent_id][m];
                game.output.bombs[agent_id][m] = game.output.bombs[agent_id][n];
                game.output.bombs[agent_id][n] = tmp;
            }
        }
    }
}


void compute_best_agents_commands() {

    static const int max_shoots_per_agent[6] = {0, 1, 2, 3, 4, 5}; // [nb_agents_vivants] => shoots max / agent
    static const int max_bombs_per_agent[6]  = {0, 1, 2, 3, 4, 5}; // max bombs/agent (ajustable)

    int my_alive_agents = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (game.state.agents[i].alive && game.consts.agent_info[i].player_id == game.consts.my_player_id) {
            my_alive_agents++;
        }
    }
    if (my_alive_agents > 5) my_alive_agents = 5;

    int shoot_limit_per_agent = max_shoots_per_agent[my_alive_agents];
    int bomb_limit_per_agent  = max_bombs_per_agent[my_alive_agents];

    for (int i = 0; i < MAX_AGENTS; i++) {

        game.output.agent_command_counts[i] = 0;        
        if (!game.state.agents[i].alive) continue;
        int cmd_index = 0;
        
        compute_best_agents_moves(i);     
        int move_count = game.output.move_counts[i];

        for (int m = 0; m < move_count && cmd_index < MAX_COMMANDS_PER_AGENT; m++) {
            AgentAction* mv = &game.output.moves[i][m];
            int mv_x = mv->target_x_or_id;
            int mv_y = mv->target_y;
            compute_best_agents_bomb(i, mv_x, mv_y);
            int bomb_count = game.output.bomb_counts[i];
            if (bomb_count > bomb_limit_per_agent) bomb_count = bomb_limit_per_agent;
            for (int b = 0; b < bomb_count && cmd_index < MAX_COMMANDS_PER_AGENT; b++) {
                AgentAction* bomb = &game.output.bombs[i][b];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_THROW,
                    .target_x_or_id = bomb->target_x_or_id,
                    .target_y = bomb->target_y,
                    .score = mv->score
                };
            }
            compute_best_agents_shoot(i, mv_x, mv_y);
            int shoot_count = game.output.shoot_counts[i];
            if (shoot_count > shoot_limit_per_agent) shoot_count = shoot_limit_per_agent;
            for (int s = 0; s < shoot_count && cmd_index < MAX_COMMANDS_PER_AGENT; s++) {
                AgentAction* shoot = &game.output.shoots[i][s];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_SHOOT,
                    .target_x_or_id = shoot->target_x_or_id,
                    .target_y = shoot->target_y,
                    .score = mv->score
                };
            }
            if (cmd_index < MAX_COMMANDS_PER_AGENT) {
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_HUNKER,
                    .target_x_or_id = -1,
                    .target_y = -1,
                    .score = mv->score
                };
            }
        }

        game.output.agent_command_counts[i] = cmd_index;
    }
}
int check_mv_collision(int my_player_id, int cmds_index, int agent_start_id, int agent_stop_id) {
    for (int a1 = agent_start_id; a1 <= agent_stop_id; a1++) {
        if (!game.state.agents[a1].alive) continue;
        AgentCommand *cmd1 = &game.output.player_commands[my_player_id][cmds_index][a1];
        int from1_x = game.state.agents[a1].x;
        int from1_y = game.state.agents[a1].y;
        int to1_x = cmd1->mv_x;
        int to1_y = cmd1->mv_y;

        for (int a2 = a1 + 1; a2 <= agent_stop_id; a2++) {
            if (!game.state.agents[a2].alive) continue;
            AgentCommand *cmd2 = &game.output.player_commands[my_player_id][cmds_index][a2];
            int from2_x = game.state.agents[a2].x;
            int from2_y = game.state.agents[a2].y;
            int to2_x = cmd2->mv_x;
            int to2_y = cmd2->mv_y;

            if (to1_x == to2_x && to1_y == to2_y) {
                // fprintf(stderr,"collision a:%d-a:%d x:%d y:%d\n",a1,a2,to1_x,to1_y);
                return 1;
            }
            if (to1_x == from2_x && to1_y == from2_y && to2_x == from1_x && to2_y == from1_y) {
                
                // fprintf(stderr,"swap a:%d-a:%d x:%d y:%d\n",a1,a2,to1_x,to1_y);
                return 1;
            }
        }
    }
    return 0;
}


void compute_best_player_commands() {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        game.output.player_command_count[p] = 0;

        int max_total_cmds = (p == game.consts.my_player_id) ? MAX_COMMANDS_PLAYER_ME : MAX_COMMANDS_PLAYER_ENEMIE;
        int agent_start_id = game.consts.player_info[p].agent_start_index;
        int agent_stop_id = game.consts.player_info[p].agent_stop_index;
        int max_cmds[MAX_AGENTS] = {0};
        int total = 1;

        for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
            if (!game.state.agents[agent_id].alive) continue;
            max_cmds[agent_id] = 1;
        }

        bool updated = true;
        while (updated) {
            updated = false;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;

                int current = max_cmds[agent_id];
                int available = game.output.agent_command_counts[agent_id];

                if (current < available) {
                    int new_total = total / current * (current + 1);
                    if (new_total <= max_total_cmds) {
                        max_cmds[agent_id]++;
                        total = new_total;
                        updated = true;
                    }
                }
            }
        }
        int indices[MAX_AGENTS] = {0};

        while (true) {
            if (game.output.player_command_count[p] >= max_total_cmds) ERROR_INT("ERROR to many command",max_total_cmds)

            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;
                int cmd_id = indices[agent_id];
                game.output.player_commands[p][game.output.player_command_count[p]][agent_id] =
                    game.output.agent_commands[agent_id][cmd_id];
            }
            bool collision = check_mv_collision(p,game.output.player_command_count[p],agent_start_id,agent_stop_id);

            if(!collision) game.output.player_command_count[p]++;

            // game.output.player_command_count[p]++;
            int carry = 1;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id && carry; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;

                indices[agent_id]++;
                if (indices[agent_id] >= max_cmds[agent_id]) {
                    indices[agent_id] = 0;
                    carry = 1;
                } else {
                    carry = 0;
                }
            }

            if (carry) break;
        }
    }
}
typedef struct {
    AgentState sim_agents[MAX_AGENTS];
    int wetness_gain;
    int nb_50_wet_gain;
    int nb_100_wet_gain;
    int control_score;
} SimulationContext;

void simulate_players_commands(int my_cmd_index, int en_cmd_index, SimulationContext* ctx) {
    int my_id = game.consts.my_player_id;
    int en_id = !my_id;
    int my_start = game.consts.player_info[my_id].agent_start_index;
    int my_stop  = game.consts.player_info[my_id].agent_stop_index;
    int en_start = game.consts.player_info[en_id].agent_start_index;
    int en_stop  = game.consts.player_info[en_id].agent_stop_index;

    memcpy(ctx->sim_agents, game.state.agents, sizeof(ctx->sim_agents));
    ctx->wetness_gain = 0;
    ctx->nb_50_wet_gain = 0;
    ctx->nb_100_wet_gain = 0;
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;

        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) {
            cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        } else if (aid >= en_start && aid <= en_stop) {
            cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
        } else continue;

        ctx->sim_agents[aid].x = cmd->mv_x;
        ctx->sim_agents[aid].y = cmd->mv_y;
    }
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;

        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) {
            cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        } else if (aid >= en_start && aid <= en_stop) {
            cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
        } else continue;

        if (cmd->action_type == CMD_THROW) {
            for (int t = 0; t < MAX_AGENTS; t++) {
                if (!ctx->sim_agents[t].alive) continue;
                int dx = abs(ctx->sim_agents[t].x - cmd->target_x_or_id);
                int dy = abs(ctx->sim_agents[t].y - cmd->target_y);
                if (dx <= 1 && dy <= 1)
                    ctx->sim_agents[t].wetness += 30;
            }
        } else if (cmd->action_type == CMD_SHOOT) {
            int target_id = cmd->target_x_or_id;
            if (!ctx->sim_agents[target_id].alive) continue;

            AgentState* shooter = &ctx->sim_agents[aid];
            AgentState* target  = &ctx->sim_agents[target_id];
            AgentInfo* shooter_info = &game.consts.agent_info[aid];

            int dx = abs(shooter->x - target->x);
            int dy = abs(shooter->y - target->y);
            int dist = dx + dy;
            float range_modifier = dist <= shooter_info->optimal_range ? 1.0f :
                                   dist <= 2 * shooter_info->optimal_range ? 0.5f : 0.0f;
            if (range_modifier == 0.0f) continue;

            float cover_modifier = 1.0f;
            int adj_x = -((target->x - shooter->x) > 0) + ((target->x - shooter->x) < 0);
            int adj_y = -((target->y - shooter->y) > 0) + ((target->y - shooter->y) < 0);
            int cx = target->x + adj_x;
            int cy = target->y + adj_y;
            if (cx >= 0 && cx < game.consts.map.width && cy >= 0 && cy < game.consts.map.height) {
                int tile = game.consts.map.map[cy][cx].type;
                if (tile == 1) cover_modifier = 0.5f;
                else if (tile == 2) cover_modifier = 0.25f;
            }

            float damage = shooter_info->soaking_power * range_modifier * cover_modifier;
            if (damage > 0) target->wetness += (int)damage;
        }
    }
    int my_id_player = game.consts.my_player_id;
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        int curr = game.state.agents[aid].wetness;
        int now  = ctx->sim_agents[aid].wetness;
        if (now >= 100) {
            ctx->sim_agents[aid].alive = 0;
            now = 100;
        }

        int pid = game.consts.agent_info[aid].player_id;
        int delta = now - curr;
        if (delta == 0) continue;

        if (now >= 100 && curr < 100)
            ctx->nb_100_wet_gain += (pid == my_id_player) ? -1 : +1;
        if (now >= 50 && curr < 50)
            ctx->nb_50_wet_gain += (pid == my_id_player) ? -1 : +1;

        ctx->wetness_gain += (pid == my_id_player) ? -delta : +delta;
    }
    ctx->control_score = 0;
    // for (int aid = my_start; aid <= my_stop; aid++) {
    //     if (!ctx->sim_agents[aid].alive) continue;
    //     ctx->control_score += controlled_score_gain_if_agent_moves_to(aid, ctx->sim_agents[aid].x, ctx->sim_agents[aid].y);
    // }
 
    float score_sum = 0.0f;
    int count = 0;

    for (int aid = my_start; aid <= my_stop; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        AgentCommand* cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        score_sum += cmd->score;
        count++;
    }

    ctx->control_score += score_sum /100.0;

}

float evaluate_simulation(const SimulationContext* ctx) {
    
    
    return
        ctx->control_score / 100.0f  * 20.0f +
        ctx->wetness_gain / 100.0f   * 1.0f +
        ctx->nb_50_wet_gain / 10.0f  * 1000.0f +
        ctx->nb_100_wet_gain / 10.0f * 2000.0f;
}

void compute_evaluation() {
    int my_id = game.consts.my_player_id;
    int en_id = !my_id;
    game.output.simulation_count = 0;

    int my_count = game.output.player_command_count[my_id];
    int en_count = game.output.player_command_count[en_id];

    for (int i = 0; i < my_count; i++) {
        float worst_score = 1e9f;
        int worst_enemy_cmd = -1;

        for (int j = 0; j < en_count; j++) {
            SimulationContext ctx;
            simulate_players_commands(i, j, &ctx);
            float score = evaluate_simulation(&ctx);

            if (score < worst_score) {
                worst_score = score;
                worst_enemy_cmd = j;
            }
        }

        game.output.simulation_results[game.output.simulation_count++] = (SimulationResult){
            .score = worst_score,
            .my_cmds_index = i,
            .op_cmds_index = worst_enemy_cmd
        };
    }
    for (int a = 0; a < game.output.simulation_count - 1; a++) {
        for (int b = a + 1; b < game.output.simulation_count; b++) {
            if (game.output.simulation_results[b].score > game.output.simulation_results[a].score) {
                SimulationResult tmp = game.output.simulation_results[a];
                game.output.simulation_results[a] = game.output.simulation_results[b];
                game.output.simulation_results[b] = tmp;
            }
        }
    }
}




void apply_output() {
    float cpu = CPU_MS_USED;
    int my_player_id = game.consts.my_player_id;
    int enemy_player_id = !game.consts.my_player_id;
    int agent_start_id = game.consts.player_info[my_player_id].agent_start_index;
    int agent_stop_id = game.consts.player_info[my_player_id].agent_stop_index;
    int enemy_start_id = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop_id  = game.consts.player_info[enemy_player_id].agent_stop_index;

    if (game.output.simulation_count == 0) {
        ERROR("no simu result");
    }
    // fprintf(stderr, "\n=== ENEMY AGENTS (cmd_id = 0) ===\n");
    // for (int enemy_id = enemy_start_id; enemy_id <= enemy_stop_id; enemy_id++) {
    //     AgentCommand *e_cmd = &game.output.player_commands[enemy_player_id][0][enemy_id];
    //     const char *e_act = (e_cmd->action_type == CMD_SHOOT) ? "SH" :
    //                         (e_cmd->action_type == CMD_THROW) ? "TH" : "HK";
    //     fprintf(stderr, "E%d:(%d,%d)%s(%d,%d)[%.1f]\n",
    //         enemy_id + 1, e_cmd->mv_x, e_cmd->mv_y,
    //         e_act, e_cmd->target_x_or_id, e_cmd->target_y, e_cmd->score);
    // }
    // int top_n = (game.output.simulation_count < 50) ? game.output.simulation_count : 50;
    // for (int rank = 0; rank < top_n; rank++) {
    //     SimulationResult *res = &game.output.simulation_results[rank];
    //     fprintf(stderr, "#%d: S=%.1f | my=%d | en=%d || ",
    //         rank + 1, res->score, res->my_cmds_index, res->op_cmds_index);

    //     for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
    //         AgentCommand *cmd = &game.output.player_commands[my_player_id][res->my_cmds_index][agent_id];
    //         const char *act = (cmd->action_type == CMD_SHOOT) ? "SH" :
    //                           (cmd->action_type == CMD_THROW) ? "TH" : "HK";
    //         fprintf(stderr, "A%d:(%d,%d)%s(%d,%d)[%.1f] ",
    //             agent_id + 1, cmd->mv_x, cmd->mv_y,
    //             act, cmd->target_x_or_id, cmd->target_y, cmd->score);
    //     }

    //     fprintf(stderr, "\n");
    // }
    // fprintf(stderr, "================\n");

    int best_index = game.output.simulation_results[0].my_cmds_index;


    for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
        if(!game.state.agents[agent_id].alive) continue;
        AgentCommand* cmd = &game.output.player_commands[my_player_id][best_index][agent_id];

        printf("%d", agent_id+1);

        if (cmd->mv_x != game.state.agents[agent_id].x || cmd->mv_y != game.state.agents[agent_id].y) {
            printf(";MOVE %d %d", cmd->mv_x, cmd->mv_y);
        }
        if (cmd->action_type == CMD_SHOOT) {
            printf(";SHOOT %d",cmd->target_x_or_id +1);
        } else if (cmd->action_type == CMD_THROW) {
            printf(";THROW %d %d", cmd->target_x_or_id, cmd->target_y);
        } else if (cmd->action_type == CMD_HUNKER) {
            printf(";HUNKER_DOWN");
        } else {
        }

        printf(";MESSAGE %.2fms",cpu);

        printf("\n");
        fflush(stdout);
    }
}

int main() {
    read_game_inputs_init();

    while (1) {
        read_game_inputs_cycle();
        precompute_bfs_distances();
        compute_best_agents_commands();
        compute_best_player_commands();
        compute_evaluation();
        apply_output();
        // debug_stats();

    }

    return 0;
}