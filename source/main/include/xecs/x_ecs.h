#ifndef __XECS_ECS_H__
#define __XECS_ECS_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/x_ecs_types.h"

namespace xcore
{
    extern ecs2_t* g_ecs_create(alloc_t* allocator);
    extern void    g_ecs_destroy(ecs2_t* ecs);

    // Entity Type
    en_type_t const* g_register_entity_type(ecs2_t* r, u32 max_entities);

    // Registers a component type and returns its type information
    cp_type_t const*                         g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name);
    template <typename T> cp_type_t const*   g_register_component_type(ecs2_t* r) { return g_register_component_type(r, sizeof(T), nameof<T>()); }

	// TODO: Add tags as a bit for each entity, e.g. a hbb_t for each tag.
	// So in the same manner a component we can have a entity_tag_t.

    extern entity_t                 g_create_entity(ecs2_t* ecs, en_type_t const* en_type);
    extern void                     g_delete_entity(ecs2_t* ecs, entity_t entity);
    extern bool                     g_has_cp(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void                     g_set_cp(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void                     g_rem_cp(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void*                    g_get_cp(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    template <typename T> extern T* g_get_cp(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return (T*)g_get_cp(ecs, entity, cp_type); }
    extern bool                     g_has_tag(ecs2_t* ecs, entity_t entity, tg_type_t const* cp_type);
    extern void                     g_set_tag(ecs2_t* ecs, entity_t entity, tg_type_t const* cp_type);
    extern void                     g_rem_tag(ecs2_t* ecs, entity_t entity, tg_type_t const* cp_type);

} // namespace xcore

#endif
