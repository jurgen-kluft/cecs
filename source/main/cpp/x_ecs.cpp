#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xbase/x_debug.h"
#include "xbase/x_integer.h"
#include "xbase/x_memory.h"
#include "xecs/x_ecs.h"

#include "xecs/private/x_ecs_cp_store.h"
#include "xecs/private/x_ecs_entity0.h"
#include "xecs/private/x_ecs_entity2.h"

namespace xcore
{
    struct ecs2_t
    {
        alloc_t*           m_allocator;
        components_store_t m_component_store;
        entity0_store_t    m_entity0_store;
        entity2_store_t    m_entity2_store;
    };

    ecs2_t* g_ecs_create(alloc_t* allocator)
    {
        ecs2_t* ecs      = (ecs2_t*)allocator->allocate(sizeof(ecs2_t));
        ecs->m_allocator = allocator;

        s_init(&ecs->m_component_store, allocator);
        s_init(&ecs->m_entity0_store, allocator);
        s_init(&ecs->m_entity2_store, 1024, 4, allocator);

        return ecs;
    }

    cp_type_t const* g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name)
    {
        return s_cp_register_cp_type(&r->m_component_store, cp_sizeof, cp_name);
    }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        entity0_t* ei = &ecs->m_entity0_store.m_array[g_entity_id(e)];

        u8 const shard_bit = cp.cp_id / 32;
        if ((ei->m_cp1_bitset & (1 << shard_bit)) != 0)
        {
            s8 const shard_idx = s_compute_index(ei->m_cp1_bitset, shard_bit);
            return (ei->m_cp2_bitset[shard_idx] & (1 << shard_bit)) != 0;
        }
        return false;
    }

    static void* s_entity_get_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity0_t* ei = &ecs->m_entity0_store.m_array[g_entity_id(e)];

        u8 const shard_bit = cp_type.cp_id / 32;
        if ((ei->m_cp1_bitset & (1 << shard_bit)) != 0)
        {
            u8 const shard_idx = s_compute_index(ei->m_cp1_bitset, shard_bit);
            if ((ei->m_cp2_bitset[shard_idx] & (1 << shard_bit)) != 0)
            {
                // so now we need to retrieve the component data offset from entity2_t
                u16 const        en2_a_i = (ei->m_en2_index.get_index());
                entity2_array_t* a_en2   = &ecs->m_entity2_store.m_arrays[en2_a_i];
                u16 const        en2_i   = (ei->m_en2_index.get_offset());
                entity2_t*       en2     = &a_en2->m_array[en2_i];

                // The 32 bits of m_cp2_bitcnt are split into 5 groups of bits with the following width in order [8,7,7,6,0]
                // The group with 4 bits is always 0, group with 5 bits indicates how many bits where set in m_cp2_bitset below index 'shard_idx'.
                s8 const  shifts[] = {0, 0, 6, 13, 20};
                u16 const masks[]  = {0x000, 0x03F, 0x07F, 0x07F, 0xFF};
                u32 const adders[] = {0, (1<<0), (1<<0) | (1<<6), (1<<0) | (1<<6) | (1<<13), (1<<0) | (1<<6) | (1<<13) | (1<<20), (1<<0) | (1<<6) | (1<<13) | (1<<20) };
                u32 const cp_a_i   = ((ei->m_cp2_bitcnt >> shifts[shard_idx]) & masks[shard_idx]) + s_compute_index(ei->m_cp2_bitset[shard_idx], shard_bit);
                u32 const cp_i     = en2->m_cp_data_offset[cp_a_i];

                cp_store_t* cp_store = &ecs->m_component_store.m_a_cp_store[cp_type.cp_id];
                ASSERT(cp_store != nullptr);

                return s_cp_store_get_cp(cp_store, cp_type, cp_i);
            }
        }
        return nullptr;
    }

    // Attach requested component to the entity
    static void s_entity_attach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        // set/register a component on an entity.
        // Depending on the bit position we need to insert an index into the cp_inf->m_cp_data_offset[] array, this
        // means moving up some entries.
        if (ecs->m_component_store.m_a_cp_store[cp_type.cp_id].m_cp_data == nullptr)
        {
            s_cp_store_init(&ecs->m_component_store.m_a_cp_store[cp_type.cp_id], cp_type, ecs->m_allocator);
        }

        u32 const cp_index = s_cp_store_alloc_cp(&ecs->m_component_store.m_a_cp_store[cp_type.cp_id], cp_type, ecs->m_allocator);
    }

    // Remove/detach component from the entity
    static bool s_entity_remove_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        // Remove component from the shard and bitset
        // Remove component data offset entry from entity2_t
        // Remove component data from cp_store

        return false;
    }

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    const entity_t g_null_entity = (entity_t)ECS_ENTITY_ID_MASK;


    // primed size list reaching until 8 M
    // 63 items (0x3F)
    static constexpr const u32 s_prime_size_list_count = 63;
    static constexpr const u32 s_prime_size_list[]     = {2,     3,      5,      7,      11,     13,     17,     23,     29,     37,     47,     59,      73,      97,      127,     151,     197,     251,     313,     397,     499,
                                                      631,   797,    1009,   1259,   1597,   2011,   2539,   3203,   4027,   5087,   6421,   8089,    10193,   12853,   16193,   20399,   25717,   32401,   40823,   51437,   64811,
                                                      81649, 102877, 129607, 163307, 205759, 259229, 326617, 411527, 518509, 653267, 823117, 1037059, 1306601, 1646237, 2074129, 2613229, 3292489, 4148279, 5226491, 6584983, 8388607};

} // namespace xcore
