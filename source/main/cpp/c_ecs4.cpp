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
        // ecs4: based on using entity shards, each shard holds up to 65536 entities and has component containters.
        // A shard is purely using virtual memory.

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        // The actual component data exists in virtual memory address space for 65536 entities
        struct component_container_t
        {
            u32 m_free_index;
            u32 m_sizeof_component;
        };

        // Component data is organized as follows:
        // byte* m_component_data;  // 65536 * component size, 4 pages * N bytes of component size
        // u16*  m_global_to_local; // 128 cKB, 8 pages of 16 cKB
        // u16*  m_local_to_global; // 128 cKB, 8 pages of 16 cKB

        // With 256 components and an average size of 32 bytes per component, this gives us:
        // Component data: 65536 * 32 = 2 MB per component, for 256 components = 512 MB
        // Global to local: 256 * 128 KB = 32 MB
        // Local to global: 256 * 128 KB = 32 MB
        // Total: 576 MB of address space per entity shard

        static u32 sizeof_data_for_one_entity(u32 max_components, u32 max_tags)
        {
            u32 sz = 0;
            sz += sizeof(byte);                            // generation is 1 byte
            sz += ((max_components + 7) / 8) * sizeof(u8); // component occupancy bits
            sz += ((max_tags + 7) / 8) * sizeof(u8);       // tags bits
            return sz;
        }

        struct entity_container_t
        {
            u16                    m_num_entities;               // Number of alive entities in this container
            u8                     m_component_words_per_entity; // Number of u32 words to hold all component bits per entity
            u8                     m_tag_words_per_entity;       // Number of u32 words to hold all tags per entity
            u16                    m_entity_free_bin0;           // 16 * 64 * 64 = 65536 entities
            u16                    m_entity_alive_bin0;          // 16 * 64 * 64 = 65536 entitiess
            u32                    m_component_data_size;        // Current size of the component data
            byte*                  m_per_entity_data;            // Page growing array (4 pages)
            component_container_t* m_component_containers;       // N component containers
            byte*                  m_component_data;             // Component data for all components (virtual memory)
            u64*                   m_entity_free_bin1;           // Track the 0 bits in m_entity_bin2
            u64*                   m_entity_alive_bin1;          // Track the 1 bits in m_entity_bin2
            u64*                   m_entity_bin2;                // '1' bit = alive entity, '0' bit = free entity
        };

        entity_container_t* s_entity_container_create(alloc_t* allocator, u32 max_components, u32 max_tags)
        {
            // Virtual memory layout:
            // entity_container_t (32 bytes)
            // component_container_t (max_components * sizeof(component_container_t))
            // entity_free_bin1  (1024 bits = 128 bytes)
            // entity_alive_bin1 (1024 bits = 128 bytes)
            // entity_alive_bin2 (65536 bits = 8 KB)
            // per_entity_data (sizeof_data_for_one_entity(max_components, max_tags) * 65536)
            // component_data
        }

        static void s_entity_container_teardown(alloc_t* allocator, component_container_t* container)
        {
            // Release component data virtual memory
            // Release container memory
        }

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u32                  m_max_component_containers;
            u32                  m_max_entity_containers;
            u32                  m_num_entity_containers;
            entity_container_t** m_entity_containers;
        };

    } // namespace necs4
} // namespace ncore
