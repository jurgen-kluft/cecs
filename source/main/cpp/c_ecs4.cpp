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
        // Description:
        //     - Uses virtual memory
        //     - Uses hierarchical bitmaps for tracking entity alive/free state
        //     - Uses ncore::nbin::bin_t for storing component data
        // Limitations:
        //     - Maximum entities: 1,048,576
        //     - Maximum component types: 1024
        //     - Per entity maximum components: 64
        //
        // Note: Using bin_t from ccore, each bin being a component container
        // Note: For ensuring contiguous memory usage of a bin, we can introduce an additional function that
        //       the user can call that will defragment or compact the memory within each bin. We could give
        //       some parameters for the user to control the defragmentation process and performance timing.
        //       Steps
        //        - For each component bin, find last empty, remember this index
        //        - Iterate over all entities
        //          - For each entity iterate over its component, check the index against the 'empty index',
        //            if higher than swap them.
        //       Control
        //        - How many components to move each call (remembering the last entity index that was processed)
        //        - How many entities to iterate

        // Occupancy (example):
        // - Total maximum component types = 1024
        // - Per entity maximum components = 64
        // Entity component occupancy works as follows:
        // - u64[16], 1024 bits, each bit indicating if the component is registered for this entity
        // - u32[64], each u32 is a [u10 (component type), u22(index in component bin)]

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            nbin::bin_t** m_component_bins;             // Array of component bins
            u16           m_max_component_bins;         // Maximum number of component bins
            u16           m_num_component_bins;         // Current number of component bins
            u16           m_occupancy_bytes_per_entity; // Number of bytes for component occupancy per entity
            u16           m_reference_bytes_per_entity; // Number of bytes for component references per entity
            u16           m_tag_bytes_per_entity;       // Number of bytes for tags per entity
            u16           m_base_cur_pages;             // Current number of pages allocated for ecs base data
            u16           m_base_max_pages;             // Maximum number of pages reserved for ecs base data
            u16           m_generation_array_cur_pages; // Current number of pages allocated for entity generation array
            u16           m_generation_array_max_pages; // Maximum number of pages reserved for entity generation array
            u16           m_occupancy_array_cur_pages;  // Current number of pages allocated for entity component occupancy array
            u16           m_occupancy_array_max_pages;  // Maximum number of pages reserved for entity component occupancy array
            u16           m_reference_array_cur_pages;  // Current number of pages allocated for entity component reference array
            u16           m_reference_array_max_pages;  // Maximum number of pages reserved for entity component reference array
            u16           m_tags_array_cur_pages;       // Current number of pages allocated for entity tags array
            u16           m_tags_array_max_pages;       // Maximum number of pages reserved for entity tags array
            u32           m_bin3_max_index;             // When entity index reaches this, we need to grow it
            u32           m_generation_array_max_index; // When entity index reaches this, we need to grow it
            u32           m_occupancy_array_max_index;  // When entity index reaches this, we need to grow it
            u32           m_reference_array_max_index;  // When entity index reaches this, we need to grow it
            u32           m_tags_array_max_index;       // When entity index reaches this, we need to grow it
            u32           m_free_index;                 // First free entity index
            u32           m_alive_count;                // Number of alive entities
            u32           m_free_bin0;                  // 32 * 32 * 32 = 32768 entities
            u32           m_alive_bin0;                 // 32 * 32 * 32 = 32768 entities
            u32*          m_free_bin1;                  // Track the 0 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*          m_alive_bin1;                 // Track the 1 bits in m_entity_bin2 (32 * sizeof(u32) = 128 bytes)
            u32*          m_free_bin2;                  // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*          m_alive_bin2;                 // '1' bit = alive entity, '0' bit = free entity (32768 bits = 4 KB)
            u32*          m_bin3;                       // 32 * 32 * 32 * 32 = maximum 1,048,576 entities (growing)
            byte*         m_generation_array;           // Pointer to generation array (page aligned and growing)
            byte*         m_component_occupancy_array;  // Pointer to component occupancy bits array (page aligned and growing)
            u32*          m_component_reference_array;  // Pointer to component reference array (page aligned and growing)
            byte*         m_tags_array;                 // Pointer to tags bits array (page aligned and growing)
        };
        // ecs_t is followed in memory by:
        // bin_t*                               m_component_bins[m_max_component_types];   // Array of component bins
        // u32                                  m_free_bin1[1024 / 32];             // 1024 bits = 128 bytes
        // u32                                  m_alive_bin1[1024 / 32];            // 1024 bits = 128 bytes
        // u32                                  m_free_bin2[32768 / 32];            // 32768 bits = 4 KB
        // u32                                  m_alive_bin2[32768 / 32];           // 32768 bits = 4 KB
        // u32                                  m_bin3[1*cMB / 32];                 // 1 MB = 32 * 32 * 32 * 32 bits = 1,048,576 bits = 128 KB
        // {page aligned} {1 byte per entity}   m_generation_array[1*cMB];          // Pointer to generation array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_component_occupancy_array[1*cMB]; // Pointer to component occupancy bits array (page aligned and growing)
        // {page aligned} {4 bytes per entity}  m_component_reference_array[1*cMB]; // Pointer to component reference array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_tags_array[1*cMB];                // Pointer to tags bits array (page aligned and growing)

        static bool register_component_type(ecs_t* ecs, u16 component_type_index, u32 max_component_count, u32 sizeof_component)
        {
            ASSERT(component_type_index < ecs->m_max_component_bins);
            if (ecs->m_component_bins[component_type_index] != nullptr)
                return false; // already registered
            ecs->m_component_bins[component_type_index] = nbin::make_bin(sizeof_component, max_component_count);
            return true;
        }

         static byte* alloc_component(ecs_t* ecs, u32 entity_index, u16 component_type_index)
        {
            // check if we already have this component allocated for this entity, check this
            // in the occupancy bits of the entity
            byte* occupancy_bits = ecs->m_component_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity);
            u32   bit_index      = component_type_index;
            u32   byte_index     = bit_index >> 3;
            u8    bit_mask       = (1 << (bit_index & 7));
            if (occupancy_bits[byte_index] & bit_mask)
            {
                // already allocated, get the component pointer from the reference array
                u32* reference_array  = (u32*)(ecs->m_component_reference_array + (entity_index * ecs->m_reference_bytes_per_entity));
                u32  component_offset = reference_array[component_type_index];

                // todo, get the component data pointer

                return nullptr;
            }

            // todo not yet allocated, allocate from the component bin

            return nullptr;
        }

        static void free_component(ecs_t* ecs, u32 entity_index, u16 component_type_index)
        {
            // todo
        }

        static byte* get_component(ecs_t* ecs, u32 entity_index, u16 component_type_index)
        {
            // todo
            return nullptr;
        }

        // Example calculation of memory usage:
        //    With 256 components and an average size of 32 bytes per component, this gives us:
        //    Component data: 32 KiB * 32 = 1 MB per component, for 256 components = 256 MB
        //    Global to local: 256 * 128 KB = 32 MB
        //    Local to global: 256 * 128 KB = 32 MB
        //    Total: 320 MB
        static inline int_t s_align_to(u32 page_size, int_t size) { return (size + (page_size - 1)) & ~(page_size - 1); }

        ecs_t* g_create_ecs(u32 max_entities, u16 components_per_entity, u16 max_component_types, u16 max_tag_types)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (1 << page_size_shift);

            int_t component_bins_array_offset      = -1;
            int_t free_bin1_offset                 = -1;
            int_t alive_bin1_offset                = -1;
            int_t free_bin2_offset                 = -1;
            int_t alive_bin2_offset                = -1;
            int_t bin3_offset                      = -1;
            int_t generation_array_offset          = -1;
            int_t component_occupancy_array_offset = -1;
            int_t component_reference_array_offset = -1;
            int_t tags_array_offset                = -1;

            int_t mem_size                   = sizeof(ecs_t);                                                         // ecs_t
            component_bins_array_offset      = mem_size;                                                              // record offset of component_bins_array
            mem_size                         = mem_size + (max_component_types * sizeof(void*));                      // component bin array
            free_bin1_offset                 = mem_size;                                                              // record offset of free_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                            // free_bin1
            alive_bin1_offset                = mem_size;                                                              // record offset of alive_bin1
            mem_size                         = mem_size + ((32 * 32) / 8);                                            // alive_bin1
            free_bin2_offset                 = mem_size;                                                              // record offset of free_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                                       // free_bin2
            alive_bin2_offset                = mem_size;                                                              // record offset of alive_bin2
            mem_size                         = mem_size + ((32 * 32 * 32) / 8);                                       // alive_bin2
            bin3_offset                      = mem_size;                                                              // record offset of bin3
            mem_size                         = mem_size + ((32 * 32 * 32 * 32) / 8);                                  // bin3
            mem_size                         = s_align_to(page_size, mem_size);                                       // align to page size
            generation_array_offset          = mem_size;                                                              // record offset of generation_array
            mem_size                         = mem_size + (1 * cMB);                                                  // entity_generation_array
            mem_size                         = s_align_to(page_size, mem_size);                                       // align to page size
            component_occupancy_array_offset = mem_size;                                                              // record offset of component_occupancy_array
            mem_size                         = mem_size + (max_entities * (s_align_to(8, max_component_types) >> 3)); // entity_component_occupancy_array
            mem_size                         = s_align_to(page_size, mem_size);                                       // align to page size
            component_reference_array_offset = mem_size;                                                              // record offset of component_reference_array
            mem_size                         = mem_size + (max_entities * max_component_types * sizeof(u32));         // entity_component_reference_array
            mem_size                         = s_align_to(page_size, mem_size);                                       // align to page size
            tags_array_offset                = mem_size;                                                              // record offset of tags_array
            mem_size                         = mem_size + (max_entities * (s_align_to(8, max_tag_types) >> 3));       // entity_tags_array
            mem_size                         = s_align_to(page_size, mem_size);                                       // align to page size

            byte* base = (byte*)v_alloc_reserve(mem_size);
            if (base == nullptr)
                return nullptr;

            // commit size that contains ecs_t, binmap arrays and part of bin3
            const u32 pages_to_commit = (bin3_offset + (32 * 4) + (page_size - 1)) >> page_size_shift;
            v_alloc_commit(base, (int_t)pages_to_commit << page_size_shift);
            g_memclr(base, (int_t)pages_to_commit << page_size_shift);

            ecs_t* ecs                        = (ecs_t*)base;
            ecs->m_component_bins             = (nbin::bin_t**)(base + component_bins_array_offset);
            ecs->m_max_component_bins         = max_component_types;
            ecs->m_occupancy_bytes_per_entity = (u16)((s_align_to(8, max_component_types)) >> 3);
            ecs->m_reference_bytes_per_entity = (u16)(max_component_types * sizeof(u32));
            ecs->m_tag_bytes_per_entity       = (u8)((max_tag_types + 7) / 8);
            ecs->m_free_bin1                  = (u32*)(base + free_bin1_offset);
            ecs->m_alive_bin1                 = (u32*)(base + alive_bin1_offset);
            ecs->m_free_bin2                  = (u32*)(base + free_bin2_offset);
            ecs->m_alive_bin2                 = (u32*)(base + alive_bin2_offset);
            ecs->m_bin3                       = (u32*)(base + bin3_offset);
            ecs->m_generation_array           = (byte*)base + generation_array_offset;
            ecs->m_component_occupancy_array  = (byte*)base + component_occupancy_array_offset;
            ecs->m_tags_array                 = (byte*)base + tags_array_offset;

            const int_t bin3_size_in_bytes    = (pages_to_commit << page_size_shift) - bin3_offset;
            ecs->m_bin3_max_index             = bin3_size_in_bytes * 8;
            ecs->m_generation_array_max_index = page_size / sizeof(byte);
            ecs->m_occupancy_array_max_index  = page_size / ecs->m_occupancy_bytes_per_entity;
            ecs->m_reference_array_max_index  = page_size / ecs->m_reference_bytes_per_entity;
            ecs->m_tags_array_max_index       = page_size / ecs->m_tag_bytes_per_entity;

            ecs->m_base_cur_pages             = (u8)pages_to_commit;
            ecs->m_base_max_pages             = (u8)(generation_array_offset >> page_size_shift);
            ecs->m_generation_array_cur_pages = 1;
            ecs->m_generation_array_max_pages = (u8)((component_occupancy_array_offset - generation_array_offset) >> page_size_shift);
            ecs->m_occupancy_array_cur_pages  = 1;
            ecs->m_occupancy_array_max_pages  = (u8)((component_reference_array_offset - component_occupancy_array_offset) >> page_size_shift);
            ecs->m_reference_array_cur_pages  = 1;
            ecs->m_reference_array_max_pages  = (u8)((tags_array_offset - component_reference_array_offset) >> page_size_shift);
            ecs->m_tags_array_cur_pages       = 1;
            ecs->m_tags_array_max_pages       = (u8)((mem_size - tags_array_offset) >> page_size_shift);

            if (!v_alloc_commit(ecs->m_generation_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_component_occupancy_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_component_reference_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_tags_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }

            return ecs;
        }

        static void s_grow(ecs_t* ecs, u32 entity_index)
        {
            const u32 page_size       = v_alloc_get_page_size();
            const u8  page_size_shift = v_alloc_get_page_size_shift();

            if (entity_index > ecs->m_bin3_max_index)
            {
                byte* address = (byte*)ecs + ((int_t)ecs->m_base_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_base_cur_pages++;
                ecs->m_bin3_max_index += (page_size * 8);
            }
            if (entity_index > ecs->m_generation_array_max_index)
            {
                byte* address = ecs->m_generation_array + ((int_t)ecs->m_generation_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_generation_array_cur_pages++;
                ecs->m_generation_array_max_index += (page_size / sizeof(byte));
            }
            if (entity_index > ecs->m_occupancy_array_max_index)
            {
                byte* address = ecs->m_component_occupancy_array + ((int_t)ecs->m_occupancy_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_occupancy_array_cur_pages++;
                ecs->m_occupancy_array_max_index += (page_size / ecs->m_occupancy_bytes_per_entity);
            }
            if (entity_index > ecs->m_reference_array_max_index)
            {
                byte* address = (byte*)ecs->m_component_reference_array + ((int_t)ecs->m_reference_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_reference_array_cur_pages++;
                ecs->m_reference_array_max_index += (page_size / ecs->m_reference_bytes_per_entity);
            }
            if (entity_index > ecs->m_tags_array_max_index)
            {
                byte* address = ecs->m_tags_array + ((int_t)ecs->m_tags_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_tags_array_cur_pages++;
                ecs->m_tags_array_max_index += (page_size / ecs->m_tag_bytes_per_entity);
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
            s32 entity_index = -1;
            if (ecs->m_alive_count < ecs->m_free_index)
            {
                // The hierarchical duomap can be used to find a free entity index
                entity_index = nduomap20::find0_and_set(&ecs->m_free_bin0, ecs->m_free_bin1, ecs->m_free_bin2, &ecs->m_alive_bin0, ecs->m_alive_bin1, ecs->m_alive_bin2, ecs->m_bin3, ecs->m_free_index);
            }
            else
            {
                entity_index = ecs->m_free_index++;
                s_grow(ecs, ecs->m_free_index);

                // mark entity as alive in the duomap
                nduomap20::set(&ecs->m_free_bin0, ecs->m_free_bin1, ecs->m_free_bin2, &ecs->m_alive_bin0, ecs->m_alive_bin1, ecs->m_alive_bin2, ecs->m_bin3, ecs->m_free_index, entity_index);
            }

            ecs->m_generation_array[entity_index] = 0;
            g_memclr(ecs->m_component_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity), ecs->m_occupancy_bytes_per_entity);
            g_memclr(ecs->m_component_reference_array + (entity_index * ecs->m_reference_bytes_per_entity), ecs->m_reference_bytes_per_entity);
            g_memclr(ecs->m_tags_array + (entity_index * ecs->m_tag_bytes_per_entity), ecs->m_tag_bytes_per_entity);

            return s_entity_make(0, (entity_index_t)entity_index);
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            for (u16 i = 0; i < ecs->m_num_component_bins; i++)
            {
                if (ecs->m_component_bins[i] != nullptr)
                {
                    nbin::destroy(ecs->m_component_bins[i]);
                    ecs->m_component_bins[i] = nullptr;
                }
            }
            v_alloc_release((byte*)ecs, ((int_t)ecs->m_base_max_pages) << v_alloc_get_page_size_shift());
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
