#ifndef __CECS_ECS_H__
#define __CECS_ECS_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    typedef u32           entity_t;
    extern const entity_t g_null_entity;

    struct ecs_t;

    // Component Type - identifier information
    // Note: The user needs to create them like cp_type_t position_cp = { -1, sizeof(position_cp_t), "position" }; and register them at the ECS
    struct cp_type_t
    {
        s32               cp_id;     // Initialize to -1, calling 'g_register_tag_type' will initialize it
        s32 const         cp_sizeof; // Size of the component
        const char* const cp_name;   // Name of the component
    };

    // Tag Type - identifier information
    // Note: The user needs to create them like tg_type_t enemy_tag = { -1, "enemy" }; and register them at the ECS
    struct tg_type_t
    {
        s32               tg_id;   // Initialize to -1, calling 'g_register_tag_type' will initialize it
        const char* const tg_name; // Name of the tag
    };

    // Entity Type
    struct en_type_t;

    extern ecs_t*     g_create_ecs(alloc_t* allocator);
    extern void       g_destroy_ecs(ecs_t* ecs);
    extern en_type_t* g_register_entity_type(ecs_t* r, u32 max_entities);
    extern void       g_unregister_entity_type(ecs_t* r, en_type_t*);
    extern entity_t   g_create_entity(ecs_t* ecs, en_type_t* en_type);
    extern void       g_delete_entity(ecs_t* ecs, entity_t entity);

    // Registers a component type and returns its type information
    // Note: Do not register the same
    extern void g_register_component_type(ecs_t* r, cp_type_t* cp_type);

    // Registers a tag type and returns its type information
    extern void g_register_tag_type(ecs_t* r, tg_type_t* tg_type);

    extern bool                g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
    extern void                g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
    extern void                g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
    extern void*               g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
    template <typename T> T*   g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return (T*)g_get_cp(ecs, entity, cp_type); }
    extern bool                g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t* cp_type);
    template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { return g_has_tag(ecs, entity, tg_type); }
    extern void                g_set_tag(ecs_t* ecs, entity_t entity, tg_type_t* cp_type);
    extern void                g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t* cp_type);

    struct en_iterator_t // 186 bytes
    {
        ecs_t*     m_ecs;
        en_type_t* m_en_type; // Current entity type
        u32        m_en_id;   // Current entity id
        u16        m_cp_type_cnt;
        u16        m_cp_type_arr[64]; // Only entities with the following components
        u16        m_tg_type_cnt;
        u8         m_tg_type_arr[32]; // Only entities with the following tags

        void initialize(ecs_t*);
        void initialize(en_type_t*);

        // Mark the things you want to iterate on
        void cp_type(cp_type_t*);
        void tg_type(tg_type_t*);

        void     begin();
        entity_t item() const;
        void     next();
        bool     end() const;
    };

} // namespace ncore

#endif
