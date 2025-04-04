#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD (1 << 30)
const long long pow10[9] = {100000000LL, 10000000LL, 1000000LL, 100000LL, 10000LL, 1000LL, 100LL, 10LL, 1LL};
const int adjacency[9][4] = {
    {1, 3, -1, -1}, {0, 2, 4, -1}, {1, 5, -1, -1},
    {0, 4, 6, -1}, {1, 3, 5, 7}, {2, 4, 8, -1},
    {3, 7, -1, -1}, {4, 6, 8, -1}, {5, 7, -1, -1}
};

const int masks_2[] = {3};
const int masks_3[] = {3,5,6,7};
const int masks_4[] = {3,5,6,7,9,10,11,12,13,14,15};

typedef struct {
    long long h;
    long long cnt;
} State;

int is_full(long long h) {
    for(int i = 0; i < 9; i++) {
        if((h / pow10[i]) % 10 == 0) return 0;
    }
    return 1;
}

int cmp_state(const void *a, const void *b) {
    return ((State*)a)->h < ((State*)b)->h ? -1 : 1;
}

int main() {
    int depth;
    scanf("%d", &depth);
    int grid[3][3];
    long long initial_h = 0;
    
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            scanf("%d", &grid[i][j]);
            int pos = i * 3 + j;
            initial_h += grid[i][j] * pow10[pos];
        }
    }

    State *curr = malloc(sizeof(State));
    curr[0] = (State){initial_h, 1};
    int curr_size = 1;
    long long final_sum = 0;

    for(int move = 0; move <= depth; move++) {
        State *next = NULL;
        int next_size = 0;
        int next_cap = 0;

        for(int i = 0; i < curr_size; i++) {
            long long h = curr[i].h;
            long long cnt = curr[i].cnt;

            if(move == depth || is_full(h)) {
                final_sum = (final_sum + (h % MOD) * cnt) % MOD;
                continue;
            }

            int digits[9];
            for(int j = 0; j < 9; j++) {
                digits[j] = (h / pow10[j]) % 10;
            }

            for(int pos = 0; pos < 9; pos++) {
                if(digits[pos] != 0) continue;

                int adjPos[4], adjCount = 0;
                for(int d = 0; d < 4; d++) {
                    int p = adjacency[pos][d];
                    if(p == -1) break;
                    if(digits[p] > 0) adjPos[adjCount++] = p;
                }

                if(adjCount < 2) {
                    long long new_h = h + pow10[pos];
                    if(next_size >= next_cap) {
                        next_cap = next_cap ? next_cap*2 : 16;
                        next = realloc(next, next_cap * sizeof(State));
                    }
                    next[next_size++] = (State){new_h, cnt};
                } else {
                    const int *masks = NULL;
                    int masks_size = 0;
                    switch(adjCount) {
                        case 2: masks = masks_2; masks_size = sizeof(masks_2)/sizeof(int); break;
                        case 3: masks = masks_3; masks_size = sizeof(masks_3)/sizeof(int); break;
                        case 4: masks = masks_4; masks_size = sizeof(masks_4)/sizeof(int); break;
                    }

                    int any_valid = 0;
                    for(int m = 0; m < masks_size; m++) {
                        int mask = masks[m];
                        int sum = 0;
                        long long new_h = h;
                        
                        for(int j = 0; j < adjCount; j++) {
                            if(mask & (1 << j)) {
                                int p = adjPos[j];
                                int val = (new_h / pow10[p]) % 10;
                                sum += val;
                                new_h -= val * pow10[p];
                                if(sum > 6) break;
                            }
                        }
                        if(sum > 6 || sum == 0) continue;
                        new_h += sum * pow10[pos];
                        any_valid = 1;
                        
                        if(next_size >= next_cap) {
                            next_cap = next_cap ? next_cap*2 : 16;
                            next = realloc(next, next_cap * sizeof(State));
                        }
                        next[next_size++] = (State){new_h, cnt};
                    }
                    if(!any_valid) {
                        long long new_h = h + pow10[pos];
                        if(next_size >= next_cap) {
                            next_cap = next_cap ? next_cap*2 : 16;
                            next = realloc(next, next_cap * sizeof(State));
                        }
                        next[next_size++] = (State){new_h, cnt};
                    }
                }
            }
        }

        if(move < depth) {
            qsort(next, next_size, sizeof(State), cmp_state);
            int merged_size = 0;
            for(int i = 0; i < next_size; i++) {
                if(merged_size > 0 && next[i].h == curr[merged_size-1].h) {
                    curr[merged_size-1].cnt = (curr[merged_size-1].cnt + next[i].cnt) % MOD;
                } else {
                    if(merged_size >= curr_size) {
                        curr_size = curr_size ? curr_size*2 : 16;
                        curr = realloc(curr, curr_size * sizeof(State));
                    }
                    curr[merged_size++] = next[i];
                }
            }
            free(next);
            curr_size = merged_size;
        } else {
            free(next);
        }
    }

    free(curr);
    printf("%lld\n", (final_sum % MOD + MOD) % MOD);
    return 0;
}