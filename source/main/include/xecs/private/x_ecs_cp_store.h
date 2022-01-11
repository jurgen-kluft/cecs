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

    static void s_cp_store_init(cp_store_t* cp_store, cp_type_t const& cp_type, alloc_t* allocator)
    {
        cp_store->m_cap_size     = index_t(0, 0);
        u32 const cp_capacity    = s_cp_store_capacities[0];
        cp_store->m_cp_data      = (u8*)allocator->allocate(cp_capacity * cp_type.cp_sizeof);
        cp_store->m_cp_data_used = (u8*)allocator->allocate(cp_capacity);
    }

    static void s_cp_store_exit(cp_store_t* cp_store, alloc_t* allocator)
    {
        cp_store->m_cap_size = index_t(0, 0);
        if (cp_store->m_cp_data != nullptr)
        {
            allocator->deallocate(cp_store->m_cp_data);
            allocator->deallocate(cp_store->m_cp_data_used);
            cp_store->m_cp_data = nullptr;
        }
    }

    static void s_init(components_store_t* cps, alloc_t* allocator)
    {
        init(cps->m_a_cp_hbb, components_store_t::COMPONENTS_MAX, components_store_t::COMPONENTS_TYPE_HBB_CONFIG, 1);
        cps->m_a_cp_type  = (cp_type_t*)allocator->allocate(sizeof(cp_type_t) * components_store_t::COMPONENTS_MAX);
        cps->m_a_cp_store = (cp_store_t*)allocator->allocate(sizeof(cp_store_t) * components_store_t::COMPONENTS_MAX);
        x_memset(cps->m_a_cp_store, 0, sizeof(cp_store_t) * components_store_t::COMPONENTS_MAX);
    }

    static void s_exit(components_store_t* cps, alloc_t* allocator)
    {
        for (u32 i = 0; i < components_store_t::COMPONENTS_MAX; ++i)
        {
            s_cp_store_exit(&cps->m_a_cp_store[i], allocator);
        }
        allocator->deallocate(cps->m_a_cp_type);
        allocator->deallocate(cps->m_a_cp_store);
        cps->m_a_cp_type    = nullptr;
        cps->m_a_cp_store   = nullptr;
        init(cps->m_a_cp_hbb, components_store_t::COMPONENTS_MAX, components_store_t::COMPONENTS_TYPE_HBB_CONFIG, 1);
    }

    static cp_type_t const* s_cp_register_cp_type(components_store_t* cps, u32 cp_sizeof, const char* name)
    {
        u32 cp_id;
        find(cps->m_a_cp_hbb, components_store_t::COMPONENTS_MAX, components_store_t::COMPONENTS_TYPE_HBB_CONFIG, cp_id);

        cp_nctype_t* cp_type     = (cp_nctype_t*)&cps->m_a_cp_type[cp_id];
        cp_type[cp_id].cp_id     = cp_id;
        cp_type[cp_id].cp_sizeof = cp_sizeof;
        cp_type[cp_id].cp_name   = name;

        clr(cps->m_a_cp_hbb, components_store_t::COMPONENTS_MAX, components_store_t::COMPONENTS_TYPE_HBB_CONFIG, cp_id);
        return ((cp_type_t const*)cp_type);
    }

    static void s_cp_unregister_cp_type(components_store_t* cps, cp_type_t const* cp_type)
    {
        u32 const cp_id = cp_type->cp_id;
        set(cps->m_a_cp_hbb, components_store_t::COMPONENTS_MAX, components_store_t::COMPONENTS_TYPE_HBB_CONFIG, cp_id);
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
        cp_store->m_cap_size.set_index(cp_store->m_cap_size.get_index() + 1);
        u32 const cap            = cp_store->m_cap_size.get_index();
        u32 const size           = cp_store->m_cap_size.get_offset();
        cp_store->m_cp_data      = (u8*)s_reallocate(cp_store->m_cp_data, size * cp_type.cp_sizeof, s_cp_store_capacities[cap] * cp_type.cp_sizeof, allocator);
        cp_store->m_cp_data_used = (u8*)s_reallocate(cp_store->m_cp_data, size, s_cp_store_capacities[cap], allocator);
    }

    static inline u8* s_cp_store_get_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_index)
    {
        u8* cp_data = cp_store->m_cp_data + (cp_type.cp_sizeof * cp_index);
        return cp_data;
    }

    static void s_cp_store_dealloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 index) {}

    static u32 s_cp_store_alloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, alloc_t* allocator) { return 0; }

    static u32 s_component_alloc(components_store_t* cps, cp_type_t const* cp_type, alloc_t* allocator)
    {
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_type->cp_id];
        return s_cp_store_alloc_cp(cp_store, *cp_type, allocator);
    }

    static void s_component_dealloc(components_store_t* cps, u32 cp_id, u32 index, entity_id_t& changed_entity_id, u32& new_cp_offset)
    {
        cp_type_t*  cp_type  = (cp_type_t*)&cps->m_a_cp_type[cp_id];
        cp_store_t* cp_store = &cps->m_a_cp_store[cp_id];
        s_cp_store_dealloc_cp(cp_store, *cp_type, index);
    }

} // namespace xcore

#endif