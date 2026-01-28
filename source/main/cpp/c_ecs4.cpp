#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_arena.h"
#include "ccore/c_debug.h"
#include "ccore/c_memory.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs4.h"

namespace ncore
{
    namespace necs4
    {
        // ecs4: Entity Component System, version 4

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        struct components_t;

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u32            m_max_component_containers;         // Maximum number of component containers
            u32            m_num_component_containers;         // Current number of component containers
            u32            m_free_component_containers;        // Bit array to track free component containers
            u8             m_component_bytes_per_entity;       // Number of u32 words to hold all component bits per entit (u8)((max_component_types + 7) / 8);
            u8             m_tag_bytes_per_entity;             // Number of u32 words to hold all tags per enti(u8)((max_tag_types + 31) / 32);
            u32            m_entity_free_bin0;                 // 32 * 32 * 32 = 32768 entities
            u32            m_entity_alive_bin0;                // 32 * 32 * 32 = 32768 entities
            u32*           m_entity_free_bin1;                 // Track the 0 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*           m_entity_alive_bin1;                // Track the 1 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*           m_entity_free_bin2;                 // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*           m_entity_alive_bin2;                // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*           m_entity_bin3;                      // 32 * 32 * 32 * 32 = maximum 1,048,576 entities (growing page array)
            byte*          m_per_entity_data;                  // Entity[]{generation, component occupancy, tags}, page growing array (4 pages)
            byte*          m_entity_generation_array;          // Pointer to generation array (page aligned and growing)
            byte*          m_entity_component_occupancy_array; // Pointer to component occupancy bits array (page aligned and growing)
            byte*          m_entity_tags_array;                // Pointer to tags bits array (page aligned and growing)
            components_t** m_components_array;                 // Array of component containers
        };
        // ecs_t is followed in memory by:
        // components_t*        m_entity_containers[m_max_entity_containers];
        // u32                  m_entity_free_bin1[1024 / 32];             // 1024 bits = 128 bytes
        // u32                  m_entity_alive_bin1[1024 / 32];            // 1024 bits = 128 bytes
        // u32                  m_entity_free_bin2[32768 / 32];            // 32768 bits = 4 KB
        // u32                  m_entity_alive_bin2[32768 / 32];           // 32768 bits = 4 KB
        // u32                  m_entity_bin3[1*cMB / 32];                 // 1 MB = 32 * 32 * 32 * 32 bits = 1,048,576 bits = 128 KB
        // {page aligned} {1 byte per entity}   m_entity_generation_array[1*cMB];          // Pointer to generation array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_entity_component_occupancy_array[1*cMB]; // Pointer to component occupancy bits array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_entity_tags_array[1*cMB];                // Pointer to tags bits array (page aligned and growing)

#define D_ECS4_MAX_ENTITIES_PER_CONTAINER (32768) // 32 * 32 * 32 = 32768 entities

        struct component_type_t
        {
            u16 m_num_components;   //
            u16 m_sizeof_component; //
            u32 m_page_offset;      // offset in pages from the start of component data
        };

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // The actual component type data exists in virtual memory address space for max 32768 entities
        // Virtual address space of the component data in the container is organized as follows:
        // u16*  m_global_to_local; // 64 cKB (max entities)
        // u16*  m_local_to_global; // 64 cKB (max components)
        // byte* m_component_data;  // max_components * component size
        struct components_t
        {
            u32   m_used_pages;     // Current size of the component data (in pages)
            u32   m_max_pages;      // Maximum size of the component data (in pages)
            byte* m_component_data; // Component data for all components (virtual memory)
        };
        // components_t         is followed in memory by:
        // component_type_t     m_component_containers[m_max_components];
        // {page aligned}       component data virtual memory
        // --------------------------------------------------------------------------------------------------------

        static u16* get_global_to_local(byte* base, u8 page_size_shift, component_type_t* container) { return (u16*)(base + ((container->m_page_offset + 0) << page_size_shift)); }
        static u16* get_local_to_global(byte* base, u8 page_size_shift, component_type_t* container)
        {
            const u32 array_num_pages = (64 * cKB) >> page_size_shift;
            return (u16*)(base + ((container->m_page_offset + array_num_pages) << page_size_shift));
        }
        static component_type_t* get_component_type_array(components_t* container) { return (component_type_t*)((byte*)container + sizeof(components_t)); }
        static byte*             get_component_data(byte* base, u8 page_size_shift, component_type_t* container)
        {
            const u32 array_num_pages = (64 * cKB) >> page_size_shift;
            return (base + ((container->m_page_offset + (array_num_pages * 2)) << page_size_shift));
        }

        static bool allocate_component_type(components_t* container, u16 component_type_index, u32 max_component, u32 sizeof_component)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            component_type_t* comp_type_array = get_component_type_array(container);

            // calculate required pages
            const u32 component_data_size = ((max_component * sizeof_component) + (page_size - 1)) & ~(page_size - 1);
            const u32 required_pages      = component_data_size >> page_size_shift;

            // check if we have enough space
            if ((container->m_used_pages + required_pages) > container->m_max_pages)
                return false;

            // setup component type info
            comp_type_array->m_num_components   = 0;
            comp_type_array->m_sizeof_component = (u16)sizeof_component;
            comp_type_array->m_page_offset      = container->m_used_pages;

            // update used pages
            container->m_used_pages += required_pages;

            return true;
        }

        static byte* allocate_component(components_t* container, u16 entity_index, u16 component_type_index)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            component_type_t* comp_type_array = get_component_type_array(container);
            component_type_t& comp_type       = comp_type_array[component_type_index];
            ASSERT(comp_type.m_num_components < D_ECS4_MAX_ENTITIES_PER_CONTAINER);
            ASSERT(comp_type.m_sizeof_component > 0);

            byte* base = container->m_component_data;

            u16* global_to_local = get_global_to_local(base, page_size_shift, &comp_type);
            u16* local_to_global = get_local_to_global(base, page_size_shift, &comp_type);

            // allocate local index
            u16 local_index               = comp_type.m_num_components;
            global_to_local[entity_index] = local_index;
            local_to_global[local_index]  = entity_index;

            comp_type.m_num_components++;
            return get_component_data(base, page_size_shift, &comp_type) + (local_index * comp_type.m_sizeof_component);
        }

        static byte* get_component(components_t* container, u16 entity_index, u16 component_type_index)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            component_type_t* comp_type_array = get_component_type_array(container);
            component_type_t& comp_type       = comp_type_array[component_type_index];

            byte* base = container->m_component_data;

            const u16* global_to_local = get_global_to_local(base, page_size_shift, &comp_type);
            const u16  local_index     = global_to_local[entity_index];
            return get_component_data(base, page_size_shift, &comp_type) + (local_index * comp_type.m_sizeof_component);
        }

        // With 256 components and an average size of 32 bytes per component, this gives us:
        // Component data: 32768 * 32 = 1 MB per component, for 256 components = 256 MB
        // Global to local: 256 * 128 KB = 32 MB
        // Local to global: 256 * 128 KB = 32 MB
        // Total: 320 MB

        static components_t* s_components_create(u32 max_component_types, u32 average_component_count, u32 average_component_size)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            const u32 component_data_size = ((average_component_count * average_component_size) + (page_size - 1)) & ~(page_size - 1);

            int_t address_space_size = sizeof(components_t);
            address_space_size += max_component_types * sizeof(component_type_t);
            address_space_size += ((64 * cKB) + (64 * cKB)); // global to local + local to global

            // reserve virtual address space
            void* base = v_alloc_reserve(address_space_size + (component_data_size * max_component_types));
            if (base == nullptr)
                return nullptr;

            // commit first page containing components_t and component_type_t array
            v_alloc_commit(base, page_size);

            components_t* container = (components_t*)base;
            g_memclr(container, sizeof(components_t));

            container->m_used_pages     = 0;
            container->m_max_pages      = (component_data_size * max_component_types) >> page_size_shift;
            container->m_component_data = (byte*)base + address_space_size;

            return container;
        }

        static void s_components_teardown(components_t* container)
        {
            // todo
        }

        ecs_t* g_create_ecs(u32 max_component_types, u32 max_tag_types)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            int_t free_bin1_offset                 = -1;
            int_t alive_bin1_offset                = -1;
            int_t free_bin2_offset                 = -1;
            int_t alive_bin2_offset                = -1;
            int_t bin3_offset                      = -1;
            int_t generation_array_offset          = -1;
            int_t component_occupancy_array_offset = -1;
            int_t tags_array_offset                = -1;

            int_t mem_size                   = sizeof(components_t);
            mem_size                         = mem_size + (32 * sizeof(components_t*));                                        // m_component_containers
            free_bin1_offset                 = mem_size;                                                                       // record offset of free_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                                     // free_bin1
            alive_bin1_offset                = mem_size;                                                                       // record offset of alive_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                                     // alive_bin1
            free_bin2_offset                 = mem_size;                                                                       // record offset of free_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                                                // free_bin2
            alive_bin2_offset                = mem_size;                                                                       // record offset of alive_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                                                // alive_bin2
            bin3_offset                      = mem_size;                                                                       // record offset of bin3
            mem_size                         = mem_size + ((32 * 32 * 32 * 32) / 8);                                           // bin3
            mem_size                         = ((mem_size + (page_size - 1)) & ~(page_size - 1));                              // align to page size
            generation_array_offset          = mem_size;                                                                       // record offset of generation_array
            mem_size                         = mem_size + (1 * cMB);                                                           // entity_generation_array
            mem_size                         = ((mem_size + (page_size - 1)) & ~(page_size - 1));                              // align to page size
            component_occupancy_array_offset = mem_size;                                                                       // record offset of component_occupancy_array
            mem_size                         = mem_size + ((max_component_types + 7) / 8) * D_ECS4_MAX_ENTITIES_PER_CONTAINER; // entity_component_occupancy_array
            mem_size                         = ((mem_size + (page_size - 1)) & ~(page_size - 1));                              // align to page size
            tags_array_offset                = mem_size;                                                                       // record offset of tags_array
            mem_size                         = mem_size + ((max_tag_types + 7) / 8) * D_ECS4_MAX_ENTITIES_PER_CONTAINER;       // entity_tags_array
            mem_size                         = ((mem_size + (page_size - 1)) & ~(page_size - 1));                              // align to page size

            void* base = v_alloc_reserve(mem_size);
            if (base == nullptr)
                return nullptr;

            // commit size that contains ecs_t, binmap arrays and part of bin3
            const u32 pages_to_commit = (bin3_offset + (32 * 4) + (page_size - 1)) >> page_size_shift;
            v_alloc_commit(base, (int_t)pages_to_commit << page_size_shift);
            g_memclr(base, (int_t)pages_to_commit << page_size_shift);

            ecs_t* ecs                              = (ecs_t*)base;
            ecs->m_max_component_containers         = 32;
            ecs->m_component_bytes_per_entity       = (u8)((max_component_types + 7) / 8);
            ecs->m_tag_bytes_per_entity             = (u8)((max_tag_types + 7) / 8);
            ecs->m_entity_free_bin1                 = (u32*)((byte*)ecs + free_bin1_offset);
            ecs->m_entity_alive_bin1                = (u32*)((byte*)ecs + alive_bin1_offset);
            ecs->m_entity_free_bin2                 = (u32*)((byte*)ecs + free_bin2_offset);
            ecs->m_entity_alive_bin2                = (u32*)((byte*)ecs + alive_bin2_offset);
            ecs->m_entity_bin3                      = (u32*)((byte*)ecs + bin3_offset);
            ecs->m_per_entity_data                  = (byte*)ecs + tags_array_offset;
            ecs->m_entity_generation_array          = (byte*)ecs + generation_array_offset;
            ecs->m_entity_component_occupancy_array = (byte*)ecs + component_occupancy_array_offset;
            ecs->m_entity_tags_array                = (byte*)ecs + tags_array_offset;
            ecs->m_components_array                 = (components_t**)((byte*)ecs + sizeof(ecs_t));

            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            // todo
        }

    } // namespace necs4
} // namespace ncore
