#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xbase/x_debug.h"
#include "xbase/x_hbb.h"
#include "xbase/x_integer.h"
#include "xbase/x_memory.h"

#include "xecs/x_ecs.h"
#include "xecs/private/x_ecs_cp_store.h"
#include "xecs/private/x_ecs_entity_store.h"

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
        s_init(&ecs->m_entity_type_store, allocator);
        return ecs;
    }

    void g_ecs_destroy(ecs2_t* ecs)
    {
        alloc_t* allocator = ecs->m_allocator;
        s_exit(&ecs->m_entity_type_store, allocator);
        s_exit(&ecs->m_component_store, allocator);
        allocator->deallocate(ecs);
    }

    entity_type_t const* g_register_entity_type(ecs2_t* r, u32 max_entities) { return s_register_entity_type(&r->m_entity_type_store, max_entities, r->m_allocator); }
    cp_type_t const* g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name) { return s_cp_register_cp_type(&r->m_component_store, cp_sizeof, cp_name); }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        return false;
    }

    static void* s_entity_get_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        //entity0_t* e0 = &ecs->m_a_entity_type.m_array[g_entity_id(e)];

        return nullptr;
    }

    // Attach requested component to the entity
    static bool s_entity_attach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
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

    entity_t g_create_entity(ecs2_t* es, entity_type_t const* et)
    {

        return 0;
    }

    void g_delete_entity(ecs2_t* ecs, entity_t entity)
    {
    }
} // namespace xcore
