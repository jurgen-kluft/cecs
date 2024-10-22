#ifndef __CECS_ECS2_H__
#define __CECS_ECS2_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace necs2
    {
        // ECS Version 2, a simple Entity-Component-System (ECS) implementation.
        typedef u32 entity_t;
        typedef u8  entity_generation_t;
        typedef u32 entity_index_t;

        const u32 ECS_ENTITY_NULL        = (0xFFFFFFFF); // Null entity
        const u32 ECS_ENTITY_INDEX_MASK  = (0x00FFFFFF); // Mask to use to get the entity index from an entity identifier
        const u32 ECS_ENTITY_GEN_ID_MASK = (0xFF000000); // Mask to use to get the generation id from an entity identifier
        const s8  ECS_ENTITY_GEN_SHIFT   = (24);         // Extent of the entity id + type within an identifier

        inline bool                s_entity_is_null(entity_t e) { return e == ECS_ENTITY_NULL; }
        inline entity_generation_t s_entity_generation(entity_t e) { return ((u32)e & ECS_ENTITY_GEN_ID_MASK) >> ECS_ENTITY_GEN_SHIFT; }
        inline entity_index_t      s_entity_index(entity_t e) { return (entity_index_t)e & ECS_ENTITY_INDEX_MASK; }

        struct ecs_t;

#define DECLARE_ECS2_GROUP(N) \
    enum                      \
    {                         \
        ECS_GROUP2_INDEX = N  \
    }
#define DECLARE_ECS2_COMPONENT(N) \
    enum                          \
    {                             \
        ECS_COMPONENT2_INDEX = N  \
    }
#define DECLARE_ECS2_TAG(N) \
    enum                    \
    {                       \
        ECS_TAG2_INDEX = N  \
    }

        // Register a Component Group with an ECS
        extern bool                g_register_cp_group(ecs_t* ecs, u32 cg_max_entities, u32 cg_index, const char* cg_name);
        extern void                g_unregister_cp_group(ecs_t* ecs, u32 cg_index);
        template <typename T> bool g_register_group(ecs_t* ecs, const char* cg_name, u32 max_entities) { return g_register_cp_group(ecs, max_entities, T::ECS_GROUP2_INDEX, cg_name); }
        template <typename T> void g_unregister_group(ecs_t* ecs) { g_unregister_cp_group(ecs, T::ECS_GROUP2_INDEX); }

        // Register a Component under a Component Group
        extern bool                            g_register_component(ecs_t* ecs, u32 cg_index, u32 cp_index, const char* cp_name, s32 cp_sizeof, s32 cp_alignof = 8);
        extern void                            g_unregister_component(ecs_t* ecs, u32 cg_index, u32 cp_index);
        template <typename G, typename T> bool g_register_component(ecs_t* ecs, const char* cp_name) { return g_register_component(ecs, G::ECS_GROUP2_INDEX, T::ECS_COMPONENT2_INDEX, cp_name, sizeof(T), alignof(T)); }
        template <typename G, typename T> void g_unregister_component(ecs_t* ecs) { g_unregister_component(ecs, G::ECS_GROUP2_INDEX, T::ECS_COMPONENT2_INDEX); }

        // Register a Tag under a Component Group
        extern bool                            g_register_tag(ecs_t* ecs, u32 cg_index, u32 tg_index, const char* tg_name);
        extern void                            g_unregister_tag(ecs_t* ecs, u32 cg_index, u32 tg_index);
        template <typename G, typename T> bool g_register_tag(ecs_t* ecs, const char* tg_name) { return g_register_tag(ecs, G::ECS_GROUP2_INDEX, T::ECS_TAG2_INDEX, tg_name); }
        template <typename G, typename T> void g_unregister_tag(ecs_t* ecs) { g_unregister_tag(ecs, G::ECS_GROUP2_INDEX, T::ECS_TAG2_INDEX); }

        extern ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities);
        extern void   g_destroy_ecs(ecs_t* ecs);

        // Create and Destroy Entity
        extern entity_t g_create_entity(ecs_t* ecs);
        extern void     g_destroy_entity(ecs_t* ecs, entity_t e);

        extern bool                       g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        template <typename T> extern bool g_has_cp(ecs_t* ecs, entity_t entity) { return g_has_cp(ecs, entity, T::ECS_COMPONENT2_INDEX); }
        extern void*                      g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        template <typename T> T*          g_add_cp(ecs_t* ecs, entity_t entity) { return (T*)g_add_cp(ecs, entity, T::ECS_COMPONENT2_INDEX); }
        extern void                       g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        template <typename T> void        g_rem_cp(ecs_t* ecs, entity_t entity) { g_rem_cp(ecs, entity, T::ECS_COMPONENT2_INDEX); }
        extern void*                      g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        template <typename T> T*          g_get_cp(ecs_t* ecs, entity_t entity) { return (T*)g_get_cp(ecs, entity, T::ECS_COMPONENT2_INDEX); }

        extern bool                g_has_tag(ecs_t* ecs, entity_t entity, u32 tg_index);
        template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity) { return g_has_tag(ecs, entity, T::ECS_TAG2_INDEX); }
        extern void                g_add_tag(ecs_t* ecs, entity_t entity, u32 tg_index);
        template <typename T> void g_add_tag(ecs_t* ecs, entity_t entity) { g_add_tag(ecs, entity, T::ECS_TAG2_INDEX); }
        extern void                g_rem_tag(ecs_t* ecs, entity_t entity, u32 tg_index);
        template <typename T> void g_rem_tag(ecs_t* ecs, entity_t entity) { g_rem_tag(ecs, entity, T::ECS_TAG2_INDEX); }

        struct en_iterator_t
        {
            ecs_t* m_ecs;              // The ECS
            u64    m_group_mask;       // The group mask, there cannot be more than a total of 64 component groups
            u32    m_group_cp_mask[7]; // An entity cannot be in more than 7 component groups
            s32    m_entity_index;     // Current entity index
            s32    m_entity_index_max; // Maximum entity index
            s8     m_num_groups;       // Number of component groups that the iterator is looking at

            en_iterator_t(ecs_t* ecs);

            void set_cp_type(u32 cp_index); // Mark the things you want to iterate on
            void set_tg_type(u32 tg_index) { set_cp_type(tg_index); }

            template <typename T> void set_cp_type() { set_cp_type(T::ECS_COMPONENT2_INDEX); }
            template <typename T> void set_tg_type() { set_tg_type(T::ECS_TAG2_INDEX); }

            // Example:
            //     en_iterator_t iter(ecs);
            //
            //     iter.set_cp_type<position_t>();
            //     iter.set_cp_type<velocity_t>();
            //     iter.set_tg_type<enemy_tag_t>();
            //
            //     iter.begin();
            //     while (!iter.end())
            //     {
            //         entity_t e = iter.entity();
            //         ...
            //         iter.next();
            //     }

            void     begin();
            entity_t entity() const;
            void     next();
            bool     end() const;
        };
    } // namespace necs2
} // namespace ncore

#endif
