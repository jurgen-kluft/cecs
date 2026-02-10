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
    template <typename T> void g_array_insert(T* array, u16& count, u32 max_count, u32 index, T const& value)
    {
        ASSERT(count < max_count);
        ASSERT(index <= count);
        for (u32 i = count; i > index; --i)
        {
            array[i] = array[i - 1];
        }
        array[index] = value;
        count++;
    }

    template <typename T> void g_array_remove(T* array, u16& count, u16 max_count, u32 index)
    {
        ASSERT(index < count);
        for (u32 i = index; i < count - 1; ++i)
        {
            array[i] = array[i + 1];
        }
        count--;
    }

    namespace necs4
    {
        // ecs4: Entity Component System, version 4
        // Description:
        //     - Uses virtual memory
        //     - Uses hierarchical bitmaps for tracking entity alive/free state
        //     - Uses ncore::nbin::bin_t for storing component data
        //     - Uses ncore::arena_t for managing entity data
        // Limitations:
        //     - Maximum archetypes: 256
        //     - Maximum entities: 65536
        //     - Maximum component types: 64
        //     - Per entity maximum components: 64
        //     - Per entity maximum tags: 32
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
        //
        // archetype:
        // Introducing 'archetype', this is a group of entities that share the same set of component types and tags.
        // This will allow us to optimize memory layout and access patterns, and is there to provide a mechanism to
        // the user to 'manage' both memory and performance.
        //
        // On the user level there are components and tags. A particular component/tag can be part of multiple archetypes.
        // When creating an entity, the user can specify an archetype that defines the initial set of components and tags.
        // Also the user could define entity, component and tag functions within a namespace that 'holds' the archetype Id.
        // e.g.
        //
        // enum EArchetypes
        // {
        //     ARCHETYPE_GAME_OBJECTS = 0,
        //     ARCHETYPE_UI_ELEMENTS  = 1,
        // };
        //
        // namespace ngame
        // {
        //     static ecs_t* g_game_entities        = nullptr;
        //
        //     template <typename T> register_component_type(ecs_t* ecs) { necs4::g_register_component_type(ecs, ARCHETYPE_GAME_OBJECTS, T::COMPONENT_TYPE_INDEX, sizeof(T)); }
        //     template <typename T> register_tag_type(ecs_t* ecs) { necs4::g_register_tag_type(ecs, ARCHETYPE_GAME_OBJECTS, T::TAG_INDEX); }
        //
        //     entity_t create_entity()            { return necs4::g_create_entity(g_game_entities, ARCHETYPE_GAME_OBJECTS); }
        //     void     destroy_entity(entity_t e) { necs4::g_destroy_entity(g_game_entities, e); }
        //
        //     template <typename T> T*   add_component(entity_t e) { return (T*)necs4::g_add_cp(g_game_entities, e, T::COMPONENT_TYPE_INDEX); }
        //     template <typename T> void rem_component(entity_t e) { necs4::g_rem_cp(g_game_entities, e, T::COMPONENT_TYPE_INDEX); }
        //     template <typename T> T*   get_component(entity_t e) { return (T*)necs4::g_get_cp(g_game_entities, e, T::COMPONENT_TYPE_INDEX); }
        //     template <typename T> bool has_component(entity_t e) { return necs4::g_has_cp(g_game_entities, e, T::COMPONENT_TYPE_INDEX); }
        //
        //     template <typename T> void add_tag(entity_t e) { necs4::g_add_tag(g_game_entities, e, T::TAG_INDEX); }
        //     template <typename T> void rem_tag(entity_t e) { necs4::g_rem_tag(g_game_entities, e, T::TAG_INDEX); }
        //     template <typename T> bool has_tag(entity_t e) { return necs4::g_has_tag(g_game_entities, e, T::TAG_INDEX); }
        // } // namespace ngame

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // type definitions and utility functions
        static inline entity_t s_entity_make(entity_generation_t genid, u8 archetype_index, entity_index_t entity_index)
        {
            // Combine generation ID, archetype index, and entity index into a single entity identifier
            return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | ((u32)archetype_index << ECS_ENTITY_ARCHETYPE_SHIFT) | ((u32)entity_index & ECS_ENTITY_INDEX_MASK);
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // ecs archetype (112 bytes), maximum of 65536 entities per archetype
        // --------------------------------------------------------------------------------------------------------
#define ECS_archetype_MAX_ENTITIES 65536

        struct archetype_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u16*            m_global_to_local_cp_type;  // map global component type index to local component type index
            u8*             m_global_to_local_tag_type; // map global tag type index to local tag type index
            nbin16::bin_t** m_cp_bins;                  // array of component bins (max 64)
            arena_t*        m_cp_occupancy;             // component occupancy bits per entity (u64)
            arena_t*        m_cp_reference;             // component reference array (u16[])
            arena_t*        m_tags;                     // tag bits array (u8, u16 or u32)
            u32             m_reserved_pages;           // total number of reserved pages for this archetype
            u16             m_max_cp_types;             // maximum number of component bins (max 64)
            u16             m_num_cp_bins;              // current number of component bins
            u16             m_num_tags;                 // current number of tags
            u16             m_per_entity_cps;           // number of components per entity
            u16             m_per_entity_tags;          // tag bits array
            u16             m_page_size_shift;          // padding to make the structure aligned
            u32             m_free_index;               // first free entity index
            u32             m_alive_count;              // number of alive entities
            u64             m_free_bin0;                // 16 * 64 * 64 = 65536 entities
            u64             m_alive_bin0;               // 16 * 64 * 64 = 65536 entities
            u64*            m_free_bin1;                // track the 0 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            u64*            m_alive_bin1;               // track the 1 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            u64*            m_bin2;                     // '1' bit = alive entity, '0' bit = free entity (65536 bits = 8 KB)
        };
        // ecs_archetype_t is followed in memory by:
        // u16         m_global_to_local_cp_type[];     // map global component type index to local component type index (u16[])
        // u8          m_global_to_local_tag_type[];    // map global tag type
        // arena_t     m_cp_occupancy;                  // component occupancy bits array (u8[])
        // arena_t     m_cp_reference;                  // component reference array (u16[])
        // arena_t     m_tags;                          // tag bits array (u8[])
        // bin_t*      m_cp_bins[m_max_cp_types];       // Array of component bins
        // u64         m_free_bin1[1024 / 64];          // 1024 bits = 128 B
        // u64         m_alive_bin1[1024 / 64];         // 1024 bits = 128 B
        // u64         m_bin2[65536 / 64];              // 65536 bits = 8 KiB

        static archetype_t* s_create(u32 max_archetype_component_types, u32 max_global_component_types, u32 max_components_per_entity, u32 max_archetype_tags_per_entity, u32 max_global_tags_per_entity)
        {
            ASSERT(max_archetype_component_types <= 64);
            ASSERT(max_components_per_entity <= 64);
            ASSERT(max_archetype_tags_per_entity <= 32); // sanity check

            // tags per entity is either 8, 16 or 32
            // fix incoming max_tags_per_entity
            max_archetype_tags_per_entity = math::alignUp(max_archetype_tags_per_entity, 8);
            max_archetype_tags_per_entity = math::min(max_archetype_tags_per_entity, 32u);
            max_archetype_tags_per_entity = math::ceilpo2(max_archetype_tags_per_entity >> 3);

            // calculate the address space to reserve for all arenas
            const u32 page_size       = v_alloc_get_page_size();
            const u8  page_size_shift = v_alloc_get_page_size_shift();

            const int_t max_entities              = ECS_archetype_MAX_ENTITIES;
            const int_t global_to_local_cp_size   = sizeof(u16) * max_global_component_types;
            const int_t global_to_local_tg_size   = sizeof(u8) * max_global_tags_per_entity;
            const int_t arenas_size               = sizeof(arena_t) * 5;
            const int_t component_bins_array_size = sizeof(nbin16::bin_t*) * max_archetype_component_types;
            const int_t archetype_struct_size     = sizeof(archetype_t);
            const int_t binmap_size               = 8192 + 128 + 128;
            const int_t cp_occupancy              = math::alignUp(sizeof(u64) * max_entities, page_size);
            const int_t cp_reference              = math::alignUp((sizeof(u16) * max_components_per_entity * max_entities), page_size);
            const int_t tags_array                = math::alignUp((max_archetype_tags_per_entity * max_entities) >> 3, page_size);
            const int_t archetype_size            = math::alignUp(archetype_struct_size + arenas_size + component_bins_array_size + binmap_size, page_size);
            const int_t total_size                = archetype_size + cp_occupancy + cp_reference + tags_array;

            byte* base_address = (byte*)v_alloc_reserve(total_size);
            if (base_address == nullptr)
                return nullptr;

            if (!v_alloc_commit(base_address, archetype_size))
            {
                v_alloc_release(base_address, total_size);
                return nullptr;
            }

            const int_t free_bin1_offset  = archetype_struct_size + arenas_size + component_bins_array_size;
            const int_t alive_bin1_offset = free_bin1_offset + 128;
            const int_t bin2_offset       = alive_bin1_offset + 128;

            archetype_t* archetype                = (archetype_t*)base_address;
            archetype->m_global_to_local_cp_type  = (u16*)(archetype + 1);
            archetype->m_global_to_local_tag_type = (u8*)(archetype->m_global_to_local_cp_type + max_global_component_types);
            archetype->m_cp_occupancy             = (arena_t*)(archetype->m_global_to_local_tag_type + max_global_tags_per_entity);
            archetype->m_cp_reference             = archetype->m_cp_occupancy + 1;
            archetype->m_tags                     = archetype->m_cp_reference + 1;
            archetype->m_reserved_pages           = (u32)(total_size >> page_size_shift);
            archetype->m_cp_bins                  = (nbin16::bin_t**)(archetype->m_tags + 1);
            archetype->m_max_cp_types             = (u16)max_archetype_component_types;
            archetype->m_num_cp_bins              = 0;
            archetype->m_per_entity_cps           = max_archetype_component_types;
            archetype->m_per_entity_tags          = (u16)((math::alignUp(max_archetype_tags_per_entity, 8)));
            archetype->m_page_size_shift          = page_size_shift;
            archetype->m_free_index               = 0;
            archetype->m_alive_count              = 0;
            archetype->m_free_bin0                = D_U64_MAX;
            archetype->m_alive_bin0               = D_U64_MAX;
            archetype->m_free_bin1                = (u64*)((byte*)base_address + free_bin1_offset);
            archetype->m_alive_bin1               = (u64*)((byte*)base_address + alive_bin1_offset);
            archetype->m_bin2                     = (u64*)((byte*)base_address + bin2_offset);

            // initialize all arenas
            byte* arena_memory_base = base_address + archetype_size;
            narena::init_arena(archetype->m_cp_occupancy, arena_memory_base, cp_occupancy, 0);
            arena_memory_base += cp_occupancy;
            narena::init_arena(archetype->m_cp_reference, arena_memory_base, cp_reference, 0);
            arena_memory_base += cp_reference;
            narena::init_arena(archetype->m_tags, arena_memory_base, tags_array, 0);

            g_memory_fill(archetype->m_global_to_local_cp_type, 0xFFFFFFFF, global_to_local_cp_size);
            g_memory_fill(archetype->m_global_to_local_tag_type, 0xFFFFFFFF, global_to_local_tg_size);
            g_memory_fill(archetype->m_cp_bins, 0, component_bins_array_size);

            return archetype;
        }

        static void s_destroy(archetype_t* archetype)
        {
            if (archetype != nullptr)
            {
                const int_t reserved_size = (int_t)archetype->m_reserved_pages << archetype->m_page_size_shift;

                // free all component bins
                for (u16 i = 0; i < archetype->m_max_cp_types; i++)
                {
                    if (archetype->m_cp_bins[i] != nullptr)
                    {
                        nbin16::destroy(archetype->m_cp_bins[i]);
                        archetype->m_cp_bins[i] = nullptr;
                    }
                }
                // free all arenas
                narena::destroy(archetype->m_cp_occupancy);
                narena::destroy(archetype->m_cp_reference);
                narena::destroy(archetype->m_tags);

                v_alloc_release((void*)archetype, reserved_size);
            }
        }

        static void s_register_component_type(archetype_t* archetype, u16 global_component_type_index, u32 sizeof_component)
        {
            ASSERT(global_component_type_index < archetype->m_max_cp_types);
            if (archetype->m_global_to_local_cp_type[global_component_type_index] != 0xFFFF)
                return;
            archetype->m_global_to_local_cp_type[global_component_type_index] = (u16)archetype->m_num_cp_bins;
            archetype->m_cp_bins[archetype->m_num_cp_bins]                    = nbin16::make_bin(sizeof_component, 65535);
            archetype->m_num_cp_bins++;
        }

        static void s_register_tag_type(archetype_t* archetype, u16 global_tag_type_index)
        {
            ASSERT(archetype->m_num_tags < archetype->m_per_entity_tags);
            if (archetype->m_global_to_local_tag_type[global_tag_type_index] != 0xFF)
                return;
            archetype->m_global_to_local_tag_type[global_tag_type_index] = (u16)archetype->m_num_tags;
            archetype->m_num_tags++;
        }

        static byte* s_alloc_component(archetype_t* archetype, u32 entity_index, u16 global_component_type_index)
        {
            ASSERT(global_component_type_index < archetype->m_max_cp_types);
            ASSERT(entity_index < archetype->m_free_index);

            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_component_type_index];

            u64* occupancy_array = (u64*)archetype->m_cp_occupancy->m_base;
            u64& occupancy       = occupancy_array[entity_index];

            const u64 bit_mask = ((u64)1 << component_type_index);

            nbin16::bin_t* cp_bin = archetype->m_cp_bins[component_type_index];
            ASSERT(cp_bin != nullptr);

            if (occupancy & bit_mask)
            {
                // count the bits that are before 'bit_index' to find the local index (popcount)
                const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                const u16* cp_reference_array = (const u16*)archetype->m_cp_reference->m_base;
                const u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                const u16  cp_reference       = cp_references[cp_index];
                return (byte*)nbin16::idx2ptr(cp_bin, cp_reference);
            }
            else
            {
                void* cp_ptr = nbin16::alloc(cp_bin);
                if (cp_ptr != nullptr)
                {
                    // calculate the number of components currently in the reference array
                    u16 num_components = (u16)math::countBits(occupancy);
                    // mark component as allocated
                    occupancy = occupancy | bit_mask;

                    // find local component index by counting bits before 'bit_index'
                    const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                    // store component reference
                    u16* cp_reference_array = (u16*)archetype->m_cp_reference->m_base;
                    u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                    u16  cp_reference       = (u16)nbin16::ptr2idx(cp_bin, cp_ptr);
                    g_array_insert(cp_references, num_components, archetype->m_per_entity_cps, cp_index, cp_reference);

                    return (byte*)cp_ptr;
                }
            }
            return nullptr;
        }

        static void s_free_local_component(archetype_t* archetype, u32 entity_index, u16 component_type_index)
        {
            const u64 bit_mask        = ((u64)1 << component_type_index);
            u64*      occupancy_array = (u64*)archetype->m_cp_occupancy->m_base;
            u64&      occupancy       = occupancy_array[entity_index];
            if (occupancy & bit_mask)
            {
                u16 num_components = (u16)math::countBits(occupancy);
                occupancy          = occupancy & (~bit_mask);

                // find local component index by counting bits before 'bit_index'
                const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                nbin16::bin_t* cp_bin = archetype->m_cp_bins[component_type_index];
                ASSERT(cp_bin != nullptr);

                u16*  cp_reference_array = (u16*)archetype->m_cp_reference->m_base;
                u16*  cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                u16   cp_reference       = cp_references[cp_index];
                void* cp_ptr             = nbin16::idx2ptr(cp_bin, cp_reference);
                nbin16::free(cp_bin, cp_ptr);

                // remove component reference from the array
                g_array_remove(cp_references, num_components, archetype->m_per_entity_cps, cp_index);
            }
        }

        static void s_free_component(archetype_t* archetype, u32 entity_index, u16 global_component_type_index)
        {
            ASSERT(global_component_type_index < archetype->m_max_cp_types);
            ASSERT(entity_index < archetype->m_free_index);
            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_component_type_index];
            s_free_local_component(archetype, entity_index, component_type_index);
        }

        static bool s_has_component(archetype_t* archetype, u32 entity_index, u16 global_component_type_index)
        {
            ASSERT(global_component_type_index < archetype->m_max_cp_types);
            ASSERT(entity_index < archetype->m_free_index);
            const u16  component_type_index = archetype->m_global_to_local_cp_type[global_component_type_index];
            const u64* occupancy_array      = (const u64*)archetype->m_cp_occupancy->m_base;
            const u64  occupancy            = occupancy_array[entity_index];
            const u64  bit_mask             = ((u64)1 << component_type_index);
            return (occupancy & bit_mask) != 0;
        }

        static byte* s_get_component(archetype_t* archetype, u32 entity_index, u16 global_component_type_index)
        {
            ASSERT(global_component_type_index < archetype->m_max_cp_types);
            ASSERT(entity_index < archetype->m_free_index);

            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_component_type_index];
            const u64 bit_mask             = ((u64)1 << component_type_index);

            const u64* occupancy_array = (const u64*)archetype->m_cp_occupancy->m_base;
            const u64  occupancy       = occupancy_array[entity_index];

            if (occupancy & bit_mask)
            {
                // count the bits that are before 'bit_index' to find the local index (popcount)
                const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                const u16* cp_reference_array = (const u16*)archetype->m_cp_reference->m_base;
                const u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                const u16  cp_reference       = cp_references[cp_index];

                nbin16::bin_t* cp_bin = archetype->m_cp_bins[component_type_index];
                ASSERT(cp_bin != nullptr);

                return (byte*)nbin16::idx2ptr(cp_bin, cp_reference);
            }
            return nullptr;
        }

        static bool s_has_tag(archetype_t* archetype, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (archetype->m_per_entity_tags))
                return false;
            byte const* tag_occupancy = archetype->m_tags->m_base + (g_entity_index(entity) * (math::alignUp(archetype->m_per_entity_tags, 8) >> 3));
            return (tag_occupancy[tg_index >> 3] & ((u8)1 << (tg_index & 7))) != 0;
        }

        static void s_add_tag(archetype_t* archetype, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (archetype->m_per_entity_tags))
                return;
            byte* tag_occupancy = archetype->m_tags->m_base + (g_entity_index(entity) * (math::alignUp(archetype->m_per_entity_tags, 8) >> 3));
            tag_occupancy[tg_index >> 3] |= ((u8)1 << (tg_index & 7));
        }

        static void s_rem_tag(archetype_t* archetype, entity_t entity, u16 tg_index)
        {
            if (tg_index >= (archetype->m_per_entity_tags))
                return;
            byte* tag_occupancy = archetype->m_tags->m_base + (g_entity_index(entity) * (math::alignUp(archetype->m_per_entity_tags, 8) >> 3));
            tag_occupancy[tg_index >> 3] &= ~((u8)1 << (tg_index & 7));
        }

        static s32 s_create_entity(archetype_t* archetype)
        {
            s32 entity_index = -1;

            u64* occupancy_array;
            u16* reference_array;
            u8*  tags_array;
            if (archetype->m_alive_count < archetype->m_free_index)
            {
                // The hierarchical duomap can be used to find a free entity index
                entity_index = nduomap18::find0_and_set(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index);

                occupancy_array = (u64*)archetype->m_cp_occupancy->m_base;
                reference_array = (u16*)archetype->m_cp_reference->m_base;
                tags_array      = archetype->m_tags->m_base;
                occupancy_array = occupancy_array + (entity_index);
                reference_array = reference_array + (entity_index * archetype->m_per_entity_cps);
                tags_array      = tags_array + (entity_index * (math::alignUp(archetype->m_per_entity_tags, 8) >> 3));
            }
            else
            {
                entity_index = archetype->m_free_index++;

                occupancy_array = (u64*)narena::alloc_and_zero(archetype->m_cp_occupancy, sizeof(u64));
                reference_array = (u16*)narena::alloc_and_zero(archetype->m_cp_reference, archetype->m_per_entity_cps * sizeof(u16));
                tags_array      = (u8*)narena::alloc_and_zero(archetype->m_tags, (math::alignUp(archetype->m_per_entity_tags, 8) >> 3));

                nduomap18::set(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index, entity_index);
            }

            *occupancy_array = 0;
            g_memclr(tags_array, math::alignUp(archetype->m_per_entity_tags, 8) >> 3);

            archetype->m_alive_count++;

            return entity_index;
        }

        static void s_destroy_entity(archetype_t* archetype, u32 entity_index)
        {
            // Free all components associated with this entity
            // TODO, just scan for '1' bits in occupancy instead of checking all component types
            u64* occupancy_array = (u64*)archetype->m_cp_occupancy->m_base;
            u64  occupancy       = occupancy_array[entity_index];
            while (occupancy != 0)
            {
                const u8 bin_index = (u8)math::findFirstBit(occupancy);
                s_free_local_component(archetype, entity_index, bin_index);
                occupancy = occupancy & (~((u64)1 << bin_index));
            }

            nduomap18::clr(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index, entity_index);

            archetype->m_alive_count--;
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            u32           m_archetypes_capacity;
            u32           m_page_size_shift;
            archetype_t** m_archetypes; // array of archetype pointers
        };
        // ecs_t is followed in memory by:
        // archetype_t*   m_archetypes[];       // array of archetype pointers

        void g_register_archetype(ecs_t* ecs, u16 archetype_index, u16 components_per_entity, u16 max_archetype_component_types, u16 max_global_component_types, u16 max_archetype_tag_types, u16 max_global_tag_types)
        {
            ASSERT(archetype_index < ecs->m_archetypes_capacity);
            if (ecs->m_archetypes[archetype_index] != nullptr)
                return;
            archetype_t* archetype             = s_create(max_archetype_component_types, max_global_component_types, components_per_entity, max_archetype_tag_types, max_global_tag_types);
            ecs->m_archetypes[archetype_index] = archetype;
        }

        ecs_t* g_create_ecs(u16 max_archetypes)
        {
            const u8  page_size_shift = v_alloc_get_page_size_shift();
            const u32 page_size       = (u32)1 << page_size_shift;

            const int_t ecs_size = math::alignUp(sizeof(ecs_t) + (sizeof(u8) * 256) + (sizeof(archetype_t*) * 256), page_size);

            void* base_address = v_alloc_reserve(ecs_size);
            if (base_address == nullptr)
                return nullptr;
            if (!v_alloc_commit(base_address, ecs_size))
            {
                v_alloc_release(base_address, ecs_size);
                return nullptr;
            }

            ecs_t* ecs                 = (ecs_t*)base_address;
            ecs->m_page_size_shift     = page_size_shift;
            ecs->m_archetypes_capacity = 256;
            ecs->m_archetypes          = (archetype_t**)(ecs + 1);

            // initialize archetypes array
            g_memclr(ecs->m_archetypes, sizeof(archetype_t*) * ecs->m_archetypes_capacity);

            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            for (u32 i = 0; i < ecs->m_archetypes_capacity; i++)
            {
                archetype_t* archetype = ecs->m_archetypes[i];
                if (archetype != nullptr)
                    s_destroy(archetype);
            }
            const u32   page_size = (u32)1 << ecs->m_page_size_shift;
            const int_t ecs_size  = math::alignUp(sizeof(ecs_t) + (sizeof(u8) * 256) + (sizeof(archetype_t*) * 256), page_size);
            v_alloc_release((byte*)ecs, ecs_size);
        }

        entity_t g_create_entity(ecs_t* ecs, u16 archetype_index)
        {
            archetype_t* archetype    = ecs->m_archetypes[archetype_index];
            const s32    entity_index = s_create_entity(archetype);
            return s_entity_make(0, archetype_index, (entity_index_t)entity_index);
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            const u8     archetype_index = g_entity_archetype_index(e);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            s_destroy_entity(archetype, g_entity_index(e));
        }

        void g_register_component_type(ecs_t* ecs, u16 archetype_index, u16 cp_index, u32 cp_sizeof)
        {
            archetype_t* archetype = ecs->m_archetypes[archetype_index];
            if (archetype == nullptr)
                return;
            s_register_component_type(archetype, cp_index, cp_sizeof);
        }

        void g_register_tag_type(ecs_t* ecs, u16 archetype_index, u16 tg_index)
        {
            archetype_t* archetype = ecs->m_archetypes[archetype_index];
            if (archetype == nullptr)
                return;
            s_register_tag_type(archetype, tg_index);
        }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            return s_has_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            return s_alloc_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            s_free_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            return s_get_component(archetype, g_entity_index(entity), (u16)cp_index);
        }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            return s_has_tag(archetype, entity, tg_index);
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            s_add_tag(archetype, entity, tg_index);
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            const u8     archetype_index = g_entity_archetype_index(entity);
            archetype_t* archetype       = ecs->m_archetypes[archetype_index];
            s_rem_tag(archetype, entity, tg_index);
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

        entity_t en_iterator_t::entity() const
        {
            //
            return m_entity_index >= 0 ? s_entity_make(0, m_archetype_index, m_entity_index) : ECS_ENTITY_NULL;
        }

        // TODO, optimize this, we can determine the 'words' that have bits set in the reference entity, same for the tags

        s32 en_iterator_t::find(s32 entity_index) const
        {
            if (m_entity_reference < 0)
            {
                return entity_index >= 0 ? nduomap18::find1_after(&m_archetype->m_free_bin0, m_archetype->m_free_bin1, &m_archetype->m_alive_bin0, m_archetype->m_alive_bin1, m_archetype->m_bin2, m_archetype->m_free_index, entity_index) : -1;
            }

            // See if this entity has all the components and tags that we are looking for by taking the
            // intersection of the component and tag occupancy using the reference entity
            s32 const    ref_entity_index    = g_entity_index(m_entity_reference);
            u8 const     ref_archetype_index = g_entity_archetype_index(m_entity_reference);
            archetype_t* ref_archetype       = m_ecs->m_archetypes[ref_archetype_index];
            u64 const*   ref_cp_occupancy    = (u64*)&ref_archetype->m_cp_occupancy->m_base[ref_entity_index * sizeof(u64)];
            u8 const*    ref_tag_occupancy   = &ref_archetype->m_tags->m_base[ref_entity_index * (math::alignUp(m_archetype->m_per_entity_tags, 8) >> 3)];

            entity_index = nduomap18::find1_after(&m_archetype->m_free_bin0, m_archetype->m_free_bin1, &m_archetype->m_alive_bin0, m_archetype->m_alive_bin1, m_archetype->m_bin2, m_archetype->m_free_index, entity_index);
            while (entity_index >= 0)
            {
                if (entity_index != ref_entity_index)
                {
                    u64 const* cur_cp_occupancy   = (u64*)&m_archetype->m_cp_occupancy->m_base[entity_index * sizeof(u64)];
                    bool       has_all_components = ((*cur_cp_occupancy & *ref_cp_occupancy) == *ref_cp_occupancy);
                    if (has_all_components)
                    {
                        switch (m_archetype->m_per_entity_tags)
                        {
                            case 8:
                            {
                                u8 const* cur_tag_occupancy = (u8*)m_archetype->m_tags->m_base;
                                if ((cur_tag_occupancy[entity_index] & ref_tag_occupancy[0]) == ref_tag_occupancy[0])
                                    return entity_index;
                                break;
                            }
                            case 16:
                            {
                                u16 const* ref_tag_occupancy2 = (u16*)ref_tag_occupancy;
                                u16 const* cur_tag_occupancy  = (u16*)m_archetype->m_tags->m_base;
                                if ((cur_tag_occupancy[entity_index] & *ref_tag_occupancy2) == *ref_tag_occupancy2)
                                    return entity_index;
                                break;
                            }
                            case 32:
                            {
                                u32 const* ref_tag_occupancy4 = (u32*)ref_tag_occupancy;
                                u32 const* cur_tag_occupancy  = (u32*)m_archetype->m_tags->m_base;
                                if ((cur_tag_occupancy[entity_index] & *ref_tag_occupancy4) == *ref_tag_occupancy4)
                                    return entity_index;
                                break;
                            }
                        }
                    }
                }
                entity_index = nduomap18::find1_after(&m_archetype->m_free_bin0, m_archetype->m_free_bin1, &m_archetype->m_alive_bin0, m_archetype->m_alive_bin1, m_archetype->m_bin2, m_archetype->m_free_index, entity_index);
            }
            return entity_index;
        }

    } // namespace necs4
} // namespace ncore
