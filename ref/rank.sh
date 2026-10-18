#!/bin/sh
# print -D flags with the permutation and orientation ranks of a 14-digit state
echo "$1" | awk '{
    p = 0
    for (i = 1; i <= 7; i++) {
        s = 0
        for (j = i + 1; j <= 7; j++)
            if (substr($0, j, 1) < substr($0, i, 1))
                s++
        p = p * (8 - i) + s
    }
    o = 0
    for (i = 8; i <= 13; i++)
        o = o * 3 + substr($0, i, 1) - 1
    printf "-DSTATE_P=%d -DSTATE_O=%d\n", p, o
}'
