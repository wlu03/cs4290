# Two-Level Cache Simulator

Two-level (L1 + L2) cache simulator with configurable prefetching strategies. Built for CS 4290 at Georgia Tech.

The simulator models an L1 cache with write-back, write-allocate (WBWA) policy and an L2 cache with write-through, write-no-allocate (WTWNA) policy. It supports +1, Markov, and hybrid prefetchers.

## Analysis

The full cache analysis — best configurations, diminishing returns, prefetcher comparisons, and metadata calculations — is in the notebook:

**[Cache Analysis Notebook](project1_v1_1/search/cache_analysis.ipynb)**

Exhaustively tested 4,896 configurations across six traces (gcc, leela, linpack, matmul_naive, matmul_tiled, mcf) varying $C_1, B, S_1, C_2, S_2$ and prefetcher mode.

## File Structure

| File | Purpose |
|---|---|
| `cachesim.cpp` | Core implementation: `sim_setup`, `sim_access`, `sim_finish` |
| `cachesim.hpp` | Config structs, constants, timing formulas |
| `cachesim_driver.cpp` | CLI argument parsing and trace I/O |
| `traces/` | Full test traces |
| `short_traces/` | Smaller traces for debugging |
| `ref_outs/` | Reference outputs for validation |

## Build and Test

```bash
cd project1_v1_1
make
./cachesim -D < short_traces/short_gcc.trace   # L1 only
./cachesim < short_traces/short_gcc.trace       # L1 + L2
./cachesim -F plus1 < traces/gcc.trace          # +1 prefetcher
./validate_undergrad.sh                         # Run all validation tests
```

## Key Findings

- **Plus-One prefetcher** provides the best AAT improvement across all traces, especially linpack (−30.8%)
- **Direct-mapped L1** ($S_1=0$) is universally optimal — the hit-time penalty per doubling of ways outweighs conflict-miss savings
- **Markov table size $r$** has virtually no effect on AAT — $r=4$ is sufficient since most blocks have very few distinct successors
- **128-byte blocks** ($B=7$) benefit most traces by amortizing DRAM access over more useful bytes
