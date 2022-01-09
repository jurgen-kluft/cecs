#ifndef __XECS_ECS_CP_STORE_H__
#define __XECS_ECS_CP_STORE_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    static constexpr const s32 s_cp_store_capacities_count = 60;
    static constexpr const u32 s_cp_store_capacities[]     = {4,      6,      10,     14,     22,     26,     34,     46,     58,     74,     94,      118,     146,     194,     254,     302,     394,     502,     626,     794,
                                                          998,    1262,   1594,   2018,   2518,   3194,   4022,   5078,   6406,   8054,   10174,   12842,   16178,   20386,   25706,   32386,   40798,   51434,   64802,   81646,
                                                          102874, 129622, 163298, 205754, 259214, 326614, 411518, 518458, 653234, 823054, 1037018, 1306534, 1646234, 2074118, 2613202, 3292474, 4148258, 5226458, 6584978, 8296558};

    struct cp_nctype_t
    {
        u32         cp_id;
        u32         cp_sizeof;
        const char* cp_name;
    };

    struct cp_store_t
    {
        u8* m_cp_data;
        u8* m_entity_ids;
        u32 m_capacity_index;
        u32 m_size;
    };

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

    static void s_cp_store_init(cp_store_t* cp_store, cp_type_t const& cp_type, alloc_t* allocator)
    {
        cp_store->m_capacity_index = 0;
        cp_store->m_size           = 0;
        u32 const cp_capacity      = s_cp_store_capacities[0];
        cp_store->m_cp_data        = (u8*)allocator->allocate(cp_capacity * cp_type.cp_sizeof);
        cp_store->m_entity_ids     = (u8*)allocator->allocate(cp_capacity * 3); // 3 bytes per entity id
    }

    static void s_cp_store_exit(cp_store_t* cp_store, alloc_t* allocator)
    {
        cp_store->m_capacity_index = 0;
        cp_store->m_size           = 0;
        if (cp_store->m_cp_data != nullptr)
        {
            allocator->deallocate(cp_store->m_cp_data);
            allocator->deallocate(cp_store->m_entity_ids);
            cp_store->m_cp_data = nullptr;
            cp_store->m_entity_ids = nullptr;
        }
    }

    struct components_store_t
    {
        u32          m_num_cp_store;
        u32          m_bitset1;
        u32          m_a_bitset0[32]; // To identify which component stores are still free (to give out new component id)
        cp_nctype_t* m_a_cp_type;     // The type of each store
        cp_store_t*  m_a_cp_store;    // N max number of components
    };

    static void s_init(components_store_t* cps, alloc_t* allocator)
    {
        cps->m_num_cp_store = 32 * 32;
        cps->m_bitset1 = 0xFFFFFFFF;
        x_memset(cps->m_a_bitset0, 0xFFFFFFFF, 32 * sizeof(u32));

        cps->m_a_cp_type  = (cp_nctype_t*)allocator->allocate(sizeof(cp_nctype_t) * cps->m_num_cp_store);
        cps->m_a_cp_store = (cp_store_t*)allocator->allocate(sizeof(cp_store_t) * cps->m_num_cp_store);
        x_memset(cps->m_a_cp_store, 0, sizeof(cp_store_t) * cps->m_num_cp_store);
    }

    static void s_exit(components_store_t* cps, alloc_t* allocator)
    {
        for (u32 i=0; i<cps->m_num_cp_store; ++i)
        {
            s_cp_store_exit(&cps->m_a_cp_store[i], allocator);
        }
        allocator->deallocate(cps->m_a_cp_type);
        allocator->deallocate(cps->m_a_cp_store);
        cps->m_a_cp_type = nullptr;
        cps->m_a_cp_store = nullptr;
        cps->m_num_cp_store = 0;
        cps->m_bitset1 = 0;
        x_memset(cps->m_a_bitset0, 0, 32 * sizeof(u32));
    }

    static cp_type_t const* s_cp_register_cp_type(components_store_t* cps, u32 cp_sizeof, const char* name)
    {
        if (cps->m_bitset1 == 0)
            return nullptr;

        s8 const o1 = xfindFirstBit(cps->m_bitset1);
        s8 const o0 = xfindFirstBit(cps->m_a_bitset0[o1]);

        u32 const cp_id                   = o1 * 32 + o0;
        cps->m_a_cp_type[cp_id].cp_id     = cp_id;
        cps->m_a_cp_type[cp_id].cp_sizeof = cp_sizeof;
        cps->m_a_cp_type[cp_id].cp_name   = name;

        if (s_clr_bit_in_u32(cps->m_a_bitset0[o1], o0) == 0)
        {
            // No more free items in here, mark upper level
            s_clr_bit_in_u32(cps->m_bitset1, o0);
        }

        return ((cp_type_t const*)&cps->m_a_cp_type[o1 * 32 + o0]);
    }

    static void s_cp_unregister_cp_type(components_store_t* cps, cp_type_t const* cp_type)
    {
        s8 const o1 = cp_type->cp_id / 32;
        s8 const o0 = cp_type->cp_id & (32 - 1);
        if (s_set_bit_in_u32(cps->m_a_bitset0[o1], o0) == 0xFFFFFFFF)
        {
            s_set_bit_in_u32(cps->m_bitset1, o1);
        }
    }

    static void* s_reallocate(void* current_data, u32 current_datasize_in_bytes, u32 datasize_in_bytes, alloc_t* allocator)
    {
        void* data = allocator->allocate(datasize_in_bytes);
        if (current_data != nullptr)
        {
            x_memcpy(data, current_data, current_datasize_in_bytes);
            allocator->deallocate(current_data);
        }
        return data;
    }

    static void s_cp_store_grow(cp_store_t* cp_store, cp_type_t const& cp_type, alloc_t* allocator)
    {
        cp_store->m_capacity_index += 1;
        cp_store->m_cp_data    = (u8*)s_reallocate(cp_store->m_cp_data, cp_store->m_size * cp_type.cp_sizeof, s_cp_store_capacities[cp_store->m_capacity_index] * cp_type.cp_sizeof, allocator);
        cp_store->m_entity_ids = (u8*)s_reallocate(cp_store->m_entity_ids, cp_store->m_size * 3, s_cp_store_capacities[cp_store->m_capacity_index] * 3, allocator);
    }

    static inline u8* s_cp_store_get_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_index)
    {
        u8* cp_data = cp_store->m_cp_data + (cp_type.cp_sizeof * cp_index);
        return cp_data;
    }

    static void s_cp_store_dealloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_index, u32& changed_entity_id, u32& new_cp_index)
    {
        // user should check if the entity that is processed is unequal to the 'changed entity id' to
        // see if it needs to update component index.
        u32 const last_cp_index = cp_store->m_size - 1;
        changed_entity_id       = cp_store->m_entity_ids[last_cp_index];
        if (cp_index != last_cp_index)
        {
            // swap remove
            cp_store->m_entity_ids[cp_index] = changed_entity_id;

            // copy component data (slow version)
            // TODO: speed up (align size and struct to u32?)
            u8 const* src_data = s_cp_store_get_cp(cp_store, cp_type, cp_index);
            u8*       dst_data = s_cp_store_get_cp(cp_store, cp_type, last_cp_index);
            s32       n        = 0;
            while (n < cp_type.cp_sizeof)
                *dst_data++ = *src_data++;
        }
        new_cp_index = cp_index;
        cp_store->m_size -= 1;
    }

    static u32 s_cp_store_alloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, alloc_t* allocator)
    {
        // return index of newly allocated component
        if (cp_store->m_size == s_cp_store_capacities[cp_store->m_capacity_index])
        {
            s_cp_store_grow(cp_store, cp_type, allocator);
        }
        u32 const new_index = cp_store->m_size++;
        return new_index;
    }    

} // namespace xcore

#endif