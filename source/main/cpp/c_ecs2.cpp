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
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_genid_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        // Component Type, cp_type_t and tg_type_t
        struct cp_type_t
        {
            const char* cp_name;           // Name of the component
            s32         cp_sizeof;         // Size of the component in bits
            s16         cp_alignof;        // Alignment requirement of the component
            s8          cp_group_index;    // The group index
            s8          cp_group_cp_index; // The component index in the group
        };

        // Component Group managing a maximum of 32 components
        struct cg_type_t
        {
            const char* m_name;             // The name of the component group
            binmap_t    m_en_binmap;        // For iteration, which entities are used
            u32         m_max_entities;     // Maximum number of entities in this group
            u32         m_cp_used;          // Each bit represents if for this group the component is free or used
            byte*       m_a_en_cp_data[32]; // The component data array per component type
            alloc_t*    m_allocator;        // The allocator
            s8          m_group_index;
        };

        static void s_cp_group_init(cg_type_t* group)
        {
            group->m_en_binmap.reset();
            group->m_max_entities = 0;
            group->m_cp_used      = 0;
            for (u32 i = 0; i < 32; ++i)
                group->m_a_en_cp_data[i] = nullptr;
            group->m_allocator   = nullptr;
            group->m_group_index = -1;
        }

        static void s_cp_group_construct(cg_type_t* group, s8 index, u32 max_entities, alloc_t* allocator)
        {
            if (max_entities > 0)
            {
                group->m_allocator    = allocator;
                group->m_group_index  = index;
                group->m_max_entities = max_entities;
                binmap_t::config_t cfg = binmap_t::config_t::compute(max_entities);
                group->m_en_binmap.init_all_free(cfg, allocator);
            }
        }
        static void s_cp_group_destruct(cg_type_t* group)
        {
            alloc_t* allocator = group->m_allocator;
            u32      cp_used   = group->m_cp_used;
            while (cp_used != 0)
            {
                s32 index = math::findFirstBit(cp_used);
                if (group->m_a_en_cp_data[index] != nullptr)
                    allocator->deallocate(group->m_a_en_cp_data[index]);
                cp_used &= ~(1 << index);
            }
            group->m_en_binmap.release(allocator);
            s_cp_group_init(group);
        }

        static s8 s_cp_group_register_cp(cg_type_t* group, cp_type_t* cp_type)
        {
            s8 const cp_id = math::findFirstBit(~group->m_cp_used);
            if (cp_id >= 0 && cp_id < 32)
            {
                group->m_cp_used |= (1 << cp_id);

                // Only create entity-component data array for components and *not* for tags
                if (cp_type->cp_sizeof > 0)
                {
                    group->m_a_en_cp_data[cp_id] = (byte*)group->m_allocator->allocate(group->m_max_entities * cp_type->cp_sizeof);
                }

                return (s8)cp_id;
            }
            return -1;
        }

        static void s_cp_group_unregister_cp(cg_type_t* group, s8 cp_index)
        {
            ASSERT(cp_index >= 0 && cp_index < 32);
            group->m_cp_used &= ~(1 << cp_index);
            if (group->m_a_en_cp_data[cp_index] != nullptr)
            {
                group->m_allocator->deallocate(group->m_a_en_cp_data[cp_index]);
                group->m_a_en_cp_data[cp_index] = nullptr;
            }
        }

        // Component Type Manager
        struct cp_type_mgr_t
        {
            binmap_t   m_cp_binmap; // 2 * max_components bytes; To identify which component stores are still free (to give out new component type)
            cp_type_t* m_a_cp_type; // 8 bytes; The type information attached to each store
        };

        // Component Group Manager
        // Note that we can have a maximum of 64 component groups, each component group can have a maximum of 32 components.
        // So in theory we can handle a total of 64 * 32 = 2048 components.
        struct cp_group_mgr_t
        {
            u32        m_cp_groups_max;  // Maximum number of component groups
            u64        m_cp_groups_used; // Bits indicate which groups are used/free
            alloc_t*   m_allocator;
            cg_type_t* m_cp_groups; // The array of component groups
        };

        // Entity data structure (64 bytes)
        // Note that this setup limits an entity to 7 component groups (so a theoretical maximum of 224 components per entity)
        struct entity_instance_t
        {
            u64 m_cp_groups;            // Global maximum of 64 component groups, each bit represents a group index (bit 0 is group 0, bit 3 is group 3 etc..)
            u32 m_cp_group_cp_used[7];  // Each bit index represents the 'component index' in that component group
            u32 m_cp_group_en_index[7]; // The index of the entity in the component group
        };

        struct entity_mgr_t
        {
            duomap_t           m_entity_state; // Which entities are alive/dead
            entity_genid_t*    m_a_entity_ver; // The generation Id of each entity
            entity_instance_t* m_a_entity;     // The array of entities entries
        };

        struct ecs_t
        {
            alloc_t*       m_allocator;    // The allocator
            cp_type_mgr_t  m_cp_type_mgr;  // The component type manager
            cp_group_mgr_t m_cp_group_mgr; // The component group manager
            entity_mgr_t   m_entity_mgr;   // The entity manager
        };

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // component type, component type manager

        static void s_init(cp_type_mgr_t* cps, u32 max_components, alloc_t* allocator)
        {
            cps->m_cp_binmap.reset();
            binmap_t::config_t cfg = binmap_t::config_t::compute(max_components);
            cps->m_cp_binmap.init_all_free(cfg, allocator);
            cps->m_a_cp_type = (cp_type_t*)allocator->allocate(sizeof(cp_type_t) * max_components);
        }

        static void s_exit(cp_type_mgr_t* cps, alloc_t* allocator)
        {
            allocator->deallocate(cps->m_a_cp_type);
            cps->m_cp_binmap.release(allocator);
            cps->m_a_cp_type = nullptr;
        }

        static cp_type_t* s_register_cp_type(cp_type_mgr_t* cps, cg_type_t* cp_group, const char* cp_name, s32 cp_sizeof, s32 cp_alignof)
        {
            s32 const cp_id = cps->m_cp_binmap.find();
            if (cp_id >= 0)
            {
                cp_type_t* cp_type      = (cp_type_t*)&cps->m_a_cp_type[cp_id];
                cp_type->cp_name        = cp_name;
                cp_type->cp_sizeof      = cp_sizeof;
                cp_type->cp_alignof     = cp_alignof;
                cp_type->cp_group_index = cp_group->m_group_index;
                cps->m_cp_binmap.set_used(cp_id);

                // Register a component in the component group
                cp_type->cp_group_cp_index = s_cp_group_register_cp(cp_group, cp_type);

                return cp_type;
            }
            return nullptr;
        }

        static void s_unregister_cp_type(cp_type_mgr_t* cps, cp_type_t* cp_type, cp_group_mgr_t* cp_group_mgr)
        {
            u32 const cp_id = (u32)(cp_type - cps->m_a_cp_type);
            if (cps->m_cp_binmap.is_used(cp_id))
            {
                cg_type_t* group = &cp_group_mgr->m_cp_groups[cp_type->cp_group_index];
                s_cp_group_unregister_cp(group, cp_type->cp_group_cp_index);
                cps->m_cp_binmap.set_free(cp_id);
            }
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // tag type, tag type manager (tags are basically components, but they have no backing component data)

        static tg_type_t* s_register_tag_type(cp_type_mgr_t* ts, const char* cp_name, cg_type_t* cp_group) { return (tg_type_t*)s_register_cp_type(ts, cp_group, cp_name, 0, 0); }
        static void       s_unregister_tg_type(cp_type_mgr_t* cps, cp_type_t* cp_type, cp_group_mgr_t* cp_group_mgr) { s_unregister_cp_type(cps, cp_type, cp_group_mgr); }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // component group manager, create and destroy

        static void s_init(cp_group_mgr_t* cp_group_mgr, u32 max_groups, alloc_t* allocator)
        {
            ASSERT(max_groups > 0 && max_groups <= 64);
            cp_group_mgr->m_allocator      = allocator;
            cp_group_mgr->m_cp_groups_max  = max_groups;
            cp_group_mgr->m_cp_groups      = (cg_type_t*)allocator->allocate(sizeof(cg_type_t) * max_groups);
            cp_group_mgr->m_cp_groups_used = 0;
            for (u32 i = 0; i < max_groups; ++i)
                s_cp_group_init(&cp_group_mgr->m_cp_groups[i]);
        }

        static void s_exit(cp_group_mgr_t* cp_group_mgr, alloc_t* allocator)
        {
            u64 groups_used = cp_group_mgr->m_cp_groups_used;
            while (groups_used != 0)
            {
                s32 index = math::findFirstBit(groups_used);
                groups_used &= ~(1 << index);
                s_cp_group_destruct(&cp_group_mgr->m_cp_groups[index]);
            }

            allocator->deallocate(cp_group_mgr->m_cp_groups);

            cp_group_mgr->m_cp_groups_max  = 0;
            cp_group_mgr->m_cp_groups      = nullptr;
            cp_group_mgr->m_cp_groups_used = 0;
        }

        static cg_type_t* s_register_group(cp_group_mgr_t* cp_group_mgr, u32 max_entities, const char* cg_name)
        {
            // Find a '0' bit in the group used array
            s8 const id = math::findFirstBit(~cp_group_mgr->m_cp_groups_used);
            if (id >= 0 && id < 64)
            {
                cp_group_mgr->m_cp_groups_used |= ((u64)1 << id);
                cg_type_t* group = &cp_group_mgr->m_cp_groups[id];
                s_cp_group_construct(group, id, max_entities, cp_group_mgr->m_allocator);
                group->m_name = cg_name;
                return group;
            }
            return nullptr;
        }

        static void s_unregister_group(cp_group_mgr_t* cp_group_mgr, cg_type_t* cp_group)
        {
            cp_group_mgr->m_cp_groups_used &= ~((u64)1 << cp_group->m_group_index);
            s_cp_group_destruct(cp_group);
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

            entity_mgr->m_a_entity_ver = (entity_genid_t*)allocator->allocate(sizeof(entity_genid_t) * max_entities);
            entity_mgr->m_a_entity     = (entity_instance_t*)allocator->allocate(sizeof(entity_instance_t) * max_entities);
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

        static void s_destroy_entity(entity_mgr_t* entity_mgr, entity_index_t index)
        {
            entity_mgr->m_entity_state.set_free(index);
        }

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
            ecs_t* ecs       = (ecs_t*)allocator->allocate(sizeof(ecs_t));
            ecs->m_allocator = allocator;
            s_init(&ecs->m_cp_type_mgr, 64, allocator);
            s_init(&ecs->m_cp_group_mgr, 64, allocator);
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

        cg_type_t* g_register_cp_group(ecs_t* ecs, u32 max_entities, const char* cg_name) { return s_register_group(&ecs->m_cp_group_mgr, max_entities, cg_name); }
        void       g_unregister_cp_group(ecs_t* ecs, cg_type_t* cg_type) { return s_unregister_group(&ecs->m_cp_group_mgr, cg_type); }

        cp_type_t* g_register_cp_type(ecs_t* r, cg_type_t* cp_group, const char* cp_name, s32 cp_sizeof, s32 cp_alignof) { return s_register_cp_type(&r->m_cp_type_mgr, cp_group, cp_name, cp_sizeof, cp_alignof); }
        void       g_unregister_cp_type(ecs_t* r, cp_type_t* cp_type) { return s_unregister_cp_type(&r->m_cp_type_mgr, cp_type, &r->m_cp_group_mgr); }

        tg_type_t* g_register_tg_type(ecs_t* r, cg_type_t* cp_group, const char* cp_name) { return s_register_tag_type(&r->m_cp_type_mgr, cp_name, cp_group); }
        void       g_unregister_tg_type(ecs_t* r, tg_type_t* tg_type) { return s_unregister_tg_type(&r->m_cp_type_mgr, (cp_type_t*)tg_type, &r->m_cp_group_mgr); }

        // --------------------------------------------------------------------------------------------------------
        // entity functionality

        static bool s_entity_has_component(ecs_t* ecs, entity_t entity, cp_type_t* cp_type)
        {
            s8 const cp_group_index    = cp_type->cp_group_index;
            s8 const cp_group_cp_index = cp_type->cp_group_cp_index;

            ASSERT(cp_group_index >= 0 && cp_group_index < 64);
            ASSERT(cp_group_cp_index >= 0 && cp_group_cp_index < 32);

            entity_instance_t const& entity_instance = ecs->m_entity_mgr.m_a_entity[s_entity_index(entity)];
            u32 const                cp_group_bit    = (1 << cp_group_index);
            if ((entity_instance.m_cp_groups & cp_group_bit) == cp_group_bit)
            {
                // How many '1' bits are there before 'cp_group_bit' in 'm_cp_groups'
                u32 const gi      = math::countBits(entity_instance.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity_instance.m_cp_group_cp_used[gi];
                return (cp_used & (1 << cp_group_cp_index)) != 0;
            }
            return false;
        }

        static void* s_entity_get_component(ecs_t* ecs, entity_t entity, cp_type_t* cp_type)
        {
            s8 const cp_group_index    = cp_type->cp_group_index;
            s8 const cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const          entity_index    = s_entity_index(entity);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            u32 const          cp_group_bit    = (1 << cp_group_index);
            if (entity_instance.m_cp_groups & cp_group_bit)
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gi      = math::countBits(entity_instance.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity_instance.m_cp_group_cp_used[gi];
                if (cp_used & (1 << cp_group_cp_index))
                {
                    u32 const  cp_group_en_index = entity_instance.m_cp_group_en_index[gi];
                    cg_type_t* group             = &ecs->m_cp_group_mgr.m_cp_groups[cp_group_index];
                    return group->m_a_en_cp_data[cp_group_en_index];
                }
            }
            return nullptr;
        }

        static void s_entity_add_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            s8 const  cp_group_index    = cp_type->cp_group_index;
            s8 const  cp_group_cp_index = cp_type->cp_group_cp_index;
            u32 const c_group_bit       = (1 << cp_group_index);

            u32 const          entity_index    = s_entity_index(e);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            if ((entity_instance.m_cp_groups & c_group_bit) == 0)
            {
                // An entity_instance can have a maximum of 7 component groups
                if (math::countBits(entity_instance.m_cp_groups) == 7)
                    return;

                entity_instance.m_cp_groups |= c_group_bit;
            }

            // How many '1' bits are there before in 'm_cp_group_cp_used'
            u32 const gb      = entity_instance.m_cp_groups & (c_group_bit - 1);
            s8 const  gi      = math::countBits(gb);
            u32&      cp_used = entity_instance.m_cp_group_cp_used[gi];
            cp_used |= (1 << cp_group_cp_index);
        }

        // Remove/detach component from the entity
        static void s_entity_rem_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            s8 const cp_group_index    = cp_type->cp_group_index;
            s8 const cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const          entity_index    = s_entity_index(e);
            entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
            if (entity_instance.m_cp_groups & ((u64)1 << cp_group_index))
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gb      = entity_instance.m_cp_groups & ((1 << cp_group_index) - 1);
                s8 const  gi      = math::countBits(gb);
                u32&      cp_used = entity_instance.m_cp_group_cp_used[gi];
                cp_used &= ~(1 << cp_group_cp_index);

                // TODO Remove the component in the component group if it is a component.
                //      If it is a tag, we do not need to do anything.
            }
        }

        // Tags are just components with no data, and since we use bits to indicate if a component is used or not, we can use the same system for tags.

        static bool s_entity_has_tag(ecs_t* ecs, entity_t e, tg_type_t* tg_type) { return s_entity_has_component(ecs, e, (cp_type_t*)tg_type); }
        static void s_entity_add_tag(ecs_t* ecs, entity_t e, tg_type_t* tg_type) { s_entity_add_component(ecs, e, (cp_type_t*)tg_type); }
        static void s_entity_rem_tag(ecs_t* ecs, entity_t e, tg_type_t* tg_type) { s_entity_rem_component(ecs, e, (cp_type_t*)tg_type); }

        entity_t g_create_entity(ecs_t* ecs)
        {
            entity_index_t entity_index;
            if (s_create_entity(&ecs->m_entity_mgr, entity_index))
            {
                entity_instance_t& entity_instance = ecs->m_entity_mgr.m_a_entity[entity_index];
                entity_instance.m_cp_groups        = 0;
                for (s8 i = 0; i < 7; ++i)
                {
                    entity_instance.m_cp_group_cp_used[i]  = 0;
                    entity_instance.m_cp_group_en_index[i] = 0;
                }

                entity_genid_t& gen_id = ecs->m_entity_mgr.m_a_entity_ver[entity_index];
                gen_id += 1;
                return s_entity_make(gen_id, entity_index);
            }
            return ECS_ENTITY_NULL;
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            entity_genid_t const gen_id = s_entity_genid(e);
            entity_genid_t const cur_id = ecs->m_entity_mgr.m_a_entity_ver[s_entity_index(e)];
            // NOTE Do we verify the generation id and if it doesn't match we do not delete the entity?
            if (gen_id != cur_id)
                return;

            entity_index_t const entity_index = s_entity_index(e);
            s_destroy_entity(&ecs->m_entity_mgr, entity_index);
        }

        bool  g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_has_component(ecs, entity, cp_type); }
        void  g_add_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_add_component(ecs, entity, cp_type); }
        void  g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_rem_component(ecs, entity, cp_type); }
        void* g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_get_component(ecs, entity, cp_type); }

        bool g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { return s_entity_has_tag(ecs, entity, tg_type); }
        void g_add_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { s_entity_add_tag(ecs, entity, tg_type); }
        void g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { s_entity_rem_tag(ecs, entity, tg_type); }
        bool g_get_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { return s_entity_has_tag(ecs, entity, tg_type); }

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
        void en_iterator_t::set_cp_type(cp_type_t* cp_type)
        {
            s8 const cp_group_index    = cp_type->cp_group_index;
            s8 const cp_group_cp_index = cp_type->cp_group_cp_index;

            u32 const group_bit    = (1 << cp_group_index);
            s8 const  groups_pivot = math::countBits(m_group_mask & (group_bit - 1));
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

        static inline s32 s_first_entity(entity_mgr_t* mgr)
        {
            s32 const index = mgr->m_entity_state.find_used();
            return (s32)index;
        }

        static inline s32 s_next_entity(entity_mgr_t* mgr, u32 index)
        {
            index += 1;
            s32 const next_index = mgr->m_entity_state.upper_used(index);
            return next_index;
        }

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
                        s8 const iter_group_index   = math::findFirstBit(iter.m_group_mask & iter_group_mask);
                        s8       entity_group_index = math::findFirstBit(entity_instance.m_cp_groups & entity_group_mask);
                        while (entity_group_index < iter_group_index)
                        {
                            entity_group_index = math::findFirstBit(entity_instance.m_cp_groups & ~(1 << entity_group_index));
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
