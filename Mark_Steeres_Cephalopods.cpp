#include <iostream>
#include <unordered_map>
#include <cstdint>
#include <vector>
using namespace std;
const int MOD = 1 << 30;
const long long pow10_arr[9] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
const int adj[9][4] = {
    {1, 3, -1, -1},
    {0, 2, 4, -1},
    {1, 5, -1, -1},
    {0, 4, 6, -1},
    {1, 3, 5, 7},
    {2, 4, 8, -1},
    {3, 7, -1, -1},
    {4, 6, 8, -1},
    {5, 7, -1, -1}
};
inline int getDigit(uint64_t rep, int pos) {
    return (rep >> (4 * pos)) & 0xF;
}
inline uint64_t setDigit(uint64_t rep, int pos, int d) {
    return (rep & ~(0xFULL << (4 * pos))) | ((uint64_t)d << (4 * pos));
}
inline bool isFull(uint64_t rep) {
    for (int i = 0; i < 9; ++i)
        if (getDigit(rep, i) == 0)
            return false;
    return true;
}
struct State {
    uint64_t rep;
    long long hash;
    bool operator==(const State &other) const {
        return rep == other.rep && hash == other.hash;
    }
};
struct StateHash {
    std::size_t operator()(const State &s) const {
        auto h1 = std::hash<uint64_t>()(s.rep);
        auto h2 = std::hash<long long>()(s.hash);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
int main(){
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);
    int depth;
    cin >> depth;
    int grid[3][3];
    uint64_t rep0 = 0;
    long long hash0 = 0;
    for (int i = 0; i < 3; i++){
        for (int j = 0; j < 3; j++){
            cin >> grid[i][j];
            int pos = i * 3 + j;
            rep0 = setDigit(rep0, pos, grid[i][j]);
            hash0 += grid[i][j] * pow10_arr[pos];
        }
    }
    unordered_map<State, long long, StateHash> curr;
    curr[{rep0, hash0}] = 1;
    long long final_sum = 0;
    
    for (int move = 0; move <= depth; ++move) {
        unordered_map<State, long long, StateHash> next;
        for (auto &entry : curr) {
            State s = entry.first;
            long long cnt = entry.second;
            if (isFull(s.rep) || move == depth) {
                final_sum = (final_sum + (s.hash % MOD + MOD) % MOD * cnt) % MOD;
                continue;
            }
            int digits[9];
            for (int i = 0; i < 9; ++i)
                digits[i] = getDigit(s.rep, i);
            for (int pos = 0; pos < 9; ++pos) {
                if (digits[pos] != 0) continue;
                int adj_positions[4];
                int adjCount = 0;
                for (int d = 0; d < 4; d++){
                    int p = adj[pos][d];
                    if (p == -1) break;
                    if (digits[p] > 0)
                        adj_positions[adjCount++] = p;
                }
                if (adjCount < 2) {
                    uint64_t newRep = setDigit(s.rep, pos, 1);
                    long long newHash = s.hash + pow10_arr[pos];
                    State ns = {newRep, newHash};
                    next[ns] = (next[ns] + cnt) % MOD;
                } else {
                    bool valid = false;
                    for (int mask = 0; mask < (1 << adjCount); mask++){
                        if (__builtin_popcount(mask) < 2)
                            continue;
                        int sum = 0;
                        uint64_t newRep = s.rep;
                        long long newHash = s.hash;
                        bool over = false;
                        for (int i = 0; i < adjCount; i++){
                            if (mask & (1 << i)){
                                int p = adj_positions[i];
                                sum += digits[p];
                                newRep = setDigit(newRep, p, 0);
                                newHash -= digits[p] * pow10_arr[p];
                                if (sum > 6) { over = true; break; }
                            }
                        }
                        if (over) continue;
                        newRep = setDigit(newRep, pos, sum);
                        newHash += sum * pow10_arr[pos];
                        State ns = {newRep, newHash};
                        next[ns] = (next[ns] + cnt) % MOD;
                        valid = true;
                    }
                    if (!valid) {
                        uint64_t newRep = setDigit(s.rep, pos, 1);
                        long long newHash = s.hash + pow10_arr[pos];
                        State ns = {newRep, newHash};
                        next[ns] = (next[ns] + cnt) % MOD;
                    }
                }
            }
        }
        curr = std::move(next);
    }
    cout << (final_sum % MOD + MOD) % MOD << "\n";
    return 0;
}
