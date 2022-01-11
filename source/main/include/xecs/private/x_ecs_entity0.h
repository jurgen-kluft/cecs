#ifndef __XECS_ECS_ENTITY0_H__
#define __XECS_ECS_ENTITY0_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/private/x_ecs_types.h"

namespace xcore
{
    static constexpr const s32 s_entity0_array_capacities_count = 40;
    static constexpr const u32 s_entity0_array_capacities[]     = {256,    512,    768,    1280,   1792,   2816,   3328,   4352,   5888,   7424,   9472,    12032,   15104,   18688,   24832,   32512,   38656,   50432,   64256,   80128,
                                                               101632, 127744, 161536, 204032, 258304, 322304, 408832, 514816, 649984, 819968, 1030912, 1302272, 1643776, 2070784, 2609408, 3290368, 4145408, 5222144, 6583552, 8294656};


    static inline bool s_en0_has_cp(entity0_t* en0, u32 const cp_id)
    {
        s8 const shard_bit = 1 << (cp_id >> 5);
        if ((en0->m_cp1_bitset & shard_bit) != 0)
        {
            s8 const shard_idx    = s_compute_index(en0->m_cp1_bitset, shard_bit);
            s8 const shard_cp_bit = 1 << (cp_id & 0x1F);
            return ((en0->m_cp2_bitset[shard_idx] & shard_cp_bit) != 0);
        }
        return false;
    }

    static inline s16 s_get_en2_cp_index(entity0_t* en0, u32 const cp_id)
    {
        s8 const shard_bit    = 1 << (cp_id >> 5);
        s8 const shard_idx    = s_compute_index(en0->m_cp1_bitset, shard_bit);
        s8 const shard_cp_bit = 1 << (cp_id & 0x1F);
        s16      a_i          = s_compute_index(en0->m_cp2_bitset[shard_idx], shard_cp_bit);
        switch (shard_idx)
        {
            case 4: a_i += en0->m_cp2_bitcnt[3];
            case 3: a_i += en0->m_cp2_bitcnt[2];
            case 2: a_i += en0->m_cp2_bitcnt[1];
            case 1: a_i += en0->m_cp2_bitcnt[0];
            case 0: break;
        }
        return a_i;
    }

    static inline s16 s_get_en2_cp_index(entity0_t* en0, s8 const shard_idx, s8 const shard_cp_bit)
    {
        s16 a_i = s_compute_index(en0->m_cp2_bitset[shard_idx], shard_cp_bit);
        switch (shard_idx)
        {
            case 4: a_i += en0->m_cp2_bitcnt[3];
            case 3: a_i += en0->m_cp2_bitcnt[2];
            case 2: a_i += en0->m_cp2_bitcnt[1];
            case 1: a_i += en0->m_cp2_bitcnt[0];
            case 0: break;
        }
        return a_i;
    }

    static void s_init(entity0_store_t* es, alloc_t* allocator)
    {
        u32 hbb_config;
        init(es->m_entity_hbb, (1 << ECS_ENTITY_VERSION_SHIFT) / 256, hbb_config, allocator);
        es->m_level0 = (u8*)allocator->allocate(sizeof(u8) * (1 << ECS_ENTITY_VERSION_SHIFT) / 256);

        es->m_cap          = 0;
        es->m_size         = 0;
        u32 const cap_size = s_entity0_array_capacities[es->m_cap];

        // Initialize each 256 entities as a small linked list of free entities
        es->m_array = (entity0_t*)allocator->allocate(sizeof(entity0_t) * cap_size);
        for (u32 i = 0; i < cap_size; i += 1)
        {
            for (u32 j = 0; j < 255; ++j)
                es->m_array[i++].m_en2_index = index_t((j + 1), ECS_ENTITY_ID_MASK);

            // Mark the end of the list with the full ECS_ENTITY_VERSION_MASK
            es->m_array[i].m_en2_index = index_t(ECS_ENTITY_VERSION_MAX, ECS_ENTITY_ID_MASK);
        }
    }

    static void s_exit(entity0_store_t* es, alloc_t* allocator)
    {
        release(es->m_entity_hbb, allocator);
        allocator->deallocate(es->m_level0);        
        allocator->deallocate(es->m_array);
    }

    static entity0_t* s_get_entity0(entity0_store_t* es, entity_t e)
    {
        return &es->m_array[g_entity_id(e)];        
    }

    
} // namespace xcore

#endif