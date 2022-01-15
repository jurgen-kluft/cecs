#ifndef __XECS_ECS_H__
#define __XECS_ECS_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    typedef u32           entity_t;
    extern const entity_t g_null_entity;

    struct ecs_t;

    // Component Type - identifier information
    struct cp_type_t
    {
        u32 const         cp_id;
        u32 const         cp_sizeof;
        const char* const cp_name;
    };

    // Tag Type - identifier information
    struct tg_type_t
    {
        u32 const         tg_id;
        const char* const tg_name;
    };

    // Entity Type
    struct en_type_t;

    extern ecs_t* g_create_ecs(alloc_t* allocator);
    extern void   g_destroy_ecs(ecs_t* ecs);

    // Entity Type
    extern en_type_t const* g_register_entity_type(ecs_t* r, u32 max_entities);
    extern void             g_unregister_entity_type(ecs_t* r, en_type_t const*);

    // Registers a component type and returns its type information
    template <typename T> inline const char* nameof() { return "?"; }
    extern cp_type_t const*                  g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cp_name);
    template <typename T> cp_type_t const*   g_register_component_type(ecs_t* r) { return g_register_component_type(r, sizeof(T), nameof<T>()); }

    // Registers a tag type and returns its type information
    extern tg_type_t const*                g_register_tag_type(ecs_t* r, const char* cp_name);
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
