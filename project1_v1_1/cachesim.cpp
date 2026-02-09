#include "cachesim.hpp"
#include <vector>

// Vars for the Cache states
struct CacheBlock {
    uint64_t tag = 0;
    bool valid = false;
    bool dirty = false;
    uint64_t lru_count = 0;
};
static std::vector<std::vector<CacheBlock>> l1_cache;
// L1 Cache: Outer Vector: sets within the cache
// L1 Cache: Inner Vector: ways within a set, each way is a block (line)
// Total Cache Size is 2^{C_1} byte 
// Block Size is 2^B bytes
// S_1 is L1 Associativity, 2^{S_1} ways or blocks per set
// Number of Blocks is: 
    //  (Total Cache Size)/(Block Size)
    //  (2^{C_1}/2^B) = 2^{C_1 - B}
// Number of sets is: 
    //  (Number of Blocks)/(Number of Ways or Blocks per Set)
    //  (2^{C_1 - B})/2^{S_1} = 2^{C_1 - B - S_1}

static uint64_t l1_b_bits; // Block Bits (B)
static uint64_t l1_sets; // 2^{C1 - B - S1}
static uint64_t l1_idx_bits; // Log_2(Number of Sets) = C1-B-S1
static uint64_t l1_associativity; // Number of Ways 2^S1
static uint64_t global_count = 0;

/**
 * Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * TODO: You're responsible for completing this routine
 */
void sim_setup(sim_config_t *config) {
    const cache_config_t& l1_cfg = config->l1_config; // & used because it's passed by ref
    uint64_t C1 = l1_cfg.c;
    uint64_t B = l1_cfg.b;
    uint64_t S1 = l1_cfg.s;

    // store as global variables
    l1_b_bits = B;
    l1_idx_bits = C1-B-S1;
    l1_associativity = 1ULL << S1;
    l1_sets = 1ULL << l1_idx_bits;

    // Allocate Cache: [sets][ways]
    l1_cache.clear();
    l1_cache.resize(l1_sets, std::vector<CacheBlock>(l1_associativity));

    // Initialize Cache Blocks
    for (uint64_t i = 0; i < l1_sets; i++) {
        for (uint64_t w = 0; w < l1_associativity; w++) {
            l1_cache[i][w].valid = false;
            l1_cache[i][w].dirty = false;
            l1_cache[i][w].tag = 0;
            l1_cache[i][w].lru_count = 0;
        }
    }

}
/**
 * Subroutine that simulates the cache one trace event at a time.
 * TODO: You're responsible for completing this routine
 */
void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {

}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * TODO: You're responsible for completing this routine
 */
void sim_finish(sim_stats_t *stats) {
