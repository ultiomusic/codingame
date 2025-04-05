#include <bits/stdc++.h>
using namespace std;
constexpr int MOD = 1 << 30;
constexpr int MOD_MASK = MOD - 1;
constexpr uint32_t pow10_arr[9] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
uint32_t cell_hash_contrib[9][7];
constexpr uint8_t adj_count[9] = {2, 3, 2, 3, 4, 3, 2, 3, 2};
constexpr uint8_t adj[9][4] = {
    {1, 3, 255, 255}, {0, 2, 4, 255}, {1, 5, 255, 255}, {0, 4, 6, 255}, {1, 3, 5, 7}, {2, 4, 8, 255}, {3, 7, 255, 255}, {4, 6, 8, 255}, {5, 7, 255, 255}};
constexpr uint8_t validMasks3[4] = {0b011, 0b101, 0b110, 0b111};
constexpr uint8_t validMasks4[11] = {0b0011, 0b0101, 0b0110, 0b1001, 0b1010, 0b1100, 0b0111, 0b1011, 0b1101, 0b1110, 0b1111};

struct State {
    uint32_t packed;
    int cnt;
};

class FastHashTable {
private:
    static constexpr int BITS = 20;
    static constexpr int SIZE = 1 << BITS;
    static constexpr int MASK = SIZE - 1;
    uint32_t* keys;
    int* values;
    int* indices;
    int count;
    int max_count;

public:
    FastHashTable() {
        keys = (uint32_t*)aligned_alloc(64, SIZE * sizeof(uint32_t));
        values = (int*)aligned_alloc(64, SIZE * sizeof(int));
        max_count = 1 << 20;  // Increased to handle up to 1 million entries
        indices = (int*)aligned_alloc(64, max_count * sizeof(int));
        memset(keys, 0xFF, SIZE * sizeof(uint32_t));
        memset(values, 0, SIZE * sizeof(int));
        count = 0;
    }

    ~FastHashTable() {
        free(keys);
        free(values);
        free(indices);
    }

    inline void clear() {
        for (int i = 0; i < count; i++) {
            int idx = indices[i];
            keys[idx] = UINT32_MAX;
            values[idx] = 0;
        }
        count = 0;
    }

    inline void insert(uint32_t key, int val) {
        uint32_t h = key * 0x9E3779B1UL;
        uint32_t idx = h & MASK;
        while (true) {
            if (keys[idx] == UINT32_MAX) {
                keys[idx] = key;
                values[idx] = val;
                if (count < max_count) indices[count++] = idx;
                return;
            } else if (keys[idx] == key) {
                values[idx] = (values[idx] + val) & MOD_MASK;
                return;
            }
            idx = (idx + 1) & MASK;
        }
    }

    inline void gather(vector<State>& output) {
        output.clear();
        output.reserve(count);
        for (int i = 0; i < count; i++) {
            int idx = indices[i];
            int val = values[idx];
            if (val != 0) {
                output.push_back({keys[idx], val});
            }
        }
    }
};

namespace game {
inline bool is_empty(uint32_t packed, int pos) {
    return ((packed >> (pos * 3)) & 0x7) == 0;
}
}

inline uint32_t packed_to_hash(uint32_t packed) {
    uint32_t hash = 0;
    hash += cell_hash_contrib[0][(packed >> 0) & 0x7];
    hash += cell_hash_contrib[1][(packed >> 3) & 0x7];
    hash += cell_hash_contrib[2][(packed >> 6) & 0x7];
    hash += cell_hash_contrib[3][(packed >> 9) & 0x7];
    hash += cell_hash_contrib[4][(packed >> 12) & 0x7];
    hash += cell_hash_contrib[5][(packed >> 15) & 0x7];
    hash += cell_hash_contrib[6][(packed >> 18) & 0x7];
    hash += cell_hash_contrib[7][(packed >> 21) & 0x7];
    hash += cell_hash_contrib[8][(packed >> 24) & 0x7];
    return hash & MOD_MASK;
}

inline bool isFull(uint32_t packed) {
    for (int pos = 0; pos < 9; ++pos) {
        if (((packed >> (pos * 3)) & 0x7) == 0) {
            return false;
        }
    }
    return true;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);
    for (int pos = 0; pos < 9; pos++) {
        for (int val = 0; val <= 6; val++) {
            cell_hash_contrib[pos][val] = val * pow10_arr[pos];
        }
    }
    int depth;
    cin >> depth;
    uint32_t init_packed = 0;
    for (int pos = 0; pos < 9; pos++) {
        int val;
        cin >> val;
        init_packed |= (val & 0x7) << (pos * 3);
    }
    vector<State> curr, next;
    curr.reserve(1 << 20);
    next.reserve(1 << 20);
    curr.push_back({init_packed, 1});
    FastHashTable hashTable;
    int finalSum = 0;
    for (int move = 0; move <= depth; move++) {
        hashTable.clear();
        for (const State& s : curr) {
            uint32_t packed = s.packed;
            int cnt = s.cnt;
            if (isFull(packed) || move == depth) {
                finalSum = (finalSum + packed_to_hash(packed) * (uint64_t)cnt) & MOD_MASK;
                continue;
            }
            for (int pos = 0; pos < 9; pos++) {
                if (game::is_empty(packed, pos)) {
                    uint8_t adj_pos[4];
                    uint8_t adj_val[4];
                    int adj_filled = 0;
                    for (int i = 0; i < adj_count[pos]; i++) {
                        int nb = adj[pos][i];
                        int val = (packed >> (nb * 3)) & 0x7;
                        if (val != 0) {
                            adj_pos[adj_filled] = nb;
                            adj_val[adj_filled] = val;
                            adj_filled++;
                        }
                    }
                    if (adj_filled < 2) {
                        uint32_t new_packed = packed | (1 << (pos * 3));
                        hashTable.insert(new_packed, cnt);
                        continue;
                    }
                    bool valid_combination = false;
                    if (adj_filled == 2) {
                        int sum = adj_val[0] + adj_val[1];
                        if (sum <= 6) {
                            uint32_t new_packed = packed;
                            new_packed &= ~(0x7U << (adj_pos[0] * 3));
                            new_packed &= ~(0x7U << (adj_pos[1] * 3));
                            new_packed |= (sum << (pos * 3));
                            hashTable.insert(new_packed, cnt);
                            valid_combination = true;
                        }
                    } else if (adj_filled == 3) {
                        for (int m = 0; m < 4; m++) {
                            uint8_t mask = validMasks3[m];
                            int sum = 0;
                            uint32_t new_packed = packed;
                            if (mask & 1) {
                                sum += adj_val[0];
                                new_packed &= ~(0x7U << (adj_pos[0] * 3));
                            }
                            if (mask & 2) {
                                sum += adj_val[1];
                                new_packed &= ~(0x7U << (adj_pos[1] * 3));
                            }
                            if (mask & 4) {
                                sum += adj_val[2];
                                new_packed &= ~(0x7U << (adj_pos[2] * 3));
                            }
                            if (sum <= 6) {
                                new_packed |= (sum << (pos * 3));
                                hashTable.insert(new_packed, cnt);
                                valid_combination = true;
                            }
                        }
                    } else if (adj_filled == 4) {
                        for (int m = 0; m < 11; m++) {
                            uint8_t mask = validMasks4[m];
                            int sum = 0;
                            uint32_t new_packed = packed;
                            if (mask & 1) {
                                sum += adj_val[0];
                                new_packed &= ~(0x7U << (adj_pos[0] * 3));
                            }
                            if (mask & 2) {
                                sum += adj_val[1];
                                new_packed &= ~(0x7U << (adj_pos[1] * 3));
                            }
                            if (mask & 4) {
                                sum += adj_val[2];
                                new_packed &= ~(0x7U << (adj_pos[2] * 3));
                            }
                            if (mask & 8) {
                                sum += adj_val[3];
                                new_packed &= ~(0x7U << (adj_pos[3] * 3));
                            }
                            if (sum <= 6) {
                                new_packed |= (sum << (pos * 3));
                                hashTable.insert(new_packed, cnt);
                                valid_combination = true;
                            }
                        }
                    }
                    if (!valid_combination) {
                        uint32_t new_packed = packed | (1 << (pos * 3));
                        hashTable.insert(new_packed, cnt);
                    }
                }
            }
        }
        hashTable.gather(next);
        swap(curr, next);
    }
    cout << finalSum % MOD << "\n";
    return 0;
}