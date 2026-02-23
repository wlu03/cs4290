#!/bin/bash
TRACES=(gcc leela linpack matmul_naive matmul_tiled mcf)
OUTDIR="search"
HDR="trace,C1,B,S1,C2,S2,prefetch,r,L1_AAT,L1_HR,L1_MR,L2_AAT,L2_RHR,L2_RMR,PF_issued,PF_hits,PF_misses,L1_misses,L2_rhits,L2_rmisses,WB_L1"

get_stats() {
    local o="$1"
    echo "$(echo "$o" | grep 'L1 average access time' | awk '{print $NF}'),$(echo "$o" | grep 'L1 hit ratio' | awk '{print $NF}'),$(echo "$o" | grep 'L1 miss ratio' | awk '{print $NF}'),$(echo "$o" | grep 'L2 average access time' | awk '{print $NF}'),$(echo "$o" | grep 'L2 read hit ratio' | awk '{print $NF}'),$(echo "$o" | grep 'L2 read miss ratio' | awk '{print $NF}'),$(echo "$o" | grep 'L2 prefetches issued' | awk '{print $NF}'),$(echo "$o" | grep 'L2 prefetch hits' | awk '{print $NF}'),$(echo "$o" | grep 'L2 prefetch misses' | awk '{print $NF}'),$(echo "$o" | grep 'L1 misses:' | awk '{print $NF}'),$(echo "$o" | grep 'L2 read hits:' | awk '{print $NF}'),$(echo "$o" | grep 'L2 read misses:' | awk '{print $NF}'),$(echo "$o" | grep 'Write-backs from L1' | awk '{print $NF}')"
}

RV=(4 16 32 64 128 256 512)

echo  "Markov search"
F3="${OUTDIR}/markov.csv"
echo "${HDR}" > "${F3}"
for t in "${TRACES[@]}"; do
    echo "  ${t}"
    for C1 in 14 15; do
        for S1 in 1 2 3; do
            if (( C1 - 6 - S1 < 0 )); then continue; fi
            for C2 in 16 17; do
                if (( C2 <= C1 )); then continue; fi
                for S2 in 3 4; do
                    if (( S2 < S1 )); then continue; fi
                    if (( C2 - 6 - S2 < 0 )); then continue; fi
                    for r in "${RV[@]}"; do
                        o=$(./cachesim -c ${C1} -b 6 -s ${S1} -C ${C2} -S ${S2} -F markov -r ${r} < "traces/${t}.trace" 2>/dev/null) || continue
                        s=$(get_stats "$o")
                        echo "${t},${C1},6,${S1},${C2},${S2},markov,${r},${s}" >> "${F3}"
                    done
                done
            done
        done
    done
done
echo "  Done: ${F3}"

echo "Hybrid search"
F4="${OUTDIR}/hybrid.csv"
echo "${HDR}" > "${F4}"
for t in "${TRACES[@]}"; do
    echo "  ${t}"
    for C1 in 14 15; do
        for S1 in 1 2 3; do
            if (( C1 - 6 - S1 < 0 )); then continue; fi
            for C2 in 16 17; do
                if (( C2 <= C1 )); then continue; fi
                for S2 in 3 4; do
                    if (( S2 < S1 )); then continue; fi
                    if (( C2 - 6 - S2 < 0 )); then continue; fi
                    for r in "${RV[@]}"; do
                        o=$(./cachesim -c ${C1} -b 6 -s ${S1} -C ${C2} -S ${S2} -F hybrid -r ${r} < "traces/${t}.trace" 2>/dev/null) || continue
                        s=$(get_stats "$o")
                        echo "${t},${C1},6,${S1},${C2},${S2},hybrid,${r},${s}" >> "${F4}"
                    done
                done
            done
        done
    done
done
echo "  Done: ${F4}"
echo "Complete"
