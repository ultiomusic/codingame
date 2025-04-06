#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>

#define MOD (1 << 30)
#define MOD_MASK (MOD - 1)

static const uint32_t pow10_arr[9] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
uint32_t cell_hash_contrib[9][7];

static const uint8_t validMasks3[4] = {0x3, 0x5, 0x6, 0x7};
static const uint8_t validMasks4[11] = {0x3, 0x5, 0x6, 0x9, 0xA, 0xC, 0x7, 0xB, 0xD, 0xE, 0xF};

static const int cellShift[9] = {0, 3, 6, 9, 12, 15, 18, 21, 24};
static const uint32_t cellMask[9] = {
    7U << 0, 7U << 3, 7U << 6, 7U << 9, 7U << 12,
    7U << 15, 7U << 18, 7U << 21, 7U << 24
};

typedef struct {
    uint32_t packed;
    int cnt;
} State;

typedef struct {
    State *data;
    int size;
    int capacity;
} StateArray;

static inline void StateArray_init(StateArray *arr, int capacity) {
    arr->data = (State*)malloc(capacity * sizeof(State));
    arr->size = 0;
    arr->capacity = capacity;
}

static inline void StateArray_clear(StateArray *arr) {
    arr->size = 0;
}

static inline void StateArray_add(StateArray *arr, State s) {
    arr->data[arr->size++] = s;
}

static inline void StateArray_free(StateArray *arr) {
    free(arr->data);
    arr->data = NULL;
    arr->size = arr->capacity = 0;
}

static inline uint32_t packed_to_hash(uint32_t board) {
    return (cell_hash_contrib[0][ (board >> cellShift[0]) & 7 ] +
            cell_hash_contrib[1][ (board >> cellShift[1]) & 7 ] +
            cell_hash_contrib[2][ (board >> cellShift[2]) & 7 ] +
            cell_hash_contrib[3][ (board >> cellShift[3]) & 7 ] +
            cell_hash_contrib[4][ (board >> cellShift[4]) & 7 ] +
            cell_hash_contrib[5][ (board >> cellShift[5]) & 7 ] +
            cell_hash_contrib[6][ (board >> cellShift[6]) & 7 ] +
            cell_hash_contrib[7][ (board >> cellShift[7]) & 7 ] +
            cell_hash_contrib[8][ (board >> cellShift[8]) & 7 ]) & MOD_MASK;
}

static inline bool isFull(uint32_t board) {
    return (((board & cellMask[0]) != 0) &&
            ((board & cellMask[1]) != 0) &&
            ((board & cellMask[2]) != 0) &&
            ((board & cellMask[3]) != 0) &&
            ((board & cellMask[4]) != 0) &&
            ((board & cellMask[5]) != 0) &&
            ((board & cellMask[6]) != 0) &&
            ((board & cellMask[7]) != 0) &&
            ((board & cellMask[8]) != 0));
}

#define PROCESS_CELL(cell, ncount, nb1, nb2, nb3, nb4) do {                \
    const int cs = cellShift[(cell)];                                       \
    if(c[(cell)] == 0) {                                                    \
        int filled = 0, fvals[4], fshifts[4];                               \
        if(ncount >= 1 && c[(nb1)]) { fvals[filled] = c[(nb1)];              \
            fshifts[filled] = cellShift[(nb1)]; filled++; }                  \
        if(ncount >= 2 && c[(nb2)]) { fvals[filled] = c[(nb2)];              \
            fshifts[filled] = cellShift[(nb2)]; filled++; }                  \
        if(ncount >= 3 && c[(nb3)]) { fvals[filled] = c[(nb3)];              \
            fshifts[filled] = cellShift[(nb3)]; filled++; }                  \
        if(ncount >= 4 && c[(nb4)]) { fvals[filled] = c[(nb4)];              \
            fshifts[filled] = cellShift[(nb4)]; filled++; }                  \
        if(filled < 2) {                                                    \
            new_board = board | (1U << cs);                                 \
            nextStates[nextCount++] = (State){new_board, ways};             \
        } else if(filled == 2) {                                              \
            int sum = fvals[0] + fvals[1];                                   \
            if(sum <= 6) {                                                  \
                new_board = board;                                          \
                new_board &= ~(0x7U << fshifts[0]);                         \
                new_board &= ~(0x7U << fshifts[1]);                         \
                new_board |= (sum << cs);                                   \
                nextStates[nextCount++] = (State){new_board, ways};         \
            } else {                                                        \
                new_board = board | (1U << cs);                             \
                nextStates[nextCount++] = (State){new_board, ways};         \
            }                                                               \
        } else if(filled == 3) {                                              \
            bool capApplied = false;                                          \
            for(int m = 0; m < 4; m++) {                                      \
                uint8_t mask = validMasks3[m];                              \
                int sum = 0;                                                \
                new_board = board;                                            \
                if(mask & 1) { sum += fvals[0]; new_board &= ~(0x7U << fshifts[0]); } \
                if(mask & 2) { sum += fvals[1]; new_board &= ~(0x7U << fshifts[1]); } \
                if(mask & 4) { sum += fvals[2]; new_board &= ~(0x7U << fshifts[2]); } \
                if(sum <= 6) {                                              \
                    new_board |= (sum << cs);                               \
                    nextStates[nextCount++] = (State){new_board, ways};     \
                    capApplied = true;                                      \
                }                                                           \
            }                                                               \
            if(!capApplied) {                                               \
                new_board = board | (1U << cs);                             \
                nextStates[nextCount++] = (State){new_board, ways};         \
            }                                                               \
        } else {                                            \
            bool capApplied = false;                                          \
            for(int m = 0; m < 11; m++) {                                     \
                uint8_t mask = validMasks4[m];                              \
                int sum = 0;                                                \
                new_board = board;                                            \
                if(mask & 1) { sum += fvals[0]; new_board &= ~(0x7U << fshifts[0]); } \
                if(mask & 2) { sum += fvals[1]; new_board &= ~(0x7U << fshifts[1]); } \
                if(mask & 4) { sum += fvals[2]; new_board &= ~(0x7U << fshifts[2]); } \
                if(mask & 8) { sum += fvals[3]; new_board &= ~(0x7U << fshifts[3]); } \
                if(sum <= 6) {                                              \
                    new_board |= (sum << cs);                               \
                    nextStates[nextCount++] = (State){new_board, ways};     \
                    capApplied = true;                                      \
                }                                                           \
            }                                                               \
            if(!capApplied) {                                               \
                new_board = board | (1U << cs);                             \
                nextStates[nextCount++] = (State){new_board, ways};         \
            }                                                               \
        }                                                                   \
    }                                                                       \
} while(0)
static void radix_sort_states(State *arr, int n) {
    State *aux = (State*)malloc(n * sizeof(State));
    int count[1 << 16] = {0};
    for (int i = 0; i < n; i++) {
        count[arr[i].packed & 0xFFFF]++;
    }
    for (int i = 1; i < (1 << 16); i++) {
        count[i] += count[i - 1];
    }
    for (int i = n - 1; i >= 0; i--) {
        int pos = arr[i].packed & 0xFFFF;
        aux[--count[pos]] = arr[i];
    }
    memset(count, 0, sizeof(count));
    for (int i = 0; i < n; i++) {
        count[(aux[i].packed >> 16) & 0xFFFF]++;
    }
    for (int i = 1; i < (1 << 16); i++) {
        count[i] += count[i - 1];
    }
    for (int i = n - 1; i >= 0; i--) {
        int pos = (aux[i].packed >> 16) & 0xFFFF;
        arr[--count[pos]] = aux[i];
    }
    free(aux);
}

static int merge_states(State *arr, int n) {
    if(n == 0) return 0;
    int j = 0;
    for (int i = 1; i < n; i++) {
        if(arr[i].packed == arr[j].packed) {
            arr[j].cnt = (arr[j].cnt + arr[i].cnt) & MOD_MASK;
        } else {
            arr[++j] = arr[i];
        }
    }
    return j + 1;
}

int main(){
    int pos, val;

    for(pos = 0; pos < 9; pos++){
        for(val = 0; val < 7; val++){
            cell_hash_contrib[pos][val] = val * pow10_arr[pos];
        }
    }
    
    int depth;
    if(scanf("%d", &depth) != 1) return 1;
    uint32_t init = 0;
    for(pos = 0; pos < 9; pos++){
        if(scanf("%d", &val) != 1) return 1;
        init |= ((uint32_t)(val & 7)) << cellShift[pos];
    }
    const int STATE_CAP = 1 << 21;
    StateArray curr, next;
    StateArray_init(&curr, STATE_CAP);
    StateArray_init(&next, STATE_CAP);
    StateArray_add(&curr, (State){init, 1});
    
    uint32_t finalSum = 0;
    
    State *nextStates = (State*)malloc(STATE_CAP * sizeof(State));
    
    for (int move = 0; move <= depth; move++){
        int nextCount = 0;
        State *p = curr.data;
        State *pend = curr.data + curr.size;
        for (; p < pend; p++){
            uint32_t board = p->packed;
            int ways = p->cnt;
            if(isFull(board) || move == depth) {
                finalSum = (finalSum + (uint64_t)packed_to_hash(board) * ways) & MOD_MASK;
                continue;
            }
            int c0 = (int)((board >> cellShift[0]) & 7);
            int c1 = (int)((board >> cellShift[1]) & 7);
            int c2 = (int)((board >> cellShift[2]) & 7);
            int c3 = (int)((board >> cellShift[3]) & 7);
            int c4 = (int)((board >> cellShift[4]) & 7);
            int c5 = (int)((board >> cellShift[5]) & 7);
            int c6 = (int)((board >> cellShift[6]) & 7);
            int c7 = (int)((board >> cellShift[7]) & 7);
            int c8 = (int)((board >> cellShift[8]) & 7);
            int c[9] = {c0, c1, c2, c3, c4, c5, c6, c7, c8};
            
            uint32_t new_board;
            PROCESS_CELL(0, 2, 1, 3, 0, 0);
            PROCESS_CELL(1, 3, 0, 2, 4, 0);
            PROCESS_CELL(2, 2, 1, 5, 0, 0);
            PROCESS_CELL(3, 3, 0, 4, 6, 0);
            PROCESS_CELL(4, 4, 1, 3, 5, 7);
            PROCESS_CELL(5, 3, 2, 4, 8, 0);
            PROCESS_CELL(6, 2, 3, 7, 0, 0);
            PROCESS_CELL(7, 3, 4, 6, 8, 0);
            PROCESS_CELL(8, 2, 5, 7, 0, 0);
        }
        radix_sort_states(nextStates, nextCount);
        int mergedCount = merge_states(nextStates, nextCount);
        
        StateArray_clear(&curr);
        for (int i = 0; i < mergedCount; i++){
            StateArray_add(&curr, nextStates[i]);
        }
    }
    
    printf("%d\n", finalSum & MOD_MASK);
    free(nextStates);
    StateArray_free(&curr);
    StateArray_free(&next);
    
    return 0;
}
