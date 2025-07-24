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
#define MAX_COMMANDS_PER_AGENT 35
#define MAX_COMMANDS_PER_PLAYER 1024
#define MAX_SIMULATIONS 1024

// ==========================
// === DATA MODELS
// ==========================

typedef enum {
    CMD_SHOOT,
    CMD_THROW,
    CMD_HUNKER
} ActionType;
typedef struct {
    int target_x_or_id, target_y;
    float score; // utile pour trier les actions
} AgentAction;
typedef struct {
    int mv_x, mv_y;
    ActionType action_type;
    int target_x_or_id, target_y;
    float score; // utile pour trier les commandes
} AgentCommand;

typedef struct {
    float score;
    int my_cmds_index;
    int op_cmds_index;
} SimulationResult;

typedef struct {
    // [agent_id][y][x] = distance depuis agent_id à (x, y)
    int bfs_enemy_distances[MAX_AGENTS][MAX_HEIGHT][MAX_WIDTH];


    // Listes triées des meilleurs actions par agent
    AgentAction moves[MAX_AGENTS][MAX_MOVES_PER_AGENT];
    int move_counts[MAX_AGENTS];
    AgentAction shoots[MAX_AGENTS][MAX_SHOOTS_PER_AGENT];
    int shoot_counts[MAX_AGENTS];
    AgentAction bombs[MAX_AGENTS][MAX_BOMB_PER_AGENT];
    int bomb_counts[MAX_AGENTS];

    // Liste des commandes fusionnées (move+shoot+bomb+hunker) par agent
    AgentCommand agent_commands[MAX_AGENTS][MAX_COMMANDS_PER_AGENT];
    int agent_command_counts[MAX_AGENTS];

    // Liste des commandes fusionnées (combinaisons multi-agents) par joueur
    AgentCommand player_commands[MAX_PLAYERS][MAX_COMMANDS_PER_PLAYER][MAX_AGENTS];
    int player_command_count[MAX_PLAYERS];

    // Résultats de simulations triée par score pour obtenir la meilleur commande simulation_results[0].my_cmds_index
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
    int agent_count;         // nombre d'agents pour ce joueur
    int agent_start_index;   // index de début dans agent_info[]
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
    int agent_count_do_not_use; // use alive instead
    int my_agent_count_do_not_use; // use alive instead
} GameState;

typedef struct {
    GameConstants consts;
    GameState state;
    GameOutput output;
} GameInfo;

typedef struct {
    AgentState sim_agents[MAX_AGENTS];
    int wetness_gain;
    int nb_50_wet_gain;
    int nb_100_wet_gain;
    int control_score;
    int my_team_total_wetness;
    int my_cmds_index;
} SimulationContext;

GameInfo game = {0};

// ==========================
// === UTILITAIRES
// ==========================
static clock_t gCPUStart;
#define CPU_RESET        (gCPUStart = clock())
#define CPU_MS_USED      (((double)(clock() - gCPUStart)) * 1000.0 / CLOCKS_PER_SEC)
#define CPU_BREAK(val)   if (CPU_MS_USED > (val)) break;
#define ERROR(text) {fprintf(stderr,"ERROR:%s",text);fflush(stderr);exit(1);}
#define ERROR_INT(text,val) {fprintf(stderr,"ERROR:%s:%d",text,val);fflush(stderr);exit(1);}

void debug_stats() {
    fprintf(stderr, "\n=== STATS ===\n");

    // Commandes agent
    for (int a = 0; a < MAX_AGENTS; ++a) {
        if (!game.state.agents[a].alive) continue;
        fprintf(stderr, "Agent %d - Commands: %d actions[%d]\n", a+1, game.output.agent_command_counts[a],game.output.agent_command_counts[a]-game.output.move_counts[a] );
    }

    // Commandes joueur
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        fprintf(stderr, "Player %d - PlayerCommands: %d\n", p, game.output.player_command_count[p]);
    }

    // Simulations
    fprintf(stderr, "Simulations: %d\n", game.output.simulation_count);
    fprintf(stderr, "=============\n");
}


int controlled_score_gain_if_agent_moves_to(int agent_id, int nx, int ny) {
    // Calcule le gain net de zone contrôlée si l'agent se déplace en (nx, ny)
    int my_gain = 0;
    int enemy_gain = 0;

    for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue; // obstacle

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


// ==========================
// === MAIN FUNCTIONS
// ==========================

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

    // Initialiser les infos par joueur
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        game.consts.player_info[p].agent_count = 0;
        game.consts.player_info[p].agent_start_index = -1;
        game.consts.player_info[p].agent_stop_index = -1;
    }

    // Scanner les agents pour remplir les infos par player
    // les agents sont contigus dans la structure
    for (int i = 0; i < game.consts.agent_info_count; ++i) {
        int player = game.consts.agent_info[i].player_id;
        if (game.consts.player_info[player].agent_count == 0) {
            game.consts.player_info[player].agent_start_index = i;
        }
        game.consts.player_info[player].agent_count++;
        game.consts.player_info[player].agent_stop_index = i;
    }
    fprintf(stderr,"%d %d %d\n",game.consts.player_info[0].agent_count,game.consts.player_info[0].agent_start_index,game.consts.player_info[0].agent_stop_index);
    fprintf(stderr,"%d %d %d\n",game.consts.player_info[1].agent_count,game.consts.player_info[1].agent_start_index,game.consts.player_info[1].agent_stop_index);
    scanf("%d%d", &game.consts.map.width, &game.consts.map.height);
    for (int i = 0; i < game.consts.map.height * game.consts.map.width; i++) {
        int x, y, tile_type;
        scanf("%d%d%d", &x, &y, &tile_type);
        game.consts.map.map[y][x] = (Tile){x, y, tile_type};
    }
}

void read_game_inputs_cycle() {    
    // Réinitialiser tous les agents a dead
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
                if (game.consts.map.map[ny][nx].type > 0) continue; // obstacle
                if (visited[ny][nx]) continue;

                visited[ny][nx] = 1;
                dist[ny][nx] = dist[y][x] + 1;

                queue_x[back] = nx;
                queue_y[back++] = ny;
            }
        }

        // Stocker dans la sortie
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
        {0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}
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
        if (!enemy->alive || enemy->splash_bombs <= 0) continue;
        if (abs(enemy->x - agent_state->x) + abs(enemy->y - agent_state->y) <= 7) {
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
            if (!game.state.agents[k].alive) continue;
            int dist = game.output.bfs_enemy_distances[game.state.agents[k].id][ny][nx];
            if (dist < min_dist_to_enemy) min_dist_to_enemy = dist;
        }

        float penalty = 0.0f;
        if (danger) {
            for (int a = ally_start; a <= ally_stop; a++) {
                if (a == agent_id || !game.state.agents[a].alive) continue;
                if (abs(game.state.agents[a].x - nx) + abs(game.state.agents[a].y - ny) < 3) {
                    penalty += 20.0f;
                }
            }
        }
        
        // Improved territory control bonus
        float territory_bonus = 0.0f;
        int new_tiles_controlled = 0;
        for (int y = 0; y < game.consts.map.height; y++) {
            for (int x = 0; x < game.consts.map.width; x++) {
                if (game.consts.map.map[y][x].type > 0) continue;
                
                int old_dist = INT_MAX;
                int new_dist = abs(x - nx) + abs(y - ny);
                
                // Calculate control change
                for (int a = ally_start; a <= ally_stop; a++) {
                    if (!game.state.agents[a].alive) continue;
                    int ax = (a == agent_id) ? agent_state->x : game.state.agents[a].x;
                    int ay = (a == agent_id) ? agent_state->y : game.state.agents[a].y;
                    int d = abs(x - ax) + abs(y - ay);
                    if (d < old_dist) old_dist = d;
                }
                
                if (new_dist < old_dist) new_tiles_controlled++;
            }
        }
        territory_bonus = new_tiles_controlled * 8.0f;

        // Strategic positioning bonus
        float positioning_bonus = 0.0f;
        if (min_dist_to_enemy >= 2 && min_dist_to_enemy <= agent_info->optimal_range) {
            positioning_bonus = 25.0f;  // Optimal engagement range
        }

        float cover_bonus = 0.0f;
        static const int cover_dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};
        for(int i = 0; i < 4; i++) {
            int cx = nx + cover_dirs[i][0];
            int cy = ny + cover_dirs[i][1];
            if (cx >= 0 && cx < game.consts.map.width && cy >= 0 && cy < game.consts.map.height) {
                if (game.consts.map.map[cy][cx].type == 1) cover_bonus += 15.0f;
                else if (game.consts.map.map[cy][cx].type == 2) cover_bonus += 30.0f;
            }
        }

        float wetness_penalty = (agent_state->wetness > 40) ? (float)(agent_state->wetness * agent_state->wetness) / 50.0f : 0.0f;
        
        // Combine bonuses
        float score = territory_bonus + positioning_bonus + cover_bonus - min_dist_to_enemy * 1.5f - penalty - wetness_penalty;

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


void compute_best_agents_shoot(int agent_id, int new_shooter_x, int new_shooter_y) {
    AgentState* shooter_state = &game.state.agents[agent_id];
    AgentInfo* shooter_info   = &game.consts.agent_info[agent_id];
    
    game.output.shoot_counts[agent_id] = 0;
    if (shooter_state->cooldown > 0) return;

    int my_player_id = shooter_info->player_id;
    int enemy_player_id = !my_player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop  = game.consts.player_info[enemy_player_id].agent_stop_index;

    // === NEW: Calculate the danger of the shooter's position ===
    float position_risk = 0;
    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* threatening_enemy = &game.state.agents[k];
        if (!threatening_enemy->alive || threatening_enemy->cooldown > 0) continue;
        AgentInfo* threatening_enemy_info = &game.consts.agent_info[k];

        int dist_to_shooter = abs(threatening_enemy->x - new_shooter_x) + abs(threatening_enemy->y - new_shooter_y);

        // If I am in the enemy's optimal range, it's very risky.
        if (dist_to_shooter <= threatening_enemy_info->optimal_range) {
            position_risk += threatening_enemy_info->soaking_power;
        } 
        // If I am in their secondary range, it's less risky.
        else if (dist_to_shooter <= 2 * threatening_enemy_info->optimal_range) {
            position_risk += threatening_enemy_info->soaking_power * 0.5f;
        }
    }

    int shoots_count = 0;
    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* target_enemy = &game.state.agents[k];
        if (!target_enemy->alive) continue;
        
        int dist = abs(target_enemy->x - new_shooter_x) + abs(target_enemy->y - new_shooter_y);
        if (dist > 2 * shooter_info->optimal_range) continue;

        // Offensive score based on how much damage we can do
        float damage_potential = shooter_info->soaking_power;
        if (dist > shooter_info->optimal_range) {
            damage_potential *= 0.5f;
        }
        
        // Bonus for finishing off a weak target
        float kill_bonus = (target_enemy->wetness + damage_potential >= 100) ? 50.0f : 0.0f;

        // Final Score = Value of the shot MINUS the risk of taking it.
        float score = (target_enemy->wetness * 0.5f) + damage_potential + kill_bonus - position_risk;

        if (shoots_count < MAX_SHOOTS_PER_AGENT) {
            game.output.shoots[agent_id][shoots_count++] = (AgentAction){
                .target_x_or_id = k,
                .target_y = 0,
                .score = score
            };
        }
    }

    // Tri décroissant du score
    for (int m = 0; m < shoots_count - 1; m++) {
        for (int n = m + 1; n < shoots_count; n++) {
            if (game.output.shoots[agent_id][n].score > game.output.shoots[agent_id][m].score) {
                AgentAction tmp = game.output.shoots[agent_id][m];
                game.output.shoots[agent_id][m] = game.output.shoots[agent_id][n];
                game.output.shoots[agent_id][n] = tmp;
            }
        }
    }
    game.output.shoot_counts[agent_id] = shoots_count;
}

void compute_best_agents_bomb(int agent_id, int new_thrower_x, int new_thrower_y) {
    game.output.bomb_counts[agent_id] = 0;
    AgentState* thrower_state = &game.state.agents[agent_id];
    if (!thrower_state->alive || thrower_state->splash_bombs <= 0) return;

    int my_player_id = game.consts.agent_info[agent_id].player_id;
    int enemy_player_id = !my_player_id;
    int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    int enemy_stop = game.consts.player_info[enemy_player_id].agent_stop_index;
    int ally_start = game.consts.player_info[my_player_id].agent_start_index;
    int ally_stop = game.consts.player_info[my_player_id].agent_stop_index;

    // Find enemy clusters
    int cluster_centers_x[MAX_AGENTS] = {0};
    int cluster_centers_y[MAX_AGENTS] = {0};
    int cluster_sizes[MAX_AGENTS] = {0};
    int cluster_count = 0;

    for (int k = enemy_start; k <= enemy_stop; k++) {
        AgentState* enemy = &game.state.agents[k];
        if (!enemy->alive) continue;
        
        bool clustered = false;
        for (int c = 0; c < cluster_count; c++) {
            if (abs(enemy->x - cluster_centers_x[c]) + abs(enemy->y - cluster_centers_y[c]) <= 2) {
                cluster_sizes[c]++;
                clustered = true;
                break;
            }
        }
        
        if (!clustered) {
            cluster_centers_x[cluster_count] = enemy->x;
            cluster_centers_y[cluster_count] = enemy->y;
            cluster_sizes[cluster_count] = 1;
            cluster_count++;
        }
    }

    // Evaluate bomb targets
    for (int c = 0; c < cluster_count; c++) {
        if (cluster_sizes[c] < 2) continue;  // Only consider real clusters
        
        int tx = cluster_centers_x[c];
        int ty = cluster_centers_y[c];
        
        // Check throw range
        if (abs(tx - new_thrower_x) + abs(ty - new_thrower_y) > 4) continue;

        // Check allies in splash radius
        bool ally_in_danger = false;
        for (int a = ally_start; a <= ally_stop; a++) {
            AgentState* ally = &game.state.agents[a];
            if (!ally->alive) continue;
            if (abs(ally->x - tx) <= 1 && abs(ally->y - ty) <= 1) {
                ally_in_danger = true;
                break;
            }
        }
        if (ally_in_danger) continue;

        // Calculate wetness potential
        float wetness_potential = 0.0f;
        for (int k = enemy_start; k <= enemy_stop; k++) {
            AgentState* enemy = &game.state.agents[k];
            if (!enemy->alive) continue;
            if (abs(enemy->x - tx) <= 1 && abs(enemy->y - ty) <= 1) {
                wetness_potential += enemy->wetness * 0.3f;
            }
        }

        // Score based on cluster size and wetness
        float score = cluster_sizes[c] * 60.0f + wetness_potential;
        
        if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
            game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] = (AgentAction){tx, ty, score};
        }
    }
    
    // If no clusters found, use individual targeting
    if (game.output.bomb_counts[agent_id] == 0) {
        for (int ty = 0; ty < game.consts.map.height; ty++) {
            for (int tx = 0; tx < game.consts.map.width; tx++) {
                if (game.consts.map.map[ty][tx].type > 0) continue;
                if (abs(tx - new_thrower_x) + abs(ty - new_thrower_y) > 4) continue;

                int enemies_hit = 0;
                int allies_hit = 0;
                float total_enemy_wetness = 0;

                // Check enemies in splash radius
                for (int k = enemy_start; k <= enemy_stop; k++) {
                    AgentState* enemy = &game.state.agents[k];
                    if (!enemy->alive) continue;
                    if (abs(enemy->x - tx) <= 1 && abs(enemy->y - ty) <= 1) {
                        enemies_hit++;
                        total_enemy_wetness += enemy->wetness;
                    }
                }

                if (enemies_hit == 0) continue;

                // Check allies in splash radius
                for (int a = ally_start; a <= ally_stop; a++) {
                    AgentState* ally = &game.state.agents[a];
                    if (!ally->alive) continue;
                    if (abs(ally->x - tx) <= 1 && abs(ally->y - ty) <= 1) {
                        allies_hit++;
                    }
                }
                if (allies_hit > 0) continue;

                float score = (float)(enemies_hit * enemies_hit) * 50.0f + total_enemy_wetness;

                if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
                    game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] = (AgentAction){tx, ty, score};
                }
            }
        }
    }
    
    // Sort by score
    int count = game.output.bomb_counts[agent_id];
    if (count == 0) return;
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
            // 2. Bombe (max 1)
            if (game.output.bomb_counts[i] > 0 && cmd_index < MAX_COMMANDS_PER_AGENT) {
                AgentAction* bomb = &game.output.bombs[i][0]; // meilleure bombe
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_THROW,
                    .target_x_or_id = bomb->target_x_or_id,
                    .target_y = bomb->target_y,
                    .score = bomb->score
                };
            }

            compute_best_agents_shoot(i,mv_x,mv_y);
            // 1. Shoots (jusqu’à 5)
            int shoot_limit = game.output.shoot_counts[i];
            if (shoot_limit > MAX_SHOOTS_PER_AGENT) shoot_limit = MAX_SHOOTS_PER_AGENT;
            for (int s = 0; s < shoot_limit && cmd_index < MAX_COMMANDS_PER_AGENT; s++) {
                AgentAction* shoot = &game.output.shoots[i][s];
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_SHOOT,
                    .target_x_or_id = shoot->target_x_or_id,
                    .target_y = shoot->target_y,
                    .score = shoot->score
                };
            }
            // 3. Hunker
            if (cmd_index < MAX_COMMANDS_PER_AGENT) {
                AgentState* agent_state = &game.state.agents[i];
                float hunker_score = 0;

                // The value of hunkering down is proportional to the agent's wetness.
                // It's worthless at 0 wetness but critical at high wetness.
                if (agent_state->wetness > 30) {
                    hunker_score = (float)agent_state->wetness * 1.2f;
                }
                
                // Bonus score for hunkering if the agent is a likely target (high wetness and ready to be shot at).
                if (agent_state->wetness > 50 && agent_state->cooldown == 0) {
                    hunker_score += 20;
                }
                
                game.output.agent_commands[i][cmd_index++] = (AgentCommand){
                    .mv_x = mv_x,
                    .mv_y = mv_y,
                    .action_type = CMD_HUNKER,
                    .target_x_or_id = -1,
                    .target_y = -1,
                    .score = hunker_score // Use the new dynamic hunker_score instead of mv->score
                };
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

        // Initialisation avec 1 commande par agent vivant
        for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
            if (!game.state.agents[agent_id].alive) continue;
            max_cmds[agent_id] = 1;
        }

        // Répartition intelligente
        bool updated = true;
        while (updated) {
            updated = false;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;

                int current = max_cmds[agent_id];
                int available = game.output.agent_command_counts[agent_id];

                if (current < available) {
                    int new_total = total / current * (current + 1);
                    if (new_total <= MAX_COMMANDS_PER_PLAYER) {
                        max_cmds[agent_id]++;
                        total = new_total;
                        updated = true;
                    }
                }
            }
        }

        // Génération du produit cartésien avec les limites fixées
        int indices[MAX_AGENTS] = {0};

        while (true) {
            if (game.output.player_command_count[p] >= MAX_COMMANDS_PER_PLAYER) ERROR_INT("ERROR to many command",MAX_COMMANDS_PER_PLAYER)

            // Construire la combinaison
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;
                int cmd_id = indices[agent_id];
                game.output.player_commands[p][game.output.player_command_count[p]][agent_id] =
                    game.output.agent_commands[agent_id][cmd_id];
            }

            game.output.player_command_count[p]++;

            // Incrémenter les indices
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

void simulate_players_commands(int my_cmd_index, int en_cmd_index, SimulationContext* ctx) {
    int my_id    = game.consts.my_player_id;
    int en_id    = !my_id;
    int my_start = game.consts.player_info[my_id].agent_start_index;
    int my_stop  = game.consts.player_info[my_id].agent_stop_index;
    int en_start = game.consts.player_info[en_id].agent_start_index;
    int en_stop  = game.consts.player_info[en_id].agent_stop_index;

    // 0) Copy initial state
    memcpy(ctx->sim_agents, game.state.agents, sizeof(ctx->sim_agents));
    ctx->wetness_gain    = 0;
    ctx->nb_50_wet_gain  = 0;
    ctx->nb_100_wet_gain = 0;

    // --- MOVE PHASE ---
    // (This part of your code is correct and remains unchanged)
    int orig_x[MAX_AGENTS], orig_y[MAX_AGENTS];
    int des_x[MAX_AGENTS], des_y[MAX_AGENTS];
    static int occ[MAX_HEIGHT][MAX_WIDTH];
    static int desire_count[MAX_HEIGHT][MAX_WIDTH];
    memset(occ, 0, sizeof(occ));
    memset(desire_count, 0, sizeof(desire_count));
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        orig_x[aid] = ctx->sim_agents[aid].x;
        orig_y[aid] = ctx->sim_agents[aid].y;
        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        else if (aid >= en_start && aid <= en_stop) cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
        des_x[aid] = cmd->mv_x;
        des_y[aid] = cmd->mv_y;
        occ[orig_y[aid]][orig_x[aid]] = 1;
    }
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        int y = des_y[aid], x = des_x[aid];
        if (x >= 0 && x < game.consts.map.width && y >= 0 && y < game.consts.map.height && game.consts.map.map[y][x].type == 0) {
            desire_count[y][x]++;
        } else {
            des_x[aid] = orig_x[aid];
            des_y[aid] = orig_y[aid];
            desire_count[orig_y[aid]][orig_x[aid]]++;
        }
    }
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        int nx = des_x[aid], ny = des_y[aid];
        bool cancel = (desire_count[ny][nx] > 1) || (occ[ny][nx] && !(orig_x[aid] == nx && orig_y[aid] == ny));
        if (!cancel) {
            ctx->sim_agents[aid].x = nx;
            ctx->sim_agents[aid].y = ny;
        } else {
            ctx->sim_agents[aid].x = orig_x[aid];
            ctx->sim_agents[aid].y = orig_y[aid];
        }
    }

    // --- COMBAT PHASE ---
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        else if (aid >= en_start && aid <= en_stop) cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
        else continue;

        if (cmd->action_type == CMD_THROW) {
            for (int t = 0; t < MAX_AGENTS; t++) {
                if (!ctx->sim_agents[t].alive) continue;
                int dx = abs(ctx->sim_agents[t].x - cmd->target_x_or_id);
                int dy = abs(ctx->sim_agents[t].y - cmd->target_y);
                if (dx <= 1 && dy <= 1)
                    ctx->sim_agents[t].wetness += 30; // Bombs ignore cover and hunker
            }
        }
        else if (cmd->action_type == CMD_SHOOT) {
            int target_id = cmd->target_x_or_id;
            if (ctx->sim_agents[target_id].alive) {
                AgentState* shooter = &ctx->sim_agents[aid];
                AgentState* target  = &ctx->sim_agents[target_id];
                AgentInfo*  info    = &game.consts.agent_info[aid];

                int dist = abs(shooter->x - target->x) + abs(shooter->y - target->y);
                float range_mod = 0.0f;
                if (dist <= info->optimal_range) range_mod = 1.0f;
                else if (dist <= 2 * info->optimal_range) range_mod = 0.5f;

                if (range_mod > 0.0f) {
                    float total_damage_reduction = 1.0f;

                    // 1. Check for Cover
                    int adj_x = (target->x > shooter->x) ? -1 : (target->x < shooter->x) ? 1 : 0;
                    int adj_y = (target->y > shooter->y) ? -1 : (target->y < shooter->y) ? 1 : 0;
                    int cx = target->x + adj_x;
                    int cy = target->y + adj_y;
                    if (cx >= 0 && cx < game.consts.map.width && cy >= 0 && cy < game.consts.map.height) {
                        int tile_type = game.consts.map.map[cy][cx].type;
                        if (tile_type == 1) total_damage_reduction *= 0.5f; // Low cover
                        else if (tile_type == 2) total_damage_reduction *= 0.25f; // High cover
                    }

                    // ==================== FIX START ====================
                    // 2. Check if the TARGET is hunkering down
                    AgentCommand* target_cmd = NULL;
                    int target_owner = game.consts.agent_info[target_id].player_id;
                    if (target_owner == my_id) target_cmd = &game.output.player_commands[my_id][my_cmd_index][target_id];
                    else target_cmd = &game.output.player_commands[en_id][en_cmd_index][target_id];

                    if (target_cmd->action_type == CMD_HUNKER) {
                        total_damage_reduction *= 0.75f; // 25% damage reduction
                    }
                    // ===================== FIX END =====================

                    int dmg = (int)(info->soaking_power * range_mod * total_damage_reduction);
                    if (dmg > 0) target->wetness += dmg;
                }
            }
        }
    }

    // --- COOLDOWN/BOMB UPDATE & FINAL SCORING ---
    // (This part of your code is correct and remains unchanged)
    for(int aid = 0; aid < MAX_AGENTS; ++aid) {
        if (!ctx->sim_agents[aid].alive) continue;
        AgentCommand* cmd = NULL;
        if (aid >= my_start && aid <= my_stop) cmd = &game.output.player_commands[my_id][my_cmd_index][aid];
        else if (aid >= en_start && aid <= en_stop) cmd = &game.output.player_commands[en_id][en_cmd_index][aid];
        else continue;

        if (cmd->action_type == CMD_SHOOT) ctx->sim_agents[aid].cooldown = game.consts.agent_info[aid].shoot_cooldown;
        else if (ctx->sim_agents[aid].cooldown > 0) ctx->sim_agents[aid].cooldown--;
        if (cmd->action_type == CMD_THROW) ctx->sim_agents[aid].splash_bombs = (ctx->sim_agents[aid].splash_bombs > 0) ? ctx->sim_agents[aid].splash_bombs - 1 : 0;
    }

    int me = game.consts.my_player_id;
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        int before = game.state.agents[aid].wetness;
        int after  = ctx->sim_agents[aid].wetness;
        if (after >= 100) {
            ctx->sim_agents[aid].alive = 0;
            after = 100;
        }
        int delta = after - before;
        if (delta > 0) {
            int owner = game.consts.agent_info[aid].player_id;
            if (before < 50 && after >= 50) ctx->nb_50_wet_gain  += (owner == me ? -1 : 1);
            if (before < 100 && after >= 100) ctx->nb_100_wet_gain += (owner == me ? -1 : 1);
            ctx->wetness_gain += (owner == me ? -delta : delta);
        }
    }
    ctx->control_score = 0;
    for (int aid = my_start; aid <= my_stop; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        ctx->control_score += controlled_score_gain_if_agent_moves_to(aid, ctx->sim_agents[aid].x, ctx->sim_agents[aid].y);
    }
    ctx->my_team_total_wetness = 0;
    for (int aid = my_start; aid <= my_stop; aid++) {
        if (ctx->sim_agents[aid].alive) ctx->my_team_total_wetness += ctx->sim_agents[aid].wetness;
        else ctx->my_team_total_wetness += 150;
    }
}

float evaluate_simulation(const SimulationContext* ctx) {
    int my_id = game.consts.my_player_id;
    int my_start = game.consts.player_info[my_id].agent_start_index;
    int my_stop = game.consts.player_info[my_id].agent_stop_index;
    int en_start = game.consts.player_info[!my_id].agent_start_index;
    int en_stop = game.consts.player_info[!my_id].agent_stop_index;

    int my_agents_alive = 0;
    int enemy_agents_alive = 0;
    int my_total_wetness = 0;
    int enemy_total_wetness = 0;
    
    for (int i = 0; i < game.consts.agent_info_count; i++) {
        if (!ctx->sim_agents[i].alive) continue;
        if (game.consts.agent_info[i].player_id == my_id) {
            my_agents_alive++;
            my_total_wetness += ctx->sim_agents[i].wetness;
        } else {
            enemy_agents_alive++;
            enemy_total_wetness += ctx->sim_agents[i].wetness;
        }
    }

    // Dynamic weights based on game state
    float territory_weight = 25.0f;
    float aggression_weight = 100.0f;
    float survival_weight = 40.0f;
    float elimination_bonus = 10000.0f;

    // Adjust weights based on wetness difference
    if (my_total_wetness > enemy_total_wetness + 100) {
        // Play more aggressively when behind
        territory_weight = 15.0f;
        aggression_weight = 150.0f;
        survival_weight = 30.0f;
        elimination_bonus = 12000.0f;
    } else if (enemy_total_wetness > my_total_wetness + 100) {
        // Play safer when ahead
        territory_weight = 35.0f;
        aggression_weight = 70.0f;
        survival_weight = 50.0f;
    }

    // Focus fire bonus
    int targets_hit[MAX_AGENTS] = {0};
    float focus_fire_bonus = 0;
    AgentCommand* my_commands = game.output.player_commands[my_id][ctx->my_cmds_index];
    for (int aid = my_start; aid <= my_stop; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        AgentCommand* cmd = &my_commands[aid];
        if (cmd->action_type == CMD_SHOOT) {
            targets_hit[cmd->target_x_or_id]++;
        }
    }
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (targets_hit[i] > 1) {
            focus_fire_bonus += (float)(targets_hit[i] - 1) * 2000.0f;
        }
    }
    
    // Calculate territory score
    float territory_score = 0;
    for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue;
            
            int my_dist = INT_MAX;
            int en_dist = INT_MAX;
            
            for (int aid = my_start; aid <= my_stop; aid++) {
                if (!ctx->sim_agents[aid].alive) continue;
                int d = abs(x - ctx->sim_agents[aid].x) + abs(y - ctx->sim_agents[aid].y);
                if (ctx->sim_agents[aid].wetness >= 50) d *= 2;
                if (d < my_dist) my_dist = d;
            }
            
            for (int aid = en_start; aid <= en_stop; aid++) {
                if (!ctx->sim_agents[aid].alive) continue;
                int d = abs(x - ctx->sim_agents[aid].x) + abs(y - ctx->sim_agents[aid].y);
                if (ctx->sim_agents[aid].wetness >= 50) d *= 2;
                if (d < en_dist) en_dist = d;
            }
            
            if (my_dist < en_dist) territory_score += 1.0f;
        }
    }
    
    return
        territory_score * territory_weight +
        ctx->wetness_gain * aggression_weight +
        ctx->nb_100_wet_gain * elimination_bonus +
        focus_fire_bonus -
        ctx->my_team_total_wetness * survival_weight;
}

void compute_evaluation() {
    int my_id = game.consts.my_player_id;
    int en_id = !my_id;
    game.output.simulation_count = 0;

    int my_count = game.output.player_command_count[my_id];
    int en_index = 0; // On teste uniquement contre le premier jeu de commandes ennemies

    for (int i = 0; i < my_count; i++) {
        SimulationContext ctx;
        ctx.my_cmds_index = i; // Pass the command index to the context for focus fire check
        simulate_players_commands(i, en_index, &ctx);
        float score = evaluate_simulation(&ctx);

        game.output.simulation_results[game.output.simulation_count++] = (SimulationResult){
            .score = score,
            .my_cmds_index = i,
            .op_cmds_index = en_index
        };
    }

    // Tri décroissant
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
    int my_player_id    = game.consts.my_player_id;
    int agent_start_id  = game.consts.player_info[my_player_id].agent_start_index;
    int agent_stop_id   = game.consts.player_info[my_player_id].agent_stop_index;

    if (game.output.simulation_count == 0) {
        ERROR("no simu result");
    }
    int best_index = game.output.simulation_results[0].my_cmds_index;

    for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
        if (!game.state.agents[agent_id].alive) continue;
        AgentCommand* cmd = &game.output.player_commands[my_player_id][best_index][agent_id];

        AgentState* st  = &game.state.agents[agent_id];
        AgentInfo*  info = &game.consts.agent_info[agent_id];

        // 1) Cooldown yönetimi
        if (cmd->action_type == CMD_SHOOT) {
            st->cooldown = info->shoot_cooldown;
        } else if (st->cooldown > 0) {
            st->cooldown--;
        }

        // 2) Bomb stoğu yönetimi
        if (cmd->action_type == CMD_THROW) {
            st->splash_bombs = (st->splash_bombs > 0
                                ? st->splash_bombs - 1
                                : 0);
        }

        // 3) Çıktı
        printf("%d", agent_id + 1);

        // MOVE
        if (cmd->mv_x != st->x || cmd->mv_y != st->y) {
            printf(";MOVE %d %d", cmd->mv_x, cmd->mv_y);
            st->x = cmd->mv_x;
            st->y = cmd->mv_y;
        }

        // Combat
        if (cmd->action_type == CMD_SHOOT) {
            printf(";SHOOT %d", cmd->target_x_or_id + 1);
        } else if (cmd->action_type == CMD_THROW) {
            printf(";THROW %d %d", cmd->target_x_or_id, cmd->target_y);
        } else if (cmd->action_type == CMD_HUNKER) {
            printf(";HUNKER_DOWN");
        }

        printf(";MESSAGE %.2fms\n", cpu);
        fflush(stdout);
    }
}

// ==========================
// === MAIN LOOP
// ==========================

int main() {
    read_game_inputs_init();

    while (1) {
        fprintf(stderr,"current\n");
        // ========== Lecture des entrées
        read_game_inputs_cycle();

        // ========== Liste des meilleures commandes par agent ==========
        precompute_bfs_distances();
        compute_best_agents_commands();

        // ========== Combinaisons possibles entre agents ==========
        compute_best_player_commands();

        // ========== Évaluation stratégique ==========
        compute_evaluation();

        // ========== Application ==========
        apply_output();

        //debug_stats();

    }

    return 0;
}