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

    ecs_t* g_create_ecs(alloc_t* allocator)
    {
        ecs_t* ecs       = (ecs_t*)allocator->allocate(sizeof(ecs_t));
        ecs->m_allocator = allocator;
        s_init(&ecs->m_component_store, allocator);
        s_init(&ecs->m_entity_type_store, allocator);
        return ecs;
    }

    void g_destroy_ecs(ecs_t* ecs)
    {
        alloc_t* allocator = ecs->m_allocator;
        s_exit(&ecs->m_entity_type_store, allocator);
        s_exit(&ecs->m_component_store, allocator);
        allocator->deallocate(ecs);
    }

    en_type_t const* g_register_entity_type(ecs_t* r, u32 max_entities) { return s_register_entity_type(&r->m_entity_type_store, max_entities, r->m_allocator); }
    cp_type_t const* g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cp_name) { return s_cp_register_cp_type(&r->m_component_store, cp_sizeof, cp_name); }
    tg_type_t const* g_register_tag_type(ecs_t* r, const char* cp_name) { return s_cp_register_tag_type(&r->m_component_store, cp_name); }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity_type_id_t const type_id     = g_entity_type_id(e);
        en_type_t const*       entity_type = s_get_entity_type(&ecs->m_entity_type_store, type_id);
        u32 const              cp_offset   = s_get_component_data_offset(entity_type, cp_type.cp_id) + g_entity_id(e);
        return s_components_get_cp_used(&ecs->m_component_store, cp_type, cp_offset);
    }

    static void* s_entity_get_component(ecs_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity_type_id_t const type_id     = g_entity_type_id(e);
        en_type_t const*       entity_type = s_get_entity_type(&ecs->m_entity_type_store, type_id);
        u32 const              cp_offset   = s_get_component_data_offset(entity_type, cp_type.cp_id) + g_entity_id(e);
        return s_components_get_cp_data(&ecs->m_component_store, cp_type, cp_offset);
    }

    static void s_entity_set_component(ecs_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity_type_id_t const type_id     = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, type_id);
        u32                    cp_offset   = s_get_component_data_offset(entity_type, cp_type.cp_id);
        if (cp_offset == 0xFFFFFFFF)
        {
            cp_offset = s_components_alloc(&ecs->m_component_store, &cp_type, entity_type->m_type_id_and_size.get_offset(), ecs->m_allocator);
            s_set_component_data_offset(entity_type, cp_type.cp_id, cp_offset);
        }
        // Now set the mark for this entity that he has attached this component
        cp_offset = s_get_component_data_offset(entity_type, cp_type.cp_id) + g_entity_id(e);
        s_components_set_cp_used(&ecs->m_component_store, cp_type, cp_offset);
    }

    // Remove/detach component from the entity
    static void s_entity_rem_component(ecs_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity_type_id_t const type_id     = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, type_id);
        u32                    cp_offset   = s_get_component_data_offset(entity_type, cp_type.cp_id);
        if (cp_offset == 0xFFFFFFFF)
            return;
        // Now set the mark for this entity that he has attached this component
        cp_offset = s_get_component_data_offset(entity_type, cp_type.cp_id) + g_entity_id(e);
        s_components_set_cp_unused(&ecs->m_component_store, cp_type, cp_offset);
    }

    entity_t g_create_entity(ecs_t* es, en_type_t const* et) { return s_create_entity(et); }

    void g_delete_entity(ecs_t* ecs, entity_t e)
    {
        entity_type_id_t const type_id     = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, type_id);
        s_delete_entity(&ecs->m_component_store, entity_type, e);
    }

    bool  g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_has_component(ecs, entity, *cp_type); }
    void  g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type) { s_entity_set_component(ecs, entity, *cp_type); }
    void  g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type) { s_entity_rem_component(ecs, entity, *cp_type); }
    void* g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_get_component(ecs, entity, *cp_type); }

    bool g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type) { return false; }

    void g_set_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type) {}

    void g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type) {}

} // namespace xcore
