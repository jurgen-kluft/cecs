#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_arena.h"
#include "ccore/c_bin.h"
#include "ccore/c_debug.h"
#include "ccore/c_duomap1.h"
#include "ccore/c_memory.h"
#include "cbase/c_integer.h"

#include "cecs/c_ecs4.h"

namespace ncore
{
    namespace necs4
    {
        // ecs4: Entity Component System, version 4
        // Using virtual memory
        // Note: Using bin_t from ccore, each bin being a component container
        // Note: For ensuring contiguous memory usage of a bin, we can introduce an additional function that
        //       the user can call that will defragment or compact the memory within each bin. We could give
        //       some parameters for the user to control the defragmentation process and performance timing.
        //       Steps
        //        - For each component bin, find last empty, remember this index
        //        - Iterate over all entities
        //          - For each entity iterate over its component, check the index against the 'empty index',
        //            if higher than swap them.

        // Occupancy:
        // - Total maximum component types = 256
        // - Per entity maximum components = 64
        // Entity component occupancy works as follows:
        // - u64[4], 256 bits, each bit indicating if the component is registered for this entity
        // - u32[64], each u32 is a [u8 (component type), u24(index in component bin)]

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        struct component_type_t;
        struct component_shard_t;

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u8    m_max_component_bins;                // Maximum number of component containers
            u8    m_num_component_bins;                // Current number of component containers
            u8    m_tag_bytes_per_entity;              // Number of bytes for tags per entity
            u8    m_components_per_entity;             // Per entity, how many components can we have (max 250)
            u8    m_base_cur_pages;                    // Current number of pages allocated for ecs base data
            u8    m_base_max_pages;                    // Maximum number of pages reserved for ecs base data
            u8    m_generation_array_cur_pages;        // Current number of pages allocated for entity generation array
            u8    m_generation_array_max_pages;        // Maximum number of pages reserved for entity generation array
            u8    m_occupancy_array_cur_pages;         // Current number of pages allocated for entity component occupancy array
            u8    m_occupancy_array_max_pages;         // Maximum number of pages reserved for entity component occupancy array
            u8    m_tags_array_cur_pages;              // Current number of pages allocated for entity tags array
            u8    m_tags_array_max_pages;              // Maximum number of pages reserved for entity tags array
            u32   m_entity_bin3_max_index;             // When entity index reaches this, we need to grow it
            u32   m_entity_generation_array_max_index; // When entity index reaches this, we need to grow it
            u32   m_entity_occupancy_array_max_index;  // When entity index reaches this, we need to grow it
            u32   m_entity_tags_array_max_index;       // When entity index reaches this, we need to grow it
            u32   m_entity_free_index;                 // First free entity index
            u32   m_entity_alive_count;                // Number of alive entities
            u32   m_entity_free_bin0;                  // 32 * 32 * 32 = 32768 entities
            u32   m_entity_alive_bin0;                 // 32 * 32 * 32 = 32768 entities
            u32*  m_entity_free_bin1;                  // Track the 0 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*  m_entity_alive_bin1;                 // Track the 1 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*  m_entity_free_bin2;                  // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*  m_entity_alive_bin2;                 // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*  m_entity_bin3;                       // 32 * 32 * 32 * 32 = maximum 1,048,576 entities (growing)
            byte* m_entity_generation_array;           // Pointer to generation array (page aligned and growing)
            byte* m_entity_component_occupancy_array;  // Pointer to component occupancy bits array (page aligned and growing)
            u32*  m_entity_component_reference_array;  // Pointer to component reference array (page aligned and growing)
            byte* m_entity_tags_array;                 // Pointer to tags bits array (page aligned and growing)
        };
        // ecs_t is followed in memory by:
        // component_shard_t                    m_component_shards[m_max_components];      // Array of component shards
        // u32                                  m_entity_free_bin1[1024 / 32];             // 1024 bits = 128 bytes
        // u32                                  m_entity_alive_bin1[1024 / 32];            // 1024 bits = 128 bytes
        // u32                                  m_entity_free_bin2[32768 / 32];            // 32768 bits = 4 KB
        // u32                                  m_entity_alive_bin2[32768 / 32];           // 32768 bits = 4 KB
        // u32                                  m_entity_bin3[1*cMB / 32];                 // 1 MB = 32 * 32 * 32 * 32 bits = 1,048,576 bits = 128 KB
        // {page aligned} {1 byte per entity}   m_entity_generation_array[1*cMB];          // Pointer to generation array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_entity_component_occupancy_array[1*cMB]; // Pointer to component occupancy bits array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_entity_tags_array[1*cMB];                // Pointer to tags bits array (page aligned and growing)

#define D_ECS4_MAX_ENTITIES_PER_CONTAINER (32768) // 32 * 32 * 32 = 32768 entities

        // Component Type, holds static info about a single component type
        struct component_type_t
        {
            u16 m_sizeof_component; // size of a single component
            u16 m_max_components;   // maximum number of components allocated
            u32 m_page_offset;      // offset in pages from the start of component data
        };

        // Component, holds dynamic info and data for a single component type
        // Each 'component_shard_t' contains an array of 'component_data_t' for each component type
        struct component_data_t
        {
            u8  m_local_to_global_pages; // number of pages committed for local_to_global
            u8  m_global_to_local_pages; // number of pages committed for global_to_local
            u16 m_num_components;        // number of components allocated
            u32 m_data_pages;            // number of pages committed for component data
        };

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // The actual component data exists in virtual memory address space for max 32768 entities
        struct component_shard_t
        {
            u32   m_used_pages; // Current size of the component data (in pages)
            u32   m_max_pages;  // Maximum size of the component data (in pages)
            byte* m_data;       // Data for all components (virtual memory)
        };
        // component_shard_t         is followed in memory by:
        // component_data_t          m_component_data[ max_component_types ]; // Array of components
        // {page aligned}            data virtual memory
        // [
        //      u16*  m_global_to_local; // 64 cKB (max entities)
        //      u16*  m_local_to_global; // 64 cKB (max components)
        //      byte* m_data;            // max_components * component size
        // ]
        // --------------------------------------------------------------------------------------------------------

        static byte* get_component_base(component_shard_t* container, component_type_t* types, u8 page_size_shift, u32 component_type_index)
        {
            const component_type_t& comp_type = types[component_type_index];
            return container->m_data + ((int_t)comp_type.m_page_offset << page_size_shift);
        }
        static inline component_data_t* get_component_data_array(component_shard_t* container) { return (component_data_t*)(container + 1); }
        static inline component_data_t* get_component_data(component_shard_t* container, u32 component_type_index)
        {
            component_data_t* comp_array = get_component_data_array(container);
            return &comp_array[component_type_index];
        }
        static inline u16* get_global_to_local(byte* comp_base) { return (u16*)(comp_base); }
        static inline u16* get_local_to_global(byte* comp_base) { return (u16*)(comp_base + (64 * cKB)); }
        static byte*       get_component_data(byte* comp_base) { return comp_base + (128 * cKB); }

        static bool allocate_component_type(component_type_t* types, component_shard_t* container, u16 component_type_index, u32 max_component, u32 sizeof_component)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            // calculate required pages
            const u32 component_data_size = ((max_component * sizeof_component) + (page_size - 1)) & ~(page_size - 1);
            const u32 required_pages      = component_data_size >> page_size_shift;

            // check if we have enough space
            if ((container->m_used_pages + required_pages) > container->m_max_pages)
                return false;

            // setup component type and data
            types[component_type_index].m_sizeof_component = (u16)sizeof_component;
            types[component_type_index].m_page_offset      = container->m_used_pages;

            component_data_t* comp_data = get_component_data(container, component_type_index);
            comp_data->m_num_components = 0;

            // update used pages
            container->m_used_pages += required_pages;

            return true;
        }

        static byte* alloc_component(ecs_t* ecs, component_shard_t* container, u16 entity_index, u16 component_type_index)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            component_type_t* comp_type_array = ecs->m_component_type_array;
            component_type_t& comp_type       = comp_type_array[component_type_index];
            component_data_t* comp_data       = get_component_data(container, component_type_index);
            ASSERT(comp_type.m_num_components < D_ECS4_MAX_ENTITIES_PER_CONTAINER);
            ASSERT(comp_type.m_sizeof_component > 0);

            byte* base      = container->m_data;
            byte* comp_base = get_component_base(container, comp_type_array, page_size_shift, component_type_index);

            u16* global_to_local = get_global_to_local(comp_base);
            u16* local_to_global = get_local_to_global(comp_base);

            // allocate local index
            u16 local_index               = comp_data->m_num_components;
            global_to_local[entity_index] = local_index;
            local_to_global[local_index]  = entity_index;

            comp_data->m_num_components++;
            byte* cp_data = get_component_data(comp_base);
            return cp_data + (local_index * comp_type.m_sizeof_component);
        }

        static void free_component(component_shard_t* container, u16 entity_index, u16 component_type_index)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            component_t* comp_type_array = get_component_type_array(container);
            component_t& comp_type       = comp_type_array[component_type_index];

            byte* base            = container->m_component_data;
            byte* comp_base       = get_component_base(container, page_size_shift, component_type_index);
            u16*  global_to_local = get_global_to_local(comp_base);
            u16*  local_to_global = get_local_to_global(comp_base);

            const u16 local_index = global_to_local[entity_index];
            const u16 last_index  = comp_type.m_num_components - 1;

            if (local_index != last_index)
            {
                // move last component to freed spot
                byte* comp_data       = get_component_data(comp_base);
                byte* dest_comp_ptr   = comp_data + (local_index * comp_type.m_sizeof_component);
                byte* source_comp_ptr = comp_data + (last_index * comp_type.m_sizeof_component);
                g_memcpy(dest_comp_ptr, source_comp_ptr, comp_type.m_sizeof_component);

                // update moved component's indices
                const u16 moved_entity_index        = local_to_global[last_index];
                global_to_local[moved_entity_index] = local_index;
                local_to_global[local_index]        = moved_entity_index;
            }

            // clear last component's indices
            global_to_local[entity_index] = 0xFFFF;
            local_to_global[last_index]   = 0xFFFF;

            comp_type.m_num_components--;
        }

        static byte* get_component(component_shard_t* container, u16 component_type_index, u16 entity_index)
        {
            const u8 page_size_shift = v_alloc_get_page_size_shift();

            const component_t* comp_type_array = get_component_type_array(container);
            const component_t& comp_type       = comp_type_array[component_type_index];

            byte*      base            = container->m_component_data;
            byte*      comp_base       = get_component_base(container, page_size_shift, component_type_index);
            const u16* global_to_local = get_global_to_local(comp_base);

            const u16 local_index = global_to_local[entity_index];
            return get_component_data(comp_base) + (local_index * comp_type.m_sizeof_component);
        }

        // Example calculation of memory usage:
        //    With 256 components and an average size of 32 bytes per component, this gives us:
        //    Component data: 32 KiB * 32 = 1 MB per component, for 256 components = 256 MB
        //    Global to local: 256 * 128 KB = 32 MB
        //    Local to global: 256 * 128 KB = 32 MB
        //    Total: 320 MB
        static inline int_t s_align_to(u32 page_size, int_t size) { return (size + (page_size - 1)) & ~(page_size - 1); }

        // A single component container size calculation, such a component container holds data for a single component type and
        // exists within a larger component_shard_t which holds multiple component containers.
        static inline int_t s_component_container_calc_size(u32 page_size, u32 average_component_count, u32 average_component_size)
        {
            const int_t sizeof_component_data = (average_component_count * average_component_size);
            return (64 * cKB) + (64 * cKB) + s_align_to(page_size, sizeof_component_data);
        }

        // Calculate the size of a component_shard_t including the array of component containers and the component data virtual memory.
        static inline int_t s_components_container_calc_size(u32 page_size, u32 max_component_types, u32 average_component_count, u32 average_component_size)
        {
            const int_t sizeof_components_container = s_align_to(page_size, sizeof(component_shard_t) + (max_component_types * sizeof(component_t)));
            const int_t sizeof_component_type       = s_component_container_calc_size(page_size, average_component_count, average_component_size);
            return sizeof_components_container + (max_component_types * sizeof_component_type);
        }
        static inline int_t s_components_container_base_size(u32 page_size, u32 max_component_types) { return s_align_to(page_size, sizeof(component_shard_t) + (max_component_types * sizeof(component_t))); }

        static component_shard_t* s_components_create(u32 max_component_types, u32 average_component_count, u32 average_component_size)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            // compute an estimated size of component data needed
            const u32   component_container_size       = s_component_container_calc_size(page_size, average_component_count, average_component_size);
            const int_t components_container_base_size = s_components_container_base_size(page_size, max_component_types);

            // reserve virtual address space for base size + component data
            void* base = v_alloc_reserve(components_container_base_size + (component_container_size * max_component_types));
            if (base == nullptr)
                return nullptr;

            // commit first page containing struct component_shard_t and array component_t[]
            v_alloc_commit(base, page_size);

            component_shard_t* container = (component_shard_t*)base;
            g_memclr(container, sizeof(component_shard_t));

            container->m_used_pages     = 0;
            container->m_max_pages      = (component_container_size * max_component_types) >> page_size_shift;
            container->m_component_data = (byte*)base + components_container_base_size;

            return container;
        }

        static void s_components_teardown(component_shard_t* container)
        {
            const u8    page_size_shift = v_alloc_get_page_size_shift();
            const int_t size            = (container->m_component_data - (byte*)container) + ((container->m_max_pages) << page_size_shift);
            v_alloc_release(container, size);
        }

        ecs_t* g_create_ecs(u32 max_entities, u32 max_component_types, u32 max_tag_types, u32 average_component_count, u32 average_component_size)
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

            int_t mem_size                   = sizeof(component_shard_t);
            mem_size                         = mem_size + (max_component_types * sizeof(component_type_t));  // component type array
            free_bin1_offset                 = mem_size;                                                     // record offset of free_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                   // free_bin1
            alive_bin1_offset                = mem_size;                                                     // record offset of alive_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                   // alive_bin1
            free_bin2_offset                 = mem_size;                                                     // record offset of free_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                              // free_bin2
            alive_bin2_offset                = mem_size;                                                     // record offset of alive_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                              // alive_bin2
            bin3_offset                      = mem_size;                                                     // record offset of bin3
            mem_size                         = mem_size + ((32 * 32 * 32 * 32) / 8);                         // bin3
            mem_size                         = s_align_to(page_size, mem_size);                              // align to page size
            generation_array_offset          = mem_size;                                                     // record offset of generation_array
            mem_size                         = mem_size + (1 * cMB);                                         // entity_generation_array
            mem_size                         = s_align_to(page_size, mem_size);                              // align to page size
            component_occupancy_array_offset = mem_size;                                                     // record offset of component_occupancy_array
            mem_size                         = mem_size + max_entities * s_align_to(8, max_component_types); // entity_component_occupancy_array
            mem_size                         = s_align_to(page_size, mem_size);                              // align to page size
            tags_array_offset                = mem_size;                                                     // record offset of tags_array
            mem_size                         = mem_size + max_entities * s_align_to(8, max_tag_types);       // entity_tags_array
            mem_size                         = s_align_to(page_size, mem_size);                              // align to page size

            const int_t max_components_containers = (max_entities + (D_ECS4_MAX_ENTITIES_PER_CONTAINER - 1)) / D_ECS4_MAX_ENTITIES_PER_CONTAINER;

            // // Also include the address space that can hold all component containers
            // const int_t sizeof_components_container = s_components_container_calc_size(page_size, max_component_types, average_component_count, average_component_size);
            // void* base = v_alloc_reserve(mem_size + (sizeof_components_container * max_components_containers));

            void* base = v_alloc_reserve(mem_size);
            if (base == nullptr)
                return nullptr;

            // commit size that contains ecs_t, binmap arrays and part of bin3
            const u32 pages_to_commit = (bin3_offset + (32 * 4) + (page_size - 1)) >> page_size_shift;
            v_alloc_commit(base, (int_t)pages_to_commit << page_size_shift);
            g_memclr(base, (int_t)pages_to_commit << page_size_shift);

            ecs_t* ecs                              = (ecs_t*)base;
            ecs->m_max_component_bins               = (u8)max_components_containers;
            ecs->m_component_bytes_per_entity       = (u8)((max_component_types + 7) / 8);
            ecs->m_tag_bytes_per_entity             = (u8)((max_tag_types + 7) / 8);
            ecs->m_entity_free_bin1                 = (u32*)((byte*)ecs + free_bin1_offset);
            ecs->m_entity_alive_bin1                = (u32*)((byte*)ecs + alive_bin1_offset);
            ecs->m_entity_free_bin2                 = (u32*)((byte*)ecs + free_bin2_offset);
            ecs->m_entity_alive_bin2                = (u32*)((byte*)ecs + alive_bin2_offset);
            ecs->m_entity_bin3                      = (u32*)((byte*)ecs + bin3_offset);
            ecs->m_entity_generation_array          = (byte*)ecs + generation_array_offset;
            ecs->m_entity_component_occupancy_array = (byte*)ecs + component_occupancy_array_offset;
            ecs->m_entity_tags_array                = (byte*)ecs + tags_array_offset;

            const int_t bin3_size_in_bytes           = (pages_to_commit << page_size_shift) - bin3_offset;
            ecs->m_entity_bin3_max_index             = bin3_size_in_bytes * 8;
            ecs->m_entity_generation_array_max_index = page_size / sizeof(byte);
            ecs->m_entity_occupancy_array_max_index  = page_size / ecs->m_component_bytes_per_entity;
            ecs->m_entity_tags_array_max_index       = page_size / ecs->m_tag_bytes_per_entity;

            ecs->m_base_cur_pages             = (u8)pages_to_commit;
            ecs->m_base_max_pages             = (u8)(generation_array_offset >> page_size_shift);
            ecs->m_generation_array_cur_pages = 1;
            ecs->m_generation_array_max_pages = (u8)((component_occupancy_array_offset - generation_array_offset) >> page_size_shift);
            ecs->m_occupancy_array_cur_pages  = 1;
            ecs->m_occupancy_array_max_pages  = (u8)((tags_array_offset - component_occupancy_array_offset) >> page_size_shift);
            ecs->m_tags_array_cur_pages       = 1;
            ecs->m_tags_array_max_pages       = (u8)((mem_size - tags_array_offset) >> page_size_shift);

            if (!v_alloc_commit(ecs->m_entity_generation_array, page_size))
            {
                v_alloc_release(ecs, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_entity_component_occupancy_array, page_size))
            {
                v_alloc_release(ecs, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_entity_tags_array, page_size))
            {
                v_alloc_release(ecs, mem_size);
                return nullptr;
            }

            return ecs;
        }

        static void s_grow(ecs_t* ecs, u32 entity_index)
        {
            const u32 page_size       = v_alloc_get_page_size();
            const u8  page_size_shift = v_alloc_get_page_size_shift();

            if (entity_index & (D_ECS4_MAX_ENTITIES_PER_CONTAINER - 1))
            {
                // need to add a new components container
                ASSERT(ecs->m_num_component_bins < ecs->m_max_component_bins);
                component_shard_t* current         = ecs->m_components_array[ecs->m_num_component_bins];
                const u32          container_index = ecs->m_num_component_bins++;
                component_shard_t* container       = s_components_create(256, 128, 32); // hardcoded for now
                ASSERT(container != nullptr);
                ecs->m_components_array[container_index] = container;
            }

            if (entity_index > ecs->m_entity_bin3_max_index)
            {
                byte* address = (byte*)ecs + ((int_t)ecs->m_base_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_base_cur_pages++;
                ecs->m_entity_bin3_max_index += (page_size * 8);
            }
            if (entity_index > ecs->m_entity_generation_array_max_index)
            {
                byte* address = ecs->m_entity_generation_array + ((int_t)ecs->m_generation_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_generation_array_cur_pages++;
                ecs->m_entity_generation_array_max_index += (page_size / sizeof(byte));
            }
            if (entity_index > ecs->m_entity_occupancy_array_max_index)
            {
                byte* address = ecs->m_entity_component_occupancy_array + ((int_t)ecs->m_occupancy_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_occupancy_array_cur_pages++;
                ecs->m_entity_occupancy_array_max_index += (page_size / ecs->m_component_bytes_per_entity);
            }
            if (entity_index > ecs->m_entity_tags_array_max_index)
            {
                byte* address = ecs->m_entity_tags_array + ((int_t)ecs->m_tags_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_tags_array_cur_pages++;
                ecs->m_entity_tags_array_max_index += (page_size / ecs->m_tag_bytes_per_entity);
            }
        }

        static entity_t s_create_entity(ecs_t* ecs)
        {
            // check if we need to grow:
            // - bin3
            // - entity tags array
            // - entity generation array
            // - entity component occupancy array
            // - adding a components container if needed

            if (ecs->m_entity_alive_count < ecs->m_entity_free_index)
            {
                // The hierarchical bitmaps can be used to find a free entity index
                const s32 bit =
                    nduomap20::find0_and_set(&ecs->m_entity_free_bin0, ecs->m_entity_free_bin1, ecs->m_entity_free_bin2, &ecs->m_entity_alive_bin0, ecs->m_entity_alive_bin1, ecs->m_entity_alive_bin2, ecs->m_entity_bin3, ecs->m_entity_free_index);
            }
            else
            {
                const s32 bit = ecs->m_entity_free_index++;
                s_grow(ecs, ecs->m_entity_free_index);

                // mark entity as alive in the bitmaps
                nduomap20::set(&ecs->m_entity_free_bin0, ecs->m_entity_free_bin1, ecs->m_entity_free_bin2, &ecs->m_entity_alive_bin0, ecs->m_entity_alive_bin1, ecs->m_entity_alive_bin2, ecs->m_entity_bin3, ecs->m_entity_free_index, bit);
            }

            return 0;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            // todo
        }

        entity_t g_create_entity(ecs_t* ecs)
        {
            // todo
            return 0;
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            // todo
        }

        bool g_register_component(ecs_t* ecs, u32 cp_index, s32 cp_sizeof, s32 cp_alignof = 8)
        {
            // align cp_sizeof to cp_alignof
            s32 aligned_cp_sizeof = (cp_sizeof + (cp_alignof - 1)) & ~(cp_alignof - 1);
        }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            // todo
            return false;
        }

        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            // todo
            return nullptr;
        }

        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            // todo
        }

        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            // todo
            return nullptr;
        }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            // todo
            return false;
        }
        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            // todo
        }
        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            // todo
        }

    } // namespace necs4
} // namespace ncore
