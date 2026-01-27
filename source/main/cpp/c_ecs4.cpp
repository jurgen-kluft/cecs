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

        // The actual component data exists in virtual memory address space for max 65536 entities
        // Virtual address space of a component container is organized as follows:
        // u16*  m_global_to_local; // 128 cKB, 8 pages of 16 cKB (max entities)
        // u16*  m_local_to_global; // 128 cKB, 8 pages of 16 cKB (max components)
        // byte* m_component_data;  // max_components * component size, 4 pages * N bytes of component size
        struct component_container_t
        {
            u16 m_num_components;
            u16 m_max_components;
            u16 m_sizeof_component;
            u16 m_page_offset; // offset in pages from the start of component data
        };

        static u32 component_container_calculate_required_page_count(u8 page_size_shift, u16 sizeof_component, u32 max_components)
        {
            const u32 page_size = (1 << page_size_shift);
            const u32 data_size = (2 * 128 * cKB) + (u32)sizeof_component * (u32)max_components;
            const u32 num_pages = (data_size + (page_size - 1)) >> page_size_shift;
            return num_pages;
        }

        static u16* get_global_to_local(byte* base, u8 page_size_shift, component_container_t* container) { return (u16*)(base + ((container->m_page_offset + 0) << page_size_shift)); }

        static u16* get_local_to_global(byte* base, u8 page_size_shift, component_container_t* container)
        {
            const u32 array_num_pages = (128 * cKB) >> page_size_shift;
            return (u16*)(base + ((container->m_page_offset + array_num_pages) << page_size_shift));
        }

        static byte* get_component_data(byte* base, u8 page_size_shift, component_container_t* container)
        {
            const u32 array_num_pages = (128 * cKB) >> page_size_shift;
            return (base + ((container->m_page_offset + (array_num_pages * 2)) << page_size_shift));
        }

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
            u16                    m_max_entities;               // Max number of entities in this container (<65536)
            u16                    m_max_components;             // Max number of components supported
            u8                     m_component_words_per_entity; // Number of u32 words to hold all component bits per entity
            u8                     m_tag_words_per_entity;       // Number of u32 words to hold all tags per entity
            u16                    m_entity_free_bin0;           // 16 * 64 * 64 = 65536 entities
            u16                    m_entity_alive_bin0;          // 16 * 64 * 64 = 65536 entitiess
            u32                    m_component_data_pages;       // Current size of the component data (in pages)
            byte*                  m_per_entity_data;            // Entity[]{generation, component occupancy, tags}, page growing array (4 pages)
            component_container_t* m_component_containers;       // N component containers
            byte*                  m_component_data;             // Component data for all components (virtual memory)
            u64*                   m_entity_free_bin1;           // Track the 0 bits in m_entity_bin2
            u64*                   m_entity_alive_bin1;          // Track the 1 bits in m_entity_bin2
            u64*                   m_entity_bin2;                // '1' bit = alive entity, '0' bit = free entity
        };

        void s_component_containers_create()
        {
            // todo
        }

        entity_container_t* s_entity_container_create(u32 max_components, u32 max_tags)
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

        static void s_entity_container_teardown(component_container_t* container)
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
