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

    static void s_init(entity0_store_t* es, alloc_t* allocator)
    {
        u32 hbb_config;
        init(es->m_entity_hbb, (1 << ECS_ENTITY_VERSION_SHIFT) / 256, hbb_config, allocator);
        es->m_level0 = (u8*)allocator->allocate(sizeof(u8) * (1 << ECS_ENTITY_VERSION_SHIFT) / 256);

        es->m_cap_size     = index_t(0, 0);
        u32 const cap_size = s_entity0_array_capacities[es->m_cap_size.get_index()];
        es->m_array        = (entity0_t*)allocator->allocate(sizeof(entity0_t) * (ECS_ENTITY_ID_MASK + 1));
    }

    static void s_exit(entity0_store_t* es, alloc_t* allocator)
    {
        release(es->m_entity_hbb, allocator);
        allocator->deallocate(es->m_level0);        
        allocator->deallocate(es->m_array);
    }

    static entity0_t* s_get_entity0(entity0_store_t* es, entity_t e) { return &es->m_array[g_entity_id(e)]; }

    // Entity Type functionality

    static entity_type_t const* s_register_entity_type(ecs2_t* ecs, u32 max_entities) {}

} // namespace xcore

#endif