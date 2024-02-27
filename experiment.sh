#!/bin/bash

# Fixed parameters
c=15
C=17
trace_file="./traces/mcf.trace"
output="test.log"

# Iterate over the parameters within the specified ranges
for b in {5..7}; do
    for s in {0..5}; do
        for S in {1..6}; do # Start S greater than s to meet the restriction
            for P in {0..2}; do
                for I in "LIP" "MIP"; do
                    for r in "LFU" "LRU"; do
                        # Ensure the size of the L2 cache is strictly greater than the size of the L1 cache
                        if ((C > c)) && ((S > s)) && ((C - S > c - s)); then
                            ./run.sh -c "$c" -b "$b" -s "$s" -C "$C" -S "$S" -P "$P" -I "$I" -r "$r" -f "$trace_file" >>"$output"
                        fi
                    done
                done
            done
        done
    done
done
