#ifndef __XECS_ECS_CP_STORE_H__
#define __XECS_ECS_CP_STORE_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/private/x_ecs_types.h"

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

    static void s_cp_store_exit(cp_store_t* cp_store, alloc_t* allocator)
    {
        cp_store->m_size = 0;
        if (cp_store->m_cp_data != nullptr)
        {
            allocator->deallocate(cp_store->m_cp_data);
            allocator->deallocate(cp_store->m_cp_data_used);
            cp_store->m_cp_data = nullptr;
        }
    }

    static void s_init(cp_store_mgr_t* cps, alloc_t* allocator)
    {
        g_hbb_init(cps->m_a_cp_hbb, cp_store_mgr_t::COMPONENTS_MAX, 1);
        cps->m_a_cp_type  = (cp_type_t*)allocator->allocate(sizeof(cp_type_t) * cp_store_mgr_t::COMPONENTS_MAX);
        cps->m_a_cp_store = (cp_store_t*)allocator->allocate(sizeof(cp_store_t) * cp_store_mgr_t::COMPONENTS_MAX);
        x_memset(cps->m_a_cp_store, 0, sizeof(cp_store_t) * cp_store_mgr_t::COMPONENTS_MAX);
    }

    static void s_exit(cp_store_mgr_t* cps, alloc_t* allocator)
    {
        for (u32 i = 0; i < cp_store_mgr_t::COMPONENTS_MAX; ++i)
        {
            s_cp_store_exit(&cps->m_a_cp_store[i], allocator);
        }
        allocator->deallocate(cps->m_a_cp_type);
        allocator->deallocate(cps->m_a_cp_store);
        cps->m_a_cp_type  = nullptr;
        cps->m_a_cp_store = nullptr;
        g_hbb_init(cps->m_a_cp_hbb, cp_store_mgr_t::COMPONENTS_MAX, 1);
    }

    static cp_type_t const* s_cp_register_cp_type(cp_store_mgr_t* cps, u32 cp_sizeof, const char* name)
    {
        u32 cp_id;
        if (g_hbb_find(cps->m_a_cp_hbb, cp_id))
        {
            cp_nctype_t* cp_type     = (cp_nctype_t*)&cps->m_a_cp_type[cp_id];
            cp_type[cp_id].cp_id     = cp_id;
            cp_type[cp_id].cp_sizeof = cp_sizeof;
            cp_type[cp_id].cp_name   = name;

            g_hbb_clr(cps->m_a_cp_hbb, cp_id);
            return ((cp_type_t const*)cp_type);
        }
        return nullptr;
    }

    static cp_type_t* s_cp_get_cp_type(cp_store_mgr_t* cps, u32 cp_id)
    {
        cp_type_t* cp_type = (cp_type_t*)&cps->m_a_cp_type[cp_id];
        return cp_type;
    }

    static void s_cp_unregister_cp_type(cp_store_mgr_t* cps, cp_type_t const* cp_type)
    {
        u32 const cp_id = cp_type->cp_id;
        g_hbb_set(cps->m_a_cp_hbb, cp_id);
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

    static inline u8* s_cp_store_get_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_offset)
    {
        u8* cp_data = cp_store->m_cp_data + (cp_type.cp_sizeof * cp_offset);
        return cp_data;
    }

    static u32 s_cp_store_alloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 count, alloc_t* allocator)
    {
        if (cp_store->m_cp_data == nullptr)
        {
            cp_store->m_size         = count;
            cp_store->m_cp_data      = (u8*)allocator->allocate(count * cp_type.cp_sizeof);
            cp_store->m_cp_data_used = (u8*)allocator->allocate((count + 7) / 8);
            x_memset(cp_store->m_cp_data_used, 0, (count + 7) / 8);
            return 0;
        }
        else
        {
            u32 const old_size       = cp_store->m_size;
            u32 const new_size       = old_size + count;
            cp_store->m_size         = new_size;
            cp_store->m_cp_data      = (u8*)s_reallocate(cp_store->m_cp_data, old_size * cp_type.cp_sizeof, new_size * cp_type.cp_sizeof, allocator);
            cp_store->m_cp_data_used = (u8*)s_reallocate(cp_store->m_cp_data, (old_size + 7) / 8, (new_size + 7) / 8, allocator);
            x_memset(cp_store->m_cp_data + (old_size + 7) / 8, 0, (count + 7) / 8);
            return old_size;
        }
    }

    static void s_cp_store_dealloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 offset, u32 count)
    {
        // TODO: Tricky since we have no idea how to "remove" a slice out of the whole array without updating
        // other slice owners about an update to their offset.
        // One idea is to have a book keeping array that stores [entity type id, offset], we can then use that
        // to identify which entity type ids need to be informed of an update of their offset.
        return;
    }

    static u32 s_components_alloc(cp_store_mgr_t* cps, cp_type_t const* cp_type, u32 count, alloc_t* allocator)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type->cp_id];
        return s_cp_store_alloc_cp(cp_store, *cp_type, count, allocator);
    }

    static void s_components_dealloc(cp_store_mgr_t* cps, cp_type_t const* cp_type, u32 offset, u32 count)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type->cp_id];
        s_cp_store_dealloc_cp(cp_store, *cp_type, offset, count);
    }

    static inline void s_components_set_cp_used(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type.cp_id];
        u8*         data     = cp_store->m_cp_data_used;
        u8 const    bit      = 1 << (cp_offset & 0x7);
        data[cp_offset >> 3] |= bit;
    }

    static inline void s_components_set_cp_unused(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type.cp_id];
        u8*         data     = cp_store->m_cp_data_used;
        u8 const    bit      = 1 << (cp_offset & 0x7);
        data[cp_offset >> 3] &= ~bit;
    }

    static inline bool s_components_get_cp_used(cp_store_mgr_t const* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type.cp_id];
        u8 const*   data     = cp_store->m_cp_data_used;
        u8 const    bit      = 1 << (cp_offset & 0x7);
        return (data[cp_offset >> 3] & bit) != 0;
    }

    static inline u8* s_components_get_cp_data(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type.cp_id];
        u8*         cp_data  = cp_store->m_cp_data + (cp_type.cp_sizeof * cp_offset);
        return cp_data;
    }

} // namespace xcore

#endif