#include "cachesim.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <list>

static sim_config_t global_config;

struct CacheBlock {
    uint64_t tag = 0;
    bool valid = false;
    bool dirty = false;
    bool prefetched = false;
    uint64_t last_used = 0;
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
static std::list<uint64_t> markov_row_lru; // front = MRU, back = LRU
static uint64_t prev_block_addr = 0;
static bool has_prev_block = false;
static uint64_t n_markov_entries = 0;
static uint64_t n_markov_rows = 0;

static std::vector<std::vector<CacheBlock>> l1_cache;
static std::vector<std::vector<CacheBlock>> l2_cache;

static uint64_t l1_b_bits;
static uint64_t l1_sets;
static uint64_t l1_idx_bits;
static uint64_t l1_associativity;

static uint64_t l2_b_bits;
static uint64_t l2_sets;
static uint64_t l2_idx_bits;
static uint64_t l2_associativity;

static replacement_policy_t l1_repl_policy;
static replacement_policy_t l2_repl_policy;

// L2: MRU/MIP operations use (1<<32) + incrementing counter (always high)
// LIP insertions use decrementing counter from (1<<32)-1 downward (always low)
// This ensures LIP blocks are always below MRU blocks, and newest LIP is evicted first
static uint64_t l2_mru_counter = 0;
static uint64_t l2_lip_counter = 0;
// L1 timestamp counter
static uint64_t l1_timestamp = 0;

void sim_setup(sim_config_t *config) {
    global_config = *config;
    const cache_config_t& l1_cfg = global_config.l1_config;
    const cache_config_t& l2_cfg = global_config.l2_config;

    uint64_t C1 = l1_cfg.c, B1 = l1_cfg.b, S1 = l1_cfg.s;
    uint64_t C2 = l2_cfg.c, B2 = l2_cfg.b, S2 = l2_cfg.s;

    l1_b_bits = B1;
    l1_idx_bits = C1 - B1 - S1;
    l1_associativity = 1ULL << S1;
    l1_sets = 1ULL << l1_idx_bits;

    l2_b_bits = B2;
    l2_idx_bits = C2 - B2 - S2;
    l2_associativity = 1ULL << S2;
    l2_sets = 1ULL << l2_idx_bits;

    l1_repl_policy = l1_cfg.replace_policy;
    l2_repl_policy = l2_cfg.replace_policy;

    n_markov_entries = 4; // fixed at 4 entries per row
    n_markov_rows = l2_cfg.n_markov_rows;
    markov_table.clear();
    markov_row_lru.clear();
    has_prev_block = false;
    prev_block_addr = 0;

    l1_timestamp = 0;
    l2_mru_counter = 0;
    l2_lip_counter = 0;

    if (!(5 <= l1_b_bits && l1_b_bits <= 7)) {
        std::cerr << "Error: L1 b_bits must be in [5,7]. Got " << l1_b_bits << "\n";
        std::exit(1);
    }
    if (!(5 <= l2_b_bits && l2_b_bits <= 7)) {
        std::cerr << "Error: L2 b_bits must be in [5,7]. Got " << l2_b_bits << "\n";
        std::exit(1);
    }
    if (!l2_cfg.disabled && !(C2 > C1)) {
        std::cerr << "Error: Require C2 > C1. Got C1=" << C1 << " C2=" << C2 << "\n";
        std::exit(1);
    }
    if (!l2_cfg.disabled && !(S2 >= S1)) {
        std::cerr << "Error: Require S2 >= S1. Got S1=" << S1 << " S2=" << S2 << "\n";
        std::exit(1);
    }
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

    l1_cache.assign(l1_sets, std::vector<CacheBlock>(l1_associativity));
    l2_cache.assign(l2_sets, std::vector<CacheBlock>(l2_associativity));

    for (uint64_t i = 0; i < l1_sets; i++)
        for (uint64_t w = 0; w < l1_associativity; w++) {
            l1_cache[i][w] = CacheBlock();
        }
    for (uint64_t i = 0; i < l2_sets; i++)
        for (uint64_t w = 0; w < l2_associativity; w++) {
            l2_cache[i][w] = CacheBlock();
        }
}

// Promote to MRU for L1
static inline void touch_block_l1(CacheBlock &block) {
    block.last_used = ++l1_timestamp;
}

// Promote to MRU for L2
static inline void touch_block_l2(CacheBlock &block) {
    block.last_used = (1ULL << 32) + (++l2_mru_counter);
}

// Insert with appropriate policy for L1 (always MIP)
static inline void insert_block_l1(CacheBlock &block) {
    block.last_used = ++l1_timestamp;
}

// Insert with appropriate policy for L2
static inline void insert_block_l2(CacheBlock &block) {
    if (l2_repl_policy == REPLACEMENT_POLICY_MIP) {
        block.last_used = (1ULL << 32) + (++l2_mru_counter);
    } else {
        // LIP: insert at LRU position with decrementing counter
        // Newest LIP insertion gets smallest value → evicted first (correct LRU position)
        block.last_used = (1ULL << 32) - 1 - (l2_lip_counter++);
    }
}

// Pick victim: prefer invalid blocks, then LRU (smallest last_used)
static int pick_victim(std::vector<CacheBlock>& set) {
    for (int way = 0; way < (int)set.size(); way++) {
        if (!set[way].valid) {
            return way;
        }
    }
    int victim = 0;
    uint64_t smallest = set[0].last_used;
    for (int way = 1; way < (int)set.size(); way++) {
        if (set[way].last_used < smallest) {
            victim = way;
            smallest = set[way].last_used;
        }
    }
    return victim;
}

// Check if a block address is present in L1
static bool is_in_l1(uint64_t block_addr) {
    uint64_t idx = block_addr & ((1ULL << l1_idx_bits) - 1);
    uint64_t tag = block_addr >> l1_idx_bits;
    auto &s = l1_cache[idx];
    for (int w = 0; w < (int)l1_associativity; w++) {
        if (s[w].valid && s[w].tag == tag) return true;
    }
    return false;
}

// Check if a block address is present in L2
static bool is_in_l2(uint64_t block_addr) {
    uint64_t idx = block_addr & ((1ULL << l2_idx_bits) - 1);
    uint64_t tag = block_addr >> l2_idx_bits;
    auto &s = l2_cache[idx];
    for (int w = 0; w < (int)l2_associativity; w++) {
        if (s[w].valid && s[w].tag == tag) return true;
    }
    return false;
}

// Install a prefetched block into L2. Returns true if actually inserted.
static bool prefetch_install_l2(uint64_t pf_block_addr, sim_stats_t *stats) {
    // Check if already in L1 or L2
    if (is_in_l1(pf_block_addr) || is_in_l2(pf_block_addr)) {
        return false;
    }

    uint64_t pf_idx = pf_block_addr & ((1ULL << l2_idx_bits) - 1);
    uint64_t pf_tag = pf_block_addr >> l2_idx_bits;

    auto &pf_set = l2_cache[pf_idx];
    int v = pick_victim(pf_set);
    CacheBlock &victim = pf_set[v];

    // If evicting a prefetched block, count prefetch miss
    if (victim.valid && victim.prefetched) {
        stats->prefetch_misses_l2++;
    }

    victim.valid = true;
    victim.dirty = false;
    victim.tag = pf_tag;
    victim.prefetched = true;
    insert_block_l2(victim);

    stats->prefetches_issued_l2++;
    return true;
}

// Touch Markov row to MRU position
static void markov_touch_row(uint64_t block_addr) {
    for (auto it = markov_row_lru.begin(); it != markov_row_lru.end(); ++it) {
        if (*it == block_addr) {
            markov_row_lru.erase(it);
            break;
        }
    }
    markov_row_lru.push_front(block_addr);
}

// Update Markov table: record transition prev_block -> current_block
static void markov_update(uint64_t current_block_addr) {
    if (!has_prev_block) {
        has_prev_block = true;
        prev_block_addr = current_block_addr;
        return;
    }

    uint64_t A = prev_block_addr;
    uint64_t B = current_block_addr;

    // Check if row A exists
    auto row_it = markov_table.find(A);
    if (row_it != markov_table.end()) {
        // Row A exists
        MarkovRow &row = row_it->second;
        bool found = false;
        for (auto &entry : row.entries) {
            if (entry.next_block_addr == B) {
                entry.count++;
                found = true;
                break;
            }
        }
        if (!found) {
            if (row.entries.size() < n_markov_entries) {
                row.entries.push_back({1, B});
            } else {
                // Evict LFU entry; on tie, evict the one with lower block address
                auto min_it = std::min_element(
                    row.entries.begin(), row.entries.end(),
                    [](const MarkovEntry &a, const MarkovEntry &b) {
                        if (a.count != b.count) return a.count < b.count;
                        return a.next_block_addr < b.next_block_addr;
                    }
                );
                *min_it = {1, B};
            }
        }
        // Mark row A as MRU
        markov_touch_row(A);
    } else {
        // Row A doesn't exist - insert new row
        if (markov_table.size() >= n_markov_rows) {
            // Evict LRU row
            uint64_t lru_key = markov_row_lru.back();
            markov_row_lru.pop_back();
            markov_table.erase(lru_key);
        }
        MarkovRow new_row;
        new_row.entries.push_back({1, B});
        markov_table[A] = new_row;
        markov_row_lru.push_front(A);
    }

    prev_block_addr = current_block_addr;

    // The current miss block's row (if it exists) should also be marked MRU
    // This matches the PDF example where the current miss block appears at MRU
    if (markov_table.find(current_block_addr) != markov_table.end()) {
        markov_touch_row(current_block_addr);
    }
}

// Markov predict: find best successor of block_addr
// Returns true and sets predicted_addr if a prediction exists
static bool markov_predict(uint64_t block_addr, uint64_t &predicted_addr) {
    auto row_it = markov_table.find(block_addr);
    if (row_it == markov_table.end()) return false;
    MarkovRow &row = row_it->second;
    if (row.entries.empty()) return false;

    // Find entry with highest count; on tie, highest block address
    auto best = std::max_element(
        row.entries.begin(), row.entries.end(),
        [](const MarkovEntry &a, const MarkovEntry &b) {
            if (a.count != b.count) return a.count < b.count;
            return a.next_block_addr < b.next_block_addr;
        }
    );
    predicted_addr = best->next_block_addr;
    return true;
}

void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {
    uint64_t l1_index = (addr >> l1_b_bits) & ((1ULL << l1_idx_bits) - 1);
    uint64_t l1_tag = addr >> (l1_b_bits + l1_idx_bits);
    uint64_t l2_index = (addr >> l2_b_bits) & ((1ULL << l2_idx_bits) - 1);
    uint64_t l2_tag = addr >> (l2_b_bits + l2_idx_bits);
    uint64_t block_addr = addr >> l2_b_bits;

    const cache_config_t& l2_cfg = global_config.l2_config;
    bool l2_disabled = l2_cfg.disabled;

    stats->accesses_l1++;
    if (rw == 'R') stats->reads++;
    else stats->writes++;

    // === L1 Lookup ===
    auto &l1_set = l1_cache[l1_index];
    int l1_hit_way = -1;
    for (int way = 0; way < (int)l1_associativity; way++) {
        CacheBlock &blk = l1_set[way];
        if (blk.valid && blk.tag == l1_tag) {
            l1_hit_way = way;
            break;
        }
    }

    if (l1_hit_way != -1) {
        // L1 Hit
        stats->hits_l1++;
        CacheBlock &hit_blk = l1_set[l1_hit_way];
        if (rw == 'W') hit_blk.dirty = true;
        touch_block_l1(hit_blk);
        return;
    }

    // === L1 Miss ===
    stats->misses_l1++;
    int l1_victim_w = pick_victim(l1_set);
    CacheBlock &l1_victim = l1_set[l1_victim_w];

    // Save L1 victim info before overwriting
    bool victim_valid = l1_victim.valid;
    bool victim_dirty = l1_victim.dirty;
    uint64_t victim_tag = l1_victim.tag;

    // === STEP 1: Read from L2 (per spec section 6.2.2) ===
    stats->reads_l2++;
    bool l2_read_hit = false;

    if (!l2_disabled) {
        auto &l2_set = l2_cache[l2_index];
        // L2 lookup
        for (int way = 0; way < (int)l2_associativity; way++) {
            CacheBlock &blk = l2_set[way];
            if (blk.valid && blk.tag == l2_tag) {
                l2_read_hit = true;
                // Check prefetch bit
                if (blk.prefetched) {
                    stats->prefetch_hits_l2++;
                    blk.prefetched = false;
                }
                touch_block_l2(blk);
                break;
            }
        }

        if (l2_read_hit) {
            stats->read_hits_l2++;
        } else {
            // L2 read miss - install requested block in L2
            stats->read_misses_l2++;
            int l2_victim_w = pick_victim(l2_set);
            CacheBlock &l2_v = l2_set[l2_victim_w];
            // Track prefetch miss on eviction
            if (l2_v.valid && l2_v.prefetched) {
                stats->prefetch_misses_l2++;
            }
            l2_v.valid = true;
            l2_v.dirty = false;
            l2_v.tag = l2_tag;
            l2_v.prefetched = false;
            insert_block_l2(l2_v);
        }
    } else {
        // L2 disabled: every read is a miss
        stats->read_misses_l2++;
    }

    // === Prefetch (part of L2 read miss repair, before L1 install and writeback) ===
    if (!l2_disabled && !l2_read_hit) {
        prefetch_algo_t pf_algo = l2_cfg.prefetch_algorithm;

        if (pf_algo == PREFETCH_PLUS_ONE) {
            // +1 prefetcher: prefetch block_addr + 1
            prefetch_install_l2(block_addr + 1, stats);
        }
        else if (pf_algo == PREFETCH_MARKOV) {
            // Step 1: Predict and prefetch
            uint64_t predicted;
            if (markov_predict(block_addr, predicted)) {
                if (predicted != block_addr) {
                    prefetch_install_l2(predicted, stats);
                }
            }
            // Step 2: Update Markov table
            markov_update(block_addr);
        }
        else if (pf_algo == PREFETCH_HYBRID) {
            // Check Markov table for entry X
            auto row_it = markov_table.find(block_addr);
            if (row_it != markov_table.end() && !row_it->second.entries.empty()) {
                // Row entry found: prefetch as predicted by Markov
                uint64_t predicted;
                if (markov_predict(block_addr, predicted)) {
                    if (predicted != block_addr) {
                        prefetch_install_l2(predicted, stats);
                    }
                }
            } else {
                // No row entry: fall back to +1
                prefetch_install_l2(block_addr + 1, stats);
            }
            // Always update Markov table
            markov_update(block_addr);
        }
    }

    // Install block in L1 (after prefetch, before writeback)
    l1_victim.valid = true;
    l1_victim.dirty = (rw == 'W');
    l1_victim.tag = l1_tag;
    l1_victim.prefetched = false;
    insert_block_l1(l1_victim);

    // === STEP 2: Evict and Writeback to L2 (per spec section 6.2.2) ===
    if (victim_valid && victim_dirty) {
        stats->write_backs_l1++;
        stats->writes_l2++;

        if (!l2_disabled) {
            // Reconstruct victim address and find in L2
            uint64_t v_addr = (victim_tag << (l1_b_bits + l1_idx_bits)) | (l1_index << l1_b_bits);
            uint64_t v_l2_idx = (v_addr >> l2_b_bits) & ((1ULL << l2_idx_bits) - 1);
            uint64_t v_l2_tag = v_addr >> (l2_b_bits + l2_idx_bits);

            auto &v_l2_set = l2_cache[v_l2_idx];
            for (int way = 0; way < (int)l2_associativity; way++) {
                CacheBlock &blk = v_l2_set[way];
                if (blk.valid && blk.tag == v_l2_tag) {
                    // WTWNA: block present in L2, move to MRU
                    touch_block_l2(blk);
                    break;
                }
            }
            // If not found in L2: WTWNA means don't install
        }
    }
}

void sim_finish(sim_stats_t *stats) {
    // L1 ratios
    if (stats->accesses_l1 > 0) {
        stats->hit_ratio_l1 = (double)stats->hits_l1 / (double)stats->accesses_l1;
        stats->miss_ratio_l1 = (double)stats->misses_l1 / (double)stats->accesses_l1;
    }

    // L1 hit time
    uint64_t S1 = 0, tmp1 = l1_associativity;
    while (tmp1 > 1) { tmp1 >>= 1; S1++; }
    double l1_ht = L1_HIT_TIME_CONST + L1_HIT_TIME_PER_S * S1;

    // DRAM time
    uint64_t block_size = 1ULL << l1_b_bits;
    double dram_time = DRAM_AT + ((double)block_size / WORD_SIZE) * DRAM_AT_PER_WORD;

    const cache_config_t& l2_cfg = global_config.l2_config;
    if (l2_cfg.disabled) {
        // L2 disabled: HT = 0, AAT = DRAM_TIME
        stats->avg_access_time_l2 = dram_time;
        if (stats->reads_l2 > 0) {
            stats->read_hit_ratio_l2 = 0.0;
            stats->read_miss_ratio_l2 = 1.0;
        }
    } else {
        if (stats->reads_l2 > 0) {
            stats->read_hit_ratio_l2 = (double)stats->read_hits_l2 / (double)stats->reads_l2;
            stats->read_miss_ratio_l2 = (double)stats->read_misses_l2 / (double)stats->reads_l2;
        }
        // L2 hit time
        uint64_t S2 = 0, tmp2 = l2_associativity;
        while (tmp2 > 1) { tmp2 >>= 1; S2++; }
        double l2_ht = L2_HIT_TIME_CONST + L2_HIT_TIME_PER_S * S2;

        // L2 AAT = HT + MR * DRAM_TIME (NOT HR*HT + MR*DRAM)
        stats->avg_access_time_l2 = l2_ht + stats->read_miss_ratio_l2 * dram_time;
    }

    // L1 AAT = HT_L1 + MR_L1 * L2_AAT
    stats->avg_access_time_l1 = l1_ht + stats->miss_ratio_l1 * stats->avg_access_time_l2;
}
