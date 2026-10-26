#!/bin/bash
# bash version of ./run (no csh needed). Usage: ./run.sh traces [binary]
DIR=${1:-traces}; BIN=${2:-./src/predict}
sum=0; n=0
for t in $(find "$DIR" -name '*.trace.*' | sort); do
  m=$($BIN "$t" | tail -1 | sed -e 's/MPKI//' | tr -d ' ')
  printf "%-45s %8.3f\n" "$t" "$m"
  sum=$(echo "$sum + $m" | bc -l); n=$((n+1))
done
printf "average MPKI: %.3f\n" $(echo "$sum / $n" | bc -l)
