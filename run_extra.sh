#!/bin/bash
# bash version of ./run_extra (extra credit: complete Pentium M, cpm_predictor)
DIR=${1:-traces}; BIN=./src/measure_target
# measure_target = predict_extra_credit with a working target comparison
# (the framework getter returns bool); framework files are not modified
g++ -O3 -w -o src/measure_target src/measure_target.cc src/trace.cc || exit 1
sd=0; st=0; n=0
for t in $(find "$DIR" -name '*.trace.*' | sort); do
  out=$($BIN "$t" | tail -1)
  d=$(echo "$out" | sed -E 's/.*Direction: ([0-9.]+).*/\1/'); g=$(echo "$out" | sed -E 's/.*Target: ([0-9.]+).*/\1/')
  printf "%-45s direction %8.3f   target %8.3f\n" "$t" "$d" "$g"
  sd=$(echo "$sd + $d" | bc -l); st=$(echo "$st + $g" | bc -l); n=$((n+1))
done
printf "average direction MPKI: %.3f   average target MPKI: %.3f\n" $(echo "$sd / $n" | bc -l) $(echo "$st / $n" | bc -l)
