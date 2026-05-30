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
        // Entity identifier {generation(8) + archetype(8) + index(16)}

        typedef u32 entity_t;

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
        ecs_t* g_create_ecs(u8 max_archetypes = 128);
        void   g_destroy_ecs(ecs_t* ecs);

        // Archetype
        // Note: An archetype can have no more than 64 components, and each entity in that archetype can only have one or more
        //       of those components. This is because we use a 64-bit integer to track component occupancy for each entity in
        //       the archetype, so we can only track up to 64 components per archetype.
        // Note: At the global level you can have a maximum of 256 tags, and the number of tags for each entity in an archetype
        //       can be 8, 16 or 32.
        void g_register_archetype(ecs_t* ecs, u8 archetype_index, u8 max_cps_per_entity = 16, u16 max_global_cp_types = 256, u8 max_tags_per_entity = 8, u16 max_global_tag_types = 32);

        // Create and Destroy Entity
        entity_t g_create_entity(ecs_t* ecs, u8 archetype_index = 0);
        void     g_destroy_entity(ecs_t* ecs, entity_t e);

        // Components
        void                       g_register_component_type(ecs_t* ecs, u8 archetype_index, u16 cp_index, u32 cp_sizeof);
        template <typename T> void g_register_component_type(ecs_t* ecs, u8 archetype_index) { g_register_component_type(ecs, archetype_index, T::ECS4_COMPONENT_INDEX, sizeof(T)); }

        void                       g_register_tag_type(ecs_t* ecs, u8 archetype_index, u16 tg_index);
        template <typename T> void g_register_tag_type(ecs_t* ecs, u8 archetype_index) { g_register_tag_type(ecs, archetype_index, T::ECS4_TAG_INDEX); }

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

        // Iterator (will only iterate over entities in the archetype of the blueprint entity)
        struct en_iterator_t
        {
            en_iterator_t(ecs_t* ecs, u8 archetype_index);

            void mark_cp(u32 cp_index);
            void mark_tag(u16 tg_index);

            template <typename T> void mark_cp() { mark_cp(T::ECS4_COMPONENT_INDEX); }
            template <typename T> void mark_tag() { mark_tag(T::ECS4_TAG_INDEX); }

            // Example:
            //     u8 archetype_index = 0;
            //     en_iterator_t iter(ecs, archetype_index);
            //
            //     iter.mark_cp<position_t>();
            //     iter.mark_cp<velocity_t>();
            //     iter.mark_tag<enemy_tag_t>();
            //
            //     iter.begin();
            //     while (!iter.end())
            //     {
            //         entity_t e = iter.entity();
            //         ...
            //         iter.next();
            //     }
            //

            void        begin();
            inline void next() { m_entity_index = m_entity_index >= 0 ? find(m_entity_index + 1) : -1; }
            inline bool end() const { return m_entity_index < 0; }
            entity_t    entity() const;

        private:
            s32 find(s32 entity_index) const;

            archetype_t* m_archetype;         //
            u8           m_archetype_index;   //
            u64          m_ref_cp_occupancy;  //
            u32          m_ref_tag_occupancy; //
            i32          m_entity_index;      // Current entity index
        };
    } // namespace necs4
} // namespace ncore

#endif
