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

    static void s_clear(entity_type_t* et)
    {
        et->m_max_num_entities  = 0;
        et->m_a_cp_store_offset = nullptr;
        et->m_entity_hbb        = nullptr;
        et->m_entity_lll        = nullptr;
        et->m_entity_array      = nullptr;
    }

    static void s_init(entity_type_store_t* es, alloc_t* allocator)
    {
        init(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, 1);
        es->m_a_entity_type = (entity_type_t*)allocator->allocate(sizeof(entity_type_t) * (entity_type_store_t::ENTITY_TYPE_MAX));
        for (s32 i = 0; i < entity_type_store_t::ENTITY_TYPE_MAX; ++i)
        {
            s_clear(&es->m_a_entity_type[i]);
        }
    }

    static entity_type_t* s_get_entity_type(entity_type_store_t* es, u32 entity_type_id) { return &es->m_a_entity_type[entity_type_id]; }
    static bool           s_has_component(entity_type_t const* es, u32 cp_id) { return es->m_a_cp_store_offset[cp_id] != -1; }
    static u32            s_get_component_data_offset(entity_type_t const* es, u32 cp_id) { return es->m_a_cp_store_offset[cp_id]; }
    static void           s_set_component_data_offset(entity_type_t* es, u32 cp_id, u32 cp_offset) { es->m_a_cp_store_offset[cp_id] = cp_offset; }

    static entity_type_t* s_register_entity_type(entity_type_store_t* es, u32 max_entities, alloc_t* allocator)
    {
        u32 entity_type_id = 0;
        if (find(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_id))
        {
            clr(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_id);

            entity_type_t* et       = &es->m_a_entity_type[entity_type_id];
            et->m_a_cp_store_offset = (u32*)allocator->allocate(sizeof(u32) * components_store_t::COMPONENTS_MAX);
            et->m_entity_lll        = (u8*)allocator->allocate(sizeof(u8) * (max_entities / 256));
            et->m_entity_array      = (entity_t*)allocator->allocate(sizeof(entity_t) * max_entities);

            for (s32 i = 0; i < components_store_t::COMPONENTS_MAX; ++i)
                et->m_a_cp_store_offset[i] = -1;
            for (s32 i = 0; i < max_entities; ++i)
                et->m_entity_lll[i] = 0;
            for (s32 i = 0; i < max_entities; ++i)
                et->m_entity_array[i] = g_null_entity;

            init(et->m_entity_hbb, max_entities, et->m_entity_hbb_config, 1, allocator);
        }
        return nullptr;
    }

    static void s_unregister_entity_type(entity_type_store_t* es, entity_type_t const* et, alloc_t* allocator)
    {
        u32 const entity_type_id = (et - es->m_a_entity_type);
        set(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_id);

        allocator->deallocate(et->m_a_cp_store_offset);
        allocator->deallocate(et->m_entity_hbb);
        allocator->deallocate(et->m_entity_lll);
        allocator->deallocate(et->m_entity_array);
        s_clear(&es->m_a_entity_type[entity_type_id]);
    }

    static void s_exit(entity_type_store_t* es, alloc_t* allocator)
    {
        for (s32 i = 0; i < entity_type_store_t::ENTITY_TYPE_MAX; ++i)
        {
            u32 const entity_type_id = i;
            if (is_set(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, i))
            {
                entity_type_t* et = &es->m_a_entity_type[entity_type_id];
                s_unregister_entity_type(es, et, allocator);
            }
        }
        allocator->deallocate(es->m_a_entity_type);
        release((hbb_t&)es->m_entity_type_hbb, allocator);
    }

} // namespace xcore

#endif