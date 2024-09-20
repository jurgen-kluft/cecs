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
        typedef u32           entity_t;
        extern const entity_t g_null_entity;

        struct ecs_t;
        struct cp_type_t;
        struct tg_type_t;
        struct cg_type_t;

        template <typename T> struct cp_typeinfo_t
        {
            static ecs_t*            cp_ecs;   // Pointer to the ecs where this group is registered
            static const char* const cp_name;  // Name of this component
            static cg_type_t*        cp_group; // Pointer to the component group where this component is registered
            static cp_type_t*        cp_type;  // Pointer to component type used internally
        };

        template <typename T> struct tg_typeinfo_t
        {
            static ecs_t*            tg_ecs;   // Pointer to the ecs where this group is registered
            static const char* const tg_name;  // Name of this tag
            static cg_type_t*        tg_group; // Pointer to the component group where this tag is registered
            static tg_type_t*        tg_type;  // Pointer to tag type used internally
        };

        template <typename T> struct cg_typeinfo_t
        {
            static ecs_t*            cg_ecs;  // Pointer to the ecs where this group is registered
            static const char* const cg_name; // Name of this component group
            static cg_type_t*        cg_type; // Pointer to component group used internally
        };

        // Note: Once a type is used to register it at an ECS, it cannot be used to register at another ECS.

#define REGISTER_ECS_GROUP(T) static cg_typeinfo_t<T> s_cg_typeinfo_##T
#define DECLARE_ECS_GROUP(T)                                           \
    template <> ecs_t*            cg_typeinfo_t<T>::cg_ecs  = nullptr; \
    template <> const char* const cg_typeinfo_t<T>::cg_name = #T;      \
    template <> cg_type_t*        cg_typeinfo_t<T>::cg_type = nullptr

#define REGISTER_ECS_COMPONENT(T) static cp_typeinfo_t<T> s_cp_typeinfo_##T
#define DECLARE_ECS_COMPONENT(T)                                        \
    template <> ecs_t*            cp_typeinfo_t<T>::cp_ecs   = nullptr; \
    template <> const char* const cp_typeinfo_t<T>::cp_name  = #T;      \
    template <> cg_type_t*        cp_typeinfo_t<T>::cp_group = nullptr; \
    template <> cp_type_t*        cp_typeinfo_t<T>::cp_type  = nullptr

#define DECLARE_ECS_COMPONENT_NAMED(T, custom_name)                         \
    template <> ecs_t*            cp_typeinfo_t<T>::cp_ecs   = nullptr;     \
    template <> const char* const cp_typeinfo_t<T>::cp_name  = custom_name; \
    template <> cg_type_t*        cp_typeinfo_t<T>::cp_group = nullptr;     \
    template <> cp_type_t*        cp_typeinfo_t<T>::cp_type  = nullptr

#define REGISTER_ECS_TAG(T) static tg_typeinfo_t<T> s_tg_typeinfo_##T
#define DECLARE_ECS_TAG(T)                                              \
    template <> ecs_t*            tg_typeinfo_t<T>::tg_ecs   = nullptr; \
    template <> const char* const tg_typeinfo_t<T>::tg_name  = #T;      \
    template <> cg_type_t*        tg_typeinfo_t<T>::tg_group = nullptr; \
    template <> tg_type_t*        tg_typeinfo_t<T>::tg_type  = nullptr

#define DECLARE_ECS_TAG_NAMED(T, custom_name)                               \
    template <> ecs_t*            tg_typeinfo_t<T>::tg_ecs   = nullptr;     \
    template <> const char* const tg_typeinfo_t<T>::tg_name  = custom_name; \
    template <> cg_type_t*        tg_typeinfo_t<T>::tg_group = nullptr;     \
    template <> tg_type_t*        tg_typeinfo_t<T>::tg_type  = nullptr

        // Register a Component Group with an ECS
        extern cg_type_t*          g_register_cp_group(ecs_t* ecs, u32 cg_max_entities, const char* cg_name);
        template <typename T> void g_register_cp_group(ecs_t* ecs, u32 max_entities)
        {
            ASSERTS(cg_typeinfo_t<T>::cg_ecs == nullptr, "Component group already registered");
            cg_typeinfo_t<T>::cg_ecs = ecs;
            cg_typeinfo_t<T>::cg_type = g_register_cp_group(ecs, max_entities, cg_typeinfo_t<T>::cg_name);
        }

        // Register a Component under a Component Group
        extern cp_type_t*                      g_register_cp_type(ecs_t* ecs, cg_type_t* cp_group, const char* cp_name, s32 cp_sizeof, s32 cp_alignof = 8);
        template <typename G, typename T> void g_register_component()
        {
            ASSERTS(cp_typeinfo_t<T>::cp_ecs == nullptr, "Component already registered");
            cg_type_t* cg_type         = cg_typeinfo_t<G>::cg_type;
            cp_typeinfo_t<T>::cp_ecs   = cg_typeinfo_t<G>::cg_ecs;
            cp_typeinfo_t<T>::cp_group = cg_type;
            cp_typeinfo_t<T>::cp_type  = g_register_cp_type(cg_typeinfo_t<G>::cg_ecs, cg_type, cp_typeinfo_t<T>::cp_name, sizeof(T), alignof(T));
        }

        // Register a Tag under a Component Group
        extern tg_type_t*                      g_register_tg_type(ecs_t* ecs, cg_type_t* cp_group, const char* tg_name);
        template <typename G, typename T> void g_register_tag()
        {
            ASSERTS(tg_typeinfo_t<T>::tg_ecs == nullptr, "Tag already registered");
            cg_type_t* cg_type         = cg_typeinfo_t<G>::cg_type;
            tg_typeinfo_t<T>::tg_ecs   = cg_typeinfo_t<G>::cg_ecs;
            tg_typeinfo_t<T>::tg_group = cg_type;
            tg_typeinfo_t<T>::tg_type  = g_register_tg_type(cg_typeinfo_t<G>::cg_ecs, cg_type, tg_typeinfo_t<T>::tg_name);
        }

        extern ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities);
        extern void   g_destroy_ecs(ecs_t* ecs);

        // Create and Destroy Entity
        extern entity_t g_create_entity(ecs_t* ecs);
        extern void     g_destroy_entity(ecs_t* ecs, entity_t e);

        extern bool                       g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> extern bool g_has_cp(entity_t entity)
        {
            ASSERTS(cp_typeinfo_t<T>::cp_ecs != nullptr, "Component has not been registered");
            ecs_t*     ecs     = cp_typeinfo_t<T>::cp_ecs;
            cp_type_t* cp_type = cp_typeinfo_t<T>::cp_type;
            return g_has_cp(ecs, entity, cp_type);
        }
        extern void                g_add_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> void g_add_cp(entity_t entity)
        {
            ASSERTS(cp_typeinfo_t<T>::cp_ecs != nullptr, "Component has not been registered");
            ecs_t*     ecs     = cp_typeinfo_t<T>::cp_ecs;
            cp_type_t* cp_type = cp_typeinfo_t<T>::cp_type;
            g_add_cp(ecs, entity, cp_type);
        }
        extern void                g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> void g_rem_cp(entity_t entity)
        {
            ASSERTS(cp_typeinfo_t<T>::cp_ecs != nullptr, "Component has not been registered");
            ecs_t*     ecs     = cp_typeinfo_t<T>::cp_ecs;
            cp_type_t* cp_type = cp_typeinfo_t<T>::cp_type;
            g_rem_cp(ecs, entity, cp_type);
        }
        extern void*             g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type);
        template <typename T> T* g_get_cp(entity_t entity)
        {
            ASSERTS(cp_typeinfo_t<T>::cp_ecs != nullptr, "Component has not been registered");
            ecs_t*     ecs     = cp_typeinfo_t<T>::cp_ecs;
            cg_type_t* cg_type = cp_typeinfo_t<T>::cp_group;
            cp_type_t* cp_type = cp_typeinfo_t<T>::cp_type;
            return (T*)g_get_cp(ecs, entity, cp_type);
        }

        extern bool                g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type);
        template <typename T> bool g_has_tag(entity_t entity)
        {
            ASSERTS(tg_typeinfo_t<T>::tg_ecs != nullptr, "Tag has not been registered");
            ecs_t*     ecs     = tg_typeinfo_t<T>::tg_ecs;
            tg_type_t* tg_type = tg_typeinfo_t<T>::tg_type;
            return g_has_tag(ecs, entity, tg_type);
        }
        extern void                g_add_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type);
        template <typename T> void g_add_tag(entity_t entity)
        {
            ASSERTS(tg_typeinfo_t<T>::tg_ecs != nullptr, "Tag has not been registered");
            ecs_t*     ecs     = tg_typeinfo_t<T>::tg_ecs;
            tg_type_t* tg_type = tg_typeinfo_t<T>::tg_type;
            g_add_tag(ecs, entity, tg_type);
        }
        extern void                g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type);
        template <typename T> void g_rem_tag(entity_t entity)
        {
            ASSERTS(tg_typeinfo_t<T>::tg_ecs != nullptr, "Tag has not been registered");
            ecs_t*     ecs     = tg_typeinfo_t<T>::tg_ecs;
            tg_type_t* tg_type = tg_typeinfo_t<T>::tg_type;
            g_rem_tag(ecs, entity, tg_type);
        }

        struct en_iterator_t // 56 bytes
        {
            ecs_t* m_ecs;              // The ECS
            u64    m_group_mask;       // The group mask, there cannot be more than a total of 64 component groups
            u32    m_group_cp_mask[7]; // An entity cannot be in more than 7 component groups
            s32    m_entity_index;     // Current entity index
            s32    m_entity_index_max; // Maximum entity index
            s8     m_num_groups;       // Number of component groups that the iterator is looking at

            en_iterator_t(ecs_t* ecs);

            void set_cp_type(cp_type_t* cp_type); // Mark the things you want to iterate on
            void set_tg_type(tg_type_t* tg_type) { set_cp_type((cp_type_t*)tg_type); }

            template <typename T> void set_cp_type()
            {
                cp_type_t* cp_type = cp_typeinfo_t<T>::cp_type;
                set_cp_type(cp_type);
            }
            template <typename T> void set_tg_type()
            {
                tg_type_t* tg_type = tg_typeinfo_t<T>::tg_type;
                set_tg_type(tg_type);
            }

            // Example:
            //     en_iterator_t iter;
            //     iter.init(ecs);
            //
            //     iter.set_cp_type(position_cp_type);
            //     iter.set_cp_type(velocity_cp_type);
            //     iter.set_tg_type(enemy_tag);
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
