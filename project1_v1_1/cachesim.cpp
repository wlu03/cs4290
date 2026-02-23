#include "cachesim.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <stdexcept>
#include <string>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <unordered_map>

// Vars for the Cache states
// Global config pointer
static sim_config_t global_config;
struct CacheBlock {
    uint64_t tag = 0;
    bool valid = false;
    bool dirty = false;
    uint64_t last_used = 0; // timestamp
};

// Markov Prefetcher State
struct MarkovEntry {
    uint64_t count;
    uint64_t next_block_addr;
};
struct MarkovRow {
    std::vector<MarkovEntry> entries;  
};
static std::unordered_map<uint64_t, MarkovRow> markov_table;
static uint64_t prev_block_addr = 0;
static bool has_prev_block = false;
static uint64_t n_markov_entries = 0;



static std::vector<std::vector<CacheBlock>> l1_cache;
static std::vector<std::vector<CacheBlock>> l2_cache;
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

static uint64_t l2_b_bits; // Block Bits (B)
static uint64_t l2_sets; // 2^{C2 - B - S2}
static uint64_t l2_idx_bits; // Log_2(Number of Sets) = C2-B-S2
static uint64_t l2_associativity; // Number of Ways 2^S2


/**
 * Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * TODO: You're responsible for completing this routine
 */
void sim_setup(sim_config_t *config) {

    // Setup Config for L1 and L2
    global_config = *config;
    const cache_config_t& l1_cfg = global_config.l1_config;
    const cache_config_t& l2_cfg = global_config.l2_config;

    uint64_t C1 = l1_cfg.c, B1 = l1_cfg.b, S1 = l1_cfg.s;
    uint64_t C2 = l2_cfg.c, B2 = l2_cfg.b, S2 = l2_cfg.s;

    // L1 derived
    l1_b_bits = B1;
    l1_idx_bits = C1 - B1 - S1;
    l1_associativity = 1ULL << S1;
    l1_sets = 1ULL << l1_idx_bits;
    // L2 derived
    l2_b_bits = B2;
    l2_idx_bits = C2 - B2 - S2;
    l2_associativity = 1ULL << S2;
    l2_sets = 1ULL << l2_idx_bits;

    // Markov Table Setup
    n_markov_entries = l2_cfg.n_markov_rows;
    markov_table.clear();
    has_prev_block = false;
    prev_block_addr = 0;


    // Restriction
    if (!(5 <= l1_b_bits && l1_b_bits <= 7)) {
        std::cerr << "Error: L1 b_bits must be in [5,7]. Got " << l1_b_bits << "\n";
        std::exit(1);
    }
    if (!(5 <= l2_b_bits && l2_b_bits <= 7)) {
        std::cerr << "Error: L2 b_bits must be in [5,7]. Got " << l2_b_bits << "\n";
        std::exit(1);
    }
    if (!(C2 > C1)) {
        std::cerr << "Error: Require C2 > C1. Got C1=" << C1 << " C2=" << C2 << "\n";
        std::exit(1);
    }
    if (!(S2 >= S1)) {
        std::cerr << "Error: Require S2 >= S1. Got S1=" << S1 << " S2=" << S2 << "\n";
        std::exit(1);
    }
    // Markov row validation for L2 only
    if ((l2_cfg.prefetch_algorithm == PREFETCH_MARKOV || l2_cfg.prefetch_algorithm == PREFETCH_HYBRID)) {
        if (l2_cfg.n_markov_rows == 0) {
            std::cerr << "Error: Markov rows must be > 0 for Markov/Hybrid. Got " << l2_cfg.n_markov_rows << "\n";
            std::exit(1);
        }
    } else {
        if (l2_cfg.n_markov_rows != 0) {
            std::cerr << "Invalid configuration! Number of Markov rows should be 0 if not using the Markov or Hybrid prefetching algorithms\n";
            std::exit(1);
        }
    }
    // Allocate caches (assign clears + resizes)
    l1_cache.assign(l1_sets, std::vector<CacheBlock>(l1_associativity));
    l2_cache.assign(l2_sets, std::vector<CacheBlock>(l2_associativity));

    // Initialize blocks
    for (uint64_t i = 0; i < l1_sets; i++) {
        for (uint64_t w = 0; w < l1_associativity; w++) {
            l1_cache[i][w].valid = false;
            l1_cache[i][w].dirty = false;
            l1_cache[i][w].tag = 0;
            l1_cache[i][w].last_used = 0;
        }
    }
    for (uint64_t i = 0; i < l2_sets; i++) {
        for (uint64_t w = 0; w < l2_associativity; w++) {
            l2_cache[i][w].valid = false;
            l2_cache[i][w].dirty = false;
            l2_cache[i][w].tag = 0;
            l2_cache[i][w].last_used = 0;
        }
    }

}
static inline void touch_block(CacheBlock &block) {
    block.last_used = ++global_count;
}
static int pick_victim(std::vector<CacheBlock>& set) {
    
    // Use an invalid block or any free blocks that are not occupied
    for (int way = 0; way < (int)set.size(); way++) {
        if (!set[way].valid) {
            return way;
        }
    }

    // if there are no valid ways that were return, evict the LRU block
    int victim = 0;  // start with way 0 as default
    uint64_t smallest_cnt = set[0].last_used;
    for (int way = 1; way < (int)set.size(); way++) {
        uint64_t cnt = set[way].last_used;
        if (cnt < smallest_cnt) {
            victim = way;
            smallest_cnt = cnt;
        }
    }
    return victim;
}

void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {
    // calc L1 cache configs
    uint64_t l1_offset = addr & ((1ULL << l1_b_bits) - 1);
    uint64_t l1_index = (addr >> l1_b_bits) & ((1ULL << l1_idx_bits) - 1);
    uint64_t l1_tag = addr >> (l1_b_bits + l1_idx_bits);
    // calc L2 cache configs
    uint64_t l2_offset = addr & ((1ULL << l2_b_bits) - 1);
    uint64_t l2_index = (addr >> l2_b_bits) & ((1ULL << l2_idx_bits) - 1);
    uint64_t l2_tag = addr >> (l2_b_bits + l2_idx_bits);
    // for the markov table
    uint64_t block_addr = addr >> l2_b_bits;
    const cache_config_t& l2_cfg = global_config.l2_config;
    bool l2_disabled = l2_cfg.disabled;

    // update the stats for read or write
    stats->accesses_l1++;
    if (rw == 'R') {
        stats->reads++;
    } else {
        stats->writes++;
    }

    // FIRST: Do L1 Lookup
        // Find if the requested line exsits in the indexed L1 set
    auto &l1_set = l1_cache[l1_index];
    int l1_hit_way = -1; // -1 if miss
    CacheBlock* l1_hit_block = nullptr;
    for (int way = 0; way < (int)l1_associativity; way++) {
        CacheBlock &blk = l1_set[way];

        // Hit: Valid + Tag (equals)
        if (blk.tag == l1_tag && blk.valid) {
            l1_hit_way = way;
            l1_hit_block = &blk;
            break;
        }
    }

    // L1 Hit
    if (l1_hit_way != -1) {
        stats->hits_l1++;
        // Wirte Back Cache on Write Hit, mark the line dirty (data modified in cache)
        if (rw == 'W') {
            l1_hit_block->dirty = true;
        }
        touch_block(*l1_hit_block);
        return;
    }
    // L1 MISS
    stats->misses_l1++;
    int l1_victim_w = pick_victim(l1_set);
    CacheBlock &l1_victim = l1_set[l1_victim_w];
    // if evicting valid dirty line from L1, we must wrtie it back (WB)
        // this writeback goes to the next level (L2 or memory depending on config)
    if (l1_victim.valid == true && l1_victim.dirty == true) {
        stats->write_backs_l1++;
        stats->writes_l2++;
    }
    
    // -----------------------------
    // L2 Lookup (Only if enabled)
    bool l2_hit = false;
    int l2_hit_way = -1;
    CacheBlock* l2_hit_block = nullptr;
    if (!l2_disabled) {
        auto &l2_set = l2_cache[l2_index];
        for (int way = 0; way < (int)l2_associativity; way++) {
            CacheBlock &blk = l2_set[way];
            if (blk.tag == l2_tag && blk.valid) {
                l2_hit = true;
                l2_hit_way = way;
                l2_hit_block = &blk;
                break;
            }
        }
    }
    // L1 Miss cause read on the next level
    stats->reads_l2++;
    

    // L2 Disable = True
    if (l2_disabled) {
        // if L2 is disabled, every demand read goes to memory so its a read miss
        stats->read_misses_l2++;
        // install the fetched line directly into L1 (write-allocate behavior).
        l1_victim.valid = true;
        if(rw == 'W') {
            l1_victim.dirty = true;
        }
        l1_victim.tag = l1_tag;
        touch_block(l1_victim);
        return;
    }

    // L2 Disable = False 
    if (l2_hit) { // request block found in L2
        stats->read_hits_l2++;
        l1_victim.valid = true;
        if(rw == 'W') {
            l1_victim.dirty = true;
        }
        l1_victim.tag = l1_tag;
        touch_block(l1_victim);
    } else {
        // REQUESTED block is NOT found in L2 (allocate on miss)
        stats->read_misses_l2++;

        // Install block in L2 (insert at LRU position)
        auto &l2_set = l2_cache[l2_index];
        int l2_victim_way = pick_victim(l2_set);
        CacheBlock &l2_victim = l2_set[l2_victim_way];

        l2_victim.valid = true;
        l2_victim.dirty = false;
        l2_victim.tag = l2_tag;
        touch_block(l2_victim); // LRU insertion

        // Install block in L1
        l1_victim.valid = true;
        if(rw == 'W') {
            l1_victim.dirty = true;
        }
        l1_victim.tag = l1_tag;
        touch_block(l1_victim);
    }
    // RUN PREFETCHER LOGIC: on L1 MISS if enabled
    prefetch_algo_t prefetch_algo = l2_cfg.prefetch_algorithm;

    // prefetcher next sequential line
    if (prefetch_algo == PREFETCH_PLUS_ONE || prefetch_algo == PREFETCH_HYBRID) {

        // calculate NEXT BLOCK data
        uint64_t plus_one_block = block_addr + 1;
        uint64_t plus_one_addr = plus_one_block << l2_b_bits;
        uint64_t plus_one_index = (plus_one_addr >> l2_b_bits) & ((1ULL << l2_idx_bits) - 1);
        uint64_t plus_one_tag = plus_one_addr >> (l2_b_bits + l2_idx_bits);

        auto &prefetch_set = l2_cache[plus_one_index];
        bool prefetch_hit = false;
        for (int way = 0; way < (int)l2_associativity; way++) {
            CacheBlock &blk = prefetch_set[way];
            if (blk.tag == plus_one_tag && blk.valid == true) {
                prefetch_hit = true;
                break;
            }
        }
        stats->prefetches_issued_l2++;

        // if prefetch hit, block was already in cache ( no allocation was needed)
        if (prefetch_hit) {
            stats->prefetch_hits_l2++;
        } else {
        // prefetch miss mean the block is not in L2, so we need to allocate it 
            stats->prefetch_misses_l2++;

            // choose victim way in target set 
            int pf_victim_way = pick_victim(prefetch_set);
            CacheBlock &pf_victim = prefetch_set[pf_victim_way];
            pf_victim.valid = true;
            pf_victim.dirty = false;
            pf_victim.tag = plus_one_tag;
            touch_block(pf_victim);
        }
    }

    // MARKOV Prefetcher: learn transition and predict most likely successor of current lbock and prefetch it
    if (prefetch_algo == PREFETCH_MARKOV || prefetch_algo == PREFETCH_HYBRID) {
        // if i have seen previous block, record transition
            // previous_block_addr -> block_addr
        if (has_prev_block) {
            MarkovRow &row = markov_table[prev_block_addr];
            bool found = false; // find existing entry (next == current_block_addr)
            for (auto &entry : row.entries) {

                // if the next_block_addr is in the hashatble,
                    // just increment
                if (entry.next_block_addr == block_addr) {
                    entry.count++;
                    found = true;
                    break;
                }
            }

            // NOT FOUND: INSERT IT (LFU for eviction, LRU on tie)
            if (!found) {
                // If it's not full just add it
                if (row.entries.size() < n_markov_entries) {
                    row.entries.push_back({1, block_addr});
                } else {
                    // lfu eviction
                    auto min_it = std::min_element(
                        row.entries.begin(),
                        row.entries.end(),
                        [](const MarkovEntry &a, const MarkovEntry &b) {
                            return a.count < b.count;
                        }
                    );
                    *min_it = {1, block_addr};
                }
            }
        }
        // Update global previous block tracker for next access
        prev_block_addr = block_addr;
        has_prev_block = true;

        // Prefetch best successor
        MarkovRow &currentMarkovRow = markov_table[block_addr];

        // prefetch if there is at least one succesor
        if (!currentMarkovRow.entries.empty()) {
            // iterators for the vector succesors
            auto entriesBeginIt = currentMarkovRow.entries.begin();
            auto entriesEndIt = currentMarkovRow.entries.end();

            // use comparator to pick entry with hgihest count
            auto isLowerCount = [](const MarkovEntry &leftEntry, const MarkovEntry &rightEntry) {
                uint64_t leftCountValue  = leftEntry.count;
                uint64_t rightCountValue = rightEntry.count;
                bool leftIsLower = (leftCountValue < rightCountValue);
                return leftIsLower;
            };
            
            // successor with the highest transition count (most likey block)
            auto bestEntryIt = std::max_element(entriesBeginIt, entriesEndIt, isLowerCount);
            uint64_t predictedPrefetchBlockAddr = bestEntryIt->next_block_addr;
            // dont prefetcher same block 
            if (predictedPrefetchBlockAddr != block_addr) {
                uint64_t predictedL2Index = predictedPrefetchBlockAddr & ((1ULL << l2_idx_bits) - 1);
                uint64_t predictedL2Tag = predictedPrefetchBlockAddr >> l2_idx_bits;
                // select l2 set that the predicted line map to
                auto &prefetchSet = l2_cache[predictedL2Index];

                // see if predicted line is already in l2
                bool prefetchHit = false;
                for (int way = 0; way < (int)l2_associativity; way++) {
                    CacheBlock &candidate = prefetchSet[way];
                    // prefetch hit (valid line and matches)
                    if (candidate.valid == true && candidate.tag == predictedL2Tag) {
                        prefetchHit = true;
                        break;
                    }
                }
                stats->prefetches_issued_l2++;
                if (prefetchHit) {
                    stats->prefetch_hits_l2++;
                } else {
                    stats->prefetch_misses_l2++;
                    int pf_victim_way = pick_victim(prefetchSet);
                    CacheBlock &pf_victim = prefetchSet[pf_victim_way];
                    pf_victim.valid = true;
                    pf_victim.dirty = false;
                    pf_victim.tag = predictedL2Tag;
                    touch_block(pf_victim);
                }
            }
        }
    }
    return;
}
void sim_finish(sim_stats_t *stats) {
    // L1 ratios
    double l1_accesses = (double) stats->accesses_l1;
    double l1_hits = (double) stats->hits_l1; 
    double l1_misses = (double)stats->misses_l1;
    double l2_reads = (double)stats->reads_l2;
    double l2_hits = (double)stats->read_hits_l2;
    double l2_misses = (double)stats->read_misses_l2;
    if (stats->accesses_l1 > 0) {
        stats->hit_ratio_l1 = l1_hits / l1_accesses; 
        stats->miss_ratio_l1 = l1_misses / l1_accesses;
    }

    // L1 hit time = L1_HIT_TIME_CONSTANT + HIT_TIME_PER_S * S
    uint64_t S1 = 0;
    uint64_t temp = l1_associativity;
    while (temp > 1) { 
        temp >>= 1; 
        S1++; 
    }
    double l1_hit_time = L1_HIT_TIME_CONST + L1_HIT_TIME_PER_S * S1;

    // L2 AAT when disabled = DRAM time
        // DRAM_AT + (block_size / WORD_SIZE) * DRAM_AT_PER_WORD
    uint64_t block_size = 1ULL << l1_b_bits;
    double dram_time = DRAM_AT + ((double)block_size / WORD_SIZE) * DRAM_AT_PER_WORD;
    // Only set L2 AAT and miss/hit ratios if L2 is disabled
    const cache_config_t& l2_cfg = global_config.l2_config;
    if (l2_cfg.disabled) {
        stats->avg_access_time_l2 = dram_time;
        stats->read_misses_l2 = stats->reads_l2;
        stats->read_hits_l2 = 0;
        if (stats->reads_l2 > 0) {
            stats->read_hit_ratio_l2 = 0.0;
            stats->read_miss_ratio_l2 = 1.0;
        }
    } else {
        if (stats->reads_l2 > 0) {
            stats->read_hit_ratio_l2 = l2_hits / l2_reads;
            stats->read_miss_ratio_l2 = l2_misses / l2_reads;
        }
        // L2 AAT
            // Use DRAM time for misses, L2 hit time for hits
        uint64_t S2 = 0, temp2 = l2_associativity;
        while (temp2 > 1) { 
            temp2 >>= 1; 
            S2++; 
        }
        double l2_hit_time = L2_HIT_TIME_CONST + L2_HIT_TIME_PER_S * S2;
        stats->avg_access_time_l2 = stats->read_hit_ratio_l2 * l2_hit_time + stats->read_miss_ratio_l2 * dram_time;
    }

    // L1 AAT = L1_HT + miss_ratio * L2_AAT
    stats->avg_access_time_l1 = l1_hit_time + stats->miss_ratio_l1 * stats->avg_access_time_l2;
}
