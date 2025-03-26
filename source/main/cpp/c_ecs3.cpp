#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_debug.h"
#include "ccore/c_memory.h"
#include "cbase/c_duomap.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs3.h"

namespace ncore
{
    namespace necs3
    {
        // Notes on memory reduction but introducing indirection:
        //
        // 2 indirection arrays per entity and limiting entity to have a max of 256 components
        //
        // u32:
        //
        // 2048 max components
        //
        // entity has a max of 256 components
        // 2048 * sizeof(byte) + 256 * 4 = 3072 bytes per entity
        //
        // If we naively use an indirection array per component, that would take:
        // 2048 * 4 = 8192 bytes per entity
        //
        // u16:
        //
        // 2048 max components
        //
        // entity has a max of 256 components
        // 2048 * sizeof(byte) + 256 * 2 = 2560 bytes per entity
        //
        // If we naively use an indirection array per component, that would take:
        // 2048 * 2 = 4096 bytes per entity
        //
        //
        // Observation:
        //
        // If we limit each component container to hold a maximum of 65536 components, we can use u16 for
        // indirection array. This would reduce the memory footprint of each entity to 4096 bytes when
        // the maximum number of components is 2048.

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
            u32*        m_global_to_local;
            u32*        m_local_to_global;
            const char* m_name;
        };

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

        static void s_teardown(alloc_t* allocator, component_container_t* container)
        {
            g_deallocate_array(allocator, container->m_component_data);
            g_deallocate_array(allocator, container->m_global_to_local);
            g_deallocate_array(allocator, container->m_local_to_global);
            container->m_free_index       = 0;
            container->m_sizeof_component = 0;
            container->m_name             = "";
        }

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

            // allocator->deallocate(ecs->m_component_containers);
            // allocator->deallocate(ecs->m_per_entity_tags);
            // allocator->deallocate(ecs->m_per_entity_component_occupancy);
            // allocator->deallocate(ecs->m_per_entity_generation);
            g_deallocate_array(allocator, ecs->m_component_containers);
            g_deallocate_array(allocator, ecs->m_per_entity_tags);
            g_deallocate_array(allocator, ecs->m_per_entity_component_occupancy);
            g_deallocate_array(allocator, ecs->m_per_entity_generation);

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
            entity_generation_t const gen_id = g_entity_generation(e);
            entity_generation_t const cur_id = ecs->m_per_entity_generation[g_entity_index(e)];
            if (gen_id == cur_id)
                ecs->m_entity_state.set_free(g_entity_index(e));
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
                container->m_global_to_local     = g_allocate_array_and_memset<u32>(ecs->m_allocator, ecs->m_max_entities, 0xFFFFFFFF);
                container->m_local_to_global     = g_allocate_array_and_memset<u32>(ecs->m_allocator, max_components, 0xFFFFFFFF);
                container->m_name                = cp_name;
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
            u32 const* component_occupancy = &ecs->m_per_entity_component_occupancy[g_entity_index(entity) * ecs->m_component_words_per_entity];
            return (component_occupancy[cp_index >> 5] & (1 << (cp_index & 31))) != 0;
        }

        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            if (cp_index >= ecs->m_max_components)
                return nullptr;

            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0)
                return nullptr;

            u32 const entity_index = g_entity_index(entity);
            if (container->m_global_to_local[entity_index] == 0xFFFFFFFF)
            {
                s32 const local_index                      = container->m_free_index++;
                container->m_global_to_local[entity_index] = local_index;
                container->m_local_to_global[local_index]  = entity_index;
                u32* component_occupancy                   = &ecs->m_per_entity_component_occupancy[entity_index * ecs->m_component_words_per_entity];
                component_occupancy[cp_index >> 5] |= (1 << (cp_index & 31));
                return &container->m_component_data[local_index * container->m_sizeof_component];
            }
            else
            {
                u32 const local_index = container->m_global_to_local[entity_index];
                return &container->m_component_data[local_index * container->m_sizeof_component];
            }
        }

        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0 || cp_index >= container->m_free_index)
                return;

            u32 const entity_index = g_entity_index(entity);
            ASSERT(container->m_global_to_local[entity_index] != 0xFFFFFFFF);

            s32 const local_index                      = container->m_global_to_local[entity_index];
            container->m_global_to_local[entity_index] = 0xFFFFFFFF;
            container->m_local_to_global[local_index]  = 0xFFFFFFFF;
            container->m_free_index--;

            // Move the last element to the current position
            if (local_index != container->m_free_index)
            {
                u32 const last_entity_index                     = container->m_local_to_global[container->m_free_index];
                container->m_global_to_local[last_entity_index] = local_index;
                container->m_local_to_global[local_index]       = last_entity_index;

                byte* const last_component_data = &container->m_component_data[container->m_free_index * container->m_sizeof_component];
                byte* const cur_component_data  = &container->m_component_data[local_index * container->m_sizeof_component];
                g_memcopy(cur_component_data, last_component_data, container->m_sizeof_component);
            }

            u32* component_occupancy = &ecs->m_per_entity_component_occupancy[entity_index * ecs->m_component_words_per_entity];
            component_occupancy[cp_index >> 5] &= ~(1 << (cp_index & 31));
        }

        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            if (cp_index >= ecs->m_max_components)
                return nullptr;

            component_container_t* container = &ecs->m_component_containers[cp_index];
            if (container->m_sizeof_component == 0)
                return nullptr;

            u32 const entity_index = g_entity_index(entity);
            if (container->m_global_to_local[entity_index] >= 0)
                return &container->m_component_data[container->m_global_to_local[entity_index] * container->m_sizeof_component];
            return nullptr;
        }

        bool g_has_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return false;

            u32 const* tag_occupancy = &ecs->m_per_entity_tags[g_entity_index(entity) * ecs->m_tag_words_per_entity];
            return (tag_occupancy[tg_index >> 5] & (1 << (tg_index & 31))) != 0;
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return;

            u32* tag_occupancy = &ecs->m_per_entity_tags[g_entity_index(entity) * ecs->m_tag_words_per_entity];
            tag_occupancy[tg_index >> 5] |= (1 << (tg_index & 31));
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, s16 tg_index)
        {
            if (tg_index >= ecs->m_max_components)
                return;

            u32* tag_occupancy = &ecs->m_per_entity_tags[g_entity_index(entity) * ecs->m_tag_words_per_entity];
            tag_occupancy[tg_index >> 5] &= ~(1 << (tg_index & 31));
        }

        en_iterator_t::en_iterator_t(ecs_t* ecs)
            : m_ecs(ecs)
            , m_entity_reference(-1)
            , m_entity_index(-1)
        {
        }

        en_iterator_t::en_iterator_t(ecs_t* ecs, entity_t entity_reference)
            : m_ecs(ecs)
            , m_entity_reference(entity_reference == ECS_ENTITY_NULL ? -1 : g_entity_index(entity_reference))
            , m_entity_index(-1)
        {
        }

        entity_t en_iterator_t::entity() const { return m_entity_index >= 0 ? s_entity_make(m_ecs->m_per_entity_generation[m_entity_index], m_entity_index) : ECS_ENTITY_NULL; }

        // TODO, optimize this, we can determine the 'words' that have bits set in the reference entity, same for the tags

        s32 en_iterator_t::find(s32 entity_index) const
        {
            if (m_entity_reference < 0)
            {
                return entity_index >= 0 ? m_ecs->m_entity_state.next_used_up(entity_index) : -1;
            }

            // See if this entity has all the components and tags that we are looking for by taking the
            // intersection of the component and tag occupancy using the reference entity
            u32 const* ref_component_occupancy = &m_ecs->m_per_entity_component_occupancy[g_entity_index(m_entity_reference) * m_ecs->m_component_words_per_entity];
            u32 const* ref_tag_occupancy       = &m_ecs->m_per_entity_tags[g_entity_index(m_entity_reference) * m_ecs->m_tag_words_per_entity];

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
