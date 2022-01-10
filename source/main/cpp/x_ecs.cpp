#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xbase/x_debug.h"
#include "xbase/x_integer.h"
#include "xbase/x_memory.h"
#include "xecs/x_ecs.h"

#include "xecs/private/x_ecs_cp_store.h"
#include "xecs/private/x_ecs_entity2.h"
#include "xecs/private/x_ecs_entity0.h"

namespace xcore
{
    const entity_t g_null_entity = (entity_t)0xFFFFFFFF;

    // --------------------------------------------------------------------------------------------------------
    // entity component system, create and destroy

    ecs2_t* g_ecs_create(alloc_t* allocator)
    {
        ecs2_t* ecs      = (ecs2_t*)allocator->allocate(sizeof(ecs2_t));
        ecs->m_allocator = allocator;
        s_init(&ecs->m_component_store, allocator);
        s_init(&ecs->m_entity0_store, allocator);
        return ecs;
    }

    void g_ecs_destroy(ecs2_t* ecs)
    {
        alloc_t* allocator = ecs->m_allocator;
        s_exit(&ecs->m_entity0_store, allocator);
        s_exit(&ecs->m_component_store, allocator);
        allocator->deallocate(ecs);
    }

    // --------------------------------------------------------------------------------------------------------
    // hierarchical bit buffer (max 32768 bits, 3 levels)

    void s_init(entity_hbb_t* ehbb, u32 size, alloc_t* allocator)
    {
        ehbb->m_size = size;   
        ehbb->m_level2 = 0xFFFFFFFF;
        
        size = (size + (32 - 1)) & ~(32 - 1); // align up, multiple of 32
        u32 const level0_size = size >> 5;
        u32 const level1_size = ((size + 31) >> 5);
        ehbb->m_level0 = allocator->allocate(sizeof(u32) * (level0_size + level1_size));
        ehbb->m_level1 = ehbb->m_level0 + level0_size;
        x_memset(ehbb->m_level0, 0xFFFFFFFF, sizeof(u32) * (level0_size + level1_size));

        // clear the part in the hierarchy that falls outside of the incoming 
    }

    void s_exit(entity_hbb_t* ehbb, alloc_t* allocator)
    {
        allocator->deallocate(ehbb->m_level0);
        ehbb->m_level2 = 0;
        ehbb->m_level1 = 0;
        ehbb->m_level0 = 0;
    }


    cp_type_t const* g_register_component_type(ecs2_t* r, u32 cp_sizeof, const char* cp_name) { return s_cp_register_cp_type(&r->m_component_store, cp_sizeof, cp_name); }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        entity0_t* ei = &ecs->m_entity0_store.m_array[g_entity_id(e)];
        return s_en0_has_cp(ei, cp.cp_id);
    }

    static void* s_entity_get_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity0_t* e0 = &ecs->m_entity0_store.m_array[g_entity_id(e)];

        u8 const shard_bit = 1 << (cp_type.cp_id >> 5);
        if ((e0->m_cp1_bitset & shard_bit) != 0)
        {
            u8 const shard_idx    = s_compute_index(e0->m_cp1_bitset, shard_bit);
            s8 const shard_cp_bit = 1 << (cp_type.cp_id & 0x1F);
            if ((e0->m_cp2_bitset[shard_idx] & shard_cp_bit) != 0)
            {
                entity2_t* en2 = s_get_entity2(&ecs->m_entity2_store, e0->m_en2_index);

                // The 32 bits of m_cp2_bitcnt are split into 5 groups of bits with the following width in order [8,7,7,6,0]
                // The group with 4 bits is always 0, group with 5 bits indicates how many bits where set in m_cp2_bitset below index 'shard_idx'.
                s16 const cp_a_i = s_get_en2_cp_index(e0, shard_idx, shard_cp_bit);
                u32 const cp_do  = en2->m_cp_data_offset[cp_a_i];

                cp_store_t* cp_store = &ecs->m_component_store.m_a_cp_store[cp_type.cp_id];
                ASSERT(cp_store != nullptr);

                return s_cp_store_get_cp(cp_store, cp_type, cp_do);
            }
        }
        return nullptr;
    }

    // Attach requested component to the entity
    static false s_entity_attach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        if (ecs->m_component_store.m_a_cp_store[cp_type.cp_id].m_cp_data == nullptr)
        {
            s_cp_store_init(&ecs->m_component_store.m_a_cp_store[cp_type.cp_id], cp_type, ecs->m_allocator);
        }

        entity0_t* e0 = s_get_entity0(&ecs->m_entity0_store, e);
        s8 const shard_count = xcountBits(e0->m_cp1_bitset);

        u32 const cp1_bitset = e0->m_cp1_bitset | (1<<(cp_type.cp_id>>5));
        s8 const new_shard_count = xcountBits(cp1_bitset);
        if (new_shard_count == 6)
            return false;

        s8 const shard_idx = s_compute_index(e0);
        if (shard_count > 0 && shard_count != new_shard_count)
        {
            // Insert new shard
            for (s32 s=shard_count; s>shard_idx; ++s)
            {
                e0->m_cp2_bitcnt[s] = e0->m_cp2_bitcnt[s-1];
                e0->m_cp2_bitset[s] = e0->m_cp2_bitset[s-1];
            }
        }

        // what is the index of the component in the en2->m_cp_data_offset[] array?
        // does entity2_t have space for an additional component or do we need to reallocate it

        u32 const cp_offset = s_component_alloc(&ecs->m_component_store, &cp_type, ecs->m_allocator);


        e0->m_cp1_bitset = cp1_bitset;
    }

    static bool s_entity_update_component(ecs2_t* ecs, entity_t e, u32 cp_id, u32 cp_data_offset)
    {
        // Remove component from the shard and bitset
        // Remove component data offset entry from entity2_t
        // Remove component data from cp_store

        return false;
    }

    // Remove/detach component from the entity
    static bool s_entity_detach_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        // Remove component from the shard and bitset
        // Remove component data offset entry from entity2_t
        // Remove component data from cp_store

        return false;
    }

    bool  g_attach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_attach_component(ecs, entity, *cp_type); }
    void  g_dettach_component(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { s_entity_detach_component(ecs, entity, *cp_type); }
    void* g_get_component_data(ecs2_t* ecs, entity_t entity, cp_type_t const* cp_type) { return s_entity_get_component(ecs, entity, *cp_type); }

    entity_t g_create_entity(entity_hbb_t* ehbb, entity0_store_t* es)
    {
        // A hierarchical bitset can quickly tell us the lowest free entity
        s8 const o3 = xfindFirstBit(es->m_level3);
        if (o3 == -1) // No more free entities
            return g_null_entity;

        s8 const o2 = xfindFirstBit(es->m_level2[o3]);
        s8 const o1 = xfindFirstBit(es->m_level1[(o3 * 32) + o2]);

        // o0 is the full index into level 0
        u32 const o0 = ((u32)o3 * 32 + (u32)o2) * 32 + (u32)o1;

        // the byte in level 0 is an extra offset on top of o0 that gives us
        // an index of a free entity in the m_a_entity0 array.
        u32 const eo = o0 + es->m_level0[o0];

        entity0_t& e = es->m_array[eo];
        ASSERT(e.m_en2_index.get_offset() == ECS_ENTITY_ID_MASK);

        // get the next entity that is free in this linked list
        u32 const ne = e.m_en2_index.get_index();

        // however there could be no more free entities (end of list)
        if (ne == ECS_ENTITY_VERSION_MAX)
        {
            // You can point to any index, but we will be marked as full
            es->m_level0[o0] = 0xCD;

            if (s_clr_bit_in_u32(es->m_level1[(o3 * 32) + o2], o1) == 0)
            {
                if (s_clr_bit_in_u32(es->m_level2[o3], o2) == 0)
                {
                    s_clr_bit_in_u32(es->m_level3, o3);
                }
            }
        }
        else
        {
            // Update the head of the linked list
            es->m_level0[o0] = (u8)ne;
        }

        // Need to create entity2_t or do we delay it until an actual component is registered on the entity?
        e.m_en2_index    = index_t();
        e.m_cp1_bitset   = 0;
        e.m_cp2_bitcnt32 = 0;
        for (s32 i = 0; i < 5; ++i)
            e.m_cp2_bitset[i] = 0;

        return index_t(0, eo).m_value;
    }

    void g_delete_entity(ecs2_t* ecs, entity_t entity)
    {
        entity0_t* e0 = s_get_entity0(&ecs->m_entity0_store, entity);
        entity2_t* e2 = s_get_entity2(&ecs->m_entity2_store, e0->m_en2_index);

        // release all components
        u32 bitset1  = e0->m_cp1_bitset;
        s32 en2_cp_i = 0;
        while (bitset1 != 0)
        {
            s32 const shard_index = xfindFirstBit(bitset1);

            u32 bitset0 = e0->m_cp2_bitset[shard_index];
            while (bitset0 != 0)
            {
                s32 const shard_cp_index = xfindFirstBit(bitset0);
                u32 const cp_id          = (shard_index * 32) + shard_cp_index;
                u32 const cp_data_offset = s_get_u24(e2->m_cp_data_offset, en2_cp_i);

                entity_id_t changed_entity_id;
                u32         changed_cp_data_offset;
                s_component_dealloc(&ecs->m_component_store, cp_id, cp_data_offset, changed_entity_id, changed_cp_data_offset);
                if (g_entity_id(entity) != changed_entity_id)
                {
                    s_entity_update_component(ecs, changed_entity_id, cp_id, changed_cp_data_offset);
                }

                en2_cp_i += 1;
                bitset0 &= (bitset0 - 1);
            }

            bitset1 &= (bitset1 - 1);
        }
    }
} // namespace xcore
