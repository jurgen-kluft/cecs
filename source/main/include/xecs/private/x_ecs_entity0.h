#ifndef __XECS_ECS_ENTITY0_H__
#define __XECS_ECS_ENTITY0_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    static constexpr const s32 s_entity0_array_capacities_count = 40;
    static constexpr const u32 s_entity0_array_capacities[]     = {256,    512,    768,    1280,   1792,   2816,   3328,   4352,   5888,   7424,   9472,    12032,   15104,   18688,   24832,   32512,   38656,   50432,   64256,   80128,
                                                               101632, 127744, 161536, 204032, 258304, 322304, 408832, 514816, 649984, 819968, 1030912, 1302272, 1643776, 2070784, 2609408, 3290368, 4145408, 5222144, 6583552, 8294656};

    struct index_t
    {
        inline index_t(u16 index, u32 offset)
            : m_value((offset & 0x003FFFFF) | ((index << 22) & 0xFFC00000))
        {
        }
        inline u16  get_index() const { return ((m_value & 0xFFC00000) >> 22); }
        inline u32  get_offset() const { return m_value & 0x003FFFFF; }
        inline void set_index(u16 index) { m_value = (m_value & 0x003FFFFF) | ((index << 22) & 0xFFC00000); }
        inline void set_offset(u32 offset) { m_value = (m_value & 0xFFC00000) | (offset & 0x003FFFFF); }
        u32         m_value;
    };

    struct entity0_t
    {
        index_t m_en2_index;     // index/offset
        u32     m_cp1_bitset;    // Each bit at level 1 represents 32 shards of components
        u32     m_cp2_bitcnt;    // The bitcnt sum of previous shards
        u32     m_cp2_bitset[5]; // this means that an entity can only cover 5 shards and 160 total components
    };

    static inline s8 s_compute_index(u32 const bitset, s8 bit)
    {
        ASSERT(((1 << bit) & bitset) == (1 << bit));
        s8 const i = xcountBits(bitset & ((u32)1 << bit));
        return i;
    }

    struct entity0_store_t
    {
        u8*        m_level0;     // 32768 * 256 = 8 M (each byte is an index into a range of 256 entities)
        u32*       m_level1;     // 1024 * 32 = 32768 bits
        u32        m_level2[32]; // 1024 bits
        u32        m_level3;     // 32 bits
        u32        m_size;
        u32        m_cap;
        entity0_t* m_array;
    };

    static void s_init(entity0_store_t* es, alloc_t* allocator)
    {
        es->m_level0 = (u8*)allocator->allocate(sizeof(u8) * 32768);
        es->m_level1 = (u32*)allocator->allocate(sizeof(u32) * 1024);
        x_memset(es->m_level0, 0x00000000, 32 * 32 * 32 * sizeof(u8));
        x_memset(es->m_level1, 0xFFFFFFFF, 32 * 32 * sizeof(u32));
        x_memset(es->m_level2, 0xFFFFFFFF, 32 * sizeof(u32));
        es->m_level3 = 0xFFFFFFFF;

        es->m_cap          = 0;
        es->m_size         = 0;
        u32 const cap_size = s_entity0_array_capacities[es->m_cap];

        // Initialize each 256 entities as a small linked list of free entities
        es->m_array = (entity0_t*)allocator->allocate(sizeof(entity0_t) * cap_size);
        for (s32 i = 0; i < cap_size; i += 1)
        {
            for (u32 j = 0; j < 255; ++j)
                es->m_array[i++].m_en2_index = index_t((j + 1), ECS_ENTITY_ID_MASK);

            // Mark the end of the list with the full ECS_ENTITY_VERSION_MASK
            es->m_array[i].m_en2_index = index_t(ECS_ENTITY_VERSION_MAX, ECS_ENTITY_ID_MASK);
        }
    }

    static entity_t s_create_entity(entity0_store_t* es, alloc_t* allocator)
    {
        // A hierarchical bitset can quickly tell us the lowest free entity
        s8 const o3 = xfindFirstBit(es->m_level3);
        s8 const o2 = xfindFirstBit(es->m_level2[o3]);
        s8 const o1 = xfindFirstBit(es->m_level1[(o3 * 32) + o2]);

        // o0 is the full index into level 0
        u32 const o0 = ((u32)o3 * 32 + (u32)o2) * 32 + (u32)o1;

        // the byte in level 0 is an extra offset on top of o0 that gives us
        // an index of a free entity in the m_a_entity0 array.
        u32 const eo = o0 + es->m_level0[o0];

        entity0_t& e = es->m_array[eo];
        ASSERT(e.m_en2_index.get_offset() == ECS_ENTITY_ID_MASK);

        // get the next entity that is free in this linked list
        u32 const ne = e.m_en2_index.get_index();

        // however there could be no more free entities (end of list)
        if (ne == ECS_ENTITY_VERSION_MAX)
        {
            // You can point to any index, but we will be marked as full
            es->m_level0[o0] = 0;

            if (s_clr_bit_in_u32(es->m_level1[(o3 * 32) + o2], o1) == 0)
            {
                if (s_clr_bit_in_u32(es->m_level2[o3], o2) == 0)
                {
                    s_clr_bit_in_u32(es->m_level3, o3);
                }
            }
        }
        else
        {
            // Update the head of the linked list
            es->m_level0[o0] = (u8)ne;
        }

        // Need to create entity2_t or do we delay it until an actual component is registered on the entity?
        e.m_en2_index  = index_t(ECS_ENTITY_VERSION_MAX,ECS_ENTITY_ID_MASK);
        e.m_cp1_bitset = 0;
        e.m_cp2_bitcnt = 0;
        for (s32 i = 0; i < 5; ++i)
            e.m_cp2_bitset[i] = 0;
        
        return index_t(0, eo).m_value;
    }

    static void s_delete_entity(entity0_store_t* es, entity_t e, alloc_t* allocator)
    {
    }

} // namespace xcore

#endif