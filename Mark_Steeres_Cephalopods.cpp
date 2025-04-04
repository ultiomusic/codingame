#include <iostream>
#include <vector>
#include <unordered_map>
using namespace std;

const int MOD = 1 << 30;
const long long pow10[9] = {100000000LL, 10000000LL, 1000000LL, 100000LL, 10000LL, 1000LL, 100LL, 10LL, 1LL};
const int adjacency[9][4] = {
    {1, 3, -1, -1}, {0, 2, 4, -1}, {1, 5, -1, -1},
    {0, 4, 6, -1}, {1, 3, 5, 7}, {2, 4, 8, -1},
    {3, 7, -1, -1}, {4, 6, 8, -1}, {5, 7, -1, -1}
};

const int masks_2[] = {3};
const int masks_3[] = {3,5,6,7};
const int masks_4[] = {3,5,6,7,9,10,11,12,13,14,15};

struct State {
    long long h;
    long long cnt;
};

inline bool is_full(long long h) {
    for (int i = 0; i < 9; ++i) {
        if ((h / pow10[i]) % 10 == 0) return false;
    }
    return true;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    int depth;
    cin >> depth;
    int grid[3][3];
    long long initial_h = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            cin >> grid[i][j];
            int pos = i * 3 + j;
            initial_h += grid[i][j] * pow10[pos];
        }
    }
    
    vector<State> curr;
    curr.push_back({initial_h, 1});
    long long final_sum = 0;
    
    for (int move = 0; move <= depth; ++move) {
        unordered_map<long long, long long> next_map;
        if (move < depth) next_map.reserve(curr.size() * 5);
        
        for (const State &st : curr) {
            long long h = st.h;
            long long cnt = st.cnt;
            
            if (move == depth || is_full(h)) {
                final_sum = (final_sum + (h % MOD) * cnt) % MOD;
                continue;
            }
            
            int digits[9];
            for (int i = 0; i < 9; ++i) {
                digits[i] = (h / pow10[i]) % 10;
            }
            
            for (int pos = 0; pos < 9; ++pos) {
                if (digits[pos] != 0) continue;
                
                int adjPos[4];
                int adjCount = 0;
                for (int d = 0; d < 4; ++d) {
                    int p = adjacency[pos][d];
                    if (p == -1) break;
                    if (digits[p] > 0) {
                        adjPos[adjCount++] = p;
                    }
                }
                
                if (adjCount < 2) {
                    long long new_h = h + pow10[pos];
                    next_map[new_h] = (next_map[new_h] + cnt) % MOD;
                } else {
                    const int* masks = nullptr;
                    int masks_size = 0;
                    switch(adjCount) {
                        case 2: masks = masks_2; masks_size = sizeof(masks_2)/sizeof(int); break;
                        case 3: masks = masks_3; masks_size = sizeof(masks_3)/sizeof(int); break;
                        case 4: masks = masks_4; masks_size = sizeof(masks_4)/sizeof(int); break;
                    }
                    
                    bool any_valid = false;
                    for (int i = 0; i < masks_size; ++i) {
                        int mask = masks[i];
                        int sum = 0;
                        long long new_h = h;
                        for (int j = 0; j < adjCount; ++j) {
                            if (mask & (1 << j)) {
                                int p = adjPos[j];
                                int val = (new_h / pow10[p]) % 10;
                                sum += val;
                                new_h -= val * pow10[p];
                                if (sum > 6) break;
                            }
                        }
                        if (sum > 6 || sum == 0) continue;
                        new_h += sum * pow10[pos];
                        next_map[new_h] = (next_map[new_h] + cnt) % MOD;
                        any_valid = true;
                    }
                    if (!any_valid) {
                        long long new_h = h + pow10[pos];
                        next_map[new_h] = (next_map[new_h] + cnt) % MOD;
                    }
                }
            }
        }
        
        if (move < depth) {
            curr.clear();
            curr.reserve(next_map.size());
            for (auto &entry : next_map) {
                curr.push_back({entry.first, entry.second});
            }
        }
    }
    
    cout << (final_sum % MOD + MOD) % MOD << "\n";
    return 0;
}