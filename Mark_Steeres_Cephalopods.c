/*
 * Optimized Cephalopods Game Simulation Challenge
 *
 * This version is tuned for extreme speed on all test cases (from a few thousand
 * up to 316712 unique states), with a target runtime of about 0–2 ms per test.
 *
 * Game rules: Two players add dice to a 3x3 grid. On a move, a die is placed with:
 *   - a value of 1 if no capturing is possible,
 *   - or, if adjacent to two or more dice where some combination sums to <=6,
 *     the combination is captured and its sum becomes the new die value.
 * Once the board is full (or when the maximum depth is reached), the board is hashed.
 *
 * Scoring: Each board state is mapped to a 32‐bit hash.
 * The final answer is the sum of these hashes modulo 2^30.
 *
 * The code uses precomputed move tables (one per board cell), inline assembly to
 * compute keys quickly, and a custom radix sort/merge for state deduplication.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <stdbool.h>
 #include <string.h>
 
 // ---------------------------------------------------------------------------
 // Configuration & Global Constants
 // ---------------------------------------------------------------------------
 #define MOD       (1 << 30)
 #define MOD_MASK  (MOD - 1)
 #define STATE_CAP (1 << 21)  // Maximum states allocated
 #define RADIX1    (1 << 14)
 #define RADIX2    (1 << 13)
 
 // Precomputed powers-of-10 for board hash (left-to-right, top-to-bottom).
 static const uint32_t pow10_arr[9] = {
     100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1
 };
 
 // Each board cell is stored in 3 bits at fixed shifts.
 static const int cellShift[9] = { 0, 3, 6, 9, 12, 15, 18, 21, 24 };
 static const uint32_t cellMask[9] = {
     7U << 0, 7U << 3, 7U << 6, 7U << 9, 7U << 12,
     7U << 15, 7U << 18, 7U << 21, 7U << 24
 };
 
 // Precomputed cell hash contributions: cell value * its power-of-10.
 uint32_t cell_hash_contrib[9][7];
 
 // ---------------------------------------------------------------------------
 // State Structure & Global Arrays
 // ---------------------------------------------------------------------------
 typedef struct {
     uint32_t packed;   // Packed board representation (9 cells × 3 bits)
     int cnt;           // Count of ways to reach this state
 } State;
 
 State *curr;  // Current states array
 State *next;  // Next states array
 State *aux;   // Auxiliary array for radix sorting
 
 // ---------------------------------------------------------------------------
 // Fixed Board Geometry
 // ---------------------------------------------------------------------------
 static const int cellNeighborCount[9] = { 2, 3, 2, 3, 4, 3, 2, 3, 2 };
 static const int cellNeighbors[9][4] = {
     { 1, 3, -1, -1 },
     { 0, 2, 4, -1 },
     { 1, 5, -1, -1 },
     { 0, 4, 6, -1 },
     { 1, 3, 5, 7 },
     { 2, 4, 8, -1 },
     { 3, 7, -1, -1 },
     { 4, 6, 8, -1 },
     { 5, 7, -1, -1 }
 };
 
 // ---------------------------------------------------------------------------
 // Table-Driven Move Expansion Structures
 // ---------------------------------------------------------------------------
 typedef struct {
    int count;         // Number of moves for this key
    int moves[12];     // New die value (capture sum)
    int masks[12];     // Bitmask of captured neighbors (indices in the cellNeighbors order)
 } Moves;
 
 Moves *moveTable[9] = { 0 };
 
 // Helper: Compute integer power (base^exp)
 static inline int ipow(int base, int exp) __attribute__((always_inline));
 static inline int ipow(int base, int exp) {
     int r = 1;
     while(exp--) r *= base;
     return r;
 }
 
 // Precompute move table for a single board cell.
 void init_move_table_for_cell(int cell) {
     int n = cellNeighborCount[cell];
     int table_size = ipow(7, n);
     moveTable[cell] = (Moves *)malloc(table_size * sizeof(Moves));
     if (!moveTable[cell]) { perror("malloc failed"); exit(1); }
     for (int key = 0; key < table_size; key++) {
         int temp = key;
         int vals[4] = {0, 0, 0, 0};
         int filled = 0;
         for (int j = 0; j < n; j++) {
             vals[j] = temp % 7;
             temp /= 7;
             if (vals[j] != 0)
                 filled++;
         }
         Moves *entry = &moveTable[cell][key];
         if (filled < 2) {
             entry->count = 1;
             entry->moves[0] = 1;   // Default non-capturing move
             entry->masks[0] = 0;
         } else {
             int moveCount = 0;
             int subset_limit = 1 << n;
             for (int s = 0; s < subset_limit; s++) {
                 bool valid = true;
                 int popc = 0, sum = 0;
                 for (int j = 0; j < n; j++) {
                     if (s & (1 << j)) {
                         if (vals[j] == 0) { valid = false; break; }
                         popc++;
                         sum += vals[j];
                     }
                 }
                 if (!valid || popc < 2)
                     continue;
                 if (sum <= 6) {
                     entry->moves[moveCount] = sum;
                     entry->masks[moveCount] = s;
                     moveCount++;
                 }
             }
             if (moveCount == 0) {
                 entry->count = 1;
                 entry->moves[0] = 1;
                 entry->masks[0] = 0;
             } else {
                 entry->count = moveCount;
             }
         }
     }
 }
 
 // Initialize move tables for all nine cells.
 void init_move_tables() {
     for (int cell = 0; cell < 9; cell++) {
         init_move_table_for_cell(cell);
     }
 }
 
 // Free move tables.
 void free_move_tables() {
     for (int cell = 0; cell < 9; cell++) {
         free(moveTable[cell]);
     }
 }
 
 // ---------------------------------------------------------------------------
 // Radix Sort & Merge (State Deduplication)
 // ---------------------------------------------------------------------------
 static inline void radix_sort_states(State *arr, State *aux, int n) __attribute__((always_inline));
 static inline void radix_sort_states(State *arr, State *aux, int n) {
     int count[RADIX1] = {0};
     for (int i = 0; i < n; i++)
         count[arr[i].packed & (RADIX1 - 1)]++;
     for (int i = 1; i < RADIX1; i++)
         count[i] += count[i - 1];
     for (int i = n - 1; i >= 0; i--)
         aux[--count[arr[i].packed & (RADIX1 - 1)]] = arr[i];
     memset(count, 0, sizeof(count));
     for (int i = 0; i < n; i++)
         count[(aux[i].packed >> 14) & (RADIX2 - 1)]++;
     for (int i = 1; i < RADIX2; i++)
         count[i] += count[i - 1];
     for (int i = n - 1; i >= 0; i--)
         arr[--count[(aux[i].packed >> 14) & (RADIX2 - 1)]] = aux[i];
 }
 
 static inline int merge_states(State *arr, int n) __attribute__((always_inline));
 static inline int merge_states(State *arr, int n) {
     if(n == 0) return 0;
     int out = 0;
     for (int i = 1; i < n; i++) {
         if(arr[i].packed == arr[out].packed)
             arr[out].cnt = (arr[out].cnt + arr[i].cnt) & MOD_MASK;
         else
             arr[++out] = arr[i];
     }
     return out + 1;
 }
 
 // ---------------------------------------------------------------------------
 // Table-Driven Cell Expansion Macros with Inline Assembly
 // Each macro tests if a cell is empty then computes a key from its neighbors
 // (using inline assembly to speed up the multiplications and shifts) and
 // produces all applicable new states.
 // ---------------------------------------------------------------------------
 #define PROCESS_CELL_TABLE_0(board, ways, nextArray, nextCount) {           \
     if (((board >> 0) & 7) == 0) {                                          \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $3, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $9, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "add %%ebx, %%eax\n\t"                                            \
             "movl %%eax, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx" );                                                \
         Moves *entry = &moveTable[0][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 3);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 9);                      \
             nb |= ((uint32_t)entry->moves[m]) << 0;                           \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_1(board, ways, nextArray, nextCount) {           \
     if (((board >> 3) & 7) == 0) {                                          \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $0, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $6, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "movl %1, %%ecx\n\t"                                            \
             "shr $12, %%ecx\n\t"                                            \
             "and $7, %%ecx\n\t"                                             \
             "imul $49, %%ecx\n\t"                                           \
             "add %%eax, %%ecx\n\t"                                          \
             "add %%ebx, %%ecx\n\t"                                          \
             "movl %%ecx, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx", "ecx" );                                          \
         Moves *entry = &moveTable[1][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 0);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 6);                      \
             if (entry->masks[m] & 4) nb &= ~(0x7U << 12);                     \
             nb |= ((uint32_t)entry->moves[m]) << 3;                           \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_2(board, ways, nextArray, nextCount) {           \
     if (((board >> 6) & 7) == 0) {                                          \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $3, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $15, %%ebx\n\t"                                            \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "add %%ebx, %%eax\n\t"                                            \
             "movl %%eax, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx" );                                                \
         Moves *entry = &moveTable[2][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 3);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 15);                     \
             nb |= ((uint32_t)entry->moves[m]) << 6;                           \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_3(board, ways, nextArray, nextCount) {           \
     if (((board >> 9) & 7) == 0) {                                          \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $0, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $12, %%ebx\n\t"                                            \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "movl %1, %%ecx\n\t"                                            \
             "shr $18, %%ecx\n\t"                                            \
             "and $7, %%ecx\n\t"                                             \
             "imul $49, %%ecx\n\t"                                           \
             "add %%eax, %%ecx\n\t"                                          \
             "add %%ebx, %%ecx\n\t"                                          \
             "movl %%ecx, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx", "ecx" );                                          \
         Moves *entry = &moveTable[3][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 0);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 12);                     \
             if (entry->masks[m] & 4) nb &= ~(0x7U << 18);                     \
             nb |= ((uint32_t)entry->moves[m]) << 9;                           \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_4(board, ways, nextArray, nextCount) {           \
     if (((board >> 12) & 7) == 0) {                                         \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $3, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $9, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "movl %1, %%ecx\n\t"                                            \
             "shr $15, %%ecx\n\t"                                             \
             "and $7, %%ecx\n\t"                                             \
             "imul $49, %%ecx\n\t"                                           \
             "movl %1, %%edx\n\t"                                            \
             "shr $21, %%edx\n\t"                                             \
             "and $7, %%edx\n\t"                                             \
             "imul $343, %%edx\n\t"                                          \
             "add %%eax, %%edx;\n\t"                                          \
             "add %%ebx, %%edx;\n\t"                                          \
             "add %%ecx, %%edx;\n\t"                                          \
             "movl %%edx, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx", "ecx", "edx" );                                  \
         Moves *entry = &moveTable[4][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 3);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 9);                      \
             if (entry->masks[m] & 4) nb &= ~(0x7U << 15);                     \
             if (entry->masks[m] & 8) nb &= ~(0x7U << 21);                     \
             nb |= ((uint32_t)entry->moves[m]) << 12;                          \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_5(board, ways, nextArray, nextCount) {           \
     if (((board >> 15) & 7) == 0) {                                         \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $6, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $12, %%ebx\n\t"                                            \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "add %%ebx, %%eax\n\t"                                            \
             "movl %%eax, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx" );                                                \
         k += (((board >> 24) & 7) * 49);                                     \
         Moves *entry = &moveTable[5][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 6);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 12);                     \
             if (entry->masks[m] & 4) nb &= ~(0x7U << 24);                     \
             nb |= ((uint32_t)entry->moves[m]) << 15;                          \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_6(board, ways, nextArray, nextCount) {           \
     if (((board >> 18) & 7) == 0) {                                         \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $9, %%eax\n\t"                                             \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $21, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "add %%ebx, %%eax\n\t"                                            \
             "movl %%eax, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx" );                                                \
         Moves *entry = &moveTable[6][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 9);                      \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 21);                     \
             nb |= ((uint32_t)entry->moves[m]) << 18;                          \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_7(board, ways, nextArray, nextCount) {           \
     if (((board >> 21) & 7) == 0) {                                         \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $12, %%eax\n\t"                                            \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $18, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "movl %1, %%ecx\n\t"                                            \
             "shr $24, %%ecx\n\t"                                             \
             "and $7, %%ecx\n\t"                                             \
             "imul $49, %%ecx\n\t"                                           \
             "add %%eax, %%ecx\n\t"                                          \
             "add %%ebx, %%ecx\n\t"                                          \
             "movl %%ecx, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx", "ecx" );                                          \
         Moves *entry = &moveTable[7][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 12);                     \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 18);                     \
             if (entry->masks[m] & 4) nb &= ~(0x7U << 24);                     \
             nb |= ((uint32_t)entry->moves[m]) << 21;                          \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 #define PROCESS_CELL_TABLE_8(board, ways, nextArray, nextCount) {           \
     if (((board >> 24) & 7) == 0) {                                         \
         int k;                                                            \
         __asm__ volatile (                                                \
             "movl %1, %%eax\n\t"                                          \
             "shr $15, %%eax\n\t"                                            \
             "and $7, %%eax\n\t"                                             \
             "movl %1, %%ebx\n\t"                                            \
             "shr $21, %%ebx\n\t"                                             \
             "and $7, %%ebx\n\t"                                             \
             "imul $7, %%ebx\n\t"                                             \
             "add %%ebx, %%eax\n\t"                                            \
             "movl %%eax, %0\n\t"                                             \
             : "=r"(k)                                                       \
             : "r"(board)                                                    \
             : "eax", "ebx" );                                                \
         Moves *entry = &moveTable[8][k];                                    \
         for (int m = 0; m < entry->count; m++) {                            \
             uint32_t nb = board;                                            \
             if (entry->masks[m] & 1) nb &= ~(0x7U << 15);                     \
             if (entry->masks[m] & 2) nb &= ~(0x7U << 21);                     \
             nb |= ((uint32_t)entry->moves[m]) << 24;                          \
             (nextArray)[(*nextCount)++] = (State){ nb, ways };              \
         }                                                                   \
     }                                                                       \
 }
 
 // ---------------------------------------------------------------------------
 // Board Hash Calculation (Always Inlined)
 // ---------------------------------------------------------------------------
 static inline uint32_t compute_board_hash(uint32_t board) __attribute__((always_inline));
 static inline uint32_t compute_board_hash(uint32_t board) {
     return ( cell_hash_contrib[0][ (board >> 0) & 7 ] +
              cell_hash_contrib[1][ (board >> 3) & 7 ] +
              cell_hash_contrib[2][ (board >> 6) & 7 ] +
              cell_hash_contrib[3][ (board >> 9) & 7 ] +
              cell_hash_contrib[4][ (board >> 12) & 7 ] +
              cell_hash_contrib[5][ (board >> 15) & 7 ] +
              cell_hash_contrib[6][ (board >> 18) & 7 ] +
              cell_hash_contrib[7][ (board >> 21) & 7 ] +
              cell_hash_contrib[8][ (board >> 24) & 7 ] ) & MOD_MASK;
 }
 
 static inline bool board_is_full(uint32_t board) __attribute__((always_inline));
 static inline bool board_is_full(uint32_t board) {
     return ((board & cellMask[0]) &&
             (board & cellMask[1]) &&
             (board & cellMask[2]) &&
             (board & cellMask[3]) &&
             (board & cellMask[4]) &&
             (board & cellMask[5]) &&
             (board & cellMask[6]) &&
             (board & cellMask[7]) &&
             (board & cellMask[8]));
 }
 
 // ---------------------------------------------------------------------------
 // Main Simulation Loop
 // ---------------------------------------------------------------------------
 int main(){
     int depth;
     if (scanf("%d", &depth) != 1)
         return 1;
     uint32_t init = 0;
     int val;
     for (int pos = 0; pos < 9; pos++){
         if (scanf("%d", &val) != 1)
             return 1;
         init |= ((uint32_t)(val & 7)) << cellShift[pos];
     }
     // Precompute cell hash contributions.
     for (int pos = 0; pos < 9; pos++){
         for (int v = 0; v < 7; v++){
             cell_hash_contrib[pos][v] = v * pow10_arr[pos];
         }
     }
     // Initialize move tables.
     init_move_tables();
     
     uint32_t finalSum = 0;
     int curr_size = 1;
     curr = (State*)malloc(STATE_CAP * sizeof(State));
     next = (State*)malloc(STATE_CAP * sizeof(State));
     aux  = (State*)malloc(STATE_CAP * sizeof(State));
     if (!curr || !next || !aux) {
         perror("malloc failure");
         exit(1);
     }
     curr[0].packed = init;
     curr[0].cnt = 1;
     
     for (int move = 0; move <= depth; move++){
         int nextCount = 0;
         State *curPtr = curr;
         for (int i = 0; i < curr_size; i++){
             uint32_t board = curPtr->packed;
             int ways = curPtr->cnt;
             curPtr++;
             if (__builtin_expect(board_is_full(board) || move == depth, 0)) {
                 finalSum = (finalSum + (uint64_t)compute_board_hash(board) * ways) & MOD_MASK;
                 continue;
             }
             PROCESS_CELL_TABLE_0(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_1(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_2(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_3(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_4(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_5(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_6(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_7(board, ways, next, &nextCount);
             PROCESS_CELL_TABLE_8(board, ways, next, &nextCount);
         }
         radix_sort_states(next, aux, nextCount);
         nextCount = merge_states(next, nextCount);
         memcpy(curr, next, nextCount * sizeof(State));
         curr_size = nextCount;
     }
     
     printf("%d\n", finalSum & MOD_MASK);
     free(curr);
     free(next);
     free(aux);
     free_move_tables();
     return 0;
 }
 