#ifndef __XECS_ECS_ENTITY2_H__
#define __XECS_ECS_ENTITY2_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"
#include "xecs/private/x_ecs_types.h"

namespace xcore
{
    static constexpr const s32 s_entity2_array_capacities_count = 60;
    static constexpr const u32 s_entity2_array_capacities[]     = {0,      4,      6,      10,     14,     22,     26,     34,     46,     58,      74,      94,      118,     146,     194,     254,     302,     394,     502,    626,   794,
                                                               998,    1262,   1594,   2018,   2518,   3194,   4022,   5078,   6406,   8054,    10174,   12842,   16178,   20386,   25706,   32386,   40798,   51434,   64802,  81646, 102874,
                                                               129622, 163298, 205754, 259214, 326614, 411518, 518458, 653234, 823054, 1037018, 1306534, 1646234, 2074118, 2613202, 3292474, 4148258, 5226458, 6584978, 8296558};

    // NOTE: This controls the step size of the entity2_t structure and thus the amount of array's that are managed
    static inline u32 s_compute_entity2_struct_size(u16 entity2_ai) { return (entity2_ai * 12) + 4; }
    static inline u32 s_compute_entity2_array_index(u32 cp_count) { return cp_count >> 2; }

	static void s_exit(entity2_array_t* ea, alloc_t* allocator)
	{
		if (ea->m_array != nullptr)
		{
			allocator->deallocate(ea->m_array);
		}
		ea->m_size = 0;
		ea->m_capi = 0;
	}

    static void s_init(entity2_store_t* es, u32 max_components, u32 sizestep, alloc_t* allocator)
    {
        es->m_num_arrays = (max_components / sizestep);
        es->m_arrays     = (entity2_array_t*)allocator->allocate(sizeof(entity2_array_t) * es->m_num_arrays);
        x_memset(es->m_arrays, 0, es->m_num_arrays * sizeof(entity2_array_t));
    }

    static void s_exit(entity2_store_t* es, alloc_t* allocator)
    {
        for (s32 i = 0; i < es->m_num_arrays; ++i)
			s_exit(&es->m_arrays[i], allocator);
        allocator->deallocate(es->m_arrays);
        es->m_num_arrays = 0;
        es->m_arrays     = nullptr;
    }

    static entity2_t* s_get_entity2(entity2_store_t* es, index_t i)
    {
        u32 const byte_offset = i.get_offset() * s_compute_entity2_struct_size(i.get_index());
        entity2_array_t* a = es->m_arrays[i.get_index()];
        return (entity2_t*)(a->m_array + byte_offset);
    }

    // Ensure the entity2 array has enough space for en2_count new entities
    static void s_alloc_ensure(entity2_store_t* es, u32 en2_count, u32 cp_count, alloc_t* allocator)
    {
        u32 const        ai    = s_compute_entity2_array_index(cp_count);
        entity2_array_t* array = &es->m_arrays[ai];

        if ((array->m_size + en2_count) >= s_entity2_array_capacities[array->m_capi])
        {
            while ((array->m_size + en2_count) >= s_entity2_array_capacities[array->m_capi])
                array->m_capi += 1;

            entity2_t* new_array = (entity2_t*)allocator->allocate(sizeof(entity2_t) * s_entity2_array_capacities[array->m_capi]);
            if (array->m_array != nullptr)
            {
                x_memcpy(new_array, array->m_array, array->m_size * sizeof(entity2_t));
                allocator->deallocate(array->m_array);
            }
            array->m_array = new_array;
        }
    }

    static index_t s_alloc_en2(entity2_store_t* es, u32 cp_count)
    {
        u32 const        ai    = s_compute_entity2_array_index(cp_count);
        entity2_array_t* array = &es->m_arrays[ai];
        index_t          e2(ai, array->m_size);
        array->m_size += 1;
        return e2;
    }

    static index_t s_realloc_en2(entity2_store_t* es, index_t e2, u32 new_cp_count)
    {
        u16 const oai = e2.get_index();
        u32 const nai = s_compute_entity2_array_index(new_cp_count);
        if (oai != nai)
        {
            // old entity structure
            entity2_array_t* oa = &es->m_arrays[oai];
            entity2_t*       oe = &oa->m_array[e2.get_offset()];

            // new entity structure
            entity2_array_t* na = &es->m_arrays[nai];
            index_t const    en(nai, na->m_size);
            entity2_t*       ne = &na->m_array[na->m_size++];

            x_memcpy(oe, ne, s_compute_entity2_struct_size(nai > oai ? oai : nai));
            return en;
        }
        return e2;
    }

    // returns the entity_id_t (entity0_t) that now has entity2_t data at index_t e2
    // or returns 0xFFFFFFF when no update is necessary.
    static entity_id_t s_dealloc_en2(entity2_store_t* es, index_t e2)
    {
        u16 const        cai = e2.get_index();
        entity2_array_t* ca  = &es->m_arrays[cai];

        index_t el(cai, ca->m_size - 1);
        if (el.m_value != e2.m_value)
        {
            // swap removal
            entity2_t*       cep = &ca->m_array[e2.get_offset()];
            entity2_t const* lep = &ca->m_array[el.get_offset()];
            x_memcpy(cep, lep, s_compute_entity2_struct_size(cai));

            // TODO: logic for shrinking the size of the array
            ca->m_size -= 1;

            return lep->m_en0_index;
        }
        return g_null_entity;
    }

} // namespace xcore

#endif