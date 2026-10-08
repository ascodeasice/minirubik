/* Feasibility check: H1 over all states and
 * IDA* node counts on every distance-11 state, with h = max(perm PDB, orient
 * PDB). Transitions use the baseline layout: one quarter-turn table per face,
 * 3 x (5040 + 729) uint16_t = 34,614 bytes. A half turn and an inverse turn
 * are reached by applying the same face again to the previous child.
 *
 * Build: ./pdb_gen c > pdb.h && cc -O2 -std=c99 ida_check.c -o ida_check */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pdb.h"

enum { C = 7, NP = 5040, NO = 729, NS = NP * NO };
static const uint8_t source[3][C] = {{1, 4, 2, 0, 3, 5, 6},
                                     {0, 1, 2, 4, 5, 6, 3},
                                     {0, 2, 5, 3, 1, 4, 6}};
static const uint8_t twist[3][C] = {{1, 2, 0, 2, 1, 0, 0},
                                    {0, 0, 0, 1, 2, 1, 2},
                                    {0, 0, 0, 0, 0, 0, 0}};
static uint16_t pq[3][NP],
    oq[3][NO];            /* one clockwise quarter turn of each face */
static long long lookups; /* transition-table reads, one per (pq, oq) pair */

static uint16_t rank_p(const uint8_t *p)
{
    uint16_t r = 0;
    for (int i = 0; i < C; ++i) {
        int s = 0;
        for (int j = i + 1; j < C; ++j)
            s += p[j] < p[i];
        r = (uint16_t) (r * (C - i) + s);
    }
    return r;
}
static void unrank_p(int r, uint8_t *p)
{
    uint8_t a[C] = {0, 1, 2, 3, 4, 5, 6};
    int f = 720;
    for (int i = 0; i < C; ++i) {
        int q = r / f;
        r %= f;
        p[i] = a[q];
        for (int j = q; j + 1 < C - i; ++j)
            a[j] = a[j + 1];
        if (i < 5)
            f /= 6 - i;
    }
}
static uint16_t rank_o(const uint8_t *o)
{
    uint16_t r = 0;
    for (int i = 0; i < 6; ++i)
        r = (uint16_t) (r * 3 + o[i]);
    return r;
}
static void unrank_o(int r, uint8_t *o)
{
    int s = 0;
    for (int i = 6; i-- > 0;) {
        o[i] = r % 3;
        s += o[i];
        r /= 3;
    }
    o[6] = (3 - s % 3) % 3;
}

static void build_moves(void)
{
    for (int r = 0; r < NP; ++r)
        for (int f = 0; f < 3; ++f) {
            uint8_t v[C], w[C];
            unrank_p(r, v);
            for (int i = 0; i < C; ++i)
                w[i] = v[source[f][i]];
            pq[f][r] = rank_p(w);
        }
    for (int r = 0; r < NO; ++r)
        for (int f = 0; f < 3; ++f) {
            uint8_t v[C], w[C];
            unrank_o(r, v);
            for (int i = 0; i < C; ++i)
                w[i] = (v[source[f][i]] + twist[f][i]) % 3;
            oq[f][r] = rank_o(w);
        }
}

static uint8_t h(int p, int o)
{
    int a = pdb_perm[p], b = pdb_orient[o];
    return (uint8_t) (a > b ? a : b);
}

static long long expanded;
static int dfs(int p, int o, int g, int bound, int last)
{
    if (g + h(p, o) > bound)
        return 0;
    if (p == 0 && o == 0)
        return 1;
    ++expanded;
    for (int f = 0; f < 3; ++f) {
        if (f == last)
            continue; /* never turn the same face twice in a row */
        int cp = p, co = o;
        for (int t = 0; t < 3;
             ++t) { /* f, f2, f' : one more quarter turn each time */
            cp = pq[f][cp];
            co = oq[f][co];
            ++lookups;
            if (dfs(cp, co, g + 1, bound, f))
                return 1;
        }
    }
    return 0;
}
static int ida(int p, int o)
{
    for (int bound = h(p, o);; ++bound)
        if (dfs(p, o, 0, bound, -1))
            return bound;
}

int main(void)
{
    build_moves();
    uint8_t *d = malloc(NS);
    uint32_t *q = malloc((size_t) NS * 4);
    memset(d, 0xFF, NS);
    d[0] = 0;
    q[0] = 0;
    for (uint32_t head = 0, tail = 1; head < tail;) {
        uint32_t s = q[head++];
        int p = s / NO, o = s % NO;
        for (int f = 0; f < 3; ++f) {
            int cp = p, co = o;
            for (int t = 0; t < 3; ++t) {
                cp = pq[f][cp];
                co = oq[f][co];
                uint32_t n = cp * NO + co;
                if (d[n] == 0xFF) {
                    d[n] = d[s] + 1;
                    q[tail++] = n;
                }
            }
        }
    }
    long long over = 0;
    int n11 = 0;
    for (uint32_t s = 0; s < NS; ++s) {
        if (h(s / NO, s % NO) > d[s])
            ++over;
        n11 += d[s] == 11;
    }
    printf("H1: states with h > d: %lld of %d; distance-11 states: %d\n", over,
           NS, n11);

    clock_t t0 = clock();
    long long sum = 0, worst = 0, lworst = 0;
    uint32_t worst_s = 0;
    int wrong = 0;
    static long long hist[8];
    for (uint32_t s = 0; s < NS; ++s) {
        if (d[s] != 11)
            continue;
        expanded = 0;
        lookups = 0;
        if (ida(s / NO, s % NO) != 11)
            ++wrong;
        sum += expanded;
        if (lookups > lworst)
            lworst = lookups;
        if (expanded > worst) {
            worst = expanded;
            worst_s = s;
        }
        int b = 0;
        for (long long e = expanded; e >= 50000 && b < 7; e -= 50000)
            ++b;
        hist[b]++;
    }
    printf(
        "distance-11: wrong lengths %d, mean expanded %.0f, worst %lld (rank "
        "%u), %.2f s\n",
        wrong, (double) sum / n11, worst, worst_s,
        (double) (clock() - t0) / CLOCKS_PER_SEC);
    printf(
        "transition tables: %zu bytes; worst transition lookups (pq+oq pairs) "
        "%lld\n",
        sizeof pq + sizeof oq, lworst);
    printf("expanded histogram (bins of 50k):");
    for (int b = 0; b < 8; ++b)
        printf(" %lld", hist[b]);
    printf("\n");
    uint8_t p[C], o[C];
    unrank_p(worst_s / NO, p);
    unrank_o(worst_s % NO, o);
    printf("worst state as input digits: ");
    for (int i = 0; i < C; ++i)
        printf("%d", p[i] + 1);
    for (int i = 0; i < C; ++i)
        printf("%d", o[i] + 1);
    printf("\n");
    return 0;
}
