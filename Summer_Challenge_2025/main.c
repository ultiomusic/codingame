#undef _GLIBCXX_DEBUG
#pragma GCC optimize("Ofast,inline")

#pragma GCC target("bmi,bmi2,lzcnt,popcnt")
#pragma GCC target("movbe")
#pragma GCC target("aes,pclmul,rdrnd")
#pragma GCC target("avx,avx2,f16c,fma,sse2,sse3,ssse3,sse4.1,sse4.2")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <float.h>


#define MAX_WIDTH 20
#define MAX_HEIGHT 20
#define MAX_AGENTS 10
#define MAX_PLAYERS 2
#define MAX_MOVES_PER_AGENT 5
#define MAX_SHOOTS_PER_AGENT 5
#define MAX_BOMB_PER_AGENT 15
#define MAX_COMMANDS_PER_AGENT 35
#define MAX_COMMANDS_PER_PLAYER 1024
#define MAX_SIMULATIONS 1024
#define MAX_OPPONENT_SIMS_FOR_MINIMAX 3

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
    int shot_danger_map[MAX_HEIGHT][MAX_WIDTH];
    int bomb_danger_map[MAX_HEIGHT][MAX_WIDTH];

    AgentAction moves[MAX_AGENTS][MAX_MOVES_PER_AGENT];
    int move_counts[MAX_AGENTS];
    AgentAction shoots[MAX_AGENTS][MAX_SHOOTS_PER_AGENT];
    int shoot_counts[MAX_AGENTS];
    AgentAction bombs[MAX_AGENTS][MAX_BOMB_PER_AGENT];
    int bomb_counts[MAX_AGENTS];

    AgentCommand agent_commands[MAX_AGENTS][MAX_COMMANDS_PER_AGENT];
    int agent_command_counts[MAX_AGENTS];

    AgentCommand player_commands[MAX_PLAYERS][MAX_COMMANDS_PER_PLAYER][MAX_AGENTS];
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
#define CPU_RESET (gCPUStart = clock())
#define CPU_MS_USED (((double)(clock() - gCPUStart)) * 1000.0 / CLOCKS_PER_SEC)
#define ERROR(text) {fprintf(stderr,"ERROR:%s\n",text);fflush(stderr);exit(1);}
#define ERROR_INT(text,val) {fprintf(stderr,"ERROR:%s:%d\n",text,val);fflush(stderr);exit(1);}

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
        scanf("%d%d%d%d%d%d", &agent_id, &agent_x, &agent_y, &agent_cooldown, &agent_splash_bombs, &agent_wetness);
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
                if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) continue;
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
                game.output.bfs_enemy_distances[eid][y][x] = visited[y][x] ? dist[y][x] : 9999;
            }
        }
    }
}

void compute_danger_maps() {
    memset(game.output.shot_danger_map, 0, sizeof(game.output.shot_danger_map));
    memset(game.output.bomb_danger_map, 0, sizeof(game.output.bomb_danger_map));
    int enemy_player_id = !game.consts.my_player_id;

    for (int k = game.consts.player_info[enemy_player_id].agent_start_index; k <= game.consts.player_info[enemy_player_id].agent_stop_index; k++) {
        AgentState* enemy = &game.state.agents[k];
        AgentInfo* enemy_info = &game.consts.agent_info[k];
        if (!enemy->alive) continue;
        
        if (enemy->splash_bombs > 0) {
            for (int y = 0; y < game.consts.map.height; y++) {
                for (int x = 0; x < game.consts.map.width; x++) {
                    if (abs(x - enemy->x) + abs(y - enemy->y) <= 4) {
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                int ny = y + dy;
                                int nx = x + dx;
                                if (nx >= 0 && nx < game.consts.map.width && ny >= 0 && ny < game.consts.map.height) {
                                    game.output.bomb_danger_map[ny][nx] = 30;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        for (int y = 0; y < game.consts.map.height; y++) {
            for (int x = 0; x < game.consts.map.width; x++) {
                int dist = abs(x - enemy->x) + abs(y - enemy->y);
                if (dist <= enemy_info->optimal_range * 2 && enemy->cooldown == 0) {
                     float range_mod = (dist <= enemy_info->optimal_range) ? 1.0f : 0.5f;
                     game.output.shot_danger_map[y][x] += (int)(enemy_info->soaking_power * range_mod);
                }
            }
        }
    }
}


void compute_best_agents_moves(int agent_id) {
    static const int dirs[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    AgentState* agent_state = &game.state.agents[agent_id];
    game.output.move_counts[agent_id] = 0;

    int enemy_player_id = !game.consts.agent_info[agent_id].player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    for (int d = 0; d < 5; d++) {
        int nx = agent_state->x + dirs[d][0];
        int ny = agent_state->y + dirs[d][1];

        if (nx < 0 || nx >= game.consts.map.width || ny < 0 || ny >= game.consts.map.height) continue;
        if (game.consts.map.map[ny][nx].type > 0) continue;

        int min_dist_to_enemy = 9999;
        for (int k = enemy_start; k <= enemy_stop; k++) {
            if (!game.state.agents[k].alive) continue;
            int dist = game.output.bfs_enemy_distances[game.state.agents[k].id][ny][nx];
            if (dist < min_dist_to_enemy) min_dist_to_enemy = dist;
        }

        int gain = controlled_score_gain_if_agent_moves_to(agent_id, nx, ny);
        float shot_danger = (float)game.output.shot_danger_map[ny][nx];
        float bomb_danger = (float)game.output.bomb_danger_map[ny][nx];
        float score = (float)gain * 20.0f - (float)min_dist_to_enemy * 2.0f - shot_danger - bomb_danger * 1.5f;

        if (game.output.move_counts[agent_id] < MAX_MOVES_PER_AGENT) {
            game.output.moves[agent_id][game.output.move_counts[agent_id]++] = (AgentAction){nx, ny, score};
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
    AgentInfo* shooter_info = &game.consts.agent_info[agent_id];
    game.output.shoot_counts[agent_id] = 0;
    if (shooter_state->cooldown > 0) return;

    int enemy_player_id = !shooter_info->player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;

        int dist = abs(enemy->x - new_shooter_x) + abs(enemy->y - new_shooter_y);
        if (dist > 2 * shooter_info->optimal_range) continue;
        
        float damage_dealt = shooter_info->soaking_power * (dist <= shooter_info->optimal_range ? 1.0f : 0.5f);
        float score = (float)damage_dealt + (float)(100 - (enemy->wetness + damage_dealt)) * 0.5f;
        if (enemy->wetness + damage_dealt >= 100) score += 1000;

        if (game.output.shoot_counts[agent_id] < MAX_SHOOTS_PER_AGENT) {
            game.output.shoots[agent_id][game.output.shoot_counts[agent_id]++] = (AgentAction){k, 0, score};
        }
    }

    for (int m = 0; m < game.output.shoot_counts[agent_id] - 1; m++) {
        for (int n = m + 1; n < game.output.shoot_counts[agent_id]; n++) {
            if (game.output.shoots[agent_id][n].score > game.output.shoots[agent_id][m].score) {
                AgentAction tmp = game.output.shoots[agent_id][m];
                game.output.shoots[agent_id][m] = game.output.shoots[agent_id][n];
                game.output.shoots[agent_id][n] = tmp;
            }
        }
    }
}

void compute_best_agents_bomb(int agent_id,int new_thrower_x,int new_thrower_y) {
    game.output.bomb_counts[agent_id] = 0;
    AgentState* thrower_state = &game.state.agents[agent_id];
    if (!thrower_state->alive || thrower_state->splash_bombs <= 0) return;

    int my_player_id = game.consts.agent_info[agent_id].player_id;
    
    for (int ty = 0; ty < game.consts.map.height; ty++) {
        for (int tx = 0; tx < game.consts.map.width; tx++) {
            if (abs(tx - new_thrower_x) + abs(ty - new_thrower_y) > 4) continue;
            if (game.consts.map.map[ty][tx].type > 0) continue;

            float current_score = 0;
            bool hits_ally = false;

            for (int k = 0; k < MAX_AGENTS; k++) {
                if (!game.state.agents[k].alive) continue;
                if (abs(game.state.agents[k].x - tx) <= 1 && abs(game.state.agents[k].y - ty) <= 1) {
                    if (game.consts.agent_info[k].player_id == my_player_id) {
                        hits_ally = true;
                        break;
                    } else {
                        current_score += 30;
                        if(game.state.agents[k].wetness + 30 >= 100) current_score += 1000;
                    }
                }
            }
            if (hits_ally || current_score == 0) continue;

            if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
                 game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] = (AgentAction){tx, ty, current_score};
            }
        }
    }
    
    for (int m = 0; m < game.output.bomb_counts[agent_id] - 1; m++) {
        for (int n = m + 1; n < game.output.bomb_counts[agent_id]; n++) {
            if (game.output.bombs[agent_id][n].score > game.output.bombs[agent_id][m].score) {
                AgentAction tmp = game.output.bombs[agent_id][m];
                game.output.bombs[agent_id][m] = game.output.bombs[agent_id][n];
                game.output.bombs[agent_id][n] = tmp;
            }
        }
    }
}

void compute_best_agents_commands() {
    for (int i = 0; i < MAX_AGENTS; i++) {
        game.output.agent_command_counts[i]=0;        
        if(!game.state.agents[i].alive) continue;
        int cmd_index = 0;
        
        compute_best_agents_moves(i);     

        int move_count= game.output.move_counts[i];
        
        for (int m = 0; m < move_count && cmd_index < MAX_COMMANDS_PER_AGENT; m++) {
            AgentAction* mv = &game.output.moves[i][m];
            int mv_x = mv->target_x_or_id;
            int mv_y = mv->target_y;

            compute_best_agents_bomb(i,mv_x,mv_y);
            if (game.output.bomb_counts[i] > 0 && cmd_index < MAX_COMMANDS_PER_AGENT) {
                AgentAction* bomb = &game.output.bombs[i][0];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){mv_x, mv_y, CMD_THROW, bomb->target_x_or_id, bomb->target_y, mv->score + bomb->score};
            }

            compute_best_agents_shoot(i,mv_x,mv_y);
            for (int s = 0; s < game.output.shoot_counts[i] && cmd_index < MAX_COMMANDS_PER_AGENT; s++) {
                AgentAction* shoot = &game.output.shoots[i][s];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){mv_x, mv_y, CMD_SHOOT, shoot->target_x_or_id, shoot->target_y, mv->score + shoot->score};
            }
            if (cmd_index < MAX_COMMANDS_PER_AGENT) {
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){mv_x, mv_y, CMD_HUNKER, -1, -1, mv->score};
            }
        }
        game.output.agent_command_counts[i] = cmd_index;
    }
}

void compute_best_player_commands() {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        game.output.player_command_count[p] = 0;

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
                    int new_total = (total / current) * (current + 1);
                    if (new_total <= MAX_COMMANDS_PER_PLAYER) {
                        max_cmds[agent_id]++;
                        total = new_total;
                        updated = true;
                    }
                }
            }
        }
        if (total == 0) continue;

        int indices[MAX_AGENTS] = {0};

        while (true) {
            if (game.output.player_command_count[p] >= MAX_COMMANDS_PER_PLAYER) break;

            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;
                int cmd_id = indices[agent_id];
                game.output.player_commands[p][game.output.player_command_count[p]][agent_id] =
                    game.output.agent_commands[agent_id][cmd_id];
            }
            game.output.player_command_count[p]++;

            int carry = 1;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id && carry; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;
                indices[agent_id]++;
                if (indices[agent_id] >= max_cmds[agent_id] || indices[agent_id] >= game.output.agent_command_counts[agent_id]) {
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
    
    bool my_hunker[MAX_AGENTS] = {false};
    bool en_hunker[MAX_AGENTS] = {false};

    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) {
            cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
            if (cmd->action_type == CMD_HUNKER) my_hunker[aid] = true;
        } else if (aid >= en_start && aid <= en_stop) {
            cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
             if (cmd->action_type == CMD_HUNKER) en_hunker[aid] = true;
        } else continue;
        ctx->sim_agents[aid].x = cmd->mv_x;
        ctx->sim_agents[aid].y = cmd->mv_y;
    }
    
    int initial_wetness[MAX_AGENTS];
    for(int i=0; i<MAX_AGENTS; ++i) initial_wetness[i] = ctx->sim_agents[i].wetness;

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
                if (abs(ctx->sim_agents[t].x - cmd->target_x_or_id) <= 1 && abs(ctx->sim_agents[t].y - cmd->target_y) <= 1)
                    ctx->sim_agents[t].wetness += 30;
            }
        } else if (cmd->action_type == CMD_SHOOT) {
            int target_id = cmd->target_x_or_id;
            if (!ctx->sim_agents[target_id].alive) continue;

            AgentState* shooter = &ctx->sim_agents[aid];
            AgentState* target  = &ctx->sim_agents[target_id];
            AgentInfo* shooter_info = &game.consts.agent_info[aid];

            int dist = abs(shooter->x - target->x) + abs(shooter->y - target->y);
            float range_modifier = dist <= shooter_info->optimal_range ? 1.0f :
                                   dist <= 2 * shooter_info->optimal_range ? 0.5f : 0.0f;
            if (range_modifier == 0.0f) continue;

            float cover_modifier = 1.0f;
            int hunker_modifier = 1.0f;
            if((target_id >= my_start && target_id <= my_stop && my_hunker[target_id]) || (target_id >= en_start && target_id <= en_stop && en_hunker[target_id])) hunker_modifier = 0.75f;
            
            for(int dx=-1; dx<=1; ++dx) for(int dy=-1; dy<=1; ++dy){
                if(abs(dx)+abs(dy) != 1) continue;
                int cover_x = target->x + dx;
                int cover_y = target->y + dy;
                if(cover_x >= 0 && cover_x < game.consts.map.width && cover_y >= 0 && cover_y < game.consts.map.height){
                    int dot_product = (shooter->x - target->x) * dx + (shooter->y - target->y) * dy;
                    if(dot_product > 0) {
                        int tile_type = game.consts.map.map[cover_y][cover_x].type;
                        if(tile_type == 1) cover_modifier = fmin(cover_modifier, 0.5f);
                        if(tile_type == 2) cover_modifier = fmin(cover_modifier, 0.25f);
                    }
                }
            }

            float damage = shooter_info->soaking_power * range_modifier * cover_modifier * hunker_modifier;
            if (damage > 0) target->wetness += (int)round(damage);
        }
    }

    int my_id_player = game.consts.my_player_id;
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        int curr = initial_wetness[aid];
        int now  = ctx->sim_agents[aid].wetness;
        if(now > 100) now = 100;

        if (now >= 100) {
            ctx->sim_agents[aid].alive = 0;
        }

        int pid = game.consts.agent_info[aid].player_id;
        int delta = now - curr;
        if (delta <= 0) continue;

        if (now >= 100 && curr < 100)
            ctx->nb_100_wet_gain += (pid == my_id_player) ? -1 : +1;
        if (now >= 50 && curr < 50)
            ctx->nb_50_wet_gain += (pid == my_id_player) ? -1 : +1;

        ctx->wetness_gain += (pid == my_id_player) ? -delta : +delta;
    }

    ctx->control_score = 0;
    int my_tiles = 0;
    int en_tiles = 0;
     for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue;
            int d_my = INT_MAX, d_en = INT_MAX;
            for(int i=my_start; i<=my_stop; ++i){
                if(!ctx->sim_agents[i].alive) continue;
                int d = abs(x - ctx->sim_agents[i].x) + abs(y - ctx->sim_agents[i].y);
                if(ctx->sim_agents[i].wetness >= 50) d*=2;
                if(d < d_my) d_my = d;
            }
             for(int i=en_start; i<=en_stop; ++i){
                if(!ctx->sim_agents[i].alive) continue;
                int d = abs(x - ctx->sim_agents[i].x) + abs(y - ctx->sim_agents[i].y);
                if(ctx->sim_agents[i].wetness >= 50) d*=2;
                if(d < d_en) d_en = d;
            }
            if(d_my < d_en) my_tiles++; else if (d_en < d_my) en_tiles++;
        }
    }
    if(my_tiles > en_tiles) ctx->control_score = my_tiles - en_tiles;

}

float evaluate_simulation(const SimulationContext* ctx) {
    return (float)ctx->control_score * 1.0f +
           (float)ctx->wetness_gain * 2.0f +
           (float)ctx->nb_50_wet_gain * 100.0f +
           (float)ctx->nb_100_wet_gain * 5000.0f;
}

void compute_evaluation() {
    int my_id = game.consts.my_player_id;
    int en_id = !my_id;
    game.output.simulation_count = 0;

    int my_count = game.output.player_command_count[my_id];
    int en_count = game.output.player_command_count[en_id];
    int en_sim_limit = (en_count < MAX_OPPONENT_SIMS_FOR_MINIMAX) ? en_count : MAX_OPPONENT_SIMS_FOR_MINIMAX;
    if(en_sim_limit == 0) en_sim_limit = 1; // Must simulate at least one

    for (int i = 0; i < my_count; i++) {
        float min_score = FLT_MAX;
        int best_op_cmd_for_this_my_cmd = 0;
        
        for (int j = 0; j < en_sim_limit; ++j) {
             SimulationContext ctx;
             simulate_players_commands(i, j, &ctx);
             float score = evaluate_simulation(&ctx);
             if (score < min_score) {
                 min_score = score;
                 best_op_cmd_for_this_my_cmd = j;
             }
        }
        
        game.output.simulation_results[game.output.simulation_count++] = (SimulationResult){
            .score = min_score,
            .my_cmds_index = i,
            .op_cmds_index = best_op_cmd_for_this_my_cmd
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
    float cpu=CPU_MS_USED;
    int my_player_id = game.consts.my_player_id;
    int agent_start_id = game.consts.player_info[my_player_id].agent_start_index;
    int agent_stop_id = game.consts.player_info[my_player_id].agent_stop_index;

    if (game.output.simulation_count == 0) {
        for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
             if(!game.state.agents[agent_id].alive) continue;
             printf("%d;HUNKER_DOWN\n", agent_id + 1);
             fflush(stdout);
        }
        return;
    }

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
        compute_danger_maps();
        compute_best_agents_commands();
        compute_best_player_commands();
        compute_evaluation();
        apply_output();
    }

    return 0;
}