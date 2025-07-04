#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_debug.h"
#include "cbase/c_duomap.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs2.h"

namespace ncore
{
    namespace necs2
    {
        enum
        {
            ECS_MAX_GROUPS               = 64,
            ECS_MAX_COMPONENTS_PER_GROUP = 32,
        };

        static_assert(ECS_MAX_GROUPS <= 64, "ECS_MAX_GROUPS must be less than or equal to 64");

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        // Component Type, component_type_t and tag_type_t
        struct component_type_t
        {
            const char* cp_name;           // Name of the component
            s32         cp_sizeof;         // Size of the component in bits
            s16         cp_alignof;        // Alignment requirement of the component
            s8          cp_group_index;    // The group index
            s8          cp_group_cp_index; // The component index in the group
        };
        typedef component_type_t tag_type_t;

        // Component Group managing a maximum of ECS_MAX_COMPONENTS_PER_GROUP components
        struct component_group_t
        {
            const char* m_name; // The name of the component group
            binmap_t    m_en_binmap;
            u32         m_max_entities;                               // Maximum number of entities in this group
            u32         m_cp_used;                                    // Each bit represents if for this group the component is free or used
            byte*       m_a_en_cp_data[ECS_MAX_COMPONENTS_PER_GROUP]; // The component data array per component type
            alloc_t*    m_allocator;                                  // The allocator
            s8          m_group_index;
        };

        static void s_cp_group_init(component_group_t* group)
        {
            group->m_en_binmap.reset();
            group->m_max_entities = 0;
            group->m_cp_used      = 0;
            for (u32 i = 0; i < ECS_MAX_COMPONENTS_PER_GROUP; ++i)
                group->m_a_en_cp_data[i] = nullptr;
            group->m_allocator   = nullptr;
            group->m_group_index = -1;
        }

        static void s_cp_group_construct(component_group_t* group, s8 index, u32 max_entities, alloc_t* allocator)
        {
            if (max_entities > 0)
            {
                group->m_allocator     = allocator;
                group->m_group_index   = index;
                group->m_max_entities  = max_entities;
                binmap_t::config_t cfg = binmap_t::config_t::compute(max_entities);
                group->m_en_binmap.init_all_free(cfg, allocator);
            }
        }
        static void s_cp_group_destruct(component_group_t* group)
        {
            alloc_t* allocator = group->m_allocator;
            u32      cp_used   = group->m_cp_used;
            while (cp_used != 0)
            {
                s32 index = math::g_findFirstBit(cp_used);
                if (group->m_a_en_cp_data[index] != nullptr)
                    allocator->deallocate(group->m_a_en_cp_data[index]);
                cp_used &= ~(1 << index);
            }
            group->m_en_binmap.release(allocator);
            s_cp_group_init(group);
        }

        static s8 s_cp_group_register_cp(component_group_t* group, component_type_t* cp_type)
        {
            s8 const cp_id = math::g_findFirstBit(~group->m_cp_used);
            if (cp_id >= 0 && cp_id < ECS_MAX_COMPONENTS_PER_GROUP)
            {
                group->m_cp_used |= (1 << cp_id);

                // Only create entity-component data array for components and *not* for tags
                if (cp_type->cp_sizeof > 0)
                {
                    group->m_a_en_cp_data[cp_id] = g_allocate_array<byte>(group->m_allocator, group->m_max_entities * cp_type->cp_sizeof);
                }
                return (s8)cp_id;
            }
            return -1;
        }

        static void s_cp_group_unregister_cp(component_group_t* group, s8 cp_index)
        {
            ASSERT(cp_index >= 0 && cp_index < ECS_MAX_COMPONENTS_PER_GROUP);
            group->m_cp_used &= ~(1 << cp_index);
            if (group->m_a_en_cp_data[cp_index] != nullptr)
            {
                group->m_allocator->deallocate(group->m_a_en_cp_data[cp_index]);
                group->m_a_en_cp_data[cp_index] = nullptr;
            }
        }

        // Component Type Manager
        struct component_type_mgr_t
        {
            binmap_t          m_cp_binmap; //
            component_type_t* m_a_cp_type; // 8 bytes; The type information attached to each store
        };

        // Component Group Manager
        // Note that we can have a maximum of ECS_MAX_GROUPS component groups, each component group
        // can have a maximum of ECS_MAX_COMPONENTS_PER_GROUP components.
        // So in theory we can handle a total of ECS_MAX_GROUPS * ECS_MAX_COMPONENTS_PER_GROUP.
        struct component_group_mgr_t
        {
            u32                m_cp_groups_max;  // Maximum number of component groups
            u64                m_cp_groups_used; // Bits indicate which groups are used/free
            alloc_t*           m_allocator;      // The allocator
            component_group_t* m_cp_groups;      // The array of component groups
        };

        // Entity data structure (64 bytes)
        // Note: The setup here limits an entity to 7 component groups (so a theoretical maximum of 224 components per entity)
        // Note: You can easily bumps this up, of course with here and there some code changes
        struct entity_instance_t
        {
            u64 m_cp_groups;            // Global maximum of ECS_MAX_GROUPS component groups, each bit represents a group index (bit 0 is group 0, bit 3 is group 3 etc..)
            u32 m_cp_group_cp_used[7];  // Each bit index represents the 'component index' in that component group
            u32 m_cp_group_en_index[7]; // The index of the entity in the component group (max 7 component groups)
        };

        static inline void s_init(entity_instance_t* entity)
        {
            entity->m_cp_groups = 0;
            for (u32 i = 0; i < 7; ++i)
            {
                entity->m_cp_group_cp_used[i]  = 0;
                entity->m_cp_group_en_index[i] = 0;
            }
        }

        struct entity_mgr_t
        {
            duomap_t             m_entity_state; // Which entities are alive/dead
            entity_generation_t* m_a_entity_ver; // The generation Id of each entity
            entity_instance_t*   m_a_entity;     // The array of entities entries
        };

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            alloc_t*              m_allocator;    // The allocator
            component_type_mgr_t  m_cp_type_mgr;  // The component type manager
            component_group_mgr_t m_cp_group_mgr; // The component group manager
            entity_mgr_t          m_entity_mgr;   // The entity manager
        };

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // component type, component type manager

        static void s_init(component_type_mgr_t* cps, u32 max_components, alloc_t* allocator)
        {
            cps->m_cp_binmap.reset();
            binmap_t::config_t cfg = binmap_t::config_t::compute(max_components);
            cps->m_cp_binmap.init_all_free(cfg, allocator);
            cps->m_a_cp_type = g_allocate_array_and_clear<component_type_t>(allocator, max_components);
        }

        static void s_exit(component_type_mgr_t* cps, alloc_t* allocator)
        {
            allocator->deallocate(cps->m_a_cp_type);
            cps->m_cp_binmap.release(allocator);
            cps->m_a_cp_type = nullptr;
        }

        static bool s_register_cp_type(component_type_mgr_t* cps, component_group_mgr_t* cp_group_mgr, u32 cg_index, u32 cp_index, const char* cp_name, s32 cp_sizeof, s32 cp_alignof)
        {
            if (cp_index >= ECS_MAX_COMPONENTS_PER_GROUP)
                return false;

            if (cps->m_cp_binmap.is_free(cp_index))
            {
                component_type_t* cp_type = (component_type_t*)&cps->m_a_cp_type[cp_index];
                cp_type->cp_name          = cp_name;
                cp_type->cp_sizeof        = cp_sizeof;
                cp_type->cp_alignof       = cp_alignof;
                cp_type->cp_group_index   = cg_index;
                cps->m_cp_binmap.set_used(cp_index);

                // Register a component in the component group
                cp_type->cp_group_cp_index = s_cp_group_register_cp(&cp_group_mgr->m_cp_groups[cg_index], cp_type);
                return true;
            }
            return false;
        }

        static void s_unregister_cp_type(component_type_mgr_t* cps, component_group_mgr_t* cp_group_mgr, u32 cg_index, u32 cp_index)
        {
            if (cps->m_cp_binmap.is_used(cp_index))
            {
                component_group_t* group = &cp_group_mgr->m_cp_groups[cg_index];
                s_cp_group_unregister_cp(group, cg_index);
                cps->m_cp_binmap.set_free(cp_index);
            }
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // tag type, tag type manager (tags are basically components, but they have no backing component data)

        static bool s_register_tag_type(component_type_mgr_t* ts, component_group_mgr_t* cp_group_mgr, u32 cg_index, u32 tg_index, const char* tg_name) { return (tag_type_t*)s_register_cp_type(ts, cp_group_mgr, cg_index, tg_index, tg_name, 0, 0); }
        static void s_unregister_tg_type(component_type_mgr_t* cps, component_group_mgr_t* cp_group_mgr, u32 cg_index, u32 tg_index) { s_unregister_cp_type(cps, cp_group_mgr, cg_index, tg_index); }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // component group manager, create and destroy

        static void s_init(component_group_mgr_t* cp_group_mgr, u32 max_groups, alloc_t* allocator)
        {
            ASSERT(max_groups > 0 && max_groups <= ECS_MAX_GROUPS);
            cp_group_mgr->m_allocator      = allocator;
            cp_group_mgr->m_cp_groups_max  = max_groups;
            cp_group_mgr->m_cp_groups      = g_allocate_array<component_group_t>(allocator, max_groups);
            cp_group_mgr->m_cp_groups_used = 0;
            for (u32 i = 0; i < max_groups; ++i)
                s_cp_group_init(&cp_group_mgr->m_cp_groups[i]);
        }

        static void s_exit(component_group_mgr_t* cp_group_mgr, alloc_t* allocator)
        {
            u64 groups_used = cp_group_mgr->m_cp_groups_used;
            while (groups_used != 0)
            {
                s32 index = math::g_findFirstBit(groups_used);
                groups_used &= ~(1 << index);
                s_cp_group_destruct(&cp_group_mgr->m_cp_groups[index]);
            }

            allocator->deallocate(cp_group_mgr->m_cp_groups);

            cp_group_mgr->m_cp_groups_max  = 0;
            cp_group_mgr->m_cp_groups      = nullptr;
            cp_group_mgr->m_cp_groups_used = 0;
        }

        static bool s_register_group(component_group_mgr_t* cp_group_mgr, u32 max_entities, u32 cg_index, const char* cg_name)
        {
            if (cg_index >= ECS_MAX_GROUPS)
                return false;

            // Find a '0' bit in the group used array
            if (cp_group_mgr->m_cp_groups_used & ((u64)1 << cg_index))
                return false;

            cp_group_mgr->m_cp_groups_used |= ((u64)1 << cg_index);
            component_group_t* group = &cp_group_mgr->m_cp_groups[cg_index];
            s_cp_group_construct(group, cg_index, max_entities, cp_group_mgr->m_allocator);
            group->m_name = cg_name;

            return true;
        }

        static void s_unregister_group(component_group_mgr_t* cp_group_mgr, u32 cg_index)
        {
            if (cg_index >= 0 && cg_index < ECS_MAX_GROUPS)
            {
                component_group_t* group = &cp_group_mgr->m_cp_groups[cg_index];
                if (cp_group_mgr->m_cp_groups_used & ((u64)1 << cg_index))
                {
                    cp_group_mgr->m_cp_groups_used &= ~((u64)1 << cg_index);
                    s_cp_group_destruct(group);
                }
            }
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // entity manager

        static void s_init(entity_mgr_t* entity_mgr, u32 max_entities, alloc_t* allocator)
        {
            entity_mgr->m_entity_state.reset();

            binmap_t::config_t cfg = binmap_t::config_t::compute(max_entities);
            entity_mgr->m_entity_state.init_all_free(cfg, allocator);
            entity_mgr->m_a_entity_ver = g_allocate_array<entity_generation_t>(allocator, max_entities);
            entity_mgr->m_a_entity     = g_allocate_array<entity_instance_t>(allocator, max_entities);
        }

        static bool s_create_entity(entity_mgr_t* entity_mgr, entity_index_t& index)
        {
            index = entity_mgr->m_entity_state.find_free();
            if (index >= 0)
            {
                entity_mgr->m_entity_state.set_used(index);
                return true;
            }
            return false;
        }

        static void s_destroy_entity(entity_mgr_t* entity_mgr, entity_index_t index) { entity_mgr->m_entity_state.set_free(index); }

        static void s_exit(entity_mgr_t* entity_mgr, alloc_t* allocator)
        {
            allocator->deallocate(entity_mgr->m_a_entity);
            allocator->deallocate(entity_mgr->m_a_entity_ver);
            entity_mgr->m_a_entity     = nullptr;
            entity_mgr->m_a_entity_ver = nullptr;
            entity_mgr->m_entity_state.release(allocator);
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // entity component system, create and destroy

        ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities)
        {
            ecs_t* ecs       = g_construct<ecs_t>(allocator);
            ecs->m_allocator = allocator;
            s_init(&ecs->m_cp_type_mgr, ECS_MAX_GROUPS, allocator);
            s_init(&ecs->m_cp_group_mgr, ECS_MAX_GROUPS, allocator);
            s_init(&ecs->m_entity_mgr, max_entities, allocator);
            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            alloc_t* allocator = ecs->m_allocator;

            s_exit(&ecs->m_entity_mgr, allocator);
            s_exit(&ecs->m_cp_group_mgr, allocator);
            s_exit(&ecs->m_cp_type_mgr, allocator);

            allocator->deallocate(ecs);
        }

        bool g_register_cp_group(ecs_t* ecs, u32 max_entities, u32 cg_index, const char* cg_name) { return s_register_group(&ecs->m_cp_group_mgr, max_entities, cg_index, cg_name); }
        void g_unregister_cp_group(ecs_t* ecs, u32 cg_index) { return s_unregister_group(&ecs->m_cp_group_mgr, cg_index); }

        bool g_register_component(ecs_t* r, u32 cg_index, u32 cp_index, const char* cp_name, s32 cp_sizeof, s32 cp_alignof) { return s_register_cp_type(&r->m_cp_type_mgr, &r->m_cp_group_mgr, cg_index, cp_index, cp_name, cp_sizeof, cp_alignof); }
        void g_unregister_component(ecs_t* r, u32 cg_index, u32 cp_index) { return s_unregister_cp_type(&r->m_cp_type_mgr, &r->m_cp_group_mgr, cg_index, cp_index); }

        bool g_register_tag(ecs_t* r, u32 cg_index, u32 tg_index, const char* tg_name) { return s_register_tag_type(&r->m_cp_type_mgr, &r->m_cp_group_mgr, cg_index, tg_index, tg_name); }
        void g_unregister_tag(ecs_t* r, u32 cg_index, u32 tg_index) { return s_unregister_tg_type(&r->m_cp_type_mgr, &r->m_cp_group_mgr, cg_index, tg_index); }

        // --------------------------------------------------------------------------------------------------------
        // entity functionality

        static bool s_entity_has_component(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            component_type_t const* cp_type           = &ecs->m_cp_type_mgr.m_a_cp_type[cp_index];
            s8 const                cp_group_index    = cp_type->cp_group_index;
            s8 const                cp_group_cp_index = cp_type->cp_group_cp_index;

            ASSERT(cp_group_index >= 0 && cp_group_index < ECS_MAX_GROUPS);
            ASSERT(cp_group_cp_index >= 0 && cp_group_cp_index < ECS_MAX_COMPONENTS_PER_GROUP);

            entity_instance_t const& entity_instance = ecs->m_entity_mgr.m_a_entity[s_entity_index(entity)];
            u32 const                cp_group_bit    = (1 << cp_group_index);
            if ((entity_instance.m_cp_groups & cp_group_bit) == cp_group_bit)
            {
                // How many '1' bits are there before 'cp_group_bit' in 'm_cp_groups'
                u32 const gi      = math::g_countBits(entity_instance.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity_instance.m_cp_group_cp_used[gi];
                return (cp_used & (1 << cp_group_cp_index)) != 0;
            }
            return false;
        }

        static byte* s_entity_get_component(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            component_type_t const* cp_type           = &ecs->m_cp_type_mgr.m_a_cp_type[cp_index];
            s8 const                cp_group_index    = cp_type->cp_group_index;
            s8 const                cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const          entity_index    = s_entity_index(entity);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            u32 const          cp_group_bit    = (1 << cp_group_index);
            if (entity_instance.m_cp_groups & cp_group_bit)
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gi      = math::g_countBits(entity_instance.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity_instance.m_cp_group_cp_used[gi];
                if (cp_used & (1 << cp_group_cp_index))
                {
                    component_group_t* group             = &ecs->m_cp_group_mgr.m_cp_groups[cp_group_index];
                    byte*              cp_data           = group->m_a_en_cp_data[cp_group_cp_index];
                    u32 const          cp_group_en_index = entity_instance.m_cp_group_en_index[gi];
                    return cp_data + cp_group_en_index * cp_type->cp_sizeof;
                }
            }
            return nullptr;
        }

        static byte* s_entity_add_component(ecs_t* ecs, entity_t e, u32 cp_index)
        {
            component_type_t const* cp_type           = &ecs->m_cp_type_mgr.m_a_cp_type[cp_index];
            s8 const                cp_group_index    = cp_type->cp_group_index;
            s8 const                cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const          entity_index    = s_entity_index(e);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            u32 const          c_group_bit     = (1 << cp_group_index);
            if ((entity_instance.m_cp_groups & c_group_bit) == 0)
            {
                // An entity_instance can have a maximum of 7 component groups
                if (math::g_countBits(entity_instance.m_cp_groups) == 7)
                    return nullptr;

                entity_instance.m_cp_groups |= c_group_bit;
            }

            // How many '1' bits are there before in 'm_cp_group_cp_used'
            u32 const gb      = entity_instance.m_cp_groups & (c_group_bit - 1);
            s8 const  gi      = math::g_countBits(gb);
            u32&      cp_used = entity_instance.m_cp_group_cp_used[gi];
            cp_used |= (1 << cp_group_cp_index);

            component_group_t* group                = &ecs->m_cp_group_mgr.m_cp_groups[cp_group_index];
            u32 const          cp_group_en_index    = group->m_en_binmap.find_and_set();
            entity_instance.m_cp_group_en_index[gi] = cp_group_en_index;

            byte* cp_data = group->m_a_en_cp_data[cp_group_cp_index];
            return cp_data + cp_group_en_index * cp_type->cp_sizeof;
        }

        // Remove/detach component from the entity
        static void s_entity_rem_component(ecs_t* ecs, entity_t e, u32 cp_index)
        {
            component_type_t const* cp_type           = &ecs->m_cp_type_mgr.m_a_cp_type[cp_index];
            s8 const                cp_group_index    = cp_type->cp_group_index;
            s8 const                cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const          entity_index    = s_entity_index(e);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            if (entity_instance.m_cp_groups & ((u64)1 << cp_group_index))
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gb      = entity_instance.m_cp_groups & ((1 << cp_group_index) - 1);
                s8 const  gi      = math::g_countBits(gb);
                u32&      cp_used = entity_instance.m_cp_group_cp_used[gi];
                cp_used &= ~(1 << cp_group_cp_index);

                // TODO Remove the entity in the component group if it is a component.
                //      If it is a tag, we do not need to do anything.
                component_group_t* group = &ecs->m_cp_group_mgr.m_cp_groups[cp_group_index];
                group->m_en_binmap.set_free(entity_instance.m_cp_group_en_index[gi]);
            }
        }

        // Tags are just components with no data, and since we use bits to indicate if a component is used or not, we can use the same system for tags.

        static bool s_entity_has_tag(ecs_t* ecs, entity_t e, u32 tg_index) { return s_entity_has_component(ecs, e, tg_index); }
        static void s_entity_add_tag(ecs_t* ecs, entity_t e, u32 tg_index) { s_entity_add_component(ecs, e, tg_index); }
        static void s_entity_rem_tag(ecs_t* ecs, entity_t e, u32 tg_index) { s_entity_rem_component(ecs, e, tg_index); }

        entity_t g_create_entity(ecs_t* ecs)
        {
            entity_index_t entity_index;
            if (s_create_entity(&ecs->m_entity_mgr, entity_index))
            {
                entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
                s_init(&entity_instance);
                entity_generation_t& gen_id = ecs->m_entity_mgr.m_a_entity_ver[entity_index];
                gen_id += 1;
                return s_entity_make(gen_id, entity_index);
            }
            return ECS_ENTITY_NULL;
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            entity_generation_t const gen_id = s_entity_generation(e);
            entity_generation_t const cur_id = ecs->m_entity_mgr.m_a_entity_ver[s_entity_index(e)];

            // NOTE: Do we verify the generation id and if it doesn't match we do not delete the entity?
            if (gen_id != cur_id)
                return;

            entity_index_t const entity_index = s_entity_index(e);
            s_destroy_entity(&ecs->m_entity_mgr, entity_index);
        }

        bool  g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { return s_entity_has_component(ecs, entity, cp_index); }
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { return s_entity_add_component(ecs, entity, cp_index); }
        void  g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { s_entity_rem_component(ecs, entity, cp_index); }
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { return s_entity_get_component(ecs, entity, cp_index); }

        bool g_has_tag(ecs_t* ecs, entity_t entity, u32 tg_index) { return s_entity_has_tag(ecs, entity, tg_index); }
        void g_add_tag(ecs_t* ecs, entity_t entity, u32 tg_index) { s_entity_add_tag(ecs, entity, tg_index); }
        void g_rem_tag(ecs_t* ecs, entity_t entity, u32 tg_index) { s_entity_rem_tag(ecs, entity, tg_index); }
        bool g_get_tag(ecs_t* ecs, entity_t entity, u32 tg_index) { return s_entity_has_tag(ecs, entity, tg_index); }

        //////////////////////////////////////////////////////////////////////////
        // en_iterator_t

        en_iterator_t::en_iterator_t(ecs_t* ecs)
        {
            m_ecs              = ecs;
            m_entity_index     = 0;
            m_entity_index_max = ecs->m_entity_mgr.m_entity_state.size();
            m_group_mask       = 0; // The group mask
            for (s16 i = 0; i < 7; ++i)
                m_group_cp_mask[i] = 0; // An entity cannot be in more than 7 component groups
            m_num_groups = 0;
        }

        // Mark the things you want to iterate on
        void en_iterator_t::set_cp_type(u32 cp_index)
        {
            const component_type_t* cp_type           = &m_ecs->m_cp_type_mgr.m_a_cp_type[cp_index];
            s8 const                cp_group_index    = cp_type->cp_group_index;
            s8 const                cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const group_bit    = (1 << cp_group_index);
            s8 const  groups_pivot = math::g_countBits(m_group_mask & (group_bit - 1));
            if ((m_group_mask & group_bit) == 0)
            {
                // This is a new group, and we need to insert it at the correct position, so some
                // groups have to be moved up.
                for (s8 i = groups_pivot; i < (m_num_groups - 1); ++i)
                    m_group_cp_mask[i + 1] = m_group_cp_mask[i];

                // This is a newly seen group, so we need to increase the group count
                m_num_groups += 1;
            }
            m_group_cp_mask[groups_pivot] |= (1 << cp_group_cp_index);
            m_group_mask |= group_bit;
        }

        static inline s32 s_first_entity(entity_mgr_t* mgr) { return mgr->m_entity_state.find_used(); }

        static inline s32 s_next_entity(entity_mgr_t* mgr, u32 index) { return mgr->m_entity_state.next_used_up(index + 1); }

        static s32 s_search_matching_entity(en_iterator_t& iter)
        {
            while (iter.m_entity_index >= 0)
            {
                // Until we encounter an entity that has all the required components/tags
                // First match the component groups and then the components/tags
                entity_instance_t& entity_instance = iter.m_ecs->m_entity_mgr.m_a_entity[iter.m_entity_index];
                if ((entity_instance.m_cp_groups & iter.m_group_mask) == iter.m_group_mask)
                {
                    // Groups are matching, do we have the correct components/tags?

                    // An entity can have more groups than we are looking for (but not less), this means that
                    // we need to iterate the groups of the entity_instance to lock-step with the groups of the iterator

                    u64 entity_group_mask = ~0;
                    u64 iter_group_mask   = ~0;
                    for (s8 i = 0; i < iter.m_num_groups; ++i)
                    {
                        s8 const iter_group_index   = math::g_findFirstBit(iter.m_group_mask & iter_group_mask);
                        s8       entity_group_index = math::g_findFirstBit(entity_instance.m_cp_groups & entity_group_mask);
                        while (entity_group_index < iter_group_index)
                        {
                            entity_group_index = math::g_findFirstBit(entity_instance.m_cp_groups & ~(1 << entity_group_index));
                            ASSERT(entity_group_index < 0); // This should not be possible!
                        }

                        if (iter.m_group_cp_mask[i] != (entity_instance.m_cp_group_cp_used[entity_group_index] & iter.m_group_cp_mask[i]))
                            goto iter_next_entity;

                        entity_group_mask = ~((u64)1 << entity_group_index);
                        iter_group_mask   = ~((u64)1 << iter_group_index);
                    }

                    return iter.m_entity_index;
                }
            iter_next_entity:
                iter.m_entity_index = s_next_entity(&iter.m_ecs->m_entity_mgr, iter.m_entity_index);
            }
            return -1;
        }

        void en_iterator_t::begin()
        {
            if (m_ecs != nullptr && m_num_groups > 0)
            {
                m_entity_index = s_first_entity(&m_ecs->m_entity_mgr);
                m_entity_index = s_search_matching_entity(*this);
            }
            else
            {
                m_entity_index = -1;
            }
        }

        entity_t en_iterator_t::entity() const { return s_entity_make(m_ecs->m_entity_mgr.m_a_entity_ver[m_entity_index], m_entity_index); }

        void en_iterator_t::next()
        {
            m_entity_index = s_next_entity(&m_ecs->m_entity_mgr, m_entity_index);
            m_entity_index = s_search_matching_entity(*this);
        }

        bool en_iterator_t::end() const { return m_entity_index == -1; }

    } // namespace necs2
} // namespace ncore
