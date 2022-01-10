#ifndef __XECS_ECS_H__
#define __XECS_ECS_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    // Opaque 32 bits entity identifier.
    //
    // A 32 bits entity identifier guarantees:
    //   - 22 bits for the entity count (4 million)
    //   - 10 bit for the version(resets in[0 - 1023]).

    // clang-format off
    typedef u32 entity_t;
    typedef u32 entity_ver_t;
    typedef u32 entity_id_t;

    #define ECS_ENTITY_ID_MASK       ((u32)0x003FFFFF)   // Mask to use to get the entity number out of an identifier
    #define ECS_ENTITY_VERSION_MASK  ((u32)0xFFC00000)   // Mask to use to get the version out of an identifier
    #define ECS_ENTITY_VERSION_MAX   ((u32)0x000003FF)   // Maximum version
    #define ECS_ENTITY_VERSION_SHIFT ((s8)22)            // Extent of the entity number within an identifier
    inline entity_ver_t g_entity_version(entity_t e)                        { return ((u32)e & ECS_ENTITY_VERSION_MASK)>>ECS_ENTITY_VERSION_SHIFT; }
    inline entity_id_t  g_entity_id(entity_t e)                             { return (u32)e & ECS_ENTITY_ID_MASK; }
    inline entity_t     g_make_entity(entity_id_t id, entity_ver_t version) { return (u32)id | ((u32)version<<ECS_ENTITY_VERSION_SHIFT); }

    extern const entity_t g_null_entity;
    // clang-format on

    // Component Type - identifier information
    struct cp_type_t
    {
        u32 const         cp_id;
        u32 const         cp_sizeof;
        const char* const cp_name;
    };

    struct ecs2_t;
    extern ecs2_t* g_ecs_create(alloc_t* allocator);
    extern void    g_ecs_destroy(ecs2_t* ecs);

    // Entity functions
    extern entity_t g_create_entity(ecs2_t* ecs);
    extern void     g_delete_entity(ecs2_t* ecs, entity_t entity);

    // Component functions
    extern bool                     g_attach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void                     g_dettach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    extern void*                    g_get_component_data(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type);
    template <typename T> extern T* g_get_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return (T*)g_get_component_data(ecs, entity, cp_type); }

    // Signature functions (for iterators)
    extern void 

    // Registers a component type and returns its type information
    template <typename T> inline const char* nameof() { return "?"; }
    cp_type_t const*                         g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name);
    template <typename T> cp_type_t const*   g_register_component_type(ecs2_t* r) { return g_register_component_type(r, sizeof(T), nameof<T>()); }

} // namespace xcore

#endif
