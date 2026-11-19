#!/bin/sh
# F6c: the melody's (or with "bass", the bass's) clashes with the chords, each style: see tests/clash.c
cd "$(dirname "$0")/.." || exit 1
mkdir -p build
cc -O2 -std=c99 -w -DPLAT_HOST -Iapp/jungle -Ibase/include -include stdlib.h -include math.h tests/clash.c -o build/clash -lm || exit 1
for s in 0 1 2 3 4; do build/clash $s 240 $1; done
