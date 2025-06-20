#ifndef __CECS_ECS3_H__
#define __CECS_ECS3_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "ccore/c_debug.h"

namespace ncore
{
    class alloc_t;

    namespace necs3
    {
        // ECS Version 3, a simple Entity-Component-System (ECS) implementation.

        typedef u32 entity_t;
        typedef u8  entity_generation_t;
        typedef u32 entity_index_t;

        const u32 ECS_ENTITY_NULL        = (0xFFFFFFFF); // Null entity
        const u32 ECS_ENTITY_INDEX_MASK  = (0x00FFFFFF); // Mask to use to get the entity index from an entity identifier
        const u32 ECS_ENTITY_GEN_ID_MASK = (0xFF000000); // Mask to use to get the generation id from an entity identifier
        const s8  ECS_ENTITY_GEN_SHIFT   = (24);         // Extent of the entity id + type within an identifier

        inline bool                g_entity_is_null(entity_t e) { return e == ECS_ENTITY_NULL; }
        inline entity_generation_t g_entity_generation(entity_t e) { return ((u32)e & ECS_ENTITY_GEN_ID_MASK) >> ECS_ENTITY_GEN_SHIFT; }
        inline entity_index_t      g_entity_index(entity_t e) { return (entity_index_t)e & ECS_ENTITY_INDEX_MASK; }

        struct ecs_t;

#define DECLARE_ECS3_COMPONENT(N) \
    enum                          \
    {                             \
        ECS3_COMPONENT_INDEX = N  \
    }
#define DECLARE_ECS3_TAG(N) \
    enum                    \
    {                       \
        ECS3_TAG_INDEX = N  \
    }

        // Create and Destroy ECS
        ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities, u32 max_components, u32 max_tags);
        void   g_destroy_ecs(ecs_t* ecs);

        // Create and Destroy Entity
        entity_t g_create_entity(ecs_t* ecs);
        void     g_destroy_entity(ecs_t* ecs, entity_t e);

        // Components
        bool                       g_register_component(ecs_t* ecs, u32 max_components, u32 cp_index, s32 cp_sizeof, s32 cp_alignof = 8, const char* cp_name = "");
        void                       g_unregister_component(ecs_t* ecs, u32 cp_index);
        template <typename T> bool g_register_component(ecs_t* ecs, u32 max_components, const char* cp_name="") { return g_register_component(ecs, max_components, T::ECS3_COMPONENT_INDEX, sizeof(T), alignof(T), cp_name); }
        template <typename T> void g_unregister_component(ecs_t* ecs) { g_unregister_component(ecs, T::ECS3_COMPONENT_INDEX); }

        bool  g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void  g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index);
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index);

        template <typename T> bool g_has_cp(ecs_t* ecs, entity_t entity) { return g_has_cp(ecs, entity, T::ECS3_COMPONENT_INDEX); }
        template <typename T> T*   g_add_cp(ecs_t* ecs, entity_t entity) { return (T*)g_add_cp(ecs, entity, T::ECS3_COMPONENT_INDEX); }
        template <typename T> void g_rem_cp(ecs_t* ecs, entity_t entity) { g_rem_cp(ecs, entity, T::ECS3_COMPONENT_INDEX); }
        template <typename T> T*   g_get_cp(ecs_t* ecs, entity_t entity) { return (T*)g_get_cp(ecs, entity, T::ECS3_COMPONENT_INDEX); }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, s16 tg_index);
        void g_add_tag(ecs_t* ecs, entity_t entity, s16 tg_index);
        void g_rem_tag(ecs_t* ecs, entity_t entity, s16 tg_index);

        template <typename T> bool g_has_tag(ecs_t* ecs, entity_t entity)
        {
            ASSERT(T::ECS3_TAG_INDEX < 1024);
            return g_has_tag(ecs, entity, (s16)T::ECS3_TAG_INDEX);
        }
        template <typename T> void g_add_tag(ecs_t* ecs, entity_t entity)
        {
            ASSERT(T::ECS3_TAG_INDEX < 1024);
            g_add_tag(ecs, entity, (s16)T::ECS3_TAG_INDEX);
        }
        template <typename T> void g_rem_tag(ecs_t* ecs, entity_t entity)
        {
            ASSERT(T::ECS3_TAG_INDEX < 1024);
            g_rem_tag(ecs, entity, (s16)T::ECS3_TAG_INDEX);
        }

        // Iterator
        struct en_iterator_t
        {
            en_iterator_t(ecs_t* ecs);
            en_iterator_t(ecs_t* ecs, entity_t entity_reference);

            // Example:
            //     entity_t entity_reference = g_create_entity(ecs);
            //     g_add_cp<position_t>(ecs, entity_reference);
            //     g_add_cp<velocity_t>(ecs, entity_reference);
            //     g_add_tag<enemy_tag_t>(ecs, entity_reference);
            //
            //     en_iterator_t iter(ecs, entity_reference);
            //
            //     iter.begin();
            //     while (!iter.end())
            //     {
            //         entity_t e = iter.entity();
            //         ...
            //         iter.next();
            //     }
            //
            //     g_destroy_entity(ecs, entity_reference);
            //

            void        begin() { m_entity_index = find(0); }
            inline void next() { m_entity_index = m_entity_index >= 0 ? find(m_entity_index + 1) : -1; }
            inline bool end() const { return m_entity_index < 0; }
            entity_t    entity() const;

        private:
            s32 find(s32 entity_index) const;

            ecs_t* m_ecs;              // The ECS
            s32    m_entity_reference; // The entity reference that should be searched for
            s32    m_entity_index;     // Current entity index
        };
    } // namespace necs3
} // namespace ncore

#endif
