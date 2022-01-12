#ifndef __XECS_ECS_H__
#define __XECS_ECS_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/x_ecs_types.h"

namespace xcore
{
    extern ecs_t* g_create_ecs(alloc_t* allocator);
    extern void   g_destroy_ecs(ecs_t* ecs);

    // Entity Type
    en_type_t const* g_register_entity_type(ecs_t* r, u32 max_entities);

    // Registers a component type and returns its type information
    cp_type_t const*                       g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cp_name);
    template <typename T> cp_type_t const* g_register_component_type(ecs_t* r) { return g_register_component_type(r, sizeof(T), nameof<T>()); }

    // Registers a tag type and returns its type information
    tg_type_t const*                       g_register_tag_type(ecs_t* r, const char* cp_name);
    template <typename T> tg_type_t const* g_register_tag_type(ecs_t* r) { return g_register_tag_type(r, nameof<T>()); }

    extern entity_t            g_create_entity(ecs_t* ecs, en_type_t const* en_type);
    extern void                g_delete_entity(ecs_t* ecs, entity_t entity);
    extern bool                g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void                g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void                g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void*               g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type);
    template <typename T> T*   g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t const* cp_type) { return (T*)g_get_cp(ecs, entity, cp_type); }
    extern bool                g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type);
    template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t const* tg_type) { return g_has_tag(ecs, entity, tg_type); }
    extern void                g_set_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type);
    extern void                g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t const* cp_type);

} // namespace xcore

#endif
