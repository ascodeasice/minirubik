/* Generate two pattern databases for an IDA* heuristic: the half-turn-metric
 * distance to solved of every corner permutation and of every orientation
 * vector, each found by breadth-first search over that projection alone.
 * The ranks match rank_state in solver.c, so a state ranked there indexes
 * pdb_perm with rank / 729 and pdb_orient with rank % 729.
 *
 * Usage: pdb_gen c    > pdb.h   (C arrays)
 *        pdb_gen asm  > pdb.s   (.byte data for an assembly program)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    UNSEEN = UINT8_MAX
};

/* Same move model as solver.c: each destination takes a cubie from
 * source[face][destination] and adds twist[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

static void turn_p(uint8_t p[CUBIES], uint8_t face)
{
    uint8_t old[CUBIES];
    memcpy(old, p, CUBIES);
    for (uint8_t i = 0; i < CUBIES; ++i)
        p[i] = old[source[face][i]];
}

static void turn_o(uint8_t o[CUBIES], uint8_t face)
{
    uint8_t old[CUBIES];
    memcpy(old, o, CUBIES);
    for (uint8_t i = 0; i < CUBIES; ++i)
        o[i] = (uint8_t) ((old[source[face][i]] + twist[face][i]) % 3U);
}

/* Lehmer rank, identical to the permutation half of rank_state. */
static uint16_t rank_p(const uint8_t p[CUBIES])
{
    uint16_t r = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (p[j] < p[i])
                ++smaller;
        r = (uint16_t) (r * (CUBIES - i) + smaller);
    }
    return r;
}

static void unrank_p(uint16_t r, uint8_t p[CUBIES])
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint16_t f = 720;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (r / f);
        r %= f;
        p[i] = available[q];
        for (uint8_t j = q; j + 1U < (unsigned) (CUBIES - i); ++j)
            available[j] = available[j + 1U];
        if (i < 5)
            f /= 6U - i;
    }
}

/* Base-3 rank of the first six orientations, as in rank_state. */
static uint16_t rank_o(const uint8_t o[CUBIES])
{
    uint16_t r = 0;
    for (uint8_t i = 0; i < 6; ++i)
        r = (uint16_t) (r * 3U + o[i]);
    return r;
}

static void unrank_o(uint16_t r, uint8_t o[CUBIES])
{
    uint8_t sum = 0;
    for (uint8_t i = 6; i-- > 0;) {
        o[i] = (uint8_t) (r % 3U);
        sum = (uint8_t) (sum + o[i]);
        r /= 3U;
    }
    o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
}

/* Fill dist[0..size) with the distance from the solved projection (rank 0).
 * Every HTM move is one, two or three quarter turns of a face. Returns the
 * largest distance, or -1 if some entry was never reached. */
static int bfs(uint8_t *dist, uint16_t size, int permutation)
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 1;
    int max = 0;
    memset(dist, UNSEEN, size);
    queue[0] = 0;
    dist[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < 3; ++face) {
            uint8_t v[CUBIES];
            if (permutation)
                unrank_p(here, v);
            else
                unrank_o(here, v);
            for (uint8_t turn = 0; turn < 3; ++turn) {
                uint16_t there;
                if (permutation) {
                    turn_p(v, face);
                    there = rank_p(v);
                } else {
                    turn_o(v, face);
                    there = rank_o(v);
                }
                if (dist[there] == UNSEEN) {
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    if (dist[there] > max)
                        max = dist[there];
                    queue[tail++] = there;
                }
            }
        }
    }
    return tail == size ? max : -1;
}

static void report(const char *name,
                   const uint8_t *dist,
                   uint16_t size,
                   int max)
{
    fprintf(stderr, "%s: %u entries, max %d, by distance:", name,
            (unsigned) size, max);
    for (int d = 0; d <= max; ++d) {
        unsigned count = 0;
        for (uint16_t i = 0; i < size; ++i)
            count += dist[i] == d;
        fprintf(stderr, " %u", count);
    }
    fputc('\n', stderr);
}

static void emit(const char *name, const uint8_t *dist, uint16_t size, int c)
{
    if (c)
        printf("static const uint8_t %s[%u] = {\n", name, (unsigned) size);
    else
        printf("%s:\n", name);
    for (uint16_t i = 0; i < size; ++i) {
        if (i % 16 == 0)
            printf(c ? "   " : "        .byte  ");
        else
            printf(c ? "" : ",");
        printf(c ? " %u," : " %u", (unsigned) dist[i]);
        if (i % 16 == 15 || i + 1U == size)
            putchar('\n');
    }
    if (c)
        printf("};\n");
}

int main(int argc, char **argv)
{
    static uint8_t perm[PERMUTATIONS], orient[ORIENTATIONS];
    int c;
    if (argc != 2 ||
        (strcmp(argv[1], "c") != 0 && strcmp(argv[1], "asm") != 0)) {
        fprintf(stderr, "usage: %s c|asm\n", argv[0]);
        return 2;
    }
5769*2
    c = strcmp(argv[1], "c") == 0;
    int max_p = bfs(perm, PERMUTATIONS, 1);
    int max_o = bfs(orient, ORIENTATIONS, 0);
    if (max_p < 0 || max_o < 0) {
        fprintf(stderr, "pattern database incomplete\n");
        return 1;
    }
    report("pdb_perm", perm, PERMUTATIONS, max_p);
    report("pdb_orient", orient, ORIENTATIONS, max_o);

    printf(c ? "/* Generated by pdb_gen; do not edit. */\n"
             : "# Generated by pdb_gen; do not edit.\n        .data\n");
    emit("pdb_perm", perm, PERMUTATIONS, c);
    emit("pdb_orient", orient, ORIENTATIONS, c);
    return fflush(stdout) == 0 ? 0 : 1;
}
