#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MOD (1 << 30)
#define MOD_MASK (MOD - 1)
static const uint32_t pow10_arr[9] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};

uint32_t cell_hash_contrib[9][7];

const uint8_t validMasks3[4] = {0b011, 0b101, 0b110, 0b111};
const uint8_t validMasks4[11] = {
    0b0011, 0b0101, 0b0110, 0b1001, 0b1010, 0b1100,
    0b0111, 0b1011, 0b1101, 0b1110, 0b1111
};

typedef struct {
    uint32_t packed;
    int cnt;
} State;

typedef struct {
    uint32_t *keys;
    int *values;
    int *indices;
    int count;
    int max_count;
} FastHashTable;

void FastHashTable_init(FastHashTable *table) {
    const int SIZE = 1 << 20;
    table->keys = (uint32_t*)aligned_alloc(64, SIZE * sizeof(uint32_t));
    table->values = (int*)aligned_alloc(64, SIZE * sizeof(int));
    table->max_count = 1 << 20;
    table->indices = (int*)aligned_alloc(64, table->max_count * sizeof(int));
    for (int i = 0; i < SIZE; ++i) {
        table->keys[i] = UINT32_MAX;
        table->values[i] = 0;
    }
    table->count = 0;
}

void FastHashTable_free(FastHashTable *table) {
    free(table->keys);
    free(table->values);
    free(table->indices);
}

void FastHashTable_clear(FastHashTable *table) {
    for (int i = 0; i < table->count; ++i) {
        int idx = table->indices[i];
        table->keys[idx] = UINT32_MAX;
        table->values[idx] = 0;
    }
    table->count = 0;
}

void FastHashTable_insert(FastHashTable *table, uint32_t key, int val) {
    uint32_t h = key * 0x9E3779B1UL;
    uint32_t idx = h & 0xFFFFF; // Mask for 20 bits
    while (1) {
        if (table->keys[idx] == UINT32_MAX) {
            table->keys[idx] = key;
            table->values[idx] = val;
            table->indices[table->count++] = idx;
            return;
        } else if (table->keys[idx] == key) {
            table->values[idx] = (table->values[idx] + val) & MOD_MASK;
            return;
        }
        idx = (idx + 1) & 0xFFFFF;
    }
}

typedef struct {
    State *data;
    int size;
    int capacity;
} StateArray;

void StateArray_init(StateArray *arr) {
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

void StateArray_reserve(StateArray *arr, int new_cap) {
    if (new_cap > arr->capacity) {
        arr->data = (State*)realloc(arr->data, new_cap * sizeof(State));
        arr->capacity = new_cap;
    }
}

void StateArray_clear(StateArray *arr) {
    arr->size = 0;
}

void StateArray_add(StateArray *arr, State s) {
    if (arr->size >= arr->capacity) {
        StateArray_reserve(arr, arr->capacity == 0 ? 1 : arr->capacity * 2);
    }
    arr->data[arr->size++] = s;
}

void StateArray_free(StateArray *arr) {
    free(arr->data);
    arr->data = NULL;
    arr->size = arr->capacity = 0;
}

static inline bool isFull(uint32_t board) {
    for (int pos = 0; pos < 9; pos++) {
        if (((board >> (pos * 3)) & 7) == 0)
            return false;
    }
    return true;
}

static inline uint32_t packed_to_hash(uint32_t board) {
    uint32_t hash = 0;
    for (int pos = 0; pos < 9; pos++) {
        hash += cell_hash_contrib[pos][(board >> (pos * 3)) & 7];
    }
    return hash & MOD_MASK;
}

#define PROCESS_CELL(table_ptr, cell, ncount, nb1, nb2, nb3, nb4) do { \
    const int cellShift = (cell) * 3; \
    if (((board >> cellShift) & 7) == 0) { \
        int filled = 0; \
        int fvals[4]; \
        int fshifts[4]; \
        if ((ncount) >= 1) { \
            int v = (board >> (nb1 * 3)) & 7; \
            if (v) { fvals[filled] = v; fshifts[filled] = nb1 * 3; filled++; } \
        } \
        if ((ncount) >= 2) { \
            int v = (board >> (nb2 * 3)) & 7; \
            if (v) { fvals[filled] = v; fshifts[filled] = nb2 * 3; filled++; } \
        } \
        if ((ncount) >= 3) { \
            int v = (board >> (nb3 * 3)) & 7; \
            if (v) { fvals[filled] = v; fshifts[filled] = nb3 * 3; filled++; } \
        } \
        if ((ncount) >= 4) { \
            int v = (board >> (nb4 * 3)) & 7; \
            if (v) { fvals[filled] = v; fshifts[filled] = nb4 * 3; filled++; } \
        } \
        if (filled < 2) { \
            uint32_t nb = board | (1U << cellShift); \
            FastHashTable_insert(table_ptr, nb, ways); \
        } else if (filled == 2) { \
            int sum = fvals[0] + fvals[1]; \
            if (sum <= 6) { \
                uint32_t nb = board; \
                nb &= ~(0x7U << fshifts[0]); \
                nb &= ~(0x7U << fshifts[1]); \
                nb |= (sum << cellShift); \
                FastHashTable_insert(table_ptr, nb, ways); \
            } else { \
                uint32_t nb = board | (1U << cellShift); \
                FastHashTable_insert(table_ptr, nb, ways); \
            } \
        } else if (filled == 3) { \
            bool capApplied = false; \
            for (int m = 0; m < 4; ++m) { \
                uint8_t mask = validMasks3[m]; \
                int sum = 0; \
                uint32_t nb = board; \
                if (mask & 1) { sum += fvals[0]; nb &= ~(0x7U << fshifts[0]); } \
                if (mask & 2) { sum += fvals[1]; nb &= ~(0x7U << fshifts[1]); } \
                if (mask & 4) { sum += fvals[2]; nb &= ~(0x7U << fshifts[2]); } \
                if (sum <= 6) { \
                    nb |= (sum << cellShift); \
                    FastHashTable_insert(table_ptr, nb, ways); \
                    capApplied = true; \
                } \
            } \
            if (!capApplied) { \
                uint32_t nb = board | (1U << cellShift); \
                FastHashTable_insert(table_ptr, nb, ways); \
            } \
        } else if (filled == 4) { \
            bool capApplied = false; \
            for (int m = 0; m < 11; ++m) { \
                uint8_t mask = validMasks4[m]; \
                int sum = 0; \
                uint32_t nb = board; \
                if (mask & 1) { sum += fvals[0]; nb &= ~(0x7U << fshifts[0]); } \
                if (mask & 2) { sum += fvals[1]; nb &= ~(0x7U << fshifts[1]); } \
                if (mask & 4) { sum += fvals[2]; nb &= ~(0x7U << fshifts[2]); } \
                if (mask & 8) { sum += fvals[3]; nb &= ~(0x7U << fshifts[3]); } \
                if (sum <= 6) { \
                    nb |= (sum << cellShift); \
                    FastHashTable_insert(table_ptr, nb, ways); \
                    capApplied = true; \
                } \
            } \
            if (!capApplied) { \
                uint32_t nb = board | (1U << cellShift); \
                FastHashTable_insert(table_ptr, nb, ways); \
            } \
        } \
    } \
} while(0)

int main() {
    for (int pos = 0; pos < 9; ++pos) {
        for (int val = 0; val < 7; ++val) {
            cell_hash_contrib[pos][val] = val * pow10_arr[pos];
        }
    }

    int depth;
    scanf("%d", &depth);
    uint32_t init = 0;
    for (int pos = 0; pos < 9; ++pos) {
        int val;
        scanf("%d", &val);
        init |= (val & 7) << (pos * 3);
    }

    StateArray curr, next;
    StateArray_init(&curr);
    StateArray_init(&next);
    StateArray_reserve(&curr, 1 << 20);
    StateArray_add(&curr, (State){init, 1});

    FastHashTable table;
    FastHashTable_init(&table);
    uint32_t finalSum = 0;

    for (int move = 0; move <= depth; ++move) {
        FastHashTable_clear(&table);
        for (int i = 0; i < curr.size; ++i) {
            State s = curr.data[i];
            uint32_t board = s.packed;
            int ways = s.cnt;
            if (isFull(board) || move == depth) {
                finalSum = (finalSum + (uint64_t)packed_to_hash(board) * ways) % MOD;
                continue;
            }
            PROCESS_CELL(&table, 0, 2, 1, 3, 0, 0);
            PROCESS_CELL(&table, 1, 3, 0, 2, 4, 0);
            PROCESS_CELL(&table, 2, 2, 1, 5, 0, 0);
            PROCESS_CELL(&table, 3, 3, 0, 4, 6, 0);
            PROCESS_CELL(&table, 4, 4, 1, 3, 5, 7);
            PROCESS_CELL(&table, 5, 3, 2, 4, 8, 0);
            PROCESS_CELL(&table, 6, 2, 3, 7, 0, 0);
            PROCESS_CELL(&table, 7, 3, 4, 6, 8, 0);
            PROCESS_CELL(&table, 8, 2, 5, 7, 0, 0);
        }
        StateArray_clear(&next);
        for (int i = 0; i < table.count; ++i) {
            int idx = table.indices[i];
            uint32_t key = table.keys[idx];
            int val = table.values[idx];
            if (val) {
                StateArray_add(&next, (State){key, val});
            }
        }
        StateArray tmp = curr;
        curr = next;
        next = tmp;
    }

    printf("%d\n", finalSum % MOD);

    StateArray_free(&curr);
    StateArray_free(&next);
    FastHashTable_free(&table);

    return 0;
}