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

        // Occupancy:
        // What if we have a binmap per component type, where each entity (index) can have the bit 0 or 1.
        // This makes iteration over entities with a specific component type easy, as we can just iterate
        // multiple bitmaps to find entities that have all the required components.
        // Perhaps we should make a re-usable binmap that uses virtual memory, e.g.:
        // v_binmap_t (20):
        //   - uses virtual memory
        //   - free_index               u32
        //   - count                    u32
        //   - bin 0                    u32
        //   - bin 2 offset             u32
        //   - bin 3 offset             u32
        //   - bin 1 (u32[32])          32 * 32 = 1024 bits, 128 bytes
        //   - bin 2 (u32[1024 / 32])   32 * 32 * 32 = 32768 bits, 4 KB
        //   - bin 3 (growing)          32 * 32 * 32 * 32 = 1,048,576 bits, 128 KB

        // Entity Component:
        // There are 2 ways:
        // - component[max component types] = offset in bin
        // - {bin index, offset in bin}[max components per entity]
        // The first method is easier to implement, but uses more memory per entity.
        // The second method is more complex to implement, but uses less memory per entity.

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            nbin::bin_t** m_component_bins;             // Array of component bins
            u16           m_max_component_types;        // Maximum number of component bins
            u16           m_num_component_bins;         // Current number of component bins
            u16           m_info_bytes_per_entity;      // Number of bytes for info (gen + component bits) per entity
            u16           m_occupancy_bytes_per_entity; // Number of bytes for component occupancy per entity
            u16           m_reference_bytes_per_entity; // Number of bytes for component references per entity
            u16           m_tag_bytes_per_entity;       // Number of bytes for tags per entity
            u16           m_base_cur_pages;             // Current number of pages allocated for ecs base data
            u16           m_base_max_pages;             // Maximum number of pages reserved for ecs base data
            u16           m_info_array_cur_pages;       // Current number of pages allocated for entity info array
            u16           m_info_array_max_pages;       // Maximum number of pages reserved for entity info array
            u16           m_occupancy_array_cur_pages;  // Current number of pages allocated for entity component occupancy array
            u16           m_occupancy_array_max_pages;  // Maximum number of pages reserved for entity component occupancy array
            u16           m_indexed_array_cur_pages;    // Current number of pages allocated for entity component reference array
            u16           m_indexed_array_max_pages;    // Maximum number of pages reserved for entity component reference array
            u16           m_reference_array_cur_pages;  // Current number of pages allocated for entity component reference array
            u16           m_reference_array_max_pages;  // Maximum number of pages reserved for entity component reference array
            u16           m_tags_array_cur_pages;       // Current number of pages allocated for entity tags array
            u16           m_tags_array_max_pages;       // Maximum number of pages reserved for entity tags array
            u32           m_bin3_max_index;             // When entity index reaches this, we need to grow it
            u32           m_info_array_max_index;       // When entity index reaches this, we need to grow it
            u32           m_occupancy_array_max_index;  // When entity index reaches this, we need to grow it
            u32           m_indexed_array_max_index;    // When entity index reaches this, we need to grow it
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
            byte*         m_info_array;                 // Pointer to info (generation + component count) array (page aligned and growing)
            byte*         m_cp_occupancy_array;         // Pointer to component occupancy bits array (page aligned and growing)
            byte*         m_cp_indexed_array;           // Pointer to component indexed array (page aligned and growing)
            u32*          m_cp_reference_array;         // Pointer to component reference array (page aligned and growing)
            byte*         m_tags_array;                 // Pointer to tags bits array (page aligned and growing)
        };
        // ecs_t is followed in memory by:
        // bin_t*                               m_component_bins[m_max_component_types];   // Array of component bins
        // u32                                  m_free_bin1[1024 / 32];             // 1024 bits = 128 bytes
        // u32                                  m_alive_bin1[1024 / 32];            // 1024 bits = 128 bytes
        // u32                                  m_free_bin2[32768 / 32];            // 32768 bits = 4 KB
        // u32                                  m_alive_bin2[32768 / 32];           // 32768 bits = 4 KB
        // u32                                  m_bin3[1*cMB / 32];                 // 1 MB = 32 * 32 * 32 * 32 bits = 1,048,576 bits = 128 KB
        // {page aligned} {2 bytes per entity}  m_info_array[1*cMB];                // Pointer to info array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_cp_occupancy_array[1*cMB];        // Pointer to component occupancy bits array (page aligned and growing)
        // {page aligned} {4 bytes per entity}  m_cp_indexed_array[1*cMB];          // Pointer to component indexed array (page aligned and growing)
        // {page aligned} {4 bytes per entity}  m_cp_reference_array[1*cMB];        // Pointer to component reference array (page aligned and growing)
        // {page aligned} {N bytes per entity}  m_tags_array[1*cMB];                // Pointer to tag bits array (page aligned and growing)

        static bool s_register_component_type(ecs_t* ecs, u16 component_type_index, u32 max_component_count, u32 sizeof_component)
        {
            ASSERT(component_type_index < ecs->m_max_component_types);
            if (ecs->m_component_bins[component_type_index] != nullptr)
                return false; // already registered
            ecs->m_component_bins[component_type_index] = nbin::make_bin(sizeof_component, max_component_count);
            return true;
        }

        static byte* s_alloc_component(ecs_t* ecs, u32 entity_index, u16 component_type_index)
        {
            ASSERT(component_type_index < ecs->m_max_component_types);
            ASSERT(entity_index < ecs->m_free_index);

            byte*     occupancy_bits = ecs->m_cp_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity);
            const u32 bit_index      = component_type_index;
            const u32 byte_index     = bit_index >> 3;
            const u8  bit_mask       = (1 << (bit_index & 7));

            nbin::bin_t* cp_bin = ecs->m_component_bins[component_type_index];
            if (occupancy_bits[byte_index] & bit_mask)
            {
                // already allocated, get the component pointer from the reference array
                const u8   cp_local_index     = ecs->m_cp_indexed_array[entity_index * ecs->m_max_component_types + component_type_index];
                const u32* cp_reference_array = ecs->m_cp_reference_array + (entity_index * (ecs->m_reference_bytes_per_entity >> 2));
                const u32  cp_reference       = cp_reference_array[component_type_index];
                if (cp_local_index < nbin::size(cp_bin))
                {
                    return (byte*)nbin::idx2ptr(cp_bin, cp_reference);
                }
            }
            else
            {
                void* cp_ptr = nbin::alloc(cp_bin);
                if (cp_ptr != nullptr)
                {
                    occupancy_bits[byte_index]             = occupancy_bits[byte_index] | bit_mask;
                    u8& cp_count                           = ecs->m_info_array[entity_index * 2 + 1];
                    u8* cp_indexed_array                   = &ecs->m_cp_indexed_array[entity_index * ecs->m_max_component_types];
                    cp_indexed_array[component_type_index] = cp_count;
                    const u32 cp_reference                 = nbin::ptr2idx(cp_bin, cp_ptr);
                    u32*      cp_reference_array           = ecs->m_cp_reference_array + (entity_index * (ecs->m_reference_bytes_per_entity >> 2));
                    cp_reference_array[cp_count]           = cp_reference;
                    cp_count++;
                    return (byte*)cp_ptr;
                }
            }
            return nullptr;
        }

        static void s_free_component(ecs_t* ecs, u32 entity_index, u16 component_type_index)
        {
            ASSERT(component_type_index < ecs->m_max_component_types);
            ASSERT(entity_index < ecs->m_free_index);

            byte*     occupancy_bits = ecs->m_cp_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity);
            const u32 bit_index      = component_type_index;
            const u32 byte_index     = bit_index >> 3;
            const u8  bit_mask       = (1 << (bit_index & 7));

            nbin::bin_t* cp_bin = ecs->m_component_bins[component_type_index];
            if (occupancy_bits[byte_index] & bit_mask)
            {
                const u8   cp_local_index     = ecs->m_cp_indexed_array[entity_index * ecs->m_max_component_types + component_type_index];
                const u32* cp_reference_array = ecs->m_cp_reference_array + (entity_index * (ecs->m_reference_bytes_per_entity >> 2));
                const u32  cp_reference       = cp_reference_array[component_type_index];
                if (cp_local_index < nbin::size(cp_bin))
                {
                    void* cp_ptr = nbin::idx2ptr(cp_bin, cp_reference);
                    nbin::free(cp_bin, cp_ptr);
                    occupancy_bits[byte_index] = occupancy_bits[byte_index] & (~bit_mask);
                }
            }
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

            int_t cp_bins_array_offset      = -1;
            int_t free_bin1_offset          = -1;
            int_t alive_bin1_offset         = -1;
            int_t free_bin2_offset          = -1;
            int_t alive_bin2_offset         = -1;
            int_t bin3_offset               = -1;
            int_t info_array_offset         = -1;
            int_t cp_occupancy_array_offset = -1;
            int_t cp_indexed_array_offset   = -1;
            int_t cp_reference_array_offset = -1;
            int_t tg_array_offset           = -1;

            const u32 info_bytes_per_entity      = (s_align_to(8, (1 + components_per_entity)) >> 3); // +1 for generation byte
            const u32 occupancy_bytes_per_entity = (s_align_to(8, max_component_types) >> 3);         //
            const u32 indexed_bytes_per_entity   = (max_component_types * sizeof(u8));                //
            const u32 reference_bytes_per_entity = (max_component_types * sizeof(u32));               //
            const u32 tag_bytes_per_entity       = (s_align_to(8, max_tag_types) >> 3);               //

            int_t mem_size            = sizeof(ecs_t);                                          // ecs_t
            cp_bins_array_offset      = mem_size;                                               // record offset of component_bins_array
            mem_size                  = mem_size + (max_component_types * sizeof(void*));       // component bin array
            free_bin1_offset          = mem_size;                                               // record offset of free_bin1
            mem_size                  = mem_size + ((32 * 32) / 8);                             // free_bin1
            alive_bin1_offset         = mem_size;                                               // record offset of alive_bin1
            mem_size                  = mem_size + ((32 * 32) / 8);                             // alive_bin1
            free_bin2_offset          = mem_size;                                               // record offset of free_bin2
            mem_size                  = mem_size + ((32 * 32 * 32) / 8);                        // free_bin2
            alive_bin2_offset         = mem_size;                                               // record offset of alive_bin2
            mem_size                  = mem_size + ((32 * 32 * 32) / 8);                        // alive_bin2
            bin3_offset               = mem_size;                                               // record offset of bin3
            mem_size                  = mem_size + ((32 * 32 * 32 * 32) / 8);                   // bin3
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size
            info_array_offset         = mem_size;                                               // record offset of generation_array
            mem_size                  = mem_size + (max_entities * info_bytes_per_entity);      // info array
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size
            cp_occupancy_array_offset = mem_size;                                               // record offset of component occupancy array
            mem_size                  = mem_size + (max_entities * occupancy_bytes_per_entity); // component occupancy array
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size
            cp_indexed_array_offset   = mem_size;                                               // record offset of component indexed array
            mem_size                  = mem_size + (max_entities * indexed_bytes_per_entity);   // component indexed array
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size
            cp_reference_array_offset = mem_size;                                               // record offset of component reference array
            mem_size                  = mem_size + (max_entities * reference_bytes_per_entity); // component reference array
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size
            tg_array_offset           = mem_size;                                               // record offset of tags array
            mem_size                  = mem_size + (max_entities * tag_bytes_per_entity);       // tags array
            mem_size                  = s_align_to(page_size, mem_size);                        // align to page size

            byte* base = (byte*)v_alloc_reserve(mem_size);
            if (base == nullptr)
                return nullptr;

            // commit size that contains ecs_t, binmap arrays and part of bin3
            const u32 pages_to_commit = (bin3_offset + (32 * 4) + (page_size - 1)) >> page_size_shift;
            v_alloc_commit(base, (int_t)pages_to_commit << page_size_shift);
            g_memclr(base, (int_t)pages_to_commit << page_size_shift);

            ecs_t* ecs                        = (ecs_t*)base;
            ecs->m_component_bins             = (nbin::bin_t**)(base + cp_bins_array_offset);
            ecs->m_max_component_types        = max_component_types;
            ecs->m_occupancy_bytes_per_entity = (u16)(occupancy_bytes_per_entity);
            ecs->m_info_bytes_per_entity      = (u16)(info_bytes_per_entity);
            ecs->m_reference_bytes_per_entity = (u16)(reference_bytes_per_entity);
            ecs->m_tag_bytes_per_entity       = (u8)(tag_bytes_per_entity);
            ecs->m_free_bin1                  = (u32*)(base + free_bin1_offset);
            ecs->m_alive_bin1                 = (u32*)(base + alive_bin1_offset);
            ecs->m_free_bin2                  = (u32*)(base + free_bin2_offset);
            ecs->m_alive_bin2                 = (u32*)(base + alive_bin2_offset);
            ecs->m_bin3                       = (u32*)(base + bin3_offset);
            ecs->m_info_array                 = (byte*)base + info_array_offset;
            ecs->m_cp_occupancy_array         = (byte*)base + cp_occupancy_array_offset;
            ecs->m_tags_array                 = (byte*)base + tg_array_offset;

            const int_t bin3_size_in_bytes   = (pages_to_commit << page_size_shift) - bin3_offset;
            ecs->m_bin3_max_index            = bin3_size_in_bytes * 8;
            ecs->m_info_array_max_index      = page_size / ecs->m_info_bytes_per_entity;
            ecs->m_occupancy_array_max_index = page_size / ecs->m_occupancy_bytes_per_entity;
            ecs->m_indexed_array_max_index   = page_size / ecs->m_reference_bytes_per_entity;
            ecs->m_reference_array_max_index = page_size / ecs->m_reference_bytes_per_entity;
            ecs->m_tags_array_max_index      = page_size / ecs->m_tag_bytes_per_entity;

            ecs->m_base_cur_pages            = (u8)pages_to_commit;
            ecs->m_base_max_pages            = (u8)(info_array_offset >> page_size_shift);
            ecs->m_info_array_cur_pages      = 1;
            ecs->m_info_array_max_pages      = (u8)((cp_occupancy_array_offset - info_array_offset) >> page_size_shift);
            ecs->m_occupancy_array_cur_pages = 1;
            ecs->m_occupancy_array_max_pages = (u8)((cp_reference_array_offset - cp_occupancy_array_offset) >> page_size_shift);
            ecs->m_reference_array_cur_pages = 1;
            ecs->m_reference_array_max_pages = (u8)((tg_array_offset - cp_reference_array_offset) >> page_size_shift);
            ecs->m_tags_array_cur_pages      = 1;
            ecs->m_tags_array_max_pages      = (u8)((mem_size - tg_array_offset) >> page_size_shift);

            if (!v_alloc_commit(ecs->m_info_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_cp_occupancy_array, page_size))
            {
                v_alloc_release(base, mem_size);
                return nullptr;
            }
            if (!v_alloc_commit(ecs->m_cp_reference_array, page_size))
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
            if (entity_index > ecs->m_info_array_max_index)
            {
                byte* address = ecs->m_info_array + ((int_t)ecs->m_info_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_info_array_cur_pages++;
                ecs->m_info_array_max_index += (page_size / sizeof(byte));
            }
            if (entity_index > ecs->m_occupancy_array_max_index)
            {
                byte* address = ecs->m_cp_occupancy_array + ((int_t)ecs->m_occupancy_array_cur_pages << page_size_shift);
                v_alloc_commit(address, page_size);
                ecs->m_occupancy_array_cur_pages++;
                ecs->m_occupancy_array_max_index += (page_size / ecs->m_occupancy_bytes_per_entity);
            }
            if (entity_index > ecs->m_reference_array_max_index)
            {
                byte* address = (byte*)ecs->m_cp_reference_array + ((int_t)ecs->m_reference_array_cur_pages << page_size_shift);
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

            ecs->m_info_array[entity_index] = 0;
            g_memclr(ecs->m_info_array + (entity_index * ecs->m_info_bytes_per_entity), ecs->m_info_bytes_per_entity);
            g_memclr(ecs->m_cp_indexed_array + (entity_index * ecs->m_max_component_types), ecs->m_max_component_types);
            g_memclr(ecs->m_cp_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity), ecs->m_occupancy_bytes_per_entity);
            g_memclr(ecs->m_cp_reference_array + (entity_index * ecs->m_reference_bytes_per_entity), ecs->m_reference_bytes_per_entity);
            g_memclr(ecs->m_tags_array + (entity_index * ecs->m_tag_bytes_per_entity), ecs->m_tag_bytes_per_entity);

            ecs->m_alive_count++;
            return s_entity_make(0, (entity_index_t)entity_index);
        }

        static void s_destroy_entity(ecs_t* ecs, u32 entity_index)
        {
            // todo
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

        entity_t g_create_entity(ecs_t* ecs) { return s_create_entity(ecs); }
        void     g_destroy_entity(ecs_t* ecs, entity_t e) { s_destroy_entity(ecs, g_entity_index(e)); }
        bool     g_register_component(ecs_t* ecs, u32 cp_index, s32 cp_sizeof, s32 cp_alignof = 8) { return s_register_component_type(ecs, (u16)cp_index, ecs->m_bin3_max_index, (u32)cp_sizeof); }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u32   entity_index   = g_entity_index(entity);
            const byte* occupancy_bits = ecs->m_cp_occupancy_array + (entity_index * ecs->m_occupancy_bytes_per_entity);
            const u32   bit_index      = cp_index;
            const u32   byte_index     = bit_index >> 3;
            const u8    bit_mask       = (1 << (bit_index & 7));
            return (occupancy_bits[byte_index] & bit_mask) != 0;
        }

        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { return s_alloc_component(ecs, g_entity_index(entity), (u16)cp_index); }
        void  g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index) { s_free_component(ecs, g_entity_index(entity), (u16)cp_index); }
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u32    entity_index      = g_entity_index(entity);
            const byte*  component_indices = ecs->m_cp_indexed_array + (entity_index * ecs->m_occupancy_bytes_per_entity);
            const byte   component_index   = component_indices[cp_index >> 3];
            nbin::bin_t* cp_bin            = ecs->m_component_bins[cp_index];
            if (component_index < nbin::size(cp_bin))
            {
                const u32* cp_reference_array = ecs->m_cp_reference_array + (entity_index * (ecs->m_reference_bytes_per_entity >> 2));
                const u32  cp_reference       = cp_reference_array[cp_index];
                return nbin::idx2ptr(cp_bin, cp_reference);
            }
            return nullptr;
        }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (ecs->m_tag_bytes_per_entity << 3))
                return false;

            byte const* tag_occupancy = &ecs->m_tags_array[g_entity_index(entity) * ecs->m_tag_bytes_per_entity];
            return (tag_occupancy[tg_index >> 3] & (1 << (tg_index & 7))) != 0;
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (ecs->m_tag_bytes_per_entity << 3))
                return;

            byte* tag_occupancy = &ecs->m_tags_array[g_entity_index(entity) * ecs->m_tag_bytes_per_entity];
            tag_occupancy[tg_index >> 3] |= (1 << (tg_index & 7));
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (ecs->m_tag_bytes_per_entity << 3))
                return;

            byte* tag_occupancy = &ecs->m_tags_array[g_entity_index(entity) * ecs->m_tag_bytes_per_entity];
            tag_occupancy[tg_index >> 3] &= ~(1 << (tg_index & 7));
        }

    } // namespace necs4
} // namespace ncore
