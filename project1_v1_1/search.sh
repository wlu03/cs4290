#!/bin/bash
TRACES=(gcc leela linpack matmul_naive matmul_tiled mcf)
OUTDIR="search"
mkdir -p "$OUTDIR"
HDR="trace,C1,B,S1,C2,S2,prefetch,r,L1_AAT,L1_HR,L1_MR,L2_AAT,L2_RHR,L2_RMR,PF_issued,PF_hits,PF_misses,L1_misses,L2_rhits,L2_rmisses,WB_L1"

get_stats() {
    local o="$1"
    local a1=$(echo "$o" | grep "L1 average access time" | awk '{print $NF}')
    local h1=$(echo "$o" | grep "L1 hit ratio" | awk '{print $NF}')
    local m1=$(echo "$o" | grep "L1 miss ratio" | awk '{print $NF}')
    local a2=$(echo "$o" | grep "L2 average access time" | awk '{print $NF}')
    local rh2=$(echo "$o" | grep "L2 read hit ratio" | awk '{print $NF}')
    local rm2=$(echo "$o" | grep "L2 read miss ratio" | awk '{print $NF}')
    local pfi=$(echo "$o" | grep "L2 prefetches issued" | awk '{print $NF}')
    local pfh=$(echo "$o" | grep "L2 prefetch hits" | awk '{print $NF}')
    local pfm=$(echo "$o" | grep "L2 prefetch misses" | awk '{print $NF}')
    local ms1=$(echo "$o" | grep "L1 misses:" | awk '{print $NF}')
    local rh=$(echo "$o" | grep "L2 read hits:" | awk '{print $NF}')
    local rm=$(echo "$o" | grep "L2 read misses:" | awk '{print $NF}')
    local wb=$(echo "$o" | grep "Write-backs from L1" | awk '{print $NF}')
    echo "${a1},${h1},${m1},${a2},${rh2},${rm2},${pfi},${pfh},${pfm},${ms1},${rh},${rm},${wb}"
}

echo "L1+L2 no-prefetch search"
F1="$OUTDIR/l1_l2.csv"
echo "$HDR" > "$F1"
for t in "${TRACES[@]}"; do
    echo "  $t"
    for C1 in 14 15; do
        for B in 5 6 7; do
            for S1 in 0 1 2 3 4; do
                if (( C1 - B - S1 < 0 )); then continue; fi
                for C2 in 16 17; do
                    if (( C2 <= C1 )); then continue; fi
                    for S2 in 0 1 2 3 4 5; do
                        if (( S2 < S1 )); then continue; fi
                        if (( C2 - B - S2 < 0 )); then continue; fi
                        o=$(./cachesim -c $C1 -b $B -s $S1 -C $C2 -S $S2 < "traces/${t}.trace" 2>/dev/null) || continue
                        s=$(get_stats "$o")
                        echo "${t},${C1},${B},${S1},${C2},${S2},none,0,${s}" >> "$F1"
                    done
                done
            done
        done
    done
done
echo "  Done: $F1"

echo "Plus-One prefetch search"
F2="$OUTDIR/plus1.csv"
echo "$HDR" > "$F2"
for t in "${TRACES[@]}"; do
    echo "  $t"
    for C1 in 14 15; do
        for B in 5 6 7; do
            for S1 in 0 1 2 3 4; do
                if (( C1 - B - S1 < 0 )); then continue; fi
                for C2 in 16 17; do
                    if (( C2 <= C1 )); then continue; fi
                    for S2 in 0 1 2 3 4 5; do
                        if (( S2 < S1 )); then continue; fi
                        if (( C2 - B - S2 < 0 )); then continue; fi
                        o=$(./cachesim -c $C1 -b $B -s $S1 -C $C2 -S $S2 -F plus1 < "traces/${t}.trace" 2>/dev/null) || continue
                        s=$(get_stats "$o")
                        echo "${t},${C1},${B},${S1},${C2},${S2},plus1,0,${s}" >> "$F2"
                    done
                done
            done
        done
    done
done
echo "  Done: $F2"

echo "Markov prefetch search"
F3="$OUTDIR/markov.csv"
echo "$HDR" > "$F3"
RV=(4 16 32 64 128 256 512)
for t in "${TRACES[@]}"; do
    echo "  $t"
    for C1 in 14 15; do
        for B in 6; do
            for S1 in 1 2 3; do
                if (( C1 - B - S1 < 0 )); then continue; fi
                for C2 in 16 17; do
                    if (( C2 <= C1 )); then continue; fi
                    for S2 in 3 4; do
                        if (( S2 < S1 )); then continue; fi
                        if (( C2 - B - S2 < 0 )); then continue; fi
                        for r in "${RV[@]}"; do
                            o=$(./cachesim -c $C1 -b $B -s $S1 -C $C2 -S $S2 -F markov -r $r < "traces/${t}.trace" 2>/dev/null) || continue
                            s=$(get_stats "$o")
                            echo "${t},${C1},${B},${S1},${C2},${S2},markov,${r},${s}" >> "$F3"
                        done
                    done
                done
            done
        done
    done
done
echo "  Done: $F3"

echo "Hybrid prefetch search"
F4="$OUTDIR/hybrid.csv"
echo "$HDR" > "$F4"
for t in "${TRACES[@]}"; do
    echo "  $t"
    for C1 in 14 15; do
        for B in 6; do
            for S1 in 1 2 3; do
                if (( C1 - B - S1 < 0 )); then continue; fi
                for C2 in 16 17; do
                    if (( C2 <= C1 )); then continue; fi
                    for S2 in 3 4; do
                        if (( S2 < S1 )); then continue; fi
                        if (( C2 - B - S2 < 0 )); then continue; fi
                        for r in "${RV[@]}"; do
                            o=$(./cachesim -c $C1 -b $B -s $S1 -C $C2 -S $S2 -F hybrid -r $r < "traces/${t}.trace" 2>/dev/null) || continue
                            s=$(get_stats "$o")
                            echo "${t},${C1},${B},${S1},${C2},${S2},hybrid,${r},${s}" >> "$F4"
                        done
                    done
                done
            done
        done
    done
done
echo "  Done: $F4"

echo ""
echo "All searches complete"
ls -la "$OUTDIR/"
