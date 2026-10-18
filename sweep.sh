#!/bin/sh
# run every state in a list through cube.S on RV32_ISS, in parallel
# usage: ./sweep.sh tests/distance11.txt [workers] > out.txt
# prints "state iret exit-code" per line
list=$1
workers=${2:-10}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' 0 1 2 15
cut -d' ' -f1 "$list" | xargs -P "$workers" -I{} sh -c '
    gcc -E -P -x assembler-with-cpp -DRENDER=0 -DSTATE="\"{}\"" -DEXPECT=11 \
        cube.S -o "$1/{}.s" &&
    ripes --mode cli --src "$1/{}.s" -t asm --proc RV32_ISS --iret |
        awk "/exited/ { code = \$NF } /retired/ { getline; n = \$1 }
             END { print \"{}\", n, code }"
    rm -f "$1/{}.s"' _ "$dir"
