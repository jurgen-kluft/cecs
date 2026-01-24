#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_debug.h"
#include "ccore/c_memory.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs4.h"

namespace ncore
{
    namespace necs4
    {
        // ecs4: based on using entity shards, each shard holds up to 65536 entities, as well as virtual memory.

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        // Use virtual memory address space to place a component container for 65536 entities
        // This struct needs to be
        struct component_container_t
        {
            u32 m_free_index;
            u32 m_sizeof_component;
        };

        // Component data is organized as follows:
        // byte* m_component_data;  // 65536 * component size, 4 pages * N bytes of component size
        // u16*  m_global_to_local; // 128 cKB, 8 pages of 16 cKB
        // u16*  m_local_to_global; // 128 cKB, 8 pages of 16 cKB

        struct entity_shard_t
        {
            u32                    m_max_entities;                   // 65536 ?
            u32                    m_max_component_containers;       // N component containers
            u32                    m_component_words_per_entity;     // Number of u32 words to hold all component bits per entity
            u32                    m_tag_words_per_entity;           // Number of u32 words to hold all tags per entity
            byte*                  m_per_entity_generation;          // Page growing array (4 pages)
            u32*                   m_per_entity_component_occupancy; // Page growing array
            u32*                   m_per_entity_tags;                // Page growing array
            component_container_t* m_component_containers;           // N component containers
            byte*                  m_component_data;                 // Component data for all components (virtual memory)
            u16                    m_entity_free_bin0;               // 16 * 64 * 64 = 65536 entities
            u64*                   m_entity_free_bin1;               // Track the 0 bits in m_entity_alive_bin2
            u16                    m_entity_alive_bin0;              // 16 * 64 * 64 = 65536 entitiess
            u64*                   m_entity_alive_bin1;              // Track the 1 bits in m_entity_alive_bin2
            u64*                   m_entity_alive_bin2;              // A '1' bit indicates that the entity is alive
        };

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u32             m_max_shards;
            u32             m_num_shards;
            entity_shard_t* m_shards;
        };

        static void s_teardown(alloc_t* allocator, component_container_t* container)
        {
            container->m_free_index       = 0;
            container->m_sizeof_component = 0;
        }

    } // namespace necs3
} // namespace ncore