#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>

using namespace std;

const int MOD = 1 << 30;
const int GRID_SIZE = 3;

vector<vector<int>> hash_to_grid(long long hash_value) {
    vector<vector<int>> grid(GRID_SIZE, vector<int>(GRID_SIZE));
    for (int i = GRID_SIZE - 1; i >= 0; --i) {
        for (int j = GRID_SIZE - 1; j >= 0; --j) {
            grid[i][j] = hash_value % 10;
            hash_value /= 10;
        }
    }
    return grid;
}

long long grid_to_hash(const vector<vector<int>>& grid) {
    long long hash_value = 0;
    for (int i = 0; i < GRID_SIZE; ++i) {
        for (int j = 0; j < GRID_SIZE; ++j) {
            hash_value = hash_value * 10 + grid[i][j];
        }
    }
    return hash_value;
}

bool is_full(const vector<vector<int>>& grid) {
    for (const auto& row : grid) {
        for (int cell : row) {
            if (cell == 0) {
                return false;
            }
        }
    }
    return true;
}

vector<pair<int, int>> get_adjacent_cells(int i, int j) {
    vector<pair<int, int>> adjacent;
    if (i > 0) adjacent.emplace_back(i - 1, j);
    if (i < GRID_SIZE - 1) adjacent.emplace_back(i + 1, j);
    if (j > 0) adjacent.emplace_back(i, j - 1);
    if (j < GRID_SIZE - 1) adjacent.emplace_back(i, j + 1);
    return adjacent;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    int depth;
    cin >> depth;
    vector<vector<int>> grid(GRID_SIZE, vector<int>(GRID_SIZE));
    for (int i = 0; i < GRID_SIZE; ++i) {
        for (int j = 0; j < GRID_SIZE; ++j) {
            cin >> grid[i][j];
        }
    }
    long long initial_hash = grid_to_hash(grid);

    unordered_map<long long, long long> dp_current;
    unordered_map<long long, long long> dp_next;
    dp_current[initial_hash] = 1;

    long long final_sum = 0;

    for (int move = 0; move <= depth; ++move) {
        for (const auto& [board_hash, count] : dp_current) {
            vector<vector<int>> current_grid = hash_to_grid(board_hash);

            if (is_full(current_grid)) {
                final_sum = (final_sum + board_hash * count) % MOD;
                continue;
            }

            if (move == depth) {
                final_sum = (final_sum + board_hash * count) % MOD;
                continue;
            }

            vector<pair<int, int>> empty_positions;
            for (int i = 0; i < GRID_SIZE; ++i) {
                for (int j = 0; j < GRID_SIZE; ++j) {
                    if (current_grid[i][j] == 0) {
                        empty_positions.emplace_back(i, j);
                    }
                }
            }

            for (const auto& pos : empty_positions) {
                int i = pos.first;
                int j = pos.second;
                vector<pair<int, int>> adjacent_cells = get_adjacent_cells(i, j);
                vector<pair<int, int>> adj_non_zero;
                for (const auto& cell : adjacent_cells) {
                    int x = cell.first;
                    int y = cell.second;
                    if (current_grid[x][y] != 0) {
                        adj_non_zero.emplace_back(x, y);
                    }
                }

                if (adj_non_zero.size() < 2) {
                    vector<vector<int>> new_grid = current_grid;
                    new_grid[i][j] = 1;
                    long long new_hash = grid_to_hash(new_grid);
                    dp_next[new_hash] += count;
                } else {
                    int n = adj_non_zero.size();
                    bool has_valid_subset = false;
                    for (int mask = 1; mask < (1 << n); ++mask) {
                        int sum_subset = 0;
                        int bits = __builtin_popcount(mask);
                        if (bits < 2) continue;
                        for (int k = 0; k < n; ++k) {
                            if (mask & (1 << k)) {
                                sum_subset += current_grid[adj_non_zero[k].first][adj_non_zero[k].second];
                            }
                        }
                        if (sum_subset <= 6) {
                            has_valid_subset = true;
                            vector<vector<int>> new_grid = current_grid;
                            for (int k = 0; k < n; ++k) {
                                if (mask & (1 << k)) {
                                    new_grid[adj_non_zero[k].first][adj_non_zero[k].second] = 0;
                                }
                            }
                            new_grid[i][j] = sum_subset;
                            long long new_hash = grid_to_hash(new_grid);
                            dp_next[new_hash] += count;
                        }
                    }
                    if (!has_valid_subset) {
                        vector<vector<int>> new_grid = current_grid;
                        new_grid[i][j] = 1;
                        long long new_hash = grid_to_hash(new_grid);
                        dp_next[new_hash] += count;
                    }
                }
            }
        }
        dp_current = dp_next;
        dp_next.clear();
    }

    cout << (final_sum % MOD + MOD) % MOD << endl;

    return 0;
}