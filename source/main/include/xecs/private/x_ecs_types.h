#ifndef __XECS_ECS_TYPES_H__
#define __XECS_ECS_TYPES_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    struct cp_type_t;

    struct index_t
    {
        inline index_t()
            : m_value(0xFFFFFFFF)
        {
        }
        inline index_t(u16 index, u32 offset)
            : m_value((offset & 0x003FFFFF) | ((index << 22) & 0xFFC00000))
        {
        }
        inline bool is_null() const { return m_value == 0xFFFFFFFF; }
        inline u16  get_index() const { return ((m_value & 0xFFC00000) >> 22); }
        inline u32  get_offset() const { return m_value & 0x003FFFFF; }
        inline void set_index(u16 index) { m_value = (m_value & 0x003FFFFF) | ((index << 22) & 0xFFC00000); }
        inline void set_offset(u32 offset) { m_value = (m_value & 0xFFC00000) | (offset & 0x003FFFFF); }
        u32         m_value;
    };

    struct cp_store_t
    {
        u8*     m_cp_data;
        u8*     m_cp_data_used;
        index_t m_cap_size;
    };

    struct components_store_t
    {
        u32         m_num_cp_store;
        u32         m_bitset1;
        u32         m_a_bitset0[32]; // To identify which component stores are still free (to give out new component id)
        cp_type_t*  m_a_cp_type;     // The type of each store
        cp_store_t* m_a_cp_store;    // N max number of components
    };

    typedef index_t entity0_t;

    struct entity_hbb_t
    {
        u32* m_level0;
        u32* m_level1;
        u32  m_level2;
        u32  m_size; // maximum bits on level 1, / 32 = size of level 2 in bits
        s32  get_free(u32 from) const;
        void set_used(s32 item);
    };

    extern void s_init(entity_hbb_t* ehbb, u32 size, alloc_t* allocator);
    extern void s_exit(entity_hbb_t* ehbb, alloc_t* allocator);

    inline s32 entity_hbb_t::get_free(u32 from) const
    {
        // A hierarchical bitset can quickly tell us the lowest free entity
        s8 const o3 = xfindFirstBit(m_level2);
        if (o3 == -1)
            return -1;

        s8 const o2 = xfindFirstBit(m_level1[o3]);
        s8 const o1 = xfindFirstBit(m_level0[(o3 * 32) + o2]);

        // o0 is the full index into level 0
        u32 const o0 = ((u32)o3 * 32 + (u32)o2) * 32 + (u32)o1;

        return o0;
    }

    // Entity Type, (8 + 8 + sizeof(u32)*max-number-of-components) ~4Kb
    // When an entity type registers a component it will allocate component data from the specific store for N entities
    // and it will keep it there. Of course each entity can mark if it actually uses the component, but that can basically
    // just be a single bit. So if only 50% of your entities of this type use this component you might be better of
    // registering another entity type.
    struct entity_type_t
    {
        entity_id_t m_entity0_id_range[2]; // Entity Range (number of entities)
        s32*        m_a_cp_store_offset;   // The components allocated start at a certain offset for each cp_store
    };

    struct entity0_store_t
    {
        entity_hbb_t   m_entity_hbb;
        index_t        m_cap_size;
        u8*            m_level0;
        entity0_t*     m_a_entity0;

        // Maximum number of types?
        // Q: Is it a must to have a single large entity0 array?, or could we have an entity0 array per type/group?
        // Q: Can entity_t encode the type inside the u32?
        // Q: Could we have 5, 9, 18? Which means 5 bit for the version, 9 bit for the entity type and 18 bits for the entity index
        // Q: Is 262144 entities enough per entity type?
        // Q: Is 512 entity types enough?
        // Q: What is the minimum necessary version bit width?, is 5 bits enough?
        u32 m_entity_type_level1;
        u16 m_entity_type_level0[32];
        entity_type_t* m_a_entity_type;
    };

    struct ecs2_t
    {
        alloc_t*           m_allocator;
        components_store_t m_component_store;
        entity0_store_t    m_entity0_store;
    };

    static inline s8 s_compute_index(u32 const bitset, u32 bit)
    {
        ASSERT((bit & bitset) == bit);
        s16 const i = xcountBits(bitset & (bit - 1));
        return i;
    }

    static inline u32 s_clr_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset & ~(1 << bit);
        return old_bitset;
    }
    static inline u32 s_set_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset | (1 << bit);
        return old_bitset;
    }

    static inline void s_set_u24(u8* ptr, u32 i, u32 v)
    {
        ptr += (i << 1) + i;
        ptr[0] = (u8)(v << 16);
        ptr[1] = (u8)(v << 8);
        ptr[2] = (u8)(v << 0);
    }

    static inline u32 s_get_u24(u8 const* ptr, u32 i)
    {
        ptr += (i << 1) + i;
        u32 const v = (ptr[0] << 16) | (ptr[1] << 8) | (ptr[2] << 0);
        return v;
    }

} // namespace xcore

#endif