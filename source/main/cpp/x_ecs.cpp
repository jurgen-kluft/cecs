#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xbase/x_debug.h"
#include "xbase/x_integer.h"
#include "xbase/x_memory.h"
#include "xecs/x_ecs.h"

#include "xecs/private/x_ecs_cp_store.h"
#include "xecs/private/x_ecs_entity2.h"
#include "xecs/private/x_ecs_entity0.h"

namespace xcore
{
    const entity_t g_null_entity = (entity_t)0xFFFFFFFF;

    // --------------------------------------------------------------------------------------------------------
    // entity component system, create and destroy

    ecs2_t* g_ecs_create(alloc_t* allocator)
    {
        ecs2_t* ecs      = (ecs2_t*)allocator->allocate(sizeof(ecs2_t));
        ecs->m_allocator = allocator;
        s_init(&ecs->m_component_store, allocator);
        s_init(&ecs->m_entity0_store, allocator);
        return ecs;
    }

    void g_ecs_destroy(ecs2_t* ecs)
    {
        alloc_t* allocator = ecs->m_allocator;
        s_exit(&ecs->m_entity0_store, allocator);
        s_exit(&ecs->m_component_store, allocator);
        allocator->deallocate(ecs);
    }

    // --------------------------------------------------------------------------------------------------------
    // hierarchical bit buffer (max 32768 bits, 3 levels)

    void s_init(entity_hbb_t* ehbb, u32 size, alloc_t* allocator)
    {
        ehbb->m_size = size;   
        ehbb->m_level2 = 0xFFFFFFFF;
        
        size = (size + (32 - 1)) & ~(32 - 1); // align up, multiple of 32
        u32 const level0_size = size >> 5;
        u32 const level1_size = ((size + 31) >> 5);
        ehbb->m_level0 = (u32*)allocator->allocate(sizeof(u32) * (level0_size + level1_size));
        ehbb->m_level1 = ehbb->m_level0 + level0_size;
        x_memset(ehbb->m_level0, 0xFFFFFFFF, sizeof(u32) * (level0_size + level1_size));

        // clear the part in the hierarchy that falls outside of the incoming 
    }

    void s_exit(entity_hbb_t* ehbb, alloc_t* allocator)
    {
        allocator->deallocate(ehbb->m_level0);
        ehbb->m_level2 = 0;
        ehbb->m_level1 = 0;
        ehbb->m_level0 = 0;
    }

    // --------------------------------------------------------------------------------------------------------
    // Component Type Registration

    cp_type_t const* g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name) { return s_cp_register_cp_type(&r->m_component_store, cp_sizeof, cp_name); }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        return false;
    }

    static void* s_entity_get_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity0_t* e0 = &ecs->m_entity0_store.m_array[g_entity_id(e)];

        return nullptr;
    }

    // Attach requested component to the entity
    static bool s_entity_attach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        return false;
    }

    static bool s_entity_update_component(ecs2_t* ecs, entity_t e, u32 cp_id, u32 cp_data_offset)
    {
        // Remove component from the shard and bitset
        // Remove component data offset entry from entity2_t
        // Remove component data from cp_store

        return false;
    }

    // Remove/detach component from the entity
    static bool s_entity_detach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        // Remove component from the shard and bitset
        // Remove component data offset entry from entity2_t
        // Remove component data from cp_store

        return false;
    }

    bool  g_attach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_attach_component(ecs, entity, *cp_type); }
    void  g_dettach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { s_entity_detach_component(ecs, entity, *cp_type); }
    void* g_get_component_data(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_get_component(ecs, entity, *cp_type); }

    entity_t g_create_entity(entity_hbb_t* ehbb, entity0_store_t* es)
    {
        u32 const o0 = ehbb->get_free(0);

        // the byte in level 0 is an extra offset on top of o0 that gives us
        // an index of a free entity in the m_a_entity0 array.
        u32 const eo = o0 + es->m_level0[o0];

        entity0_t& e = es->m_array[eo];

        return 0;
    }

    void g_delete_entity(ecs2_t* ecs, entity_t entity)
    {
    }
} // namespace xcore
