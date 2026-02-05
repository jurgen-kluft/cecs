#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_arena.h"
#include "ccore/c_bin.h"
#include "ccore/c_debug.h"
#include "ccore/c_duomap1.h"
#include "ccore/c_math.h"
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
        //     - Per entity maximum components: 256
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
        //

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, entity_index_t index) { return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | (index & ECS_ENTITY_INDEX_MASK); }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // ecs shard (128 bytes), maximum of 65536 entities per shard
        // --------------------------------------------------------------------------------------------------------
        namespace nshard
        {

#define ECS_SHARD_MAX_ENTITIES 65536

            struct shard_t
            {
                DCORE_CLASS_PLACEMENT_NEW_DELETE
                nbin16::bin_t** m_component_bins;                    // array of component bins
                arena_t*        m_cp_global_occupancy;               // component global occupancy bits array (u8[])
                arena_t*        m_cp_local_occupancy;                // component local occupancy bits array (u8[])
                arena_t*        m_cp_indexed;                        // component indexed array (u8[])
                arena_t*        m_cp_reference;                      // component reference array (u16[])
                arena_t*        m_tags;                              // tag bits array (u8[])
                u16             m_max_component_types;               // maximum number of component bins
                u16             m_num_component_bins;                // current number of component bins
                u16             m_per_entity_global_occupancy_bytes; // component global occupancy bits array
                u16             m_per_entity_local_occupancy_bytes;  // component local occupancy bits array
                u16             m_per_entity_indexed_bytes;          // component indexed array
                u16             m_per_entity_reference_bytes;        // component reference array
                u16             m_per_entity_tags_bytes;             // tag bits array
                u16             m_padding0;                          // padding for 8 byte alignment
                u32             m_free_index;                        // first free entity index
                u32             m_alive_count;                       // number of alive entities
                u64             m_free_bin0;                         // 16 * 64 * 64 = 65536 entities
                u64             m_alive_bin0;                        // 16 * 64 * 64 = 65536 entities
                u64*            m_free_bin1;                         // track the 0 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
                u64*            m_alive_bin1;                        // track the 1 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
                u64*            m_bin2;                              // '1' bit = alive entity, '0' bit = free entity (65536 bits = 8 KB)
            };
            // ecs_shard_t is followed in memory by:
            // arena_t                              m_cp_global_occupancy;                     // component global occupancy bits array (u8[])
            // arena_t                              m_cp_local_occupancy;                      // component local occupancy bits array (u8[])
            // arena_t                              m_cp_indexed;                              // component indexed array (u8[])
            // arena_t                              m_cp_reference;                            // component reference array (u16[])
            // arena_t                              m_tags;                                    // tag bits array (u8[])
            // bin_t*                               m_component_bins[m_max_component_types];   // Array of component bins
            // u64                                  m_free_bin1[1024 / 64];                    // 1024 bits = 128 B
            // u64                                  m_alive_bin1[1024 / 64];                   // 1024 bits = 128 B
            // u64                                  m_bin2[65536 / 64];                        // 65536 bits = 8 KiB

            static shard_t* s_create(u32 max_component_types, u32 max_components_per_entity, u32 max_tags_per_entity)
            {
                // calculate the address space to reserve for all arenas
                const u32 page_size = v_alloc_get_page_size();

                const int_t max_entities     = ECS_SHARD_MAX_ENTITIES;
                const int_t global_occupancy = math::alignUp((math::alignUp(max_component_types, 8) * max_entities) >> 3, page_size);
                const int_t local_occupancy  = math::alignUp((math::alignUp(max_components_per_entity, 8) * max_entities) >> 3, page_size);
                const int_t indexed_array    = math::alignUp((max_component_types * max_entities), page_size);
                const int_t reference_array  = math::alignUp((sizeof(u16) * max_components_per_entity * max_entities), page_size);
                const int_t tags_array       = math::alignUp((math::alignUp(max_tags_per_entity, 8) * max_entities) >> 3, page_size);
                const int_t shard_size       = math::alignUp(sizeof(shard_t) + sizeof(arena_t) * 5 + sizeof(nbin16::bin_t*) * max_component_types + 8192 + 128 + 128, page_size);
                const int_t total_size       = shard_size + global_occupancy + local_occupancy + indexed_array + reference_array + tags_array;

                void* base_address = v_alloc_reserve(total_size);
                if (base_address == nullptr)
                    return nullptr;

                if (!v_alloc_commit(base_address, shard_size))
                {
                    v_alloc_release(base_address, total_size);
                    return nullptr;
                }

                const int_t free_bin1_offset  = sizeof(shard_t) + sizeof(arena_t) * 5 + sizeof(nbin16::bin_t*) * max_component_types;
                const int_t alive_bin1_offset = free_bin1_offset + 128;
                const int_t bin2_offset       = alive_bin1_offset + 128;

                shard_t* shard                             = (shard_t*)base_address;
                shard->m_max_component_types               = (u16)max_component_types;
                shard->m_num_component_bins                = 0;
                shard->m_per_entity_global_occupancy_bytes = (u16)((math::alignUp(max_component_types, 8)) >> 3);
                shard->m_per_entity_local_occupancy_bytes  = (u16)((math::alignUp(max_components_per_entity, 8)) >> 3);
                shard->m_per_entity_indexed_bytes          = (u16)(max_component_types);
                shard->m_per_entity_reference_bytes        = (u16)(sizeof(u16) * max_components_per_entity);
                shard->m_per_entity_tags_bytes             = (u16)((math::alignUp(max_tags_per_entity, 8)) >> 3);
                shard->m_free_index                        = 0;
                shard->m_alive_count                       = 0;
                shard->m_free_bin0                         = D_U64_MAX;
                shard->m_alive_bin0                        = D_U64_MAX;
                shard->m_free_bin1                         = (u64*)((byte*)base_address + free_bin1_offset);
                shard->m_alive_bin1                        = (u64*)((byte*)base_address + alive_bin1_offset);
                shard->m_bin2                              = (u64*)((byte*)base_address + bin2_offset);

                return shard;
            }

            static bool s_register_component_type(shard_t* shard, u16 component_type_index, u32 sizeof_component)
            {
                ASSERT(component_type_index < shard->m_max_component_types);
                if (shard->m_component_bins[component_type_index] != nullptr)
                    return false; // already registered
                shard->m_component_bins[component_type_index] = nbin16::make_bin(sizeof_component, 65535);
                return true;
            }

            static byte* s_alloc_component(shard_t* shard, u32 entity_index, u16 component_type_index)
            {
                ASSERT(component_type_index < shard->m_max_component_types);
                ASSERT(entity_index < shard->m_free_index);

                const u8 page_size_shift = v_alloc_get_page_size_shift();

                byte* cp_global_occupancy = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);

                const u32 bit_index  = component_type_index;
                const u32 byte_index = bit_index >> 3;
                const u8  bit_mask   = (1 << (bit_index & 7));

                nbin16::bin_t* cp_bin = shard->m_component_bins[component_type_index];
                ASSERT(cp_bin != nullptr);

                if (cp_global_occupancy[byte_index] & bit_mask)
                {
                    const u8*  cp_indices    = shard->m_cp_indexed->m_base + (entity_index * shard->m_per_entity_indexed_bytes);
                    const u8   cp_index      = cp_indices[component_type_index];
                    const u16* cp_references = (const u16*)(shard->m_cp_reference->m_base + (entity_index * shard->m_per_entity_reference_bytes));
                    const u16  cp_reference  = cp_references[cp_index];
                    if (cp_index < nbin16::size(cp_bin))
                    {
                        return (byte*)nbin16::idx2ptr(cp_bin, cp_reference);
                    }
                }
                else
                {
                    void* cp_ptr = nbin16::alloc(cp_bin);
                    if (cp_ptr != nullptr)
                    {
                        u8* cp_local_occupancy = shard->m_cp_local_occupancy->m_base + (entity_index * shard->m_per_entity_local_occupancy_bytes);
                        u8* cp_indexed_array   = shard->m_cp_indexed->m_base + (entity_index * shard->m_per_entity_indexed_bytes);
                        s32 cp_local_index     = -1;
                        for (i16 i = 0; i < shard->m_per_entity_local_occupancy_bytes; i++)
                        {
                            if (cp_local_occupancy[i] != 0xFF)
                            {
                                cp_local_index = (i * 8) + (s32)math::findFirstBit((u8)~cp_local_occupancy[i]);
                                break;
                            }
                        }
                        if (cp_local_index < 0)
                            return nullptr;
                        cp_global_occupancy[byte_index]        = cp_global_occupancy[byte_index] | bit_mask;
                        const u32 cp_reference                 = nbin16::ptr2idx(cp_bin, cp_ptr);
                        u32*      cp_reference_array           = (u32*)(shard->m_cp_reference->m_base + (entity_index * shard->m_per_entity_reference_bytes));
                        cp_reference_array[cp_local_index]     = cp_reference;
                        cp_indexed_array[component_type_index] = (u8)cp_local_index;
                        cp_local_occupancy[cp_local_index >> 3] |= (1 << (cp_local_index & 7));
                        return (byte*)cp_ptr;
                    }
                }
                return nullptr;
            }

            static void s_free_component(shard_t* shard, u32 entity_index, u16 component_type_index)
            {
                ASSERT(component_type_index < shard->m_max_component_types);
                ASSERT(entity_index < shard->m_free_index);

                const u8  page_size_shift = v_alloc_get_page_size_shift();
                const u32 byte_index      = component_type_index >> 3;
                const u8  bit_mask        = (1 << (component_type_index & 7));

                byte* cp_global_occupancy = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);
                if (cp_global_occupancy[byte_index] & bit_mask)
                {
                    const u8*  cp_indices    = shard->m_cp_indexed->m_base + (entity_index * shard->m_per_entity_indexed_bytes);
                    const u8   cp_index      = cp_indices[component_type_index];
                    const u32* cp_references = (u32*)(shard->m_cp_reference->m_base + (entity_index * shard->m_per_entity_reference_bytes));
                    const u32  cp_reference  = cp_references[component_type_index];

                    nbin16::bin_t* cp_bin = shard->m_component_bins[component_type_index];
                    if (cp_index < nbin16::size(cp_bin))
                    {
                        void* cp_ptr = nbin16::idx2ptr(cp_bin, cp_reference);
                        nbin16::free(cp_bin, cp_ptr);
                        cp_global_occupancy[byte_index] = cp_global_occupancy[byte_index] & (~bit_mask);
                    }
                }
            }

            static bool s_has_component(shard_t* shard, u32 entity_index, u16 component_type_index)
            {
                const u8    page_size_shift = v_alloc_get_page_size_shift();
                const byte* occupancy_bits  = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);
                const u32   byte_index      = component_type_index >> 3;
                const u8    bit_mask        = ((u8)1 << (component_type_index & 7));
                return (occupancy_bits[byte_index] & bit_mask) != 0;
            }

            static byte* s_get_component(shard_t* shard, u32 entity_index, u16 component_type_index)
            {
                ASSERT(component_type_index < shard->m_max_component_types);
                ASSERT(entity_index < shard->m_free_index);

                const u8 page_size_shift = v_alloc_get_page_size_shift();

                const u32 byte_index = component_type_index >> 3;
                const u8  bit_mask   = (1 << (component_type_index & 7));

                const byte* global_occupancy = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);
                if (global_occupancy[byte_index] & bit_mask)
                {
                    const u8* cp_indices = shard->m_cp_indexed->m_base + (entity_index * shard->m_per_entity_indexed_bytes);
                    const u8  cp_index   = cp_indices[component_type_index];

                    nbin16::bin_t* cp_bin = shard->m_component_bins[component_type_index];
                    ASSERT(cp_bin != nullptr);
                    if (cp_index < nbin16::size(cp_bin))
                    {
                        const u32* cp_references = (u32*)(shard->m_cp_reference->m_base + (entity_index * shard->m_per_entity_reference_bytes));
                        const u32  cp_reference  = cp_references[cp_index];
                        return (byte*)nbin16::idx2ptr(cp_bin, cp_reference);
                    }
                }
                return nullptr;
            }

            static bool s_has_tag(shard_t* shard, entity_t entity, u16 tg_index)
            {
                if (tg_index >= (shard->m_per_entity_tags_bytes << 3))
                    return false;
                const u8    page_size_shift = v_alloc_get_page_size_shift();
                byte const* tag_occupancy   = shard->m_tags->m_base + (g_entity_index(entity) * shard->m_per_entity_tags_bytes);
                return (tag_occupancy[tg_index >> 3] & ((u8)1 << (tg_index & 7))) != 0;
            }

            static void s_add_tag(shard_t* shard, entity_t entity, u16 tg_index)
            {
                if (tg_index >= (shard->m_per_entity_tags_bytes << 3))
                    return;
                const u8 page_size_shift = v_alloc_get_page_size_shift();
                byte*    tag_occupancy   = shard->m_tags->m_base + (g_entity_index(entity) * shard->m_per_entity_tags_bytes);
                tag_occupancy[tg_index >> 3] |= ((u8)1 << (tg_index & 7));
            }

            static void s_rem_tag(shard_t* shard, entity_t entity, u16 tg_index)
            {
                if (tg_index >= (shard->m_per_entity_tags_bytes << 3))
                    return;
                const u8 page_size_shift = v_alloc_get_page_size_shift();
                byte*    tag_occupancy   = shard->m_tags->m_base + (g_entity_index(entity) * shard->m_per_entity_tags_bytes);
                tag_occupancy[tg_index >> 3] &= ~((u8)1 << (tg_index & 7));
            }

            static entity_t s_create_entity(shard_t* shard)
            {
                const u8 page_size_shift = v_alloc_get_page_size_shift();
                s32      entity_index    = -1;

                u8* global_occupancy = nullptr;
                u8* local_occupancy  = nullptr;
                u8* indexed_array    = nullptr;
                u8* reference_array  = nullptr;
                u8* tags_array       = nullptr;
                if (shard->m_alive_count < shard->m_free_index)
                {
                    // The hierarchical duomap can be used to find a free entity index
                    entity_index = nduomap18::find0_and_set(&shard->m_free_bin0, shard->m_free_bin1, &shard->m_alive_bin0, shard->m_alive_bin1, shard->m_bin2, shard->m_free_index);

                    global_occupancy = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);
                    local_occupancy  = shard->m_cp_local_occupancy->m_base + (entity_index * shard->m_per_entity_local_occupancy_bytes);
                    indexed_array    = shard->m_cp_indexed->m_base + (entity_index * shard->m_per_entity_indexed_bytes);
                    reference_array  = shard->m_cp_reference->m_base + (entity_index * shard->m_per_entity_reference_bytes);
                    tags_array       = shard->m_tags->m_base + (entity_index * shard->m_per_entity_tags_bytes);
                }
                else
                {
                    entity_index = shard->m_free_index++;

                    global_occupancy = (u8*)narena::alloc_and_zero(shard->m_cp_global_occupancy, shard->m_per_entity_global_occupancy_bytes);
                    local_occupancy  = (u8*)narena::alloc_and_zero(shard->m_cp_local_occupancy, shard->m_per_entity_local_occupancy_bytes);
                    indexed_array    = (u8*)narena::alloc_and_zero(shard->m_cp_indexed, shard->m_per_entity_indexed_bytes);
                    reference_array  = (u8*)narena::alloc_and_zero(shard->m_cp_reference, shard->m_per_entity_reference_bytes);
                    tags_array       = (u8*)narena::alloc_and_zero(shard->m_tags, shard->m_per_entity_tags_bytes);

                    // mark entity as alive in the duomap
                    nduomap18::set(&shard->m_free_bin0, shard->m_free_bin1, &shard->m_alive_bin0, shard->m_alive_bin1, shard->m_bin2, shard->m_free_index, entity_index);
                }

                g_memclr(global_occupancy, shard->m_per_entity_global_occupancy_bytes);
                g_memclr(local_occupancy, shard->m_per_entity_local_occupancy_bytes);
                g_memclr(indexed_array, shard->m_per_entity_indexed_bytes);
                g_memclr(reference_array, shard->m_per_entity_reference_bytes);
                g_memclr(tags_array, shard->m_per_entity_tags_bytes);
            }

            static void s_destroy_entity(shard_t* shard, u32 entity_index)
            {
                const u8 page_size_shift = v_alloc_get_page_size_shift();

                // Free all components associated with this entity
                // TODO, just scan for '1' bits in global occupancy instead of checking all component types
                const u8* global_occupancy = shard->m_cp_global_occupancy->m_base + (entity_index * shard->m_per_entity_global_occupancy_bytes);
                for (u16 ct = 0; ct < shard->m_num_component_bins; ct++)
                {
                    const u32 byte_index = ct >> 3;
                    const u8  bit_mask   = (1 << (ct & 7));
                    if (global_occupancy[byte_index] & bit_mask)
                    {
                        s_free_component(shard, entity_index, ct);
                    }
                }

                nduomap18::clr(&shard->m_free_bin0, shard->m_free_bin1, &shard->m_alive_bin0, shard->m_alive_bin1, shard->m_bin2, shard->m_free_index, entity_index);
            }

        } // namespace nshard

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u32               m_shards_count;
            u32               m_page_size_shift;
            u8*               m_shards_sorted;
            nshard::shard_t** m_shards; // array of shards
        };
        // ecs_t is followed in memory by:
        // u8                 m_shards_sorted[]; // sorted array of shard indices, to always have an active shard at index 0
        // nshard::shard_t*   m_shards[];       // array of shard pointers

        ecs_t* g_create_ecs(u32 max_entities, u16 components_per_entity, u16 max_component_types, u16 max_tag_types)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (u32)1 << page_size_shift;

            ecs_t* ecs = (ecs_t*)v_alloc_reserve(page_size);
            if (ecs == nullptr)
                return nullptr;

            ecs->m_page_size_shift = page_size_shift;
            ecs->m_shards_count    = (max_entities + ECS_SHARD_MAX_ENTITIES - 1) / ECS_SHARD_MAX_ENTITIES;
            ecs->m_shards          = (nshard::shard_t**)(ecs + 1);

            // create first shard
            ecs->m_shards[0] = nshard::s_create(max_component_types, components_per_entity, max_tag_types);
            if (ecs->m_shards[0] == nullptr)
            {
                v_alloc_release((byte*)ecs, page_size);
                return nullptr;
            }

            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            const u32 page_size = (u32)1 << ecs->m_page_size_shift;
            for (i32 i = 0; i < ecs->m_shards_count; i++)
            {
                nshard::shard_t* shard = ecs->m_shards[i];
                for (u16 i = 0; i < shard->m_num_component_bins; i++)
                {
                    if (shard->m_component_bins[i] != nullptr)
                    {
                        nbin16::destroy(shard->m_component_bins[i]);
                        shard->m_component_bins[i] = nullptr;
                    }
                }
                const u8 page_size_shift = v_alloc_get_page_size_shift();
                v_alloc_release((byte*)ecs, page_size);
            }
        }

        entity_t g_create_entity(ecs_t* ecs)
        {
            entity_t e = nshard::s_create_entity(ecs->m_shards[0]);
            return s_entity_make(0, e);
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            const u8         shard_index = g_entity_shard_index(e);
            nshard::shard_t* shard       = ecs->m_shards[shard_index];
            nshard::s_destroy_entity(shard, g_entity_index(e));
        }

        bool g_register_component(ecs_t* ecs, u32 cp_index, s32 cp_sizeof)
        {
            // TODO : handle multiple shards
            nshard::shard_t* shard = ecs->m_shards[0];
            return nshard::s_register_component_type(shard, (u16)cp_index, (u32)cp_sizeof);
        }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            return nshard::s_has_component(shard, g_entity_index(entity), (u16)cp_index);
        }
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            return nshard::s_alloc_component(shard, g_entity_index(entity), (u16)cp_index);
        }
        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            nshard::s_free_component(shard, g_entity_index(entity), (u16)cp_index);
        }
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            return nshard::s_get_component(shard, g_entity_index(entity), (u16)cp_index);
        }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            return nshard::s_has_tag(shard, entity, tg_index);
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            nshard::s_add_tag(shard, entity, tg_index);
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8 shard_index = g_entity_shard_index(entity);
            nshard::shard_t* shard = ecs->m_shards[shard_index];
            nshard::s_rem_tag(shard, entity, tg_index);
        }

    } // namespace necs4
} // namespace ncore
