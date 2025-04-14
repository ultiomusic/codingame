#undef _GLIBCXX_DEBUG
#pragma GCC optimize ("Ofast,unroll-loops,omit-frame-pointer,inline")
#pragma GCC option("arch=native", "tune=native", "no-zero-upper")
#pragma GCC target("movbe,aes,pclmul,avx,avx2,f16c,fma,sse3,ssse3,sse4.1,sse4.2,rdrnd,popcnt,bmi,bmi2,lzcnt")

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t State;
typedef uint32_t Count;
typedef Count CountArray[8];

#define GET_DIE_VALUE(state, position) (((state) >> ((position) * 3)) & 7)
#define CLEAR_DIE_VALUE(state, position) ((state) & ~(7 << ((position) * 3)))
#define SET_DIE_VALUE(state, position, value) ((state) | ((value) << ((position) * 3)))
#define IS_POSITION_EMPTY(state, position) ((((state) >> ((position) * 3)) & 7) == 0)

static inline void print_state(const State state) {
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int die_value = GET_DIE_VALUE(state, i * 3 + j);
            printf("%d ", die_value);
        }
        printf("\n");
    }
    printf("\n");
}

static inline int state_hash(const State state) {
    int state_hash_val = 0;
    int i;
    for (i = 0; i < 9; i++) {
        state_hash_val = state_hash_val * 10 + GET_DIE_VALUE(state, i);
    }
    return state_hash_val;
}

static const State neighbors_mask[9] = {
    0b000000000000000111000111000,  // 0
    0b000000000000111000111000111,  // 1
    0b000000000111000000000111000,  // 2
    0b000000111000111000000000111,  // 3
    0b000111000111000111000111000,  // 4
    0b111000000000111000111000000,  // 5
    0b000111000000000111000000000,  // 6
    0b111000111000111000000000000,  // 7
    0b000111000111000000000000000   // 8
};

static const uint8_t symmetric_mult[64] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    1, 0, 3, 2, 6, 7, 4, 5,
    2, 3, 0, 1, 5, 4, 7, 6,
    3, 2, 1, 0, 7, 6, 5, 4,
    4, 5, 6, 7, 0, 1, 2, 3,
    5, 4, 7, 6, 2, 3, 0, 1,
    6, 7, 4, 5, 1, 0, 3, 2,
    7, 6, 5, 4, 3, 2, 1, 0
};

static const uint64_t MOD = 1ULL << 30;

#define MASK_0  (0x7)
#define MASK_1  (0x7 << 3)
#define MASK_2  (0x7 << 6)
#define MASK_3  (0x7 << 9)
#define MASK_4  (0x7 << 12)
#define MASK_5  (0x7 << 15)
#define MASK_6  (0x7 << 18)
#define MASK_7  (0x7 << 21)
#define MASK_8  (0x7 << 24)

static inline int asm_popcnt(uint32_t x) {
    int r;
    __asm__("popcnt %1, %0" : "=r"(r) : "r"(x));
    return r;
}

static inline int asm_ctz(uint32_t x) {
    int r;
    __asm__("bsf %1, %0" : "=r"(r) : "r"(x));
    return r;
}

typedef struct {
    uint32_t max_size;
    uint32_t * __restrict keys;
    uint64_t * __restrict table;
    size_t count;
    uint32_t * __restrict storage;
    uint32_t storage_capacity;
    uint32_t next_storage_index;
} HashTable;

static inline void init_hash_table(HashTable *ht) {
    // Use a power-of-two size for faster modulo operation.
    ht->max_size = 1 << 17;  // 131072 elements
    ht->keys = (uint32_t *)malloc(ht->max_size * sizeof(uint32_t));
    ht->table = (uint64_t *)malloc(ht->max_size * sizeof(uint64_t));
    memset(ht->table, 0, ht->max_size * sizeof(uint64_t));
    ht->count = 0;
    ht->storage_capacity = ht->max_size * 8;
    ht->storage = (uint32_t *)malloc(ht->storage_capacity * sizeof(uint32_t));
    ht->next_storage_index = 0;
}

static inline void free_hash_table(HashTable *ht) {
    free(ht->keys);
    free(ht->table);
    free(ht->storage);
}

static inline uint32_t get_next_storage_index(HashTable *ht) {
    if (ht->next_storage_index + 8 > ht->storage_capacity) {
        ht->storage_capacity *= 2;
        uint32_t *new_storage = (uint32_t *)malloc(ht->storage_capacity * sizeof(uint32_t));
        memcpy(new_storage, ht->storage, ht->next_storage_index * sizeof(uint32_t));
        free(ht->storage);
        ht->storage = new_storage;
    }
    uint32_t index = ht->next_storage_index;
    ht->next_storage_index += 8;
    return index;
}

static inline void hash_table_insert(HashTable *ht, const State new_state, const CountArray value) {
    if (ht->count >= ht->max_size) {
        fprintf(stderr, "Hash table is full\n");
        exit(1);
    }
    uint32_t hash = new_state & (ht->max_size - 1);
    while (ht->table[hash] != 0) {
        uint64_t pair = ht->table[hash];
        State state = (State)(pair & 0xFFFFFFFF);
        if (state == new_state) {
            uint32_t index = (pair >> 32) & 0xFFFFFFFF;
            for (int j = 0; j < 8; j++) {
                ht->storage[index + j] += value[j];
            }
            return;
        }
        hash = (hash + 1) & (ht->max_size - 1);
    }
    ht->keys[ht->count] = hash;
    uint32_t storage_index = get_next_storage_index(ht);
    memcpy(ht->storage + storage_index, value, 8 * sizeof(Count));
    ht->table[hash] = ((uint64_t)new_state) | (((uint64_t)storage_index) << 32);
    ht->count++;
}

static inline void clear_hash_table(HashTable *ht) {
    memset(ht->table, 0, ht->max_size * sizeof(uint64_t));
    ht->count = 0;
    ht->next_storage_index = 0;
}

static inline void swap_hash_table(HashTable *a, HashTable *b) {
    uint32_t *temp_keys = a->keys;
    a->keys = b->keys;
    b->keys = temp_keys;

    uint64_t *temp_table = a->table;
    a->table = b->table;
    b->table = temp_table;

    uint32_t *temp_storage = a->storage;
    a->storage = b->storage;
    b->storage = temp_storage;

    size_t temp_count = a->count;
    a->count = b->count;
    b->count = temp_count;

    uint32_t temp_capacity = a->storage_capacity;
    a->storage_capacity = b->storage_capacity;
    b->storage_capacity = temp_capacity;

    uint32_t temp_next = a->next_storage_index;
    a->next_storage_index = b->next_storage_index;
    b->next_storage_index = temp_next;
}

int max_depth;
int current_depth;
HashTable states_to_process;
HashTable new_states_to_process;
uint32_t final_sum = 0;

static inline State create_state(const char *state_str) {
    if (strlen(state_str) != 9) {
        fprintf(stderr, "Invalid state string length\n");
        return 0;
    }
    State state = 0;
    for (int i = 0; i < 9; i++) {
        state = SET_DIE_VALUE(state, i, state_str[i] - '0');
    }
    return state;
}

#define VERTCAL_FLIP(state) ( \
        (((state) & 0b111111111000000000000000000) >> 18) | \
        (((state) & 0b000000000000000000111111111) << 18) | \
        ((state) & 0b000000000111111111000000000) )

#define HORIZONTAL_FLIP(state) ( \
        (((state) & 0b000000111000000111000000111) << 6) | \
        (((state) & 0b111000000111000000111000000) >> 6) | \
        ((state) & 0b000111000000111000000111000) )

#define DIAGONAL_FLIP(state) ( \
        ((state) & MASK_0) | \
        (((state) & MASK_1) << 6) | \
        (((state) & MASK_2) << 12) | \
        (((state) & MASK_3) >> 6) | \
        ((state) & MASK_4) | \
        (((state) & MASK_5) << 6) | \
        (((state) & MASK_6) >> 12) | \
        (((state) & MASK_7) >> 6) | \
        ((state) & MASK_8) )

#define DVH_FLIP(state) ( \
        (((state) & MASK_0) << 24) | \
        (((state) & MASK_1) << 12) | \
        ((state) & MASK_2) | \
        (((state) & MASK_3) << 12) | \
        ((state) & MASK_4) | \
        (((state) & MASK_5) >> 12) | \
        ((state) & MASK_6) | \
        (((state) & MASK_7) >> 12) | \
        (((state) & MASK_8) >> 24) )

static inline State get_symmetric_state(State state, int symmetry) {
    switch (symmetry) {
        case 0: return state;                   // Identity
        case 1: return VERTCAL_FLIP(state);       // Vertical flip
        case 2: return HORIZONTAL_FLIP(state);    // Horizontal flip
        case 3: { State v = VERTCAL_FLIP(state); return HORIZONTAL_FLIP(v); } // VH
        case 4: return DIAGONAL_FLIP(state);      // Diagonal flip
        case 5: { State d = DIAGONAL_FLIP(state); return VERTCAL_FLIP(d); }   // DV
        case 6: { State d = DIAGONAL_FLIP(state); return HORIZONTAL_FLIP(d); }  // DH
        case 7: return DVH_FLIP(state);           // DVH
    }
    return state;
}

static inline void add_final_state(State new_state, const Count counts[8]) {
    for (int symmetry = 0; symmetry < 8; symmetry++) {
        if (counts[symmetry] == 0)
            continue;
        State symmetric_state = get_symmetric_state(new_state, symmetry);
        int hash = 0;
        for (int i = 0; i < 9; i++) {
            hash = hash * 10 + GET_DIE_VALUE(symmetric_state, i);
        }
        final_sum += hash * counts[symmetry];
    }
}

static inline void insert_possible_move(State new_state, const Count counts[8]) {
    State canonical_state = new_state;
    int canonical_index = 0;
    for (int i = 1; i < 8; i++) {
        State symmetric_state = get_symmetric_state(new_state, i);
        if (symmetric_state < canonical_state) {
            canonical_index = i;
            canonical_state = symmetric_state;
        }
    }

    CountArray new_counts;
    for (int symmetry = 0; symmetry < 8; symmetry++) {
        new_counts[symmetry] = counts[symmetric_mult[canonical_index * 8 + symmetry]];
    }

    if ((current_depth == max_depth - 1) ||
        ((canonical_state & MASK_0) && (canonical_state & MASK_1) && (canonical_state & MASK_2) &&
         (canonical_state & MASK_3) && (canonical_state & MASK_4) && (canonical_state & MASK_5) &&
         (canonical_state & MASK_6) && (canonical_state & MASK_7) && (canonical_state & MASK_8))) {
        add_final_state(canonical_state, new_counts);
    } else {
        hash_table_insert(&new_states_to_process, canonical_state, new_counts);
    }
}

static inline State get_neighbor_mask(State state, int position) {
    State mask = state & neighbors_mask[position];
    return ( !!(mask & 7) ) |
           ((!!((mask >> 3) & 7)) << 1) |
           ((!!((mask >> 6) & 7)) << 2) |
           ((!!((mask >> 9) & 7)) << 3) |
           ((!!((mask >> 12) & 7)) << 4) |
           ((!!((mask >> 15) & 7)) << 5) |
           ((!!((mask >> 18) & 7)) << 6) |
           ((!!((mask >> 21) & 7)) << 7) |
           ((!!((mask >> 24) & 7)) << 8);
}

void get_possible_moves(State state, const Count counts[8]) {
    for (int i = 0; i < 9; i++) {
        if (!IS_POSITION_EMPTY(state, i))
            continue;
        State neighbor_mask = get_neighbor_mask(state, i);
        int neighbor_count = asm_popcnt(neighbor_mask);
        int capture_possible = 0;

        #define two_sum(i0, n0, i1, n1)                              \
            do {                                                   \
                int sum = (n0) + (n1);                             \
                if (sum <= 6) {                                  \
                    State new_state = CLEAR_DIE_VALUE(state, (i0));\
                    new_state = CLEAR_DIE_VALUE(new_state, (i1));  \
                    new_state = SET_DIE_VALUE(new_state, i, sum);  \
                    insert_possible_move(new_state, counts);       \
                    capture_possible = 1;                          \
                }                                                  \
            } while (0)

        #define three_sum(i0, n0, i1, n1, i2, n2)                   \
            do {                                                   \
                int sum = (n0) + (n1) + (n2);                      \
                if (sum <= 6) {                                  \
                    State new_state = CLEAR_DIE_VALUE(state, (i0));\
                    new_state = CLEAR_DIE_VALUE(new_state, (i1));  \
                    new_state = CLEAR_DIE_VALUE(new_state, (i2));  \
                    new_state = SET_DIE_VALUE(new_state, i, sum);  \
                    insert_possible_move(new_state, counts);       \
                    capture_possible = 1;                          \
                }                                                  \
            } while (0)

        if (neighbor_count == 2) {
            int first_neighbor_index = asm_ctz(neighbor_mask);
            int second_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index));
            int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
            int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);
            two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
        }
        else if (neighbor_count == 3) {
            int first_neighbor_index = asm_ctz(neighbor_mask);
            int second_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index));
            int third_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index) & ~(1U << second_neighbor_index));
            int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
            int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);
            int third_die_value = GET_DIE_VALUE(state, third_neighbor_index);
            two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
            two_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value);
            two_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
            three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
        }
        else if (neighbor_count == 4) {
            int first_neighbor_index = asm_ctz(neighbor_mask);
            int second_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index));
            int third_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index) & ~(1U << second_neighbor_index));
            int fourth_neighbor_index = asm_ctz(neighbor_mask & ~(1U << first_neighbor_index) & ~(1U << second_neighbor_index) & ~(1U << third_neighbor_index));
            int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
            int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);
            int third_die_value = GET_DIE_VALUE(state, third_neighbor_index);
            int fourth_die_value = GET_DIE_VALUE(state, fourth_neighbor_index);
            two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
            two_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value);
            two_sum(first_neighbor_index, first_die_value, fourth_neighbor_index, fourth_die_value);
            two_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
            two_sum(second_neighbor_index, second_die_value, fourth_neighbor_index, fourth_die_value);
            two_sum(third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);
            three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
            three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, fourth_neighbor_index, fourth_die_value);
            three_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);
            three_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);
            {
                int sum = first_die_value + second_die_value + third_die_value + fourth_die_value;
                if (sum <= 6) {
                    State new_state = CLEAR_DIE_VALUE(state, first_neighbor_index);
                    new_state = CLEAR_DIE_VALUE(new_state, second_neighbor_index);
                    new_state = CLEAR_DIE_VALUE(new_state, third_neighbor_index);
                    new_state = CLEAR_DIE_VALUE(new_state, fourth_neighbor_index);
                    new_state = SET_DIE_VALUE(new_state, i, sum);
                    insert_possible_move(new_state, counts);
                    capture_possible = 1;
                }
            }
        }

        if (neighbor_count < 2 || !capture_possible) {
            insert_possible_move(SET_DIE_VALUE(state, i, 1), counts);
        }

        #undef two_sum
        #undef three_sum
    }
}

static inline int compute_final_sum() {
    return final_sum % MOD;
}

int main() {
    int i;
    if (scanf("%d", &max_depth) != 1) {
        fprintf(stderr, "Error reading max_depth\n");
        return 1;
    }

    State initial_state = 0;
    for (i = 0; i < 9; i++) {
        unsigned int value;
        if (scanf("%u", &value) != 1) {
            fprintf(stderr, "Error reading initial state values\n");
            return 1;
        }
        initial_state = SET_DIE_VALUE(initial_state, i, value);
    }

    CountArray initial_counts = {0};
    initial_counts[0] = 1;

    init_hash_table(&states_to_process);
    init_hash_table(&new_states_to_process);
    hash_table_insert(&states_to_process, initial_state, initial_counts);

    for (current_depth = 0; current_depth < max_depth; current_depth++) {
        if (states_to_process.count == 0)
            break;
        for (i = 0; i < (int)states_to_process.count; i++) {
            uint32_t table_index = states_to_process.keys[i];
            uint64_t pair = states_to_process.table[table_index];
            State state = (State)(pair & 0xFFFFFFFF);
            uint32_t index = (pair >> 32) & 0xFFFFFFFF;
            const Count *counts = states_to_process.storage + index;
            get_possible_moves(state, counts);
        }
        swap_hash_table(&states_to_process, &new_states_to_process);
        clear_hash_table(&new_states_to_process);
    }
    int final_value = compute_final_sum();
    printf("%d\n", final_value);

    free_hash_table(&states_to_process);
    free_hash_table(&new_states_to_process);
    return 0;
}
