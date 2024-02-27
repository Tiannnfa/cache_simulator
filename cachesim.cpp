#include "cachesim.hpp"

cache *L1;
cache *L2;

uint64_t prev_block_addr = 0x0;

/**
 * Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * TODO: You're responsible for completing this routine
 */

void sim_setup(sim_config_t *config)
{
    L1 = (cache *)malloc(sizeof(cache));
    L2 = (cache *)malloc(sizeof(cache));

    L1->config = config->l1_config;
    L2->config = config->l2_config;

    L1->timestamp_counter = 1UL << (config->l1_config.c - config->l1_config.b + 1);
    L2->timestamp_counter = 1UL << (config->l2_config.c - config->l2_config.b + 1);
    // L1->timestamp_counter = 0;
    // L2->timestamp_counter = 0;

    // Calculate the number of sets for L1 and L2
    uint64_t num_sets_L1 = 1UL << (L1->config.c - L1->config.b - L1->config.s);
    uint64_t num_sets_L2 = 1UL << (L2->config.c - L2->config.b - L2->config.s);

    // Allocate sets for L1
    L1->sets = (set *)malloc(num_sets_L1 * sizeof(set));
    for (uint64_t i = 0; i < num_sets_L1; ++i)
    {
        L1->sets[i].blocks = (block *)malloc(sizeof(block) * (1UL << (L1->config.s)));
        memset(L1->sets[i].blocks, 0, sizeof(block) * (1UL << (L1->config.s)));
    }

    // Allocate sets for L2
    L2->sets = (set *)malloc(num_sets_L2 * sizeof(set));
    for (uint64_t i = 0; i < num_sets_L2; ++i)
    {
        L2->sets[i].blocks = (block *)malloc(sizeof(block) * (1UL << (L2->config.s)));
        memset(L2->sets[i].blocks, 0, sizeof(block) * (1UL << (L2->config.s)));
    }
}

/**
 * Subroutine that simulates the cache one trace event at a time.
 * TODO: You're responsible for completing this routine
 */
void sim_access(char rw, uint64_t addr, sim_stats_t *stats)
{
    uint64_t tag = getTag(addr, L1);
    uint64_t index = getIndex(addr, L1);
    uint64_t num_blocks = 1UL << (L1->config.s);
    uint64_t l1_target = isInCache(rw, addr, stats, L1);

#ifdef DEBUG
    printf("Time: %d Address: 0x%lx Read/Write: %c \n", L1->timestamp_counter, addr, rw);
    printf("L1 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx \n", addr, tag, index);
#endif

    if (L1->config.replace_policy == REPLACE_POLICY_LRU && L2->config.replace_policy == REPLACE_POLICY_LRU)
    {
        if (l1_target == UINT64_MAX)
        {
#ifdef DEBUG
            printf("L1 miss\n");
#endif
            // L1 Write miss
            if (rw == 'W')
            {
                // make sure whether cache set is full
                uint64_t empty_block = findEmptyBlockIndex(L1, index, num_blocks);
                // Still have empty blocks
                if (empty_block != UINT64_MAX)
                {
                    // When L2 is disabled
                    if (L2->config.disabled)
                    {
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setDirtyBit(L1, index, empty_block);
                        updateTimestamp(L1, index, empty_block);
                        stats->reads_l2++;
                        stats->read_misses_l2++;
                    }
                    else // When L2 is enabled
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);

#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                updateTimestamp(L2, l2_index, l2_empty_block);
                            }
                            else
                            {
                                // LRU
                                uint64_t l2_lru_index = findLRUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);
                                // Whether this LRU is dirty?

                                setTag(L2, l2_index, l2_lru_index, l2_tag);
                                updateTimestamp(L2, l2_index, l2_lru_index);

#ifdef DEBUG
                                printf("Evict from L2: block with valid=%d and index=0x%lx\n", 0, l2_index);
#endif
                            }
                            // deal with prefetch
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            updateTimestamp(L2, l2_index, l2_target);

#ifdef DEBUG
                            printf("L2 read hit");
#endif
                        }
                        // Then Add the needed block to L1
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setDirtyBit(L1, index, empty_block);
                        updateTimestamp(L1, index, empty_block);
                    }
                }
                else // L1 is full set of blocks and eviction is needed
                {
                    // LRU
                    uint64_t lru_index = findLRUBlockIndex(&(L1->sets[index]), num_blocks);

                    if (L2->config.disabled)
                    {
                        // Whether this LRU is dirty?
                        if (getDirtyBit(L1, index, lru_index))
                        {
                            setTag(L1, index, lru_index, tag);
                            setDirtyBit(L1, index, lru_index);
                            updateTimestamp(L1, index, lru_index);
                            stats->reads_l2++;
                            stats->read_misses_l2++;
                            stats->writes_l2++;
                        }
                        else
                        {
                            setTag(L1, index, lru_index, tag);
                            setDirtyBit(L1, index, lru_index);
                            updateTimestamp(L1, index, lru_index);
                            stats->reads_l2++;
                            stats->read_misses_l2++;
                        }
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);
#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx \n", addr, l2_tag, l2_index);
#endif
                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                updateTimestamp(L2, l2_index, l2_empty_block);
                            }
                            else
                            {
                                // LRU
                                uint64_t l2_lru_index = findLRUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);

                                setTag(L2, l2_index, l2_lru_index, l2_tag);
                                updateTimestamp(L2, l2_index, l2_lru_index);
                                // deal with prefetch

#ifdef DEBUG
                                printf("Evicted from L2: block with valid: %d index: 0x%lx\n",
                                       0,
                                       l2_index);
#endif
                            }
                            /* prefetch(L2, addr, stats);
                            prev_block_addr = blockAddrTrans(L2, addr); */
                        }
                        else // When the needed block is in L2
                        {
                            updateTimestamp(L2, l2_index, l2_target);
#ifdef DEBUG
                            printf("L2 read hit\n");
#endif
                        }
                        // Then Add the needed block to L1
                        // While deal with the evicted
                        // While the dirtybit is set,
                        // the block needs to be write in L2
                        if (getDirtyBit(L1, index, lru_index))
                        {
                            uint64_t evicted_tag = L1->sets[index].blocks[lru_index].tag;
                            uint64_t l1_index_bits = (L1->config.c - L1->config.s - L1->config.b);
                            uint64_t l1_offset_bits = (L1->config.b);
                            uint64_t addr = (evicted_tag << (l1_index_bits + l1_offset_bits)) + (index << L1->config.b);
                            uint64_t l2_tag = getTag(addr, L2);
                            uint64_t l2_index = getIndex(addr, L2);
                            uint64_t evicted_target_block = isInCache('W', addr, stats, L2);

                            // Not in L2
                            if (evicted_target_block == UINT64_MAX)
                            {
                            }
                            else
                            {
                                updateTimestamp(L2, l2_index, evicted_target_block);
                            }
                        }
                        setTag(L1, index, lru_index, tag);
                        setDirtyBit(L1, index, lru_index);
                        updateTimestamp(L1, index, lru_index);

                        if (l2_target == UINT64_MAX)
                        {
                            prefetch(L2, addr, stats);
                        }
                    }
#ifdef DEBUG
                    printf("Evict from L1: block with valid=%d, dirty=%d, tag 0x%lx and index=0x%lx\n",
                           L1->sets[index].blocks[lru_index].valid_bit,
                           L1->sets[index].blocks[lru_index].dirty_bit,
                           tag,
                           index);
#endif
                }
            }

            // Read miss
            if (rw == 'R')
            {
                // make sure whether cache set is full
                uint64_t empty_block = findEmptyBlockIndex(L1, index, num_blocks);
                if (empty_block != UINT64_MAX)
                {

                    if (L2->config.disabled)
                    {
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        updateTimestamp(L1, index, empty_block);
                        stats->reads_l2++;
                        stats->read_misses_l2++;
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);
#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif

                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still have empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                updateTimestamp(L2, l2_index, l2_empty_block);
                            }
                            else
                            {
                                // LRU
                                uint64_t l2_lru_index = findLRUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);

                                setTag(L2, l2_index, l2_lru_index, l2_tag);
                                updateTimestamp(L2, l2_index, l2_lru_index);

#ifdef DEBUG
                                printf("Evict from L2: block with valid=%d and index=0x%lx\n", 0, l2_index);
#endif
                            }
                            // deal with prefetch
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            updateTimestamp(L2, l2_index, l2_target);

#ifdef DEBUG
                            printf("L2 read hit");
#endif
                        }
                        // Then Add the needed block to L1
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        updateTimestamp(L1, index, empty_block);
                    }
                }
                else // When No empty blocks in L1
                {
                    // LRU
                    uint64_t lru_index = findLRUBlockIndex(&(L1->sets[index]), num_blocks);

                    if (L2->config.disabled)
                    {
                        // Whether this LRU is dirty?
                        if (getDirtyBit(L1, index, lru_index))
                        {
                            setTag(L1, index, lru_index, tag);
                            clearDirtyBit(L1, index, lru_index);
                            updateTimestamp(L1, index, lru_index);
                            stats->writes_l2++;
                            stats->reads_l2++;
                            stats->read_misses_l2++;
                        }
                        else
                        {
                            setTag(L1, index, lru_index, tag);
                            updateTimestamp(L1, index, lru_index);
                            stats->reads_l2++;
                            stats->read_misses_l2++;
                        }
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);

#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx \n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);

#ifdef DEBUG
                            printf("L2 read miss\n");
#endif
                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                updateTimestamp(L2, l2_index, l2_empty_block);
                            }
                            else
                            {
                                // LRU
                                uint64_t l2_lru_index = findLRUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);
                                setTag(L2, l2_index, l2_lru_index, l2_tag);
                                updateTimestamp(L2, l2_index, l2_lru_index);

#ifdef DEBUG
                                printf("Evicted from L2: block with valid: %d index: 0x%lx\n",
                                       0,
                                       l2_index);
#endif
                            }
                            // deal with prefetch
                            /* prefetch(L2, addr, stats);
                            prev_block_addr = blockAddrTrans(L2, addr); */
                        }
                        else // When the needed block is in L2
                        {
                            updateTimestamp(L2, l2_index, l2_target);
#ifdef DEBUG
                            printf("L2 read hit\n");
#endif
                        }
                        // Then Add the needed block to L1
                        if (getDirtyBit(L1, index, lru_index))
                        {
                            uint64_t evicted_tag = L1->sets[index].blocks[lru_index].tag;
                            uint64_t l1_index_bits = (L1->config.c - L1->config.s - L1->config.b);
                            uint64_t l1_offset_bits = (L1->config.b);
                            uint64_t addr = (evicted_tag << (l1_index_bits + l1_offset_bits)) + (index << L1->config.b);
                            uint64_t l2_tag = getTag(addr, L2);
                            uint64_t l2_index = getIndex(addr, L2);
                            uint64_t evicted_target_block = isInCache('W', addr, stats, L2);
                            // Not in L2
                            if (evicted_target_block == UINT64_MAX)
                            {
                            }
                            else
                            {
                                updateTimestamp(L2, l2_index, evicted_target_block);
                            }
                        }
                        setTag(L1, index, lru_index, tag);
                        clearDirtyBit(L1, index, lru_index);
                        updateTimestamp(L1, index, lru_index);

                        if (l2_target == UINT64_MAX)
                        {
                            prefetch(L2, addr, stats);
                        }
                    }
#ifdef DEBUG
                    printf("Evict from L1: block with valid=%d, dirty=%d, tag 0x%lx and index=0x%lx\n",
                           L1->sets[index].blocks[lru_index].valid_bit,
                           L1->sets[index].blocks[lru_index].dirty_bit,
                           tag,
                           index);
#endif
                }
            }
        }
        else // Hit on cache
        {
#ifdef DEBUG
            printf("L1 hit\n");
#endif

            if (rw == 'W')
            {
                setDirtyBit(L1, index, l1_target);
                updateTimestamp(L1, index, l1_target);
            }
            if (rw == 'R')
            {
                updateTimestamp(L1, index, l1_target);
            }
        }
    }

    if (L1->config.replace_policy == REPLACE_POLICY_LFU && L2->config.replace_policy == REPLACE_POLICY_LFU)
    {
        if (l1_target == UINT64_MAX)
        {
#ifdef DEBUG
            printf("L1 miss\n");
#endif
            // L1 Write miss
            if (rw == 'W')
            {
                // make sure whether cache set is full
                uint64_t empty_block = findEmptyBlockIndex(L1, index, num_blocks);
                // Still have empty blocks
                if (empty_block != UINT64_MAX)
                {
                    // When L2 is disabled
                    if (L2->config.disabled)
                    {
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setDirtyBit(L1, index, empty_block);
                        setMRUBitNewAndClearOther(L1, index, empty_block);
                        L1->sets[index].blocks[empty_block].frequency = 1;
                    }
                    else // When L2 is enabled
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);

#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_empty_block);
                                L2->sets[l2_index].blocks[l2_empty_block].frequency = 1;
                            }
                            else
                            {
                                // LFU
                                uint64_t l2_lfu_index = findLFUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);
                                setTag(L2, l2_index, l2_lfu_index, l2_tag);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_lfu_index);
                                L2->sets[l2_index].blocks[l2_lfu_index].frequency = 1;
#ifdef DEBUG
                                printf("Evict from L2: block with valid=%d and index=0x%lx\n", 0, l2_index);
#endif
                            }
                            // deal with prefetch
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            setMRUBitNewAndClearOther(L2, l2_index, l2_target);
                            L2->sets[l2_index].blocks[l2_target].frequency++;

#ifdef DEBUG
                            printf("L2 read hit");
#endif
                        }
                        // Then Add the needed block to L1
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setDirtyBit(L1, index, empty_block);
                        setMRUBitNewAndClearOther(L1, index, empty_block);
                        L1->sets[index].blocks[empty_block].frequency = 1;

                        /* if (l2_target == UINT64_MAX)
                        {
                            prefetch(L2, addr, stats);
                        } */
                    }
                }
                else // L1 is full set of blocks and eviction is needed
                {
                    // LFU
                    uint64_t lfu_index = findLFUBlockIndex(&(L1->sets[index]), num_blocks);

                    if (L2->config.disabled)
                    {
                        // Whether this LFU is dirty?
                        if (getDirtyBit(L1, index, lfu_index))
                        {
                            setTag(L1, index, lfu_index, tag);
                            setDirtyBit(L1, index, lfu_index);
                            setMRUBitNewAndClearOther(L1, index, lfu_index);
                            L1->sets[index].blocks[lfu_index].frequency = 1;
                        }
                        else
                        {
                            setTag(L1, index, lfu_index, tag);
                            setDirtyBit(L1, index, lfu_index);
                            setMRUBitNewAndClearOther(L1, index, lfu_index);
                            L1->sets[index].blocks[lfu_index].frequency = 1;
                        }
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);
#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx \n", addr, l2_tag, l2_index);
#endif
                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_empty_block);
                                L2->sets[l2_index].blocks[l2_empty_block].frequency = 1;
                            }
                            else
                            {
                                // LFU
                                uint64_t l2_lfu_index = findLFUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);
                                setTag(L2, l2_index, l2_lfu_index, l2_tag);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_lfu_index);
                                L2->sets[l2_index].blocks[l2_lfu_index].frequency = 1;

#ifdef DEBUG
                                printf("Evicted from L2: block with valid: %d index: 0x%lx\n",
                                       0,
                                       l2_index);
#endif
                            }
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            setMRUBitNewAndClearOther(L2, l2_index, l2_target);
                            L2->sets[l2_index].blocks[l2_target].frequency++;
#ifdef DEBUG
                            printf("L2 read hit\n");
#endif
                        }
                        // Then Add the needed block to L1
                        // While deal with the evicted
                        // While the dirtybit is set,
                        // the block needs to be write in L2
                        if (getDirtyBit(L1, index, lfu_index))
                        {
                            uint64_t evicted_tag = L1->sets[index].blocks[lfu_index].tag;
                            uint64_t l1_index_bits = (L1->config.c - L1->config.s - L1->config.b);
                            uint64_t l1_offset_bits = (L1->config.b);
                            uint64_t addr = (evicted_tag << (l1_index_bits + l1_offset_bits)) + (index << L1->config.b);
                            uint64_t l2_tag = getTag(addr, L2);
                            uint64_t l2_index = getIndex(addr, L2);
                            uint64_t evicted_target_block = isInCache('W', addr, stats, L2);

                            // Not in L2
                            if (evicted_target_block == UINT64_MAX)
                            {
                            }
                            else
                            {
                                setMRUBitNewAndClearOther(L2, l2_index, evicted_target_block);
                                L2->sets[l2_index].blocks[evicted_target_block].frequency++;
                            }
                        }
                        setTag(L1, index, lfu_index, tag);
                        setDirtyBit(L1, index, lfu_index);
                        setMRUBitNewAndClearOther(L1, index, lfu_index);
                        L1->sets[index].blocks[lfu_index].frequency = 1;

                        /* if (l2_target == UINT64_MAX)
                        {
                            prefetch(L2, addr, stats);
                        } */
                    }
#ifdef DEBUG
                    printf("Evict from L1: block with valid=%d, dirty=%d, tag 0x%lx and index=0x%lx\n",
                           L1->sets[index].blocks[lfu_index].valid_bit,
                           L1->sets[index].blocks[lfu_index].dirty_bit,
                           tag,
                           index);
#endif
                }
            }

            // Read miss
            if (rw == 'R')
            {
                // make sure whether cache set is full
                uint64_t empty_block = findEmptyBlockIndex(L1, index, num_blocks);
                if (empty_block != UINT64_MAX)
                {

                    if (L2->config.disabled)
                    {
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setMRUBitNewAndClearOther(L1, index, empty_block);
                        L1->sets[index].blocks[empty_block].frequency = 1;
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);
#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        if (l2_target == UINT64_MAX)
                        {
#ifdef DEBUG
                            printf("L2 read miss\n");
#endif

                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);
                            // Still have empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_empty_block);
                                L2->sets[l2_index].blocks[l2_empty_block].frequency = 1;
                            }
                            else
                            {
                                // LFU
                                uint64_t l2_lfu_index = findLFUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);

                                setTag(L2, l2_index, l2_lfu_index, l2_tag);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_lfu_index);
                                L2->sets[l2_index].blocks[l2_lfu_index].frequency = 1;

#ifdef DEBUG
                                printf("Evict from L2: block with valid=%d and index=0x%lx\n", 0, l2_index);
#endif
                            }
                            // deal with prefetch
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            setMRUBitNewAndClearOther(L2, l2_index, l2_target);
                            L2->sets[l2_index].blocks[l2_target].frequency++;
#ifdef DEBUG
                            printf("L2 read hit");
#endif
                        }
                        // Then Add the needed block to L1
                        setTag(L1, index, empty_block, tag);
                        setValidBit(L1, index, empty_block);
                        setMRUBitNewAndClearOther(L1, index, empty_block);
                        L1->sets[index].blocks[empty_block].frequency = 1;
                    }
                }
                else // When No empty blocks in L1
                {
                    // LFU
                    uint64_t lfu_index = findLFUBlockIndex(&(L1->sets[index]), num_blocks);

                    if (L2->config.disabled)
                    {
                        // Whether this LFU is dirty?
                        if (getDirtyBit(L1, index, lfu_index))
                        {
                            setTag(L1, index, lfu_index, tag);
                            clearDirtyBit(L1, index, lfu_index);
                            setMRUBitNewAndClearOther(L1, index, lfu_index);
                            L1->sets[index].blocks[lfu_index].frequency = 1;
                        }
                        else
                        {
                            setTag(L1, index, lfu_index, tag);
                            setMRUBitNewAndClearOther(L1, index, lfu_index);
                            L1->sets[index].blocks[lfu_index].frequency = 1;
                        }
                    }
                    else
                    {
                        uint64_t l2_tag = getTag(addr, L2);
                        uint64_t l2_index = getIndex(addr, L2);
                        uint64_t l2_num_blocks = 1UL << (L2->config.s);
#ifdef DEBUG
                        printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx \n", addr, l2_tag, l2_index);
#endif

                        uint64_t l2_target = isInCache('R', addr, stats, L2);
                        // When the needed block is not in L2
                        // It needs to select a block to put needed
                        // block in at first
                        // L2 miss
                        if (l2_target == UINT64_MAX)
                        {
                            uint64_t l2_empty_block = findEmptyBlockIndex(L2, l2_index, l2_num_blocks);

#ifdef DEBUG
                            printf("L2 read miss\n");
#endif

                            // Still empty block in L2
                            if (l2_empty_block != UINT64_MAX)
                            {
                                setTag(L2, l2_index, l2_empty_block, l2_tag);
                                setValidBit(L2, l2_index, l2_empty_block);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_empty_block);
                                L2->sets[l2_index].blocks[l2_empty_block].frequency = 1;
                            }
                            else
                            {
                                // LFU
                                uint64_t l2_lfu_index = findLFUBlockIndex(&(L2->sets[l2_index]), l2_num_blocks);
                                setTag(L2, l2_index, l2_lfu_index, l2_tag);
                                setMRUBitNewAndClearOther(L2, l2_index, l2_lfu_index);
                                L2->sets[l2_index].blocks[l2_lfu_index].frequency = 1;

#ifdef DEBUG
                                printf("Evicted from L2: block with valid: %d index: 0x%lx\n",
                                       0,
                                       l2_index);
#endif
                            }
                            prefetch(L2, addr, stats);
                        }
                        else // When the needed block is in L2
                        {
                            setMRUBitNewAndClearOther(L2, l2_index, l2_target);
                            L2->sets[l2_index].blocks[l2_target].frequency++;
#ifdef DEBUG
                            printf("L2 read hit\n");
#endif
                        }
                        // Then Add the needed block to L1
                        if (getDirtyBit(L1, index, lfu_index))
                        {
                            uint64_t evicted_tag = L1->sets[index].blocks[lfu_index].tag;
                            uint64_t l1_index_bits = (L1->config.c - L1->config.s - L1->config.b);
                            uint64_t l1_offset_bits = (L1->config.b);
                            uint64_t addr = (evicted_tag << (l1_index_bits + l1_offset_bits)) + (index << L1->config.b);
                            uint64_t l2_tag = getTag(addr, L2);
                            uint64_t l2_index = getIndex(addr, L2);
                            uint64_t evicted_target_block = isInCache('W', addr, stats, L2);
                            // Not in L2
                            if (evicted_target_block == UINT64_MAX)
                            {
                            }
                            else
                            {
                                setMRUBitNewAndClearOther(L2, l2_index, evicted_target_block);
                                L2->sets[l2_index].blocks[evicted_target_block].frequency++;
                            }
                        }
                        setTag(L1, index, lfu_index, tag);
                        clearDirtyBit(L1, index, lfu_index);
                        setMRUBitNewAndClearOther(L1, index, lfu_index);
                        L1->sets[index].blocks[lfu_index].frequency = 1;

                        /* if (l2_target == UINT64_MAX)
                        {
                            prefetch(L2, addr, stats);
                        } */
                    }
#ifdef DEBUG
                    printf("Evict from L1: block with valid=%d, dirty=%d, tag 0x%lx and index=0x%lx\n",
                           L1->sets[index].blocks[lfu_index].valid_bit,
                           L1->sets[index].blocks[lfu_index].dirty_bit,
                           tag,
                           index);
#endif
                }
            }
        }
        else // Hit on cache
        {
#ifdef DEBUG
            printf("L1 hit\n");
#endif

            if (rw == 'W')
            {
                setDirtyBit(L1, index, l1_target);
                setMRUBitNewAndClearOther(L1, index, l1_target);
                L1->sets[index].blocks[l1_target].frequency++;
            }
            if (rw == 'R')
            {
                setMRUBitNewAndClearOther(L1, index, l1_target);
                L1->sets[index].blocks[l1_target].frequency++;
            }
        }
    }
}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * TODO: You're responsible for completing this routine
 */
void sim_finish(sim_stats_t *stats)
{
    uint64_t num_sets_L1 = 1UL << (L1->config.c - L1->config.b - L1->config.s);
    uint64_t num_sets_L2 = 1UL << (L2->config.c - L2->config.b - L2->config.s);

    stats->read_hit_ratio_l2 = static_cast<double>(stats->read_hits_l2) / stats->reads_l2;
    stats->read_miss_ratio_l2 = static_cast<double>(stats->read_misses_l2) / stats->reads_l2;
    double Hit_Time_l2 =
        L2_HIT_K3 +
        (L2_HIT_K4 * (L2->config.c - L2->config.b - L2->config.s)) +
        L2_HIT_K5 * (std::max(3, (int)L2->config.s) - 3);

    if (L2->config.disabled)
    {
        stats->avg_access_time_l2 = DRAM_ACCESS_TIME;
    }
    else
    {
        stats->avg_access_time_l2 = Hit_Time_l2 + stats->read_miss_ratio_l2 * DRAM_ACCESS_TIME;
    }
    stats->hit_ratio_l1 = static_cast<double>(stats->hits_l1) / stats->accesses_l1;
    stats->miss_ratio_l1 = static_cast<double>(stats->misses_l1) / stats->accesses_l1;
    double Hit_Time_l1 =
        L1_HIT_K0 +
        (L1_HIT_K1 * (L1->config.c - L1->config.b - L1->config.s)) +
        L1_HIT_K2 * (std::max(3, (int)L1->config.s) - 3);
    /* if (L2->config.disabled)
    {
        stats->avg_access_time_l1 = Hit_Time_l1 + stats->miss_ratio_l1 * DRAM_ACCESS_TIME;
    }
    else
    { */
    stats->avg_access_time_l1 = Hit_Time_l1 + stats->miss_ratio_l1 * stats->avg_access_time_l2;
    /* } */
    // Free L1 blocks and sets
    for (uint64_t i = 0; i < num_sets_L1; ++i)
    {
        free(L1->sets[i].blocks);
    }
    free(L1->sets);

    // Free L2 blocks and sets
    for (uint64_t i = 0; i < num_sets_L2; ++i)
    {
        free(L2->sets[i].blocks);
    }
    free(L2->sets);

    // Finally, free L1 and L2 caches themselves
    free(L1);
    free(L2);
}

uint64_t getIndex(uint64_t addr, cache *cache)
{
    uint64_t block_offset_bits = cache->config.b;
    // The index bits would directly follow the block offset bits in the address.
    uint64_t index_bits = cache->config.c - cache->config.s - cache->config.b; // Correcting my explanation here
    uint64_t mask = (1UL << index_bits) - 1;
    uint64_t index = (addr >> block_offset_bits) & mask;
    return index;
}

uint64_t getTag(uint64_t addr, cache *cache)
{
    uint64_t block_offset_bits = cache->config.b;
    uint64_t index_bits = cache->config.c - cache->config.s - cache->config.b; // Assuming this directly gives index bits
    // Shift right to remove both the block offset and index bits.
    uint64_t tag = addr >> (block_offset_bits + index_bits);
    return tag;
}

bool getValidBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{

    return (cache->sets[set_index].blocks[block_index]).valid_bit ? true : false;
}

void setValidBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{
    cache->sets[set_index].blocks[block_index].valid_bit = 1;
}

void clearValidBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{
    cache->sets[set_index].blocks[block_index].valid_bit = 0;
}

bool getDirtyBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{

    return cache->sets[set_index].blocks[block_index].dirty_bit ? true : false;
}

void setDirtyBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{
    cache->sets[set_index].blocks[block_index].dirty_bit = 1;
}

void clearDirtyBit(cache_t *cache, uint64_t set_index, uint64_t block_index)
{
    cache->sets[set_index].blocks[block_index].dirty_bit = 0;
}

void updateTimestamp(cache_t *cache, uint64_t set_index, uint64_t block_index)
{
    cache->sets[set_index].blocks[block_index].timestamp = cache->timestamp_counter;
    cache->timestamp_counter++;
}

// Prefetch blockaddr according to target insertion type
void prefetch(cache_t *cache, uint64_t addr, sim_stats_t *stats)
{
    uint64_t num_blocks = 1UL << (cache->config.s);
    uint64_t block_addr = blockAddrTrans(cache, addr);

    if (cache->config.replace_policy == REPLACE_POLICY_LRU)
    {
        // Dealing with +1 prefetcher
        if (!cache->config.prefetcher_disabled && cache->config.strided_prefetch_disabled)
        {

            uint64_t new_block_addr = block_addr + (1UL << (cache->config.b));

            uint64_t new_tag = getTag(new_block_addr, cache);
            uint64_t new_index = getIndex(new_block_addr, cache);
            uint64_t target_block_index = prefetchInCache(cache, new_block_addr);
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_MIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lru_index = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lru_index, new_tag);
                        updateTimestamp(cache, new_index, lru_index);
                    }
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        updateTimestamp(cache, new_index, empty_block);
                    }
                }
            }
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_LIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    // No empty block now in L2
                    if (empty_block == UINT64_MAX)
                    {

                        uint64_t lru_index = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        clearValidBit(cache, new_index, lru_index);
                        uint64_t lowest_block = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setValidBit(cache, new_index, lru_index);
                        setTag(cache, new_index, lru_index, new_tag);
                        cache->sets[new_index].blocks[lru_index].timestamp = cache->sets[new_index].blocks[lowest_block].timestamp - 1;
                        cache->timestamp_counter++;
                    }
                    // Empty block exists
                    else
                    {
                        if (checkAllEmpty(cache, new_index))
                        {
                            setTag(cache, new_index, empty_block, new_tag);
                            setValidBit(cache, new_index, empty_block);
                            cache->sets[new_index].blocks[empty_block].timestamp = cache->timestamp_counter;
                            cache->timestamp_counter++;
                        }
                        else
                        {
                            uint64_t lowest_block = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                            setTag(cache, new_index, empty_block, new_tag);
                            setValidBit(cache, new_index, empty_block);
                            cache->sets[new_index].blocks[empty_block].timestamp = cache->sets[new_index].blocks[lowest_block].timestamp - 1;
                            cache->timestamp_counter++;
                        }
                    }
                }
            }
        }
        // Dealing with strided prefetcher
        if (!cache->config.prefetcher_disabled && !cache->config.strided_prefetch_disabled)
        {
            uint64_t k = (block_addr - prev_block_addr);
            uint64_t new_block_addr = block_addr + k;
#ifdef DEBUG
            printf("Old block addr: 0x%lx, Prev block addr: 0x%lx, New block addr: 0x%lx\n", block_addr, prev_block_addr, new_block_addr);
#endif
            uint64_t new_tag = getTag(new_block_addr, cache);
            uint64_t new_index = getIndex(new_block_addr, cache);
            uint64_t target_block_index = prefetchInCache(cache, new_block_addr);
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_MIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lru_index = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lru_index, new_tag);
                        updateTimestamp(cache, new_index, lru_index);
                    }
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        updateTimestamp(cache, new_index, empty_block);
                    }
                }
            }
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_LIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    // No empty block now in L2
                    if (empty_block == UINT64_MAX)
                    {

                        uint64_t lru_index = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        clearValidBit(cache, new_index, lru_index);
                        uint64_t lowest_block = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setValidBit(cache, new_index, lru_index);
                        setTag(cache, new_index, lru_index, new_tag);
                        cache->sets[new_index].blocks[lru_index].timestamp = cache->sets[new_index].blocks[lowest_block].timestamp - 1;
                        cache->timestamp_counter++;
                    }
                    // Empty block exists
                    else
                    {
                        if (checkAllEmpty(cache, new_index))
                        {
                            setTag(cache, new_index, empty_block, new_tag);
                            setValidBit(cache, new_index, empty_block);
                            cache->sets[new_index].blocks[empty_block].timestamp = cache->timestamp_counter;
                            cache->timestamp_counter++;
                        }
                        else
                        {
                            uint64_t lowest_block = findLRUBlockIndex(&(cache->sets[new_index]), num_blocks);
                            setTag(cache, new_index, empty_block, new_tag);
                            setValidBit(cache, new_index, empty_block);
                            cache->sets[new_index].blocks[empty_block].timestamp = cache->sets[new_index].blocks[lowest_block].timestamp - 1;
                            cache->timestamp_counter++;
                        }
                    }
                }
            }
            prev_block_addr = block_addr;
        }
    }
    if (cache->config.replace_policy == REPLACE_POLICY_LFU)
    {
        // Dealing with +1 prefetcher
        if (!cache->config.prefetcher_disabled && cache->config.strided_prefetch_disabled)
        {

            uint64_t new_block_addr = block_addr + (1UL << (cache->config.b));

            uint64_t new_tag = getTag(new_block_addr, cache);
            uint64_t new_index = getIndex(new_block_addr, cache);
            uint64_t target_block_index = prefetchInCache(cache, new_block_addr);
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_MIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lfu_index = findLFUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lfu_index, new_tag);
                        setMRUBitNewAndClearOther(cache, new_index, lfu_index);
                        cache->sets[new_index].blocks[lfu_index].frequency = 0;
                    }
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        setMRUBitNewAndClearOther(cache, new_index, empty_block);
                        cache->sets[new_index].blocks[empty_block].frequency = 0;
                    }
                }
            }
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_LIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    // No empty block now in L2
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lfu_index = findLFUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lfu_index, new_tag);
                        cache->sets[new_index].blocks[lfu_index].MRU_bit = false;
                        cache->sets[new_index].blocks[lfu_index].frequency = 0;
                    }
                    // Empty block exists
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        cache->sets[new_index].blocks[empty_block].MRU_bit = false;
                        cache->sets[new_index].blocks[empty_block].frequency = 0;
                    }
                }
            }
        }
        // Dealing with strided prefetcher
        if (!cache->config.prefetcher_disabled && !cache->config.strided_prefetch_disabled)
        {
            uint64_t k = (block_addr - prev_block_addr);
            uint64_t new_block_addr = block_addr + k;

            uint64_t new_tag = getTag(new_block_addr, cache);
            uint64_t new_index = getIndex(new_block_addr, cache);
            uint64_t target_block_index = prefetchInCache(cache, new_block_addr);
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_MIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lfu_index = findLFUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lfu_index, new_tag);
                        setMRUBitNewAndClearOther(cache, new_index, lfu_index);
                        cache->sets[new_index].blocks[lfu_index].frequency = 0;
                    }
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        setMRUBitNewAndClearOther(cache, new_index, empty_block);
                        cache->sets[new_index].blocks[empty_block].frequency = 0;
                    }
                }
            }
            if (cache->config.prefetch_insert_policy == INSERT_POLICY_LIP)
            {
                // Prefetched block is not in L2 cache
                // Need to find the LRU or LFU position
                // Then insert into that position and update timestamp
                if (target_block_index == UINT64_MAX)
                {
#ifdef DEBUG
                    printf("Prefetch block with address 0x%lx from memrory to L2\n", new_block_addr);
#endif
                    stats->prefetches_l2++;
                    uint64_t empty_block = findEmptyBlockIndex(cache, new_index, num_blocks);
                    // No empty block now in L2
                    if (empty_block == UINT64_MAX)
                    {
                        uint64_t lfu_index = findLFUBlockIndex(&(cache->sets[new_index]), num_blocks);
                        setTag(cache, new_index, lfu_index, new_tag);
                        cache->sets[new_index].blocks[lfu_index].MRU_bit = false;
                        cache->sets[new_index].blocks[lfu_index].frequency = 0;
                    }
                    // Empty block exists
                    else
                    {
                        setTag(cache, new_index, empty_block, new_tag);
                        setValidBit(cache, new_index, empty_block);
                        cache->sets[new_index].blocks[empty_block].MRU_bit = false;
                        cache->sets[new_index].blocks[empty_block].frequency = 0;
                    }
                }
            }
            prev_block_addr = block_addr;
        }
    }
}

// Set the tag of certain block
void setTag(cache *cache, uint64_t set_index, uint64_t block_index, uint64_t tag)
{
    cache->sets[set_index].blocks[block_index].tag = tag;
}

// When you need to evict a block due to a cache miss, find the block with the smallest timestamp
uint64_t findLRUBlockIndex(set *cache_set, uint64_t set_size)
{
    uint64_t lru_index = 0;
    uint64_t lowest_timestamp = UINT64_MAX;
    for (uint64_t i = 0; i < set_size; i++)
    {
        if (cache_set->blocks[i].timestamp < lowest_timestamp && cache_set->blocks[i].valid_bit)
        {
            lru_index = i;
            lowest_timestamp = cache_set->blocks[i].timestamp;
        }
    }
    return lru_index;
}

// return the empty block index or UINT64_MAX otherwise
uint64_t findEmptyBlockIndex(cache *cache, uint64_t set_index, uint64_t set_size)
{
    for (uint64_t i = 0; i < set_size; i++)
    {
        if (!cache->sets[set_index].blocks[i].valid_bit)
        {
            return i;
        }
    }
    return UINT64_MAX;
}

// Find the largest timestamp within a set
uint64_t findMRUBlockIndex(set *cache_set, uint64_t set_size)
{
    uint64_t mru_index = 0;
    uint64_t largest_timestamp = 0;
    for (uint64_t i = 0; i < set_size; i++)
    {
        if (cache_set->blocks[i].timestamp > largest_timestamp && cache_set->blocks[i].valid_bit)
        {
            mru_index = i;
            largest_timestamp = cache_set->blocks[i].timestamp;
        }
    }
    return mru_index;
}

uint64_t findTargetBlockIndex(set *cache_set, uint64_t set_size, uint64_t tag)
{
    uint64_t target_index = 0;

    for (uint64_t i = 0; i < set_size; i++)
    {
        if (cache_set->blocks[i].tag == tag)
        {
            target_index = i;
            break;
        }
    }
    return target_index;
}

// Determine whether a certian address is in cache L1 or L2
// Return the block index for a hit and UINT64_MAX for a miss
uint64_t isInCache(char rw, uint64_t addr, sim_stats_t *stats, cache *cache)
{
    uint64_t tag = getTag(addr, cache);
    uint64_t index = getIndex(addr, cache);
    uint64_t num_blocks = 1UL << (cache->config.s);

    if (cache == L1)
    {
        stats->accesses_l1++;
        if (rw == 'W')
        {
            stats->writes++;
        }
        else
        {
            stats->reads++;
        }
        for (uint64_t i = 0; i < num_blocks; i++)
        {
            if (cache->sets[index].blocks[i].tag == tag && getValidBit(cache, index, i))
            {
                stats->hits_l1++;
                return i;
            }
        }
        stats->misses_l1++;
        return UINT64_MAX;
    }
    if (cache == L2)
    {

        stats->accesses_l2++;
        if (rw == 'R')
        {
            stats->reads_l2++;
            for (uint64_t i = 0; i < num_blocks; i++)
            {
                if (cache->sets[index].blocks[i].tag == tag && getValidBit(cache, index, i))
                {
                    stats->read_hits_l2++;
                    return i;
                }
            }
            stats->read_misses_l2++;
            return UINT64_MAX;
        }
        if (rw == 'W')
        {
            stats->writes_l2++;
            for (uint64_t i = 0; i < num_blocks; i++)
            {
                if (cache->sets[index].blocks[i].tag == tag && getValidBit(cache, index, i))
                {
                    return i;
                }
            }
            return UINT64_MAX;
        }
    }
}

// Return the target_block_index of that block_addr
// Otherwise return UINT64_MAX
uint64_t prefetchInCache(cache *cache, uint64_t new_block_addr)
{
    uint64_t index = getIndex(new_block_addr, cache);
    uint64_t tag = getTag(new_block_addr, cache);
    uint64_t num_blocks = 1UL << cache->config.s;
    for (uint64_t i = 0; i < num_blocks; i++)
    {
        if (cache->sets[index].blocks[i].tag == tag && getValidBit(cache, index, i))
        {
            return i;
        }
    }
    return UINT64_MAX;
}

// Return true if the set is all empty
// Return false if it is not
bool checkAllEmpty(cache *cache, uint64_t set_index)
{
    uint64_t num_blocks = 1UL << (cache->config.s);
    for (uint64_t i = 0; i < num_blocks; i++)
    {
        if (cache->sets[set_index].blocks[i].valid_bit)
        {
            return false;
        }
    }
    return true;
}

// Transfer into block_addr and ignore the offset
uint64_t blockAddrTrans(cache *cache, uint64_t addr)
{
    uint64_t mask = ~((1UL << cache->config.b) - 1);
    uint64_t block_addr = addr & mask;
    return block_addr;
}

void setMRUBitNewAndClearOther(cache *cache, uint64_t set_index, uint64_t block_index)
{
    uint64_t num_blocks = 1UL << cache->config.s;
    for (uint64_t i = 0; i < num_blocks; i++)
    {
        cache->sets[set_index].blocks[i].MRU_bit = false;
    }
    cache->sets[set_index].blocks[block_index].MRU_bit = true;
    return;
}

uint64_t findLFUBlockIndex(set *cache_set, uint64_t set_size)
{
    uint64_t lfu_index = UINT64_MAX; // Initialize to an invalid value
    uint64_t lowest_frequency = UINT64_MAX;
    uint64_t smallest_tag = UINT64_MAX;

    for (uint64_t i = 0; i < set_size; i++)
    {

        if (cache_set->blocks[i].frequency < lowest_frequency && !cache_set->blocks[i].MRU_bit && cache_set->blocks[i].valid_bit)
        {
            lowest_frequency = cache_set->blocks[i].frequency;
        }
    }
    for (uint64_t i = 0; i < set_size; i++)
    {
        if (cache_set->blocks[i].frequency == lowest_frequency &&
            !cache_set->blocks[i].MRU_bit &&
            cache_set->blocks[i].valid_bit &&
            cache_set->blocks[i].tag < smallest_tag)
        {
            lfu_index = i;
            smallest_tag = cache_set->blocks[i].tag;
        }
    }
    return lfu_index;
}
