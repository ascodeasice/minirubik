// host gates H1-H3 for ida.c, checked against exact BFS distances
// usage: gates [workers], default is CPUs - 2
#define _POSIX_C_SOURCE 200112L
#define IDA_NO_MAIN
#include "ida.c"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

// functions from solver.c
static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t) ((state.o[from] + twist[face][i]) % 3U);
    }
    return result;
}

static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS, o = rank % ORIENTATIONS, f = 720;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;
        state->p[i] = available[q];
        for (uint8_t j = q; j + 1U < (unsigned) (CUBIES - i); ++j)
            available[j] = available[j + 1U];
        if (i < 5)
            f /= 6U - i;
    }
    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }
    state->o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
}

static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec / 1e9;
}

// BFS from solved, NULL if some state is unreached
static uint8_t *exact_distances(void)
{
    uint8_t *dist = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 1;
    if (!dist || !queue) {
        free(dist);
        free(queue);
        return NULL;
    }
    memset(dist, UINT8_MAX, STATES);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        state_t state;
        unrank_state(here, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = state;
            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = quarter_turn(next, face);
                uint32_t there = rank_state(&next);
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    queue[tail++] = there;
                }
            }
        }
    }
    free(queue);
    if (tail != STATES) {
        free(dist);
        return NULL;
    }
    return dist;
}

static int gate_h2(const uint8_t *dist)
{
    static uint8_t min_p[PERMUTATIONS], min_o[ORIENTATIONS];
    unsigned bad_moves = 0, bad_pdb = 0;
    int max_p = 0, max_o = 0;

    for (uint8_t face = 0; face < 3; ++face) {
        for (uint32_t r = 0; r < PERMUTATIONS; ++r) {
            state_t s;
            unrank_state(r * ORIENTATIONS, &s);
            s = quarter_turn(s, face);
            bad_moves += move_perm[face][r] != rank_state(&s) / ORIENTATIONS;
        }
        for (uint32_t r = 0; r < ORIENTATIONS; ++r) {
            state_t s;
            unrank_state(r, &s);
            s = quarter_turn(s, face);
            bad_moves += move_orient[face][r] != rank_state(&s) % ORIENTATIONS;
        }
    }

    memset(min_p, UINT8_MAX, sizeof min_p);
    memset(min_o, UINT8_MAX, sizeof min_o);
    for (uint32_t s = 0; s < STATES; ++s) {
        uint32_t p = s / ORIENTATIONS, o = s % ORIENTATIONS;
        if (dist[s] < min_p[p])
            min_p[p] = dist[s];
        if (dist[s] < min_o[o])
            min_o[o] = dist[s];
    }
    for (uint32_t p = 0; p < PERMUTATIONS; ++p) {
        bad_pdb += pdb_perm[p] == UINT8_MAX || pdb_perm[p] != min_p[p];
        if (pdb_perm[p] > max_p)
            max_p = pdb_perm[p];
    }
    for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
        bad_pdb += pdb_orient[o] == UINT8_MAX || pdb_orient[o] != min_o[o];
        if (pdb_orient[o] > max_o)
            max_o = pdb_orient[o];
    }
    printf("H2: transition entries wrong: %u of %d\n", bad_moves,
           3 * (PERMUTATIONS + ORIENTATIONS));
    printf(
        "H2: pattern-database entries wrong: %u of %d; "
        "pdb_perm max %d solved %u, pdb_orient max %d solved %u\n",
        bad_pdb, PERMUTATIONS + ORIENTATIONS, max_p, (unsigned) pdb_perm[0],
        max_o, (unsigned) pdb_orient[0]);
    return bad_moves == 0 && bad_pdb == 0 && pdb_perm[0] == 0 &&
           pdb_orient[0] == 0;
}

static int gate_h1(const uint8_t *dist)
{
    unsigned over = 0;
    for (uint32_t s = 0; s < STATES; ++s)
        over += heuristic((uint16_t) (s / ORIENTATIONS),
                          (uint16_t) (s % ORIENTATIONS)) > dist[s];
    printf("H1: states with h > d: %u of %d\n", over, STATES);
    return over == 0;
}

// H3 result of one worker
struct h3_result {
    uint64_t total, sum11;
    uint32_t wrong_length, not_solved, n11, worst11, worst_rank;
};

static void h3_slice(const uint8_t *dist,
                     uint32_t first,
                     uint32_t step,
                     struct h3_result *r)
{
    memset(r, 0, sizeof *r);
    for (uint32_t s = first; s < STATES; s += step) {
        uint8_t moves[MAX_DEPTH];
        int length = solve((uint16_t) (s / ORIENTATIONS),
                           (uint16_t) (s % ORIENTATIONS), moves);
        r->total += expanded;
        if (length != dist[s]) {
            ++r->wrong_length;
            continue;
        }
        state_t state;
        unrank_state(s, &state);
        for (int i = 0; i < length; ++i)
            for (int t = 0; t <= moves[i] % 3; ++t)
                state = quarter_turn(state, (uint8_t) (moves[i] / 3));
        r->not_solved += rank_state(&state) != 0;
        if (dist[s] == 11) {
            ++r->n11;
            r->sum11 += expanded;
            if (expanded > r->worst11) {
                r->worst11 = expanded;
                r->worst_rank = s;
            }
        }
    }
}

// fork workers since `expanded` is global
static int gate_h3(const uint8_t *dist, uint32_t workers)
{
    struct h3_result all = {0};
    pid_t pid[64];
    int fd[64];
    double start = seconds();

    for (uint32_t k = 0; k < workers; ++k) {
        int pipe_fd[2];
        pid[k] = -1;
        if (workers > 1 && pipe(pipe_fd) == 0) {
            pid[k] = fork();
            if (pid[k] == 0) {
                struct h3_result r;
                close(pipe_fd[0]);
                h3_slice(dist, k, workers, &r);
                _exit(write(pipe_fd[1], &r, sizeof r) == sizeof r ? 0 : 1);
            }
            close(pipe_fd[1]);
            if (pid[k] < 0)
                close(pipe_fd[0]);
            fd[k] = pipe_fd[0];
        }
    }
    for (uint32_t k = 0; k < workers; ++k) {
        struct h3_result r;
        if (pid[k] > 0) {
            int status;
            ssize_t got = read(fd[k], &r, sizeof r);
            close(fd[k]);
            waitpid(pid[k], &status, 0);
            if (got != (ssize_t) sizeof r) {
                fprintf(stderr, "H3 worker %u failed\n", (unsigned) k);
                return 0;
            }
        } else {
            h3_slice(dist, k, workers, &r);
        }
        all.total += r.total;
        all.sum11 += r.sum11;
        all.wrong_length += r.wrong_length;
        all.not_solved += r.not_solved;
        all.n11 += r.n11;
        if (r.worst11 > all.worst11 ||
            (r.worst11 == all.worst11 && r.worst_rank < all.worst_rank)) {
            all.worst11 = r.worst11;
            all.worst_rank = r.worst_rank;
        }
    }
    double elapsed = seconds() - start;

    state_t worst;
    unrank_state(all.worst_rank, &worst);
    printf(
        "H3: wrong length %u, path not solving %u, of %d states; "
        "%.1f s wall clock with %u workers, %llu nodes expanded in total\n",
        (unsigned) all.wrong_length, (unsigned) all.not_solved, STATES, elapsed,
        (unsigned) workers, (unsigned long long) all.total);
    printf("distance 11: %u states, mean %.0f nodes expanded, worst %lu (",
           (unsigned) all.n11, all.n11 ? (double) all.sum11 / all.n11 : 0.0,
           (unsigned long) all.worst11);
    for (int i = 0; i < CUBIES; ++i)
        putchar('1' + worst.p[i]);
    for (int i = 0; i < CUBIES; ++i)
        putchar('1' + worst.o[i]);
    puts(")");
    return all.wrong_length == 0 && all.not_solved == 0 && all.n11 == 2644;
}

int main(int argc, char **argv)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t workers = cpus > 3 ? (uint32_t) (cpus - 2) : 1;
    if (argc > 2 || (argc == 2 && (atoi(argv[1]) < 1 || atoi(argv[1]) > 64))) {
        fprintf(stderr, "usage: %s [workers, 1 to 64]\n", argv[0]);
        return 2;
    }
    if (argc == 2)
        workers = (uint32_t) atoi(argv[1]);
    if (workers > 64)
        workers = 64;
    uint8_t *dist = exact_distances();
    if (!dist) {
        fputs("could not build the exact distance table\n", stderr);
        return 1;
    }
    int ok = gate_h2(dist);
    ok &= gate_h1(dist);
    fflush(stdout); // children would print it again
    ok &= gate_h3(dist, workers);
    free(dist);
    puts(ok ? "all gates passed" : "GATE FAILED");
    return ok ? 0 : 1;
}
