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

    static void s_clear(en_type_t* et)
    {
        et->m_type_id_and_size  = index_t();
        et->m_a_cp_store_offset = nullptr;
        et->m_entity_hbb        = nullptr;
        et->m_entity_array      = nullptr;
    }

    static inline bool s_is_registered(en_type_t const* et) { return !et->m_type_id_and_size.is_null(); }

    static void s_init(entity_type_store_t* es, alloc_t* allocator)
    {
        g_hbb_init(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, 1);
        es->m_entity_type_array = (en_type_t*)allocator->allocate(sizeof(en_type_t) * (entity_type_store_t::ENTITY_TYPE_MAX));
        for (s32 i = 0; i < entity_type_store_t::ENTITY_TYPE_MAX; ++i)
        {
            s_clear(&es->m_entity_type_array[i]);
        }
    }

    static en_type_t* s_get_entity_type(entity_type_store_t* es, u32 entity_type_id) { return &es->m_entity_type_array[entity_type_id]; }
    static bool           s_has_component(en_type_t const* es, u32 cp_id) { return es->m_a_cp_store_offset[cp_id] != -1; }
    static u32            s_get_component_data_offset(en_type_t const* es, u32 cp_id) { return es->m_a_cp_store_offset[cp_id]; }
    static void           s_set_component_data_offset(en_type_t* es, u32 cp_id, u32 cp_offset) { es->m_a_cp_store_offset[cp_id] = cp_offset; }

    static en_type_t* s_register_entity_type(entity_type_store_t* es, u32 max_entities, alloc_t* allocator)
    {
        u32 entity_type_id = 0;
        if (g_hbb_find(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_id))
        {
            g_hbb_clr(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_id);

            en_type_t* et = &es->m_entity_type_array[entity_type_id];
            et->m_type_id_and_size.set_index(entity_type_id);
            et->m_type_id_and_size.set_offset(max_entities);
            et->m_a_cp_store_offset = (u32*)allocator->allocate(sizeof(u32) * components_store_t::COMPONENTS_MAX);
            et->m_entity_array      = (u8*)allocator->allocate(sizeof(u8) * max_entities);

            for (s32 i = 0; i < components_store_t::COMPONENTS_MAX; ++i)
                et->m_a_cp_store_offset[i] = -1;
            for (u32 i = 0; i < max_entities; ++i)
                et->m_entity_array[i] = 0;
            g_hbb_init(et->m_entity_hbb, max_entities, et->m_entity_hbb_config, 1, allocator);
            return et;
        }
        return nullptr;
    }

    // struct en_type_t
    // {
    //     index_t   m_type_id_and_size;
    //     u32       m_entity_hbb_config;
    //     u32*      m_a_cp_store_offset; // Could be u24[], the components allocated start at a certain offset for each cp_store
    //     hbb_t     m_entity_hbb;
    //     u8*       m_entity_array;
    // };
    static entity_t s_create_entity(en_type_t const* et)
    {
        // NOTE: Do something when the incoming en_type_t is unregistered
        if (!s_is_registered(et))
            return g_null_entity;

        u32 entity_id = 0;
        if (g_hbb_find(et->m_entity_hbb, et->m_entity_hbb_config, et->m_type_id_and_size.get_offset(), entity_id))
        {
            g_hbb_clr(et->m_entity_hbb, et->m_entity_hbb_config, et->m_type_id_and_size.get_offset(), entity_id);

            u8&              eVER  = et->m_entity_array[entity_id];
            entity_type_id_t eTYPE = et->m_type_id_and_size.get_index();
            eVER += 1;
            return g_make_entity(eVER, eTYPE, entity_id);
        }
        return g_null_entity;
    }

    static void s_delete_entity(components_store_t* cps, en_type_t* et, entity_t e)
    {
        if (s_is_registered(et))
        {
            u32 const entity_id = g_entity_id(e);
            if (!g_hbb_is_set(et->m_entity_hbb, et->m_entity_hbb_config, et->m_type_id_and_size.get_offset(), entity_id))
            {
                g_hbb_set(et->m_entity_hbb, et->m_entity_hbb_config, et->m_type_id_and_size.get_offset(), entity_id);

                // NOTE: For all components for this entity type and mark them as unused for this entity

                // Bruteforce (could there be a use for hbb here?)
                // TODO: Add an hbb in entity_type to identify the components that are actually registered
                for (s32 i = 0; i < components_store_t::COMPONENTS_MAX; ++i)
                {
                    if (et->m_a_cp_store_offset[i] == -1)
                        continue;

                    u32 const        cp_offset = et->m_a_cp_store_offset[i] + i;
                    cp_type_t const* cp_type   = s_cp_get_cp_type(cps, i);
                    s_components_set_cp_unused(cps, *cp_type, cp_offset);
                }
            }
        }
    }

    static void s_unregister_entity_type(entity_type_store_t* es, en_type_t const* et, alloc_t* allocator)
    {
        if (s_is_registered(et))
        {
            u32 const entity_type_id = et->m_type_id_and_size.get_index();
            g_hbb_set(es->m_entity_type_hbb, entity_type_store_t::ENTITY_TYPE_HBB_CONFIG, entity_type_store_t::ENTITY_TYPE_MAX, entity_type_id);

            allocator->deallocate(et->m_a_cp_store_offset);
            allocator->deallocate(et->m_entity_hbb);
            allocator->deallocate(et->m_entity_array);
            s_clear(&es->m_entity_type_array[entity_type_id]);
        }
    }

    static void s_exit(entity_type_store_t* es, alloc_t* allocator)
    {
        for (s32 i = 0; i < entity_type_store_t::ENTITY_TYPE_MAX; ++i)
        {
            en_type_t* et = &es->m_entity_type_array[i];
            s_unregister_entity_type(es, et, allocator);
        }
        allocator->deallocate(es->m_entity_type_array);
    }

} // namespace xcore

#endif