#ifndef __XECS_ECS_TYPES_H__
#define __XECS_ECS_TYPES_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    // Opaque 32 bits entity identifier.
    //
    // A 32 bits entity identifier guarantees per type:
    //   - 16 bits for the entity count (65536)
    //   - 8 bit for the type(256), so in total you could have 16 million entities
    //   - 8 bit for the version(resets in[0 - 255]).

    struct ecs2_t;

    // clang-format off
    typedef u32 entity_t;
    typedef u16 entity_ver_t;
    typedef u32 entity_id_t;
    typedef u16 entity_type_id_t;

    #define ECS_ENTITY_ID_MASK       ((u32)0x0000FFFF)   // Mask to use to get the entity number out of an identifier
    #define ECS_ENTITY_TYPE_MASK     ((u32)0x00FF0000)   // Mask to use to get the entity type out of an identifier
    #define ECS_ENTITY_TYPE_MAX      ((u32)0x000000FF)   // Maximum type
    #define ECS_ENTITY_VERSION_MASK  ((u32)0xFF00000)    // Mask to use to get the version out of an identifier
    #define ECS_ENTITY_VERSION_MAX   ((u32)0x000000FF)   // Maximum version
    #define ECS_ENTITY_TYPE_SHIFT    ((s8)16)            // Extent of the entity id within an identifier
    #define ECS_ENTITY_VERSION_SHIFT ((s8)24)            // Extent of the entity id + type within an identifier

    extern const entity_t g_null_entity;
    // clang-format on

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

    template <typename T> inline const char* nameof() { return "?"; }

} // namespace xcore

#endif // __XECS_ECS_TYPES_H__