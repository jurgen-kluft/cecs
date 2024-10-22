#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_debug.h"
#include "cbase/c_duomap.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs3.h"

namespace ncore
{
    namespace necs3
    {
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        struct component_container_t
        {
            u32         m_free_index;
            u32         m_sizeof_component;
            byte*       m_component_data;
            s32*        m_redirect;
            binmap_t    m_occupancy;
            const char* m_name;
        };

        static void s_teardown(alloc_t* allocator, component_container_t* container)
        {
            allocator->deallocate(container->m_component_data);
            allocator->deallocate(container->m_redirect);
            container->m_occupancy.release(allocator);
            container->m_free_index       = 0;
            container->m_sizeof_component = 0;
        }

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            alloc_t*               m_allocator;
            u32                    m_max_entities;
            u32                    m_max_components;
            u32                    m_component_words_per_entity;
            u32                    m_tag_words_per_entity;
            byte*                  m_per_entity_generation;
            u32*                   m_per_entity_component_occupancy;
            u32*                   m_per_entity_tags;
            component_container_t* m_component_containers;
            duomap_t               m_entity_state;
        };

        ecs_t* g_create_ecs(alloc_t* allocator, u32 max_entities, u32 max_components, u32 max_tags)
        {
            ecs_t* ecs = allocator->construct<ecs_t>();

            ecs->m_allocator                  = allocator;
            ecs->m_max_entities               = max_entities;
            ecs->m_max_components             = max_components;
            ecs->m_component_words_per_entity = (max_components + 31) >> 5;
            ecs->m_tag_words_per_entity       = (max_tags + 31) >> 5;

            ecs->m_per_entity_generation          = g_allocate_array_and_memset<byte>(allocator, max_entities, 0);
            ecs->m_per_entity_component_occupancy = g_allocate_array_and_memset<u32>(allocator, max_entities * ecs->m_component_words_per_entity, 0);
            ecs->m_per_entity_tags                = g_allocate_array_and_memset<u32>(allocator, max_entities * ecs->m_tag_words_per_entity, 0);

            ecs->m_component_containers = g_allocate_array_and_memset<component_container_t>(allocator, max_components, 0);

            duomap_t::config_t cfg = duomap_t::config_t::compute(max_entities);
            ecs->m_entity_state.init_all_free(cfg, allocator);

            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            alloc_t* allocator = ecs->m_allocator;

            for (u32 i = 0; i < ecs->m_max_components; ++i)
            {
                component_container_t* container = &ecs->m_component_containers[i];
                if (container->m_sizeof_component > 0)
                    s_teardown(allocator, container);
            }
            allocator->deallocate(ecs->m_component_containers);

            allocator->deallocate(ecs->m_per_entity_tags);
            allocator->deallocate(ecs->m_per_entity_component_occupancy);
            allocator->deallocate(ecs->m_per_entity_generation);

            ecs->m_entity_state.release(allocator);

            allocator->deallocate(ecs);
        }

        entity_t g_create_entity(ecs_t* ecs)
        {
            s32 const index = ecs->m_entity_state.find_free_and_set_used();
            if (index >= 0)
            {
                // Clear the component occupancy
                u32 const offset = index * ecs->m_component_words_per_entity;
                for (u32 i = 0; i < ecs->m_component_words_per_entity; ++i)
                    ecs->m_per_entity_component_occupancy[offset + i] = 0;

                // Clear the tag occupancy
                u32 const tag_offset = index * ecs->m_tag_words_per_entity;
                for (u32 i = 0; i < ecs->m_tag_words_per_entity; ++i)
                    ecs->m_per_entity_tags[tag_offset + i] = 0;

                ecs->m_per_entity_generation[index] = 0;
                return s_entity_make(0, index);
            }
            return ECS_ENTITY_NULL;
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            entity_generation_t const gen_id = s_entity_generation(e);
            entity_generation_t const cur_id = ecs->m_per_entity_generation[s_entity_index(e)];
            if (gen_id == cur_id)
                ecs->m_entity_state.set_free(s_entity_index(e));
        }

        bool g_register_component(ecs_t* ecs, u32 max_components, u32 cp_index, s32 cp_sizeof, s32 cp_alignof, const char* cp_name)
        {
            // See if the component container is present, if not we need to initialize it
            if (ecs->m_component_containers[cp_index].m_sizeof_component == 0)
            {
                component_container_t* container = &ecs->m_component_containers[cp_index];
                container->m_free_index          = 0;
                container->m_sizeof_component    = cp_sizeof;
                container->m_component_data      = g_allocate_array<byte>(ecs->m_allocator, cp_sizeof * max_components);
                container->m_redirect            = g_allocate_array_and_memset<s32>(ecs->m_allocator, ecs->m_max_entities, -1);
                container->m_name                = cp_name;

                binmap_t::config_t const cfg = binmap_t::config_t::compute(max_components);
                container->m_occupancy.init_all_free_lazy(cfg, ecs->m_allocator);
                return true;
            }
            return false;
        }

        void g_unregister_component(ecs_t* ecs, u32 cp_index)
        {
            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component > 0)
                s_teardown(ecs->m_allocator, container);
        }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            u32 const* component_occupancy = &ecs->m_per_entity_component_occupancy[s_entity_index(entity) * ecs->m_component_words_per_entity];
            return (component_occupancy[cp_index >> 5] & (1 << (cp_index & 31))) != 0;
        }

        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            if (cp_index >= ecs->m_max_components)
                return nullptr;

            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0)
                return nullptr;

            u32 const entity_index = s_entity_index(entity);
            if (container->m_redirect[entity_index] < 0)
            {
                s32 local_component_index = container->m_occupancy.find();
                if (local_component_index == -1)
                {
                    if (container->m_free_index >= container->m_occupancy.size())
                        return nullptr;
                    container->m_occupancy.tick_all_free_lazy(container->m_free_index);
                    local_component_index = container->m_free_index++;
                }
                else
                {
                    local_component_index = container->m_occupancy.find_and_set();
                }

                container->m_redirect[entity_index] = local_component_index;
                u32* component_occupancy            = &ecs->m_per_entity_component_occupancy[entity_index * ecs->m_component_words_per_entity];
                component_occupancy[cp_index >> 5] |= (1 << (cp_index & 31));
                return &container->m_component_data[local_component_index * container->m_sizeof_component];
            }
            return nullptr;
        }

        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            if (cp_index >= ecs->m_max_components)
                return;

            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0)
                return;

            u32 const entity_index = s_entity_index(entity);
            if (container->m_redirect[entity_index] >= 0)
            {
                s32 const local_component_index     = container->m_redirect[entity_index];
                container->m_redirect[entity_index] = -1;
                container->m_occupancy.set_free(local_component_index);

                u32* component_occupancy = &ecs->m_per_entity_component_occupancy[entity_index * ecs->m_component_words_per_entity];
                component_occupancy[cp_index >> 5] &= ~(1 << (cp_index & 31));
            }
        }

        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            if (cp_index >= ecs->m_max_components)
                return nullptr;

            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0)
                return nullptr;

            u32 const index = s_entity_index(entity);
            if (container->m_redirect[index] >= 0)
                return &container->m_component_data[container->m_redirect[index] * container->m_sizeof_component];
            return nullptr;
        }

        bool g_has_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return false;

            u32 const* tag_occupancy = &ecs->m_per_entity_tags[s_entity_index(entity) * ecs->m_tag_words_per_entity];
            return (tag_occupancy[tg_index >> 5] & (1 << (tg_index & 31))) != 0;
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return;

            u32* tag_occupancy = &ecs->m_per_entity_tags[s_entity_index(entity) * ecs->m_tag_words_per_entity];
            tag_occupancy[tg_index >> 5] |= (1 << (tg_index & 31));
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return;

            u32* tag_occupancy = &ecs->m_per_entity_tags[s_entity_index(entity) * ecs->m_tag_words_per_entity];
            tag_occupancy[tg_index >> 5] &= ~(1 << (tg_index & 31));
        }

        en_iterator_t::en_iterator_t(ecs_t* ecs, entity_t entity_reference)
            : m_ecs(ecs)
            , m_entity_reference(entity_reference)
            , m_entity_index(-1)
        {
        }

        entity_t en_iterator_t::entity() const { return m_entity_index >= 0 ? s_entity_make(m_ecs->m_per_entity_generation[m_entity_index], m_entity_index) : ECS_ENTITY_NULL; }

        // TODO, optimize this, we can determine the 'words' that have bits set in the reference entity, same for the tags

        s32 en_iterator_t::find(s32 entity_index) const
        {
            u32 const* ref_component_occupancy = &m_ecs->m_per_entity_component_occupancy[s_entity_index(m_entity_reference) * m_ecs->m_component_words_per_entity];
            u32 const* ref_tag_occupancy       = &m_ecs->m_per_entity_tags[s_entity_index(m_entity_reference) * m_ecs->m_tag_words_per_entity];

            // See if this entity has all the components and tags that we are looking for by taking the
            // intersection of the component and tag occupancy using the reference entity

            entity_index = m_ecs->m_entity_state.next_used_up(entity_index);
            while (entity_index >= 0)
            {
                if (entity_index != m_entity_reference)
                {
                    u32 const* cur_component_occupancy = &m_ecs->m_per_entity_component_occupancy[entity_index * m_ecs->m_component_words_per_entity];
                    bool       has_all_components      = true;
                    for (u32 i = 0; i < m_ecs->m_component_words_per_entity; ++i)
                    {
                        if ((cur_component_occupancy[i] & ref_component_occupancy[i]) != ref_component_occupancy[i])
                        {
                            has_all_components = false;
                            break;
                        }
                    }

                    if (has_all_components)
                    {
                        u32 const* cur_tag_occupancy = &m_ecs->m_per_entity_tags[entity_index * m_ecs->m_tag_words_per_entity];
                        bool       has_all_tags      = true;
                        for (u32 i = 0; i < m_ecs->m_tag_words_per_entity; ++i)
                        {
                            if ((cur_tag_occupancy[i] & ref_tag_occupancy[i]) != ref_tag_occupancy[i])
                            {
                                has_all_tags = false;
                                break;
                            }
                        }
                        if (has_all_tags)
                            break;
                    }
                }
                entity_index = m_ecs->m_entity_state.next_used_up(entity_index + 1);
            }

            return entity_index;
        }

    } // namespace necs3
} // namespace ncore
