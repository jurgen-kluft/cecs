#ifndef __CECS_ECS_H__
#define __CECS_ECS_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace necs2
    {
        typedef u32           entity_t;
        extern const entity_t g_null_entity;

        struct ecs_t;

        // Component Group
        // Every component group has an array<u32> to re-direct the entity id to the component index.
        // It is thus quite important to manage the component groups properly.

        // Component Type - identifier information
        // Note: The user needs to create them like cp_type_t position_cp = { "position", sizeof(position_cp_t) }; and register them at the ECS
        // Note: We are using cp_type_t also for tags
        //       So to create them use cp_type_t enemy_tag = { "enemy", 0 }; and register them at the ECS

        extern ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities);
        extern void   g_destroy_ecs(ecs_t* ecs);

        // Create and Destroy Entity
        extern entity_t g_create_entity(ecs_t* ecs);
        extern void     g_destroy_entity(ecs_t* ecs, entity_t e);

        struct cp_type_t;
        struct cp_group_t;

        // Registers a component group, component type and tag type
        extern cp_group_t* g_register_cp_group(ecs_t* ecs);
        extern cp_type_t*  g_register_cp_type(ecs_t* r, const char* cp_name, s32 cp_sizeof, cp_group_t* cp_group);
        extern cp_type_t*  g_register_tg_type(ecs_t* r, const char* tg_name, cp_group_t* cp_group);

        extern bool                g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        extern void                g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        extern void                g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        extern void*               g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> T*   g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return (T*)g_get_cp(ecs, entity, cp_type); }
        extern bool                g_has_tag(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity, cp_type_t* tg_type) { return g_has_tag(ecs, entity, tg_type); }
        extern void                g_set_tag(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        extern void                g_rem_tag(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);

        struct en_iterator_t // 56 bytes
        {
            ecs_t* m_ecs;              // The ECS
            u64    m_group_mask;       // The group mask
            u32    m_group_cp_mask[7]; // An entity cannot be in more than 7 component groups
            s32    m_entity_index;     // Current entity index
            s32    m_entity_index_max;
            s8     m_num_groups;

            void initialize(ecs_t*);
            void cp_type(cp_type_t*); // Mark the things you want to iterate on
            void tg_type(cp_type_t* t) { cp_type(t); }

            void     begin();
            entity_t entity() const;
            void     next();
            bool     end() const;
        };
    } // namespace necs2
} // namespace ncore

#endif
