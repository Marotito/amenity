#!/bin/sh
# F7a: the melody's rhythm against the break and the harmony, each style: see tests/rhythm.c
cd "$(dirname "$0")/.." || exit 1
mkdir -p build
cc -O2 -std=c99 -w -DPLAT_HOST -Iapp/jungle -Ibase/include -include stdlib.h -include math.h tests/rhythm.c -o build/rhythm -lm || exit 1
for s in 0 1 2 3 4; do build/rhythm $s ${1:-240}; done
