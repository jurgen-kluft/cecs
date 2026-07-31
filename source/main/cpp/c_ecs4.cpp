#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_array.h"
#include "ccore/c_arena.h"
#include "ccore/c_bin.h"
#include "ccore/c_debug.h"
#include "ccore/c_statevec.h"
#include "ccore/c_math.h"
#include "ccore/c_memory.h"

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
        typedef u8  entity_generation_t;
        typedef u32 entity_index_t;

        const u32 ECS_ENTITY_NULL            = (0xFFFFFFFF); // Null entity
        const u32 ECS_ENTITY_INDEX_MASK      = (0x0000FFFF); // Mask to use to get the entity index from an entity identifier
        const u32 ECS_ENTITY_ARCHETYPE_MASK  = (0x00FF0000); // Mask to use to get the archetype index from an entity identifier
        const s8  ECS_ENTITY_ARCHETYPE_SHIFT = (16);         // Shift to get the archetype index
        const u32 ECS_ENTITY_GEN_ID_MASK     = (0xFF000000); // Mask to use to get the generation id from an entity identifier
        const s8  ECS_ENTITY_GEN_ID_SHIFT    = (24);         // Shift to get the generation id

        inline bool                g_entity_is_null(entity_t e) { return e == ECS_ENTITY_NULL; }
        inline entity_generation_t g_entity_generation(entity_t e) { return ((u32)e & ECS_ENTITY_GEN_ID_MASK) >> ECS_ENTITY_GEN_ID_SHIFT; }
        inline entity_index_t      g_entity_index(entity_t e) { return (entity_index_t)e & ECS_ENTITY_INDEX_MASK; }
        inline u8                  g_entity_archetype_index(entity_t e) { return (u8)((e & ECS_ENTITY_ARCHETYPE_MASK) >> ECS_ENTITY_ARCHETYPE_SHIFT); }

        static inline entity_t s_entity_make(entity_generation_t genid, u8 archetype_index, entity_index_t entity_index)
        {
            // Combine generation ID, archetype index, and entity index into a single entity identifier
            return ((u32)genid << ECS_ENTITY_GEN_ID_SHIFT) | ((u32)archetype_index << ECS_ENTITY_ARCHETYPE_SHIFT) | ((u32)entity_index & ECS_ENTITY_INDEX_MASK);
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // ecs archetype (224 bytes), maximum of 65536 entities per archetype
        // --------------------------------------------------------------------------------------------------------
#define ECS_ARCHETYPE_MAX_ENTITIES 65536

        struct archetype_t
        {
            arena_t* m_archetype_arena;          // arena for allocating member data from
            u16*     m_global_to_local_cp_type;  // map global component type index to local component type index
            u8*      m_global_to_local_tag_type; // map global tag type index to local tag type index
            bin16_t* m_cp_bins;                  // array of component bins (max 64)
            arena_t* m_cp_occupancy;             // component occupancy bits per entity (u64)
            arena_t* m_cp_reference;             // component reference array (u16[])
            arena_t* m_tags;                     // tag bits array (u8, u16 or u32)
            arena_t* m_generation;               // generation id per entity (u8)
            u16      m_max_global_cp_types;      // maximum number of global component types
            u16      m_max_global_tag_types;     // maximum number of global tag types
            u16      m_num_cps;                  // current number of component bins
            u8       m_num_tags;                 // current number of tags
            u16      m_per_entity_cps;           // number of components per entity
            u16      m_per_entity_tags;          // number of tags per entity
            u32      m_free_index;               // first free entity index
            u32      m_alive_count;              // number of alive entities
            u64      m_free_bin0;                // 16 * 64 * 64 = 65536 entities
            u64      m_alive_bin0;               // 16 * 64 * 64 = 65536 entities
            u64*     m_free_bin1;                // track the 0 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            u64*     m_alive_bin1;               // track the 1 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            u64*     m_bin2;                     // '1' bit = alive entity, '0' bit = free entity (65536 bits = 8 KB)
        };

        static void s_initialize_archetype(archetype_t* archetype, u8 max_cps_per_entity, u16 max_global_cp_types, u8 max_tags_per_entity, u16 max_global_tag_types)
        {
            ASSERT(max_global_cp_types < 2048);  // sanity
            ASSERT(max_global_tag_types <= 255); // u8 is used for mapping
            ASSERT(max_cps_per_entity <= 64);    //
            ASSERT(max_tags_per_entity <= 32);   // sanity check

            max_tags_per_entity = math::alignUp(max_tags_per_entity, 8);

            archetype->m_archetype_arena          = narena::new_arena(16 * cKB, 12 * cKB);
            archetype->m_global_to_local_cp_type  = g_allocate_array_and_fill<u16>(archetype->m_archetype_arena, max_global_cp_types, 0xFFFFFFFF);
            archetype->m_global_to_local_tag_type = g_allocate_array_and_fill<u8>(archetype->m_archetype_arena, max_global_tag_types, 0xFFFFFFFF);
            archetype->m_cp_occupancy             = narena::new_arena((int_t)sizeof(u64) * ECS_ARCHETYPE_MAX_ENTITIES, 0);
            archetype->m_cp_reference             = narena::new_arena((int_t)sizeof(u16) * max_cps_per_entity * ECS_ARCHETYPE_MAX_ENTITIES, 0);
            archetype->m_tags                     = narena::new_arena((int_t)((max_tags_per_entity * ECS_ARCHETYPE_MAX_ENTITIES) >> 3), 0);
            archetype->m_generation               = narena::new_arena((int_t)sizeof(u8) * ECS_ARCHETYPE_MAX_ENTITIES, 0);
            archetype->m_cp_bins                  = g_allocate_array_and_clear<bin16_t>(archetype->m_archetype_arena, 64);
            archetype->m_max_global_cp_types      = (u16)max_global_cp_types;
            archetype->m_max_global_tag_types     = (u16)max_global_tag_types;
            archetype->m_per_entity_cps           = (u16)max_cps_per_entity;
            archetype->m_per_entity_tags          = (u16)max_tags_per_entity;
            archetype->m_free_index               = 0;
            archetype->m_alive_count              = 0;

            archetype->m_free_bin0  = D_U64_MAX;
            archetype->m_alive_bin0 = 0;
            archetype->m_free_bin1  = g_allocate_array_and_fill<u64>(archetype->m_archetype_arena, 64, 0XFFFFFFFF); // track the 0 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            archetype->m_alive_bin1 = g_allocate_array_and_fill<u64>(archetype->m_archetype_arena, 16, 0);          // track the 1 bits in m_entity_bin2 (16 * sizeof(u64) = 128 bytes)
            archetype->m_bin2       = g_allocate_array_and_fill<u64>(archetype->m_archetype_arena, 16 * 64, 0);     // '1' bit = alive entity, '0' bit = free entity (65536 bits = 8 KB)
        }

        static void s_destroy(archetype_t* archetype)
        {
            for (u16 i = 0; i < archetype->m_num_cps; ++i)
                bin_destroy(&archetype->m_cp_bins[i]);

            narena::destroy(archetype->m_cp_occupancy);
            narena::destroy(archetype->m_cp_reference);
            narena::destroy(archetype->m_tags);
            narena::destroy(archetype->m_generation);

            narena::destroy(archetype->m_archetype_arena);
        }

        static void s_register_component_type(archetype_t* archetype, u16 global_cp_type_index, u32 sizeof_component)
        {
            ASSERT(global_cp_type_index < archetype->m_max_global_cp_types);
            ASSERT(archetype->m_num_cps < 64);
            if (global_cp_type_index >= archetype->m_max_global_cp_types || archetype->m_num_cps >= 64)
                return;
            if (archetype->m_global_to_local_cp_type[global_cp_type_index] != 0xFFFF)
                return;
            archetype->m_global_to_local_cp_type[global_cp_type_index] = (u16)archetype->m_num_cps;
            bin16_t& bin                                               = archetype->m_cp_bins[archetype->m_num_cps];
            bin_setup(&bin, sizeof_component, 65535);
            archetype->m_num_cps++;
        }

        static void s_register_tag_type(archetype_t* archetype, u16 global_tag_type_index)
        {
            ASSERT(global_tag_type_index < archetype->m_max_global_tag_types);
            ASSERT(archetype->m_num_tags < archetype->m_per_entity_tags);
            if (global_tag_type_index >= archetype->m_max_global_tag_types || archetype->m_num_tags >= archetype->m_per_entity_tags)
                return;
            if (archetype->m_global_to_local_tag_type[global_tag_type_index] != 0xFF)
                return;
            archetype->m_global_to_local_tag_type[global_tag_type_index] = archetype->m_num_tags;
            archetype->m_num_tags++;
        }

        static byte* s_alloc_component(archetype_t* archetype, u32 entity_index, u16 global_cp_type_index)
        {
            ASSERT(global_cp_type_index < archetype->m_max_global_cp_types);
            ASSERT(entity_index < archetype->m_free_index);

            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_cp_type_index];
            if (component_type_index == 0xFFFF)
                return nullptr;

            u64* occupancy_array = narena::base_ptr_as<u64>(archetype->m_cp_occupancy);
            u64& occupancy       = occupancy_array[entity_index];

            const u64 bit_mask = ((u64)1 << component_type_index);

            bin16_t* cp_bin = &archetype->m_cp_bins[component_type_index];
            ASSERT(cp_bin->m_memory != nullptr);

            if (occupancy & bit_mask)
            {
                // count the bits that are before 'bit_index' to find the local index (popcount)
                const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                const u16* cp_reference_array = narena::base_ptr_as<const u16>(archetype->m_cp_reference);
                const u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                const u16  cp_reference       = cp_references[cp_index];
                return (byte*)bin_idx2ptr(cp_bin, cp_reference);
            }
            else
            {
                if (math::countBits(occupancy) >= archetype->m_per_entity_cps)
                    return nullptr;

                void* cp_ptr = bin_alloc(cp_bin);
                if (cp_ptr != nullptr)
                {
                    // calculate the number of components currently in the reference array
                    u16 num_components = (u16)math::countBits(occupancy);
                    // mark component as allocated
                    occupancy = occupancy | bit_mask;

                    // find local component index by counting bits before 'bit_index'
                    const u16 cp_index = (u16)math::countBits(occupancy & (bit_mask - 1));

                    // store component reference
                    u16* cp_reference_array = narena::base_ptr_as<u16>(archetype->m_cp_reference);
                    u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                    u16  cp_reference       = (u16)bin_ptr2idx(cp_bin, cp_ptr);
                    g_array_insert(cp_references, archetype->m_per_entity_cps, num_components, cp_index, cp_reference);

                    return (byte*)cp_ptr;
                }
            }
            return nullptr;
        }

        static void s_free_local_component(archetype_t* archetype, u32 entity_index, u16 component_type_index)
        {
            const u64 bit_mask        = ((u64)1 << component_type_index);
            u64*      occupancy_array = narena::base_ptr_as<u64>(archetype->m_cp_occupancy);
            u64&      occupancy       = occupancy_array[entity_index];
            if (occupancy & bit_mask)
            {
                u16 num_components = (u16)math::countBits(occupancy);
                occupancy          = occupancy & (~bit_mask);

                // find local component index by counting bits before 'bit_index'
                const u16 cp_index = (u16)math::countBits(occupancy & (bit_mask - 1));

                bin16_t* cp_bin = &archetype->m_cp_bins[component_type_index];
                ASSERT(cp_bin->m_memory != nullptr);

                u16*  cp_reference_array = narena::base_ptr_as<u16>(archetype->m_cp_reference);
                u16*  cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                u16   cp_reference       = cp_references[cp_index];
                void* cp_ptr             = bin_idx2ptr(cp_bin, cp_reference);
                bin_free(cp_bin, cp_ptr);

                // remove component reference from the array
                g_array_remove(cp_references, archetype->m_per_entity_cps, num_components, cp_index);
            }
        }

        static void s_free_component(archetype_t* archetype, u32 entity_index, u16 global_cp_type_index)
        {
            ASSERT(global_cp_type_index < archetype->m_max_global_cp_types);
            ASSERT(entity_index < archetype->m_free_index);
            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_cp_type_index];
            if (component_type_index == 0xFFFF)
                return;
            s_free_local_component(archetype, entity_index, component_type_index);
        }

        static bool s_has_component(archetype_t* archetype, u32 entity_index, u16 global_cp_type_index)
        {
            ASSERT(global_cp_type_index < archetype->m_max_global_cp_types);
            ASSERT(entity_index < archetype->m_free_index);
            const u16  component_type_index = archetype->m_global_to_local_cp_type[global_cp_type_index];
            if (component_type_index == 0xFFFF)
                return false;
            const u64* occupancy_array      = narena::base_ptr_as<const u64>(archetype->m_cp_occupancy);
            const u64  occupancy            = occupancy_array[entity_index];
            const u64  bit_mask             = ((u64)1 << component_type_index);
            return (occupancy & bit_mask) != 0;
        }

        static byte* s_get_component(archetype_t* archetype, u32 entity_index, u16 global_cp_type_index)
        {
            ASSERT(global_cp_type_index < archetype->m_max_global_cp_types);
            ASSERT(entity_index < archetype->m_free_index);

            const u16 component_type_index = archetype->m_global_to_local_cp_type[global_cp_type_index];
            if (component_type_index == 0xFFFF)
                return nullptr;
            const u64 bit_mask             = ((u64)1 << component_type_index);

            const u64* occupancy_array = narena::base_ptr_as<const u64>(archetype->m_cp_occupancy);
            const u64  occupancy       = occupancy_array[entity_index];

            if (occupancy & bit_mask)
            {
                // count the bits that are before 'bit_index' to find the local index (popcount)
                const s32 cp_index = (s32)math::countBits(occupancy & (bit_mask - 1));

                const u16* cp_reference_array = narena::base_ptr_as<const u16>(archetype->m_cp_reference);
                const u16* cp_references      = cp_reference_array + (entity_index * archetype->m_per_entity_cps);
                const u16  cp_reference       = cp_references[cp_index];

                bin16_t* cp_bin = &archetype->m_cp_bins[component_type_index];
                ASSERT(cp_bin->m_memory != nullptr);

                return (byte*)bin_idx2ptr(cp_bin, cp_reference);
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
                entity_index = nstatevec18::alloc(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index);
            }
            else
            {
                if (archetype->m_free_index >= ECS_ARCHETYPE_MAX_ENTITIES)
                    return -1;
                entity_index = archetype->m_free_index++;

                nstatevec18::tick_used_lazy(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index, entity_index);

                narena::commit(archetype->m_cp_occupancy, (entity_index + 1) * sizeof(u64));
                narena::commit(archetype->m_cp_reference, (entity_index + 1) * archetype->m_per_entity_cps * sizeof(u16));
                narena::commit(archetype->m_tags, (entity_index + 1) * (archetype->m_per_entity_tags >> 3));
                narena::commit(archetype->m_generation, (entity_index + 1) * sizeof(u8));
            }

            if (entity_index < 0)
                return -1;

            occupancy_array = narena::base_ptr_as<u64>(archetype->m_cp_occupancy) + entity_index;
            reference_array = narena::base_ptr_as<u16>(archetype->m_cp_reference) + (entity_index * archetype->m_per_entity_cps);
            tags_array      = narena::base_ptr_as<u8>(archetype->m_tags) + (entity_index * (archetype->m_per_entity_tags >> 3));

            *occupancy_array = 0;
            g_memclr(reference_array, archetype->m_per_entity_cps * sizeof(u16));
            g_memclr(tags_array, archetype->m_per_entity_tags >> 3);

            archetype->m_alive_count++;

            return entity_index;
        }

        static bool s_is_alive(archetype_t const* archetype, entity_t entity)
        {
            const u32 entity_index = g_entity_index(entity);
            if (entity_index >= archetype->m_free_index)
                return false;
            if (!nstatevec18::get(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index, entity_index))
                return false;
            return narena::base_ptr_as<const u8>(archetype->m_generation)[entity_index] == g_entity_generation(entity);
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

            nstatevec18::set_free(&archetype->m_free_bin0, archetype->m_free_bin1, &archetype->m_alive_bin0, archetype->m_alive_bin1, archetype->m_bin2, archetype->m_free_index, entity_index);
            narena::base_ptr_as<u8>(archetype->m_generation)[entity_index]++;

            archetype->m_alive_count--;
        }

        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        // --------------------------------------------------------------------------------------------------------
        struct ecs_t
        {
            DCORE_CLASS_PLACEMENT_NEW_DELETE
            arena_t*     m_arena;
            u32          m_archetypes_capacity;
            archetype_t* m_archetypes; // array of archetype pointers
        };

        static archetype_t* s_get_alive_archetype(ecs_t* ecs, entity_t entity)
        {
            if (g_entity_is_null(entity))
                return nullptr;
            const u8 archetype_index = g_entity_archetype_index(entity);
            if (archetype_index >= ecs->m_archetypes_capacity)
                return nullptr;
            archetype_t* archetype = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena == nullptr || !s_is_alive(archetype, entity))
                return nullptr;
            return archetype;
        }

        void g_register_archetype(ecs_t* ecs, u8 archetype_index, u8 components_per_entity, u16 max_global_component_types, u8 tags_per_entity, u16 max_global_tag_types)
        {
            ASSERT(archetype_index < ecs->m_archetypes_capacity);
            if (archetype_index >= ecs->m_archetypes_capacity)
                return;
            archetype_t* archetype = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena != nullptr)
                return;
            s_initialize_archetype(archetype, components_per_entity, max_global_component_types, tags_per_entity, max_global_tag_types);
        }

        ecs_t* g_create_ecs(u8 max_archetypes)
        {
            const int_t ecs_size = (sizeof(ecs_t) + (sizeof(archetype_t) * max_archetypes));
            arena_t*    arena    = narena::new_arena(ecs_size, ecs_size);

            ecs_t* ecs                 = g_allocate<ecs_t>(arena);
            ecs->m_arena               = arena;
            ecs->m_archetypes_capacity = max_archetypes;
            ecs->m_archetypes          = g_allocate_array_and_clear<archetype_t>(arena, max_archetypes);

            return ecs;
        }

        void g_destroy_ecs(ecs_t* ecs)
        {
            arena_t* arena = ecs->m_arena;
            for (u32 i = 0; i < ecs->m_archetypes_capacity; i++)
            {
                archetype_t* archetype = &ecs->m_archetypes[i];
                if (archetype->m_archetype_arena != nullptr)
                    s_destroy(archetype);
            }
            narena::destroy(arena);
        }

        entity_t g_create_entity(ecs_t* ecs, u8 archetype_index)
        {
            if (archetype_index >= ecs->m_archetypes_capacity)
                return ECS_ENTITY_NULL;
            archetype_t* archetype    = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena == nullptr)
                return ECS_ENTITY_NULL;
            const s32    entity_index = s_create_entity(archetype);
            if (entity_index < 0)
                return ECS_ENTITY_NULL;
            const u8 generation = narena::base_ptr_as<const u8>(archetype->m_generation)[entity_index];
            return s_entity_make(generation, archetype_index, (entity_index_t)entity_index);
        }

        void g_destroy_entity(ecs_t* ecs, entity_t e)
        {
            if (g_entity_is_null(e))
                return;
            const u8     archetype_index = g_entity_archetype_index(e);
            if (archetype_index >= ecs->m_archetypes_capacity)
                return;
            archetype_t* archetype       = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena == nullptr || !s_is_alive(archetype, e))
                return;
            s_destroy_entity(archetype, g_entity_index(e));
        }

        void g_register_component_type(ecs_t* ecs, u8 archetype_index, u16 cp_index, u32 cp_sizeof)
        {
            if (archetype_index >= ecs->m_archetypes_capacity)
                return;
            archetype_t* archetype = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena == nullptr)
                return;
            s_register_component_type(archetype, cp_index, cp_sizeof);
        }

        void g_register_tag_type(ecs_t* ecs, u8 archetype_index, u16 tg_index)
        {
            if (archetype_index >= ecs->m_archetypes_capacity)
                return;
            archetype_t* archetype = &ecs->m_archetypes[archetype_index];
            if (archetype->m_archetype_arena == nullptr)
                return;
            s_register_tag_type(archetype, tg_index);
        }

        bool g_has_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || cp_index >= archetype->m_max_global_cp_types)
                return false;
            return s_has_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void* g_add_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || cp_index >= archetype->m_max_global_cp_types)
                return nullptr;
            return s_alloc_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void g_rem_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || cp_index >= archetype->m_max_global_cp_types)
                return;
            s_free_component(archetype, g_entity_index(entity), (u16)cp_index);
        }
        void* g_get_cp(ecs_t* ecs, entity_t entity, u32 cp_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || cp_index >= archetype->m_max_global_cp_types)
                return nullptr;
            return s_get_component(archetype, g_entity_index(entity), (u16)cp_index);
        }

        // Tags
        bool g_has_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || tg_index >= archetype->m_max_global_tag_types)
                return false;
            const u8 tag_type_index = archetype->m_global_to_local_tag_type[tg_index];
            if (tag_type_index == 0xFF)
                return false;
            return s_has_tag(archetype, entity, tag_type_index);
        }

        void g_add_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || tg_index >= archetype->m_max_global_tag_types)
                return;
            const u8 tag_type_index = archetype->m_global_to_local_tag_type[tg_index];
            if (tag_type_index != 0xFF)
                s_add_tag(archetype, entity, tag_type_index);
        }

        void g_rem_tag(ecs_t* ecs, entity_t entity, u16 tg_index)
        {
            archetype_t* archetype = s_get_alive_archetype(ecs, entity);
            if (archetype == nullptr || tg_index >= archetype->m_max_global_tag_types)
                return;
            const u8 tag_type_index = archetype->m_global_to_local_tag_type[tg_index];
            if (tag_type_index != 0xFF)
                s_rem_tag(archetype, entity, tag_type_index);
        }

        en_iterator_t::en_iterator_t(ecs_t* ecs, u8 archetype_index)
            : m_archetype(nullptr)
            , m_ref_cp_occupancy(0)
            , m_ref_tag_occupancy(0)
            , m_entity_index(-1)
        {
            m_archetype_index = archetype_index;
            if (archetype_index < ecs->m_archetypes_capacity && ecs->m_archetypes[archetype_index].m_archetype_arena != nullptr)
                m_archetype = &ecs->m_archetypes[m_archetype_index];
        }

        entity_t en_iterator_t::entity() const
        {
            if (m_archetype == nullptr || m_entity_index < 0)
                return ECS_ENTITY_NULL;
            const u8 generation = narena::base_ptr_as<const u8>(m_archetype->m_generation)[m_entity_index];
            return s_entity_make(generation, m_archetype_index, m_entity_index);
        }

        void en_iterator_t::mark_cp(u32 cp_index)
        {
            if (m_archetype == nullptr)
                return;
            ASSERT(cp_index < m_archetype->m_max_global_cp_types);
            const u16 component_type_index = m_archetype->m_global_to_local_cp_type[cp_index];
            ASSERT(component_type_index != 0xFFFF);
            m_ref_cp_occupancy |= ((u64)1 << component_type_index);
        }

        void en_iterator_t::mark_tag(u16 tg_index)
        {
            if (m_archetype == nullptr)
                return;
            ASSERT(tg_index < m_archetype->m_max_global_tag_types);
            if (tg_index >= m_archetype->m_max_global_tag_types)
                return;
            const u8 tag_type_index = m_archetype->m_global_to_local_tag_type[tg_index];
            ASSERT(tag_type_index != 0xFF);
            m_ref_tag_occupancy |= ((u32)1 << tag_type_index);
        }

        void en_iterator_t::begin()
        {
            // Start from the first alive entity
            m_entity_index = m_archetype != nullptr ? find(0) : -1;
        }

        s32 en_iterator_t::find(s32 entity_index) const
        {
            while (entity_index >= 0 && (u32)entity_index < m_archetype->m_free_index)
            {
                const bool alive = nstatevec18::get(&m_archetype->m_free_bin0, m_archetype->m_free_bin1, &m_archetype->m_alive_bin0, m_archetype->m_alive_bin1, m_archetype->m_bin2, m_archetype->m_free_index, entity_index);
                if (alive)
                {
                    u64 const* cur_cp_occupancy = (u64*)&m_archetype->m_cp_occupancy->m_base[entity_index * sizeof(u64)];
                    if ((*cur_cp_occupancy & m_ref_cp_occupancy) == m_ref_cp_occupancy)
                    {
                        const u32   tag_stride     = m_archetype->m_per_entity_tags >> 3;
                        byte const* entity_tags   = m_archetype->m_tags->m_base + (entity_index * tag_stride);
                        u32         tag_occupancy = 0;
                        for (u32 byte_index = 0; byte_index < tag_stride; ++byte_index)
                            tag_occupancy |= ((u32)entity_tags[byte_index]) << (byte_index * 8);
                        if ((tag_occupancy & m_ref_tag_occupancy) == m_ref_tag_occupancy)
                            return entity_index;
                    }
                }
                entity_index =
                    nstatevec18::find_used_after(&m_archetype->m_free_bin0, m_archetype->m_free_bin1, &m_archetype->m_alive_bin0, m_archetype->m_alive_bin1, m_archetype->m_bin2, m_archetype->m_free_index, entity_index);
            }
            return -1;
        }

    } // namespace necs4
} // namespace ncore
