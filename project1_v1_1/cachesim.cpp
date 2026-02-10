#include "cachesim.hpp"
#include <vector>

// Vars for the Cache states
struct CacheBlock {
    uint64_t tag = 0;
    bool valid = false;
    bool dirty = false;
    uint64_t last_used = 0; // timestamp
};
struct MarkovEntry {
    uint64_t count;
    uint64_t next_block_addr;
}
struct MarkovRow {
    std::vector<MarkovEntry> entries;
    
}
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
    const cache_config_t& l1_cfg = config->l1_config;
    uint64_t C1 = l1_cfg.c;
    uint64_t B = l1_cfg.b;
    uint64_t S1 = l1_cfg.s;
    const cache_config_t& l2_cfg = config->l2_config;
    uint64_t C2 = l2_cfg.c;
    uint64_t B = l2_cfg.b;
    uint64_t S2 = l2_cfg.s;
    l2_cfg.n_markov_rows;

    // Setup Up Markov Table
    

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
            l1_cache[i][w].last_used = 0;
        }
    }

}
static inline void touch_block(CacheBlock &block) {
    block.last_used = ++global_count;
}
static int pick_victim(std::vector<CacheBlock>& set) {
    
    // 1) Use an invalid block
    for (int way = 0; way < (int)set.size(); way++) {
        if (!set[way].valid) {
            return way;
        }
    }

    // 2) Evict LRU otherwise
    int victim = 0;  // Start with way 0 as default
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
/**
 * Subroutine that simulates the cache one trace event at a time.
 * TODO: You're responsible for completing this routine
 */
void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {
    // rw, R for read, W for write
    // addr: 64 bit address
    // stat: referenced which needs to be updated

    stats->accesses_l1 += 1;

    // Decompose the Address, Split 64-bit address
    uint64_t offset_bits = addr & ((1ULL << l1_b_bits) - 1);
    uint64_t index_bits = (addr >> l1_b_bits) & ((1ULL << l1_idx_bits) - 1);
    uint64_t tag = addr >> (l1_b_bits + l1_idx_bits);
    // >> 
        // 0b1010 1100, then (0b1010 1100) >> 4 equals 0b0000 1010
    // <<
        // 0b1010 1100, then (0b1010 1100) >> 4 equals 0b1100 0000

    // Search the Cache (Check for Hit)
        // If any one way in the set has valid == 1 or block.tag == tag then its a HIT
    auto &indexed_set = l1_cache[index_bits];
    int hit_way = -1;
    CacheBlock* hit_block = nullptr;
    for (int way = 0; way < (int)l1_associativity; way++) {
        CacheBlock &blk = indexed_set[way];
        if (blk.tag == tag && blk.valid) {
            hit_way = way;
            hit_block = &blk;
            break; 
        }
    }
    // If cache_hit is -1, Cache Miss
    if (hit_way == -1) {
        stats->misses_l1 += 1;
    } else {
        stats->hits_l1 += 1;
    }

    // READ 
    if (rw == 'R' || rw == 'r') {
        stats->reads++;
        // READ HIT
            // 1) Update block LRU Count
            // 2) Return the data from cache line
        if (hit_way != -1 and hit_block != nullptr) {
            touch_block(*hit_block); 
            return;
        }
        // READ MISS
            // 1) Choose a victim, look into pick_victim function
            // 2) if victim.dirty true, writeback the victim to next blk
            // 3) Fetch requested blk from L2->L1
            // 4) Install new block (Valid=1, Tag=New Tag, Dirty=0)
            // 5) MRU in the set
            // 6) Return this data 
        int victim_way = pick_victim(indexed_set);
        CacheBlock &victim = indexed_set[victim_way];
        if (victim.valid && victim.dirty) {
            stats->write_backs_l1++;
            stats->writes_l2++;  // writeback goes to L2
        }
        stats->reads_l2++;  // fetch block from L2
        // Bring New Block
        victim.valid = true;
        victim.dirty = false;
        victim.tag = tag;
        touch_block(victim);
        return;
    
    } else if (rw == 'W' || rw == 'w') {
        stats->writes++;
        // WRITE HIT
            // dont write to mem
            // modify the cache line and mark it dirty
        if (hit_way != -1 and hit_block != nullptr) {
            hit_block->dirty = true;
            touch_block(*hit_block);
            return;
        } else {
        // WRITE MISS
            // 1) Choose Victim Using LRU
            // 2) If Victim is dirty writeback victim to next block
            // 3) Fetch the request block from L2->L1 (allocate on miss)
            // 4) Install the new block (valid=1, tag = new tag)
            // 5) Perform the write on new line
            // 6) Set dirty = 1
            // 7) MRU insertion 
            int victim_way = pick_victim(indexed_set);
            CacheBlock &victim = indexed_set[victim_way];
            if (victim.valid && victim.dirty) {
                stats->write_backs_l1++;
                stats->writes_l2++;  // writeback goes to L2
            }
            stats->reads_l2++;  // fetch block from L2 (write-allocate)
            // Bring New Block
            victim.valid = true;
            victim.dirty = true;  // Writing to block, so dirty
            victim.tag = tag;
            touch_block(victim);
            return;
        }
    }
}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * TODO: You're responsible for completing this routine
 */
void sim_finish(sim_stats_t *stats) {
    // L1 ratios
    if (stats->accesses_l1 > 0) {
        stats->hit_ratio_l1 = (double)stats->hits_l1 / stats->accesses_l1;
        stats->miss_ratio_l1 = (double)stats->misses_l1 / stats->accesses_l1;
    }

    // L1 hit time = HIT_TIME_CONST + HIT_TIME_PER_S * S
    uint64_t S1 = 0, temp = l1_associativity;
    while (temp > 1) { temp >>= 1; S1++; }
    double l1_hit_time = L1_HIT_TIME_CONST + L1_HIT_TIME_PER_S * S1;

    // L2 AAT when disabled = DRAM time
    // DRAM_AT + (block_size / WORD_SIZE) * DRAM_AT_PER_WORD
    uint64_t block_size = 1ULL << l1_b_bits;
    double dram_time = DRAM_AT + ((double)block_size / WORD_SIZE) * DRAM_AT_PER_WORD;
    stats->avg_access_time_l2 = dram_time;

    // L2 read stats (when L2 disabled, all reads are misses)
    stats->read_misses_l2 = stats->reads_l2;
    stats->read_hits_l2 = 0;
    if (stats->reads_l2 > 0) {
        stats->read_hit_ratio_l2 = 0.0;
        stats->read_miss_ratio_l2 = 1.0;
    }

    // L1 AAT = L1_HT + miss_ratio * L2_AAT
    stats->avg_access_time_l1 = l1_hit_time + stats->miss_ratio_l1 * stats->avg_access_time_l2;
}
