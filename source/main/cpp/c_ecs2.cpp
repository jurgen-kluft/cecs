#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_debug.h"
#include "cbase/c_hbb.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs2.h"

namespace ncore
{
    namespace necs2
    {
        const entity_t g_null_entity = (entity_t)0xFFFFFFFF;

        typedef u8 entity_gen_id_t;

        const u32 ECS_ENTITY_INDEX_MASK  = (0x00FFFFFF); // Mask to use to get the entity number out of an identifier
        const u32 ECS_ENTITY_GEN_ID_MASK = (0xFF000000); // Mask to use to get the generation id out of an identifier
        const u32 ECS_ENTITY_GEN_ID_MAX  = (0x000000FF); // Maximum generation id
        const s8  ECS_ENTITY_GEN_SHIFT   = (24);         // Extent of the entity id + type within an identifier

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions

        struct cp_type_t;

        static inline entity_gen_id_t s_entity_gen(entity_t e) { return ((u32)e & ECS_ENTITY_GEN_ID_MASK) >> ECS_ENTITY_GEN_SHIFT; }
        static inline u32             s_entity_index(entity_t e) { return (u32)e & ECS_ENTITY_INDEX_MASK; }
        static inline entity_t        s_make_entity(entity_gen_id_t gen, u32 id) { return ((u32)gen << ECS_ENTITY_GEN_SHIFT) | (id & ECS_ENTITY_INDEX_MASK); }

        struct cp_nctype_t
        {
            const char* cp_name;   // Name of the component
            s32         cp_sizeof; // Size of the component in bits
            u32         cp_data;   // Internal data (initialized when registered)
        };

        static inline void s_set_cp_type_data(u32* data, s8 cp_group_index, s8 cp_group_cp_index) { *data = (cp_group_index << 8) | cp_group_cp_index; }
        static inline s8   s_get_cp_type_cp_group_index(u32 data) { return (data >> 8) & 0xFF; }
        static inline s8   s_get_cp_type_cp_group_cp_index(u32 data) { return (data >> 0) & 0xFF; }

        // Component Group
        // This means that when an entity is subscribed to a group, it will have a slot for each component that is active in the group.
        // Also if you have 8 groups, that means that an entity already uses 8 * 4 (Entity Index to Group Entity Index) = 32 bytes.
        // Every component that is marked as active has a slot for each entity that is active in the group, this means that if you
        // have a lot of entities in the group that are not (mostly) consistently using the same components, you will have a lot of
        // wasted memory (this is a trade-off between memory and speed).

        // Note: If a component group always has less than 65536 entities, we can use a u16 for the group index.
        // TODO We could use virtual array's for the component data, as well as the global entity array

        struct cp_group_t
        {
            hbb_hdr_t  m_en_hbb_hdr;       // The header for the hbb data, this is used to keep track of which entities are used
            hbb_data_t m_en_hbb_data;      // For iteration, which entities are used
            u32        m_max_entities;     // Maximum number of entities in this group
            u32        m_cp_used;          // Each bit represents if for this group the component is free or used
            byte*      m_a_en_cp_data[32]; // The component data array per component type
            alloc_t*   m_allocator;        // The allocator
            u8         m_group_index;
        };

        static void s_cp_group_construct(cp_group_t* group, s8 index)
        {
            group->m_en_hbb_hdr   = hbb_hdr_t();
            group->m_en_hbb_data  = nullptr;
            group->m_max_entities = 0;
            group->m_cp_used      = 0;
            for (u32 i = 0; i < 32; ++i)
                group->m_a_en_cp_data[i] = nullptr;
            group->m_allocator   = nullptr;
            group->m_group_index = index;
        }

        static s8 s_cp_group_register_cp(cp_group_t* group)
        {
            u32 cp_id = math::findFirstBit(group->m_cp_used);
            if (cp_id >= 0 && cp_id < 32)
            {
                group->m_cp_used |= (1 << cp_id);

                // TODO Allocate the component data array etc...

                return (s8)cp_id;
            }
            return -1;
        }

        static void s_cp_group_unregister_cp(cp_group_t* group, s8 cp_index)
        {
            ASSERT(cp_index >= 0 && cp_index < 32);
            group->m_cp_used &= ~(1 << cp_index);
            group->m_allocator->deallocate(group->m_a_en_cp_data[cp_index]);
        }

        // Component Type Manager
        struct cp_type_mgr_t
        {
            hbb_hdr_t    m_cp_hbb_hdr;  // The header for the hbb data
            hbb_data_t   m_cp_hbb_data; // 2 * max_components bytes; To identify which component stores are still free (to give out new component type)
            cp_nctype_t* m_a_cp_type;   // 8 bytes; The type information attached to each store
        };

        // Component Group Manager
        // Note that we can have a maximum of 64 component groups, each component group can have a maximum of 32 components.
        // So in total we can handle 64 * 32 = 2048 components.
        struct cp_group_mgr_t
        {
            u32         m_cp_groups_max;  // Maximum number of component groups
            u64         m_cp_groups_used; // Bits indicate which groups are used/free
            cp_group_t* m_cp_groups;      // The array of component groups
        };

        // Entity data structure
        // Note that this setup limits an entity to 7 component groups (so a maximum of 224 components per entity)
        struct eentity_t
        {
            u64 m_cp_groups;            // Global maximum of 64 component groups, each bit represents a group index (bit 0 is group 0, bit 3 is group 3 etc..)
            u32 m_cp_group_cp_used[7];  // Each bit represents the 'components used' in that component group
            u32 m_cp_group_en_index[7]; // The index of the entity in the component group
        };

        struct entity_mgr_t
        {
            hbb_hdr_t        m_entity_hbb_hdr;  // The header for the hbb data
            hbb_data_t       m_entity_hbb_data; // Which entities are free
            entity_gen_id_t* m_a_entity_ver;    // The generation Id of each entity
            eentity_t*       m_a_entity;        // The array of entities entries
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
            g_hbb_init(cps->m_cp_hbb_hdr, max_components);
            g_hbb_init(cps->m_cp_hbb_hdr, cps->m_cp_hbb_data, 1);
            cps->m_a_cp_type = (cp_nctype_t*)allocator->allocate(sizeof(cp_nctype_t) * max_components);
        }

        static void s_exit(cp_type_mgr_t* cps, alloc_t* allocator)
        {
            allocator->deallocate(cps->m_a_cp_type);
            allocator->deallocate(cps->m_cp_hbb_data);
            cps->m_a_cp_type   = nullptr;
            cps->m_cp_hbb_data = nullptr;
        }

        static cp_type_t* s_register_cp_type(cp_type_mgr_t* cps, const char* cp_name, s32 cp_sizeof, cp_group_t* group)
        {
            u32 cp_id = 0;
            if (g_hbb_find(cps->m_cp_hbb_hdr, cps->m_cp_hbb_data, cp_id))
            {
                cp_nctype_t* cp_nctype = (cp_nctype_t*)&cps->m_a_cp_type[cp_id];
                cp_nctype->cp_sizeof   = cp_sizeof;
                cp_nctype->cp_name     = cp_name;

                // Register a component in the component group
                u32 const cp_group_cp_index = s_cp_group_register_cp(group);
                s_set_cp_type_data(&cp_nctype->cp_data, group->m_group_index, cp_group_cp_index);

                return (cp_type_t*)cp_nctype;
            }
            return nullptr;
        }

        static void s_unregister_cp_type(cp_type_mgr_t* cps, cp_type_t* cp_type, cp_group_mgr_t* cp_group_mgr)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);
            cp_group_t*        group             = &cp_group_mgr->m_cp_groups[cp_group_index];
            s_cp_group_unregister_cp(group, cp_group_cp_index);
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // tag type, tag type manager

        static cp_type_t* s_register_tag_type(cp_type_mgr_t* ts, const char* cp_name, cp_group_t* cp_group) { return s_register_cp_type(ts, cp_name, 0, cp_group); }
        static void       s_unregister_tg_type(cp_type_mgr_t* cps, cp_type_t* cp_type, cp_group_mgr_t* cp_group_mgr) { s_unregister_cp_type(cps, cp_type, cp_group_mgr); }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // component group manager, create and destroy

        static void s_init(cp_group_mgr_t* cp_group_mgr, u32 max_groups, alloc_t* allocator)
        {
            ASSERT(max_groups > 0 && max_groups <= 64);
            cp_group_mgr->m_cp_groups_max = max_groups;
            cp_group_mgr->m_cp_groups     = (cp_group_t*)allocator->allocate(sizeof(cp_group_t) * max_groups);
            for (u32 i = 0; i < max_groups; ++i)
            {
                cp_group_t* group = &cp_group_mgr->m_cp_groups[i];
                s_cp_group_construct(group, i);
            }
            cp_group_mgr->m_cp_groups_used = 0;
        }

        static cp_group_t* s_register_group(cp_group_mgr_t* cp_group_mgr)
        {
            // Find a zero bit in the group used array
            s8 id = math::findFirstBit(cp_group_mgr->m_cp_groups_used);
            if (id >= 0 && id < 64)
            {
                cp_group_mgr->m_cp_groups_used |= ((u64)1 << id);
                return &cp_group_mgr->m_cp_groups[id];
            }
            return nullptr;
        }

        static void s_exit(cp_group_mgr_t* cp_group_mgr, alloc_t* allocator)
        {
            allocator->deallocate(cp_group_mgr->m_cp_groups);
            cp_group_mgr->m_cp_groups = nullptr;
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // entity manager

        static void s_init(entity_mgr_t* entity_mgr, u32 max_entities, alloc_t* allocator)
        {
            g_hbb_init(entity_mgr->m_entity_hbb_hdr, max_entities);
            g_hbb_init(entity_mgr->m_entity_hbb_hdr, entity_mgr->m_entity_hbb_data, 1, allocator);
            entity_mgr->m_a_entity_ver = (entity_gen_id_t*)allocator->allocate(sizeof(entity_gen_id_t) * max_entities);
            entity_mgr->m_a_entity     = (eentity_t*)allocator->allocate(sizeof(eentity_t) * max_entities);
        }

        static bool s_create_entity(entity_mgr_t* entity_mgr, u32& index)
        {
            if (g_hbb_find(entity_mgr->m_entity_hbb_hdr, entity_mgr->m_entity_hbb_data, index))
            {
                g_hbb_set(entity_mgr->m_entity_hbb_hdr, entity_mgr->m_entity_hbb_data, index);
                return true;
            }
            return false;
        }

        static void s_destroy_entity(entity_mgr_t* entity_mgr, u32 index) { g_hbb_clr(entity_mgr->m_entity_hbb_hdr, entity_mgr->m_entity_hbb_data, index); }

        static void s_exit(entity_mgr_t* entity_mgr, alloc_t* allocator)
        {
            allocator->deallocate(entity_mgr->m_a_entity);
            allocator->deallocate(entity_mgr->m_a_entity_ver);
            allocator->deallocate(entity_mgr->m_entity_hbb_data);
            entity_mgr->m_a_entity        = nullptr;
            entity_mgr->m_a_entity_ver    = nullptr;
            entity_mgr->m_entity_hbb_data = nullptr;
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

        cp_group_t* g_register_cp_group(ecs_t* ecs) { return s_register_group(&ecs->m_cp_group_mgr); }
        cp_type_t*  g_register_cp_type(ecs_t* r, const char* cp_name, s32 cp_sizeof, cp_group_t* cp_group) { return s_register_cp_type(&r->m_cp_type_mgr, cp_name, cp_sizeof, cp_group); }
        cp_type_t*  g_register_tg_type(ecs_t* r, const char* cp_name, cp_group_t* cp_group) { return s_register_tag_type(&r->m_cp_type_mgr, cp_name, cp_group); }

        // --------------------------------------------------------------------------------------------------------
        // entity functionality

        static bool s_entity_has_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);

            eentity_t& entity       = ecs->m_entity_mgr.m_a_entity[s_entity_index(e)];
            u32 const  cp_group_bit = (1 << cp_group_index);
            if (entity.m_cp_groups & cp_group_bit)
            {
                // How many '1' bits are there before 'cp_group_bit' in 'm_cp_groups'
                u32 const gi      = math::countBits(entity.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity.m_cp_group_cp_used[gi];
                return (cp_used & (1 << cp_group_cp_index)) != 0;
            }
            return false;
        }

        static void* s_entity_get_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);

            u32 const  en_idx = s_entity_index(e);
            eentity_t& entity = ecs->m_entity_mgr.m_a_entity[en_idx];
            u32 const  cp_group_bit = (1 << cp_group_index);
            if (entity.m_cp_groups & cp_group_bit)
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gi      = math::countBits(entity.m_cp_groups & (cp_group_bit - 1));
                u32 const cp_used = entity.m_cp_group_cp_used[gi];
                if (cp_used & (1 << cp_group_cp_index))
                {
                    u32 const   cp_group_en_index = entity.m_cp_group_en_index[gi];
                    cp_group_t* group             = &ecs->m_cp_group_mgr.m_cp_groups[cp_group_index];
                    return group->m_a_en_cp_data[cp_group_en_index];
                }
            }
            return nullptr;
        }

        static void s_entity_set_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);

            u32 const  en_idx = s_entity_index(e);
            eentity_t& entity = ecs->m_entity_mgr.m_a_entity[en_idx];
            if (entity.m_cp_groups & (1 << cp_group_index))
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gb      = entity.m_cp_groups & ((1 << cp_group_index) - 1);
                s8 const  gi      = math::countBits(gb);
                u32&      cp_used = entity.m_cp_group_cp_used[gi];
                cp_used |= (1 << cp_group_cp_index);

                // TODO Initialize the component in the component group if it is a component.
                //      If it is a tag, we do not need to do anything.
            }
            else
            {
                // The incoming component type has a group that is not yet registered?
                // NOTE Should we auto register ?
            }
        }

        // Remove/detach component from the entity
        static void s_entity_rem_component(ecs_t* ecs, entity_t e, cp_type_t* cp_type)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);

            u32 const  en_idx = s_entity_index(e);
            eentity_t& entity = ecs->m_entity_mgr.m_a_entity[en_idx];
            if (entity.m_cp_groups & (1 << cp_group_index))
            {
                // How many '1' bits are there before in 'm_cp_group_cp_used'
                u32 const gb      = entity.m_cp_groups & ((1 << cp_group_index) - 1);
                s8 const  gi      = math::countBits(gb);
                u32&      cp_used = entity.m_cp_group_cp_used[gi];
                cp_used &= ~(1 << cp_group_cp_index);

                // TODO Remove the component in the component group if it is a component.
                //      If it is a tag, we do not need to do anything.
            }
        }

        // Tags are just components with no data, and since we use bits to indicate if a component is used or not, we can use the same system for tags.

        static bool s_entity_has_tag(ecs_t* ecs, entity_t e, cp_type_t* tg_type) { return s_entity_has_component(ecs, e, tg_type); }
        static void s_entity_set_tag(ecs_t* ecs, entity_t e, cp_type_t* tg_type) { s_entity_set_component(ecs, e, tg_type); }
        static void s_entity_rem_tag(ecs_t* ecs, entity_t e, cp_type_t* tg_type) { s_entity_rem_component(ecs, e, tg_type); }

        entity_t g_create_entity(ecs_t* ecs)
        {
            u32 index;
            if (s_create_entity(&ecs->m_entity_mgr, index))
            {
                eentity_t& entity  = ecs->m_entity_mgr.m_a_entity[index];
                entity.m_cp_groups = 0;

                entity_gen_id_t& gen_id = ecs->m_entity_mgr.m_a_entity_ver[index];
                gen_id += 1;
                return s_make_entity(gen_id, index);
            }
            return g_null_entity;
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            entity_gen_id_t const gen_id = s_entity_gen(e);
            entity_gen_id_t const cur_id = ecs->m_entity_mgr.m_a_entity_ver[s_entity_index(e)];
            // NOTE Do we verify the generation id and if it doesn't match we do not delete the entity?
            if (gen_id != cur_id)
                return;

            u32 const index = s_entity_index(e);
            s_destroy_entity(&ecs->m_entity_mgr, index);
        }

        bool  g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_has_component(ecs, entity, cp_type); }
        void  g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_set_component(ecs, entity, cp_type); }
        void  g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_rem_component(ecs, entity, cp_type); }
        void* g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_get_component(ecs, entity, cp_type); }

        bool g_has_tag(ecs_t* ecs, entity_t entity, cp_type_t* tg_type) { return s_entity_has_tag(ecs, entity, tg_type); }
        void g_set_tag(ecs_t* ecs, entity_t entity, cp_type_t* tg_type) { s_entity_set_tag(ecs, entity, tg_type); }
        void g_rem_tag(ecs_t* ecs, entity_t entity, cp_type_t* tg_type) { s_entity_rem_tag(ecs, entity, tg_type); }

        //////////////////////////////////////////////////////////////////////////
        // en_iterator_t

        void en_iterator_t::initialize(ecs_t* ecs)
        {
            m_ecs              = ecs;
            m_entity_index     = 0;
            m_entity_index_max = ecs->m_entity_mgr.m_entity_hbb_hdr.get_max_bits();
            m_group_mask       = 0; // The group mask
            for (s16 i = 0; i < 7; ++i)
                m_group_cp_mask[i] = 0; // An entity cannot be in more than 7 component groups
            m_num_groups = 0;
        }

        // Mark the things you want to iterate on
        void en_iterator_t::cp_type(cp_type_t* cp_type)
        {
            cp_nctype_t const* cp_nctype         = (cp_nctype_t*)cp_type;
            s8 const           cp_group_index    = s_get_cp_type_cp_group_index(cp_nctype->cp_data);
            s8 const           cp_group_cp_index = s_get_cp_type_cp_group_cp_index(cp_nctype->cp_data);

            u32 const group_bit = (1 << cp_group_index);
            if (m_group_mask & group_bit)
                return;

            u32 const groups_pivot = math::countBits(m_group_mask & (group_bit - 1));

            for (u32 i = groups_pivot; i < m_num_groups; ++i)
            {
                m_group_cp_mask[i + 1] = m_group_cp_mask[i];
            }

            u32 const cp_group_cp_bit = (1 << cp_group_cp_index);
            if (m_group_cp_mask[groups_pivot] & cp_group_cp_bit)
                return;

            m_group_mask |= group_bit;
        }

        static inline s32 s_first_entity(entity_mgr_t* mgr)
        {
            u32 index;
            if (g_hbb_find(mgr->m_entity_hbb_hdr, mgr->m_entity_hbb_data, index))
                return (s32)index;
            return -1;
        }

        static inline s32 s_next_entity(entity_mgr_t* mgr, u32 index)
        {
            u32 next_index;
            if (g_hbb_upper(mgr->m_entity_hbb_hdr, mgr->m_entity_hbb_data, index, next_index))
                return (s32)next_index;
            return -1;
        }

        static s32 s_search_matching_entity(en_iterator_t& iter)
        {
            while (iter.m_entity_index >= 0)
            {
            iter_next_entity:
                while (iter.m_entity_index < iter.m_entity_index_max)
                {
                    // Until we encounter an entity that has all the required components/tags
                    // First match the component groups and then the components/tags
                    eentity_t& entity = iter.m_ecs->m_entity_mgr.m_a_entity[iter.m_entity_index];
                    if ((entity.m_cp_groups & iter.m_group_mask) == iter.m_group_mask)
                    {
                        // Groups are matching, do we have the correct components/tags?

                        // An entity can have more groups than we are looking for (but not less), this means that
                        // we need to iterate the groups of the entity to lock-step with the groups of the iterator

                        s8 iter_group_index   = math::findFirstBit(iter.m_group_mask);
                        s8 entity_group_index = math::findFirstBit(entity.m_cp_groups);
                        for (s8 i = 0; i < iter.m_num_groups; ++i)
                        {
                            while (entity_group_index < iter_group_index)
                            {
                                entity_group_index = math::findFirstBit(entity.m_cp_groups & ~(1 << entity_group_index));
                                ASSERT(entity_group_index < 0); // This should not be possible!
                            }

                            if (iter.m_group_cp_mask[i] != (entity.m_cp_group_cp_used[entity_group_index] & iter.m_group_cp_mask[i]))
                                goto iter_next_entity;

                            // Next group
                            entity_group_index = math::findFirstBit(entity.m_cp_groups & ~(1 << entity_group_index));
                            iter_group_index   = math::findFirstBit(iter.m_group_mask & ~(1 << iter_group_index));
                        }

                        return iter.m_entity_index;
                    }
                    goto iter_next_entity;
                }
            }
            return -1;
        }

        void en_iterator_t::begin()
        {
            m_num_groups = math::countBits(m_group_mask);
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

        entity_t en_iterator_t::entity() const { return s_make_entity(m_ecs->m_entity_mgr.m_a_entity_ver[m_entity_index], m_entity_index); }

        void en_iterator_t::next()
        {
            m_entity_index = s_next_entity(&m_ecs->m_entity_mgr, m_entity_index);
            m_entity_index = s_search_matching_entity(*this);
        }

        bool en_iterator_t::end() const { return m_entity_index == -1; }

    } // namespace necs2
} // namespace ncore
