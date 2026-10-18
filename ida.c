#include <stdint.h>
#include <stdio.h>

#include "pdb.h"

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9,
    MAX_DEPTH = 11 // diameter in half-turn metric
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

static const char *const move_names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                              "B'", "D",  "D2", "D'"};

// how many nodes are expanded, only counted in host tools
#ifdef IDA_COUNT
static uint32_t expanded;
#define COUNT() (++expanded)
#else
#define COUNT() ((void) 0)
#endif

// functions from solver.c
static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        p = p * (CUBIES - i) + smaller;
    }
    for (uint8_t i = 0; i < 6; ++i)
        o = o * 3U + state->o[i];
    return p * ORIENTATIONS + o;
}

static uint8_t heuristic(uint16_t p, uint16_t o)
{
    // heuristic value from pattern database
    uint8_t hp = pdb_perm[p], ho = pdb_orient[o];
    return hp > ho ? hp : ho;
}

// DFS with depth limit `bound`
// return final move length/-1, and store result in `moves`
static int search(uint16_t root_p,
                  uint16_t root_o,
                  uint8_t bound,
                  uint8_t moves[MAX_DEPTH])
{
    // use 4 stacks to prevent recursion
    uint16_t p[MAX_DEPTH + 1], o[MAX_DEPTH + 1];

    // which face previous node has moved, for pruning
    uint8_t face[MAX_DEPTH + 1]; // 0,1,2 for R,B,D; 3 for none

    // how many quarter turns node at depth has done
    uint8_t turns[MAX_DEPTH + 1]; // 0,1,2 for 90,180,270

    int depth = 1;

    // push root into stack
    p[0] = root_p;
    o[0] = root_o;
    face[0] = 3; // no previous node
    COUNT();

    face[1] = 0;
    turns[1] = 0;

    // we have items in stack
    while (depth > 0) {
        if (turns[depth] == 3) {
            // try next face moves
            ++face[depth];
            turns[depth] = 0;
        }

        // prune consecutive face moves
        if (face[depth] == face[depth - 1]) {
            ++face[depth];
            turns[depth] = 0;
        }

        // all moves has been tried
        if (face[depth] >= 3) {
            --depth;
            continue;
        }

        // haven't started turning
        if (turns[depth] == 0) {
            p[depth] = p[depth - 1];
            o[depth] = o[depth - 1];
        }

        p[depth] = move_perm[face[depth]][p[depth]];
        o[depth] = move_orient[face[depth]][o[depth]];
        ++turns[depth];
        if (depth + heuristic(p[depth], o[depth]) > bound)
            continue;
        if (p[depth] == 0 && o[depth] == 0) {
            for (int i = 1; i <= depth; ++i)
                moves[i - 1] = (uint8_t) (face[i] * 3 + turns[i] - 1);
            return depth;
        }
        // not solved means h >= 1, so depth + 1 <= bound
        COUNT();
        ++depth;
        face[depth] = 0;
        turns[depth] = 0;
    }
    return -1;
}

// ida* for cube, store result in moves
static int solve(uint16_t p, uint16_t o, uint8_t moves[MAX_DEPTH])
{
#ifdef IDA_COUNT
    expanded = 0;
#endif
    // solved
    if (p == 0 && o == 0)
        return 0;
    for (uint8_t bound = heuristic(p, o); bound <= MAX_DEPTH; ++bound) {
        int length = search(p, o, bound, moves);
        if (length >= 0)
            return length;
    }
    return -1;
}

#ifndef IDA_NO_MAIN

// functions below are from solver.c
static int valid(const state_t *state)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;
        for (uint8_t j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;
        sum = (uint8_t) (sum + state->o[i]);
    }
    return sum % 3U == 0;
}

static int parse_state(const char *input, state_t *state)
{
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        (i < 7 ? state->p : state->o)[i % 7] = (uint8_t) (input[i] - '1');
    }
    return input[14] == '\0' && valid(state);
}

static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

int main(int argc, char **argv)
{
    state_t state;
    uint8_t moves[MAX_DEPTH]; // result
    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }

    uint32_t rank = rank_state(&state);
    int length = solve((uint16_t) (rank / ORIENTATIONS),
                       (uint16_t) (rank % ORIENTATIONS), moves);
    if (length < 0) {
        fputs("no solution within the diameter\n", stderr);
        return 1;
    }
    for (int i = 0; i < length; ++i)
        printf("%s%s", i ? " " : "", move_names[moves[i]]);
    putchar('\n');
    fprintf(stderr, "%d moves, %lu nodes expanded\n", length,
            (unsigned long) expanded);
    return output_failed();
}
#endif
