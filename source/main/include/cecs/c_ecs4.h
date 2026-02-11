#ifndef __CECS_ECS4_H__
#define __CECS_ECS4_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "ccore/c_debug.h"

namespace ncore
{
    class alloc_t;

    namespace necs4
    {
        // ECS Version 4, an Entity-Component-System (ECS) implementation.

        typedef u32 entity_t; // Entity identifier {generation(8) + archetype(8) + index(16)}
        typedef u8  entity_generation_t;
        typedef u32 entity_index_t;

        const u32 ECS_ENTITY_NULL            = (0xFFFFFFFF); // Null entity
        const u32 ECS_ENTITY_INDEX_MASK      = (0x0000FFFF); // Mask to use to get the entity index from an entity identifier
        const u32 ECS_ENTITY_ARCHETYPE_MASK  = (0x00FF0000); // Mask to use to get the archetype index from an entity identifier
        const s8  ECS_ENTITY_ARCHETYPE_SHIFT = (16);         // Shift to get the archetype index
        const u32 ECS_ENTITY_GEN_ID_MASK     = (0xFF000000); // Mask to use to get the generation id from an entity identifier
        const s8  ECS_ENTITY_GEN_ID_SHIFT    = (24);         // Shift to get the generation id

        inline bool                g_entity_is_null(entity_t e) { return e == ECS_ENTITY_NULL; }
        inline entity_generation_t g_entity_generation(entity_t e) { return ((u32)e & ECS_ENTITY_GEN_ID_MASK) >> ECS_ENTITY_GEN_ID_SHIFT; }
        inline entity_index_t      g_entity_index(entity_t e) { return (entity_index_t)e & ECS_ENTITY_INDEX_MASK; }
        inline u8                  g_entity_archetype_index(entity_t e) { return (u8)((e & ECS_ENTITY_ARCHETYPE_MASK) >> ECS_ENTITY_ARCHETYPE_SHIFT); }

        struct ecs_t;
        struct archetype_t;

#define DECLARE_ECS4_COMPONENT(N) \
    enum                          \
    {                             \
        ECS4_COMPONENT_INDEX = N  \
    }
#define DECLARE_ECS4_TAG(N) \
    enum                    \
    {                       \
        ECS4_TAG_INDEX = N  \
    }

        // Create and Destroy ECS
        ecs_t* g_create_ecs(u16 max_archetypes = 32);
        void   g_destroy_ecs(ecs_t* ecs);

        // Archetype registration
        void g_register_archetype(ecs_t* ecs, u16 archetype_index, u16 components_per_entity = 16, u16 max_archetype_component_types = 32, u16 max_global_component_types = 256, u16 max_archetype_tag_types = 8, u16 max_global_tag_types = 256);

        // Create and Destroy Entity
        entity_t g_create_entity(ecs_t* ecs, u16 archetype_index = 0);
        void     g_destroy_entity(ecs_t* ecs, entity_t e);

        // Components
        void                       g_register_component_type(ecs_t* ecs, u16 archetype_index, u16 cp_index, u32 cp_sizeof);
        template <typename T> void g_register_component_type(ecs_t* ecs, u16 archetype_index) { g_register_component_type(ecs, archetype_index, T::ECS4_COMPONENT_INDEX, sizeof(T)); }

        void                       g_register_tag_type(ecs_t* ecs, u16 archetype_index, u16 tg_index);
        template <typename T> void g_register_tag_type(ecs_t* ecs, u16 archetype_index) { g_register_tag_type(ecs, archetype_index, T::ECS4_TAG_INDEX); }

        bool  g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void  g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index);

        template <typename T> bool g_has_cp(ecs_t* ecs, entity_t entity) { return g_has_cp(ecs, entity, T::ECS4_COMPONENT_INDEX); }
        template <typename T> T*   g_add_cp(ecs_t* ecs, entity_t entity) { return (T*)g_add_cp(ecs, entity, T::ECS4_COMPONENT_INDEX); }
        template <typename T> void g_rem_cp(ecs_t* ecs, entity_t entity) { g_rem_cp(ecs, entity, T::ECS4_COMPONENT_INDEX); }
        template <typename T> T*   g_get_cp(ecs_t* ecs, entity_t entity) { return (T*)g_get_cp(ecs, entity, T::ECS4_COMPONENT_INDEX); }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index);
        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index);
        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index);

        template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity) { return g_has_tag(ecs, entity, (u16)T::ECS4_TAG_INDEX); }
        template <typename T> void g_add_tag(ecs_t* ecs, entity_t entity) { g_add_tag(ecs, entity, (u16)T::ECS4_TAG_INDEX); }
        template <typename T> void g_rem_tag(ecs_t* ecs, entity_t entity) { g_rem_tag(ecs, entity, (u16)T::ECS4_TAG_INDEX); }

        // Iterator
        struct en_iterator_t
        {
            en_iterator_t(ecs_t* ecs);
            en_iterator_t(ecs_t* ecs, entity_t blueprint_entity);

            // Example:
            //     entity_t blueprint = g_create_entity(ecs);
            //     g_add_cp<position_t>(ecs, blueprint);
            //     g_add_cp<velocity_t>(ecs, blueprint);
            //     g_add_tag<enemy_tag_t>(ecs, blueprint);
            //
            //     en_iterator_t iter(ecs, blueprint);
            //
            //     iter.begin();
            //     while (!iter.end())
            //     {
            //         entity_t e = iter.entity();
            //         ...
            //         iter.next();
            //     }
            //
            //     g_destroy_entity(ecs, blueprint);
            //

            void        begin() { m_entity_index = find(0); }
            inline void next() { m_entity_index = m_entity_index >= 0 ? find(m_entity_index + 1) : -1; }
            inline bool end() const { return m_entity_index < 0; }
            entity_t    entity() const;

        private:
            s32 find(s32 entity_index) const;

            ecs_t*       m_ecs;              // The ECS
            archetype_t* m_archetype;        // The current archetype
            entity_t     m_entity_reference; // Current entity reference
            s32          m_entity_index;     // Current entity index
            u8           m_archetype_index;  // The current archetype index
        };
    } // namespace necs4
} // namespace ncore

#endif
