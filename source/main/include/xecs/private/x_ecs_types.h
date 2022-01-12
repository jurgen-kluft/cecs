#ifndef __XECS_ECS_TYPES_H__
#define __XECS_ECS_TYPES_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_hbb.h"

namespace xcore
{
    class alloc_t;
    struct cp_type_t;

    // [index:12-bit, offset:20-bit]
    struct index_t
    {
        // clang-format off
        enum { NILL_VALUE = 0xFFFFFFFF, INDEX_MASK = 0xFFF00000, INDEX_SHIFT = 20, OFFSET_MASK = 0x000FFFFF };
        inline index_t() : m_value(NILL_VALUE) {}
        inline index_t(u16 index, u32 offset) : m_value((offset & OFFSET_MASK) | ((index << INDEX_SHIFT) & INDEX_MASK)) {}
        // clang-format on

        inline bool is_null() const { return m_value == NILL_VALUE; }
        inline u16  get_index() const { return ((m_value & INDEX_MASK) >> INDEX_SHIFT); }
        inline u32  get_offset() const { return m_value & OFFSET_MASK; }
        inline void set_index(u16 index) { m_value = (m_value & OFFSET_MASK) | ((index << INDEX_SHIFT) & INDEX_MASK); }
        inline void set_offset(u32 offset) { m_value = (m_value & INDEX_MASK) | (offset & OFFSET_MASK); }
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
        enum
        {
            COMPONENTS_MAX             = 1024,
            COMPONENTS_TYPE_HBB_CONFIG = 2, // 1024 maxbits
        };
        u32         m_a_cp_hbb[33]; // To identify which component stores are still free (to give out new component id)
        cp_type_t*  m_a_cp_type;    // The type information attached to each store
        cp_store_t* m_a_cp_store;   // N max number of components
    };

    // Entity Type, (8 + 8 + sizeof(u32)*max-number-of-components) ~4Kb
    // When an entity type registers a component it will allocate component data from the specific store for N entities
    // and it will keep it there. Of course each entity can mark if it actually uses the component, but that can basically
    // just be a single bit. So if only 50% of your entities of this type use this component you might be better of
    // registering another entity type.
    struct entity_type_t
    {
        index_t m_type_id_and_size;
        u32     m_entity_hbb_config;
        u32*    m_a_cp_store_offset; // Could be u24[], the components allocated start at a certain offset for each cp_store
        hbb_t   m_entity_hbb;
        u8*     m_entity_array; // Just versions
    };

    struct entity_type_store_t
    {
        enum
        {
            ENTITY_TYPE_MAX        = 256,          // (related to ECS_ENTITY_TYPE_MASK)
            ENTITY_TYPE_HBB_CONFIG = (8 << 5) | 2, // 256 maxbits
        };

        u32            m_entity_type_hbb[9];
        entity_type_t* m_entity_type_array;
    };

    struct ecs2_t
    {
        alloc_t*            m_allocator;
        components_store_t  m_component_store;
        entity_type_store_t m_entity_type_store;
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

    struct u24
    {
        u8 b[3];
    };

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