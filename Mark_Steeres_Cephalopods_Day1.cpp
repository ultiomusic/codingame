#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>

using namespace std;

const int MOD = 1 << 30;
const int GRID_SIZE = 3;
const long long pow10[3][3] = {
    {100000000LL, 10000000LL, 1000000LL},
    {100000LL, 10000LL, 1000LL},
    {100LL, 10LL, 1LL}
};

long long grid_to_hash(const vector<vector<int>>& grid) {
    long long hash = 0;
    for (int i = 0; i < GRID_SIZE; ++i) {
        for (int j = 0; j < GRID_SIZE; ++j) {
            hash += grid[i][j] * pow10[i][j];
        }
    }
    return hash;
}

bool is_full(long long hash) {
    for (int i = 0; i < 9; ++i) {
        if (hash % 10 == 0) return false;
        hash /= 10;
    }
    return true;
}

const vector<pair<int, int>> adjacency[3][3] = {
    {{{0,1}, {1,0}}, {{0,0}, {0,2}, {1,1}}, {{0,1}, {1,2}}},
    {{{1,1}, {0,0}, {2,0}}, {{1,0}, {1,2}, {0,1}, {2,1}}, {{1,1}, {0,2}, {2,2}}},
    {{{2,1}, {1,0}}, {{2,0}, {2,2}, {1,1}}, {{2,1}, {1,2}}}
};

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
            if (is_full(board_hash) || move == depth) {
                final_sum = (final_sum + board_hash * count) % MOD;
                continue;
            }

            vector<pair<int, int>> empty_positions;
            long long tmp_hash = board_hash;
            for (int k = 0; k < 9; ++k) {
                int digit = tmp_hash % 10;
                if (digit == 0) {
                    int i = 2 - (k / 3);
                    int j = 2 - (k % 3);
                    empty_positions.emplace_back(i, j);
                }
                tmp_hash /= 10;
            }

            for (const auto& pos : empty_positions) {
                int i = pos.first;
                int j = pos.second;
                const auto& adjacent_cells = adjacency[i][j];
                vector<pair<int, int>> adj_non_zero;
                for (const auto& cell : adjacent_cells) {
                    int x = cell.first;
                    int y = cell.second;
                    long long cell_value = (board_hash / pow10[x][y]) % 10;
                    if (cell_value != 0) {
                        adj_non_zero.emplace_back(x, y);
                    }
                }

                if (adj_non_zero.size() < 2) {
                    long long new_hash = board_hash + pow10[i][j];
                    dp_next[new_hash] += count;
                } else {
                    int n = adj_non_zero.size();
                    bool has_valid_subset = false;
                    for (int mask = 3; mask < (1 << n); ++mask) { // Start from 3 (binary 11) to ensure at least 2 bits set
                        if (__builtin_popcount(mask) < 2) continue;
                        long long subset_sum = 0;
                        long long new_hash = board_hash;
                        bool valid = true;
                        for (int k = 0; k < n; ++k) {
                            if (mask & (1 << k)) {
                                int x = adj_non_zero[k].first;
                                int y = adj_non_zero[k].second;
                                long long digit = (board_hash / pow10[x][y]) % 10;
                                subset_sum += digit;
                                new_hash -= digit * pow10[x][y];
                            }
                        }
                        if (subset_sum > 6) continue;
                        has_valid_subset = true;
                        new_hash += subset_sum * pow10[i][j];
                        dp_next[new_hash] += count;
                    }
                    if (!has_valid_subset) {
                        long long new_hash = board_hash + pow10[i][j];
                        dp_next[new_hash] += count;
                    }
                }
            }
        }
        dp_current.swap(dp_next);
        dp_next.clear();
    }

    cout << (final_sum % MOD + MOD) % MOD << endl;

    return 0;
}