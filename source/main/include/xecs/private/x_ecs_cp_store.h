#ifndef __XECS_ECS_CP_STORE_H__
#define __XECS_ECS_CP_STORE_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/private/x_ecs_types.h"

namespace xcore
{
    static constexpr const s32 s_cp_store_capacities_count = 60;
    static constexpr const u32 s_cp_store_capacities[]     = {4,      6,      10,     14,     22,     26,     34,     46,     58,     74,     94,      118,     146,     194,     254,     302,     394,     502,     626,     794,
                                                          998,    1262,   1594,   2018,   2518,   3194,   4022,   5078,   6406,   8054,   10174,   12842,   16178,   20386,   25706,   32386,   40798,   51434,   64802,   81646,
                                                          102874, 129622, 163298, 205754, 259214, 326614, 411518, 518458, 653234, 823054, 1037018, 1306534, 1646234, 2074118, 2613202, 3292474, 4148258, 5226458, 6584978, 8296558};

    struct cp_nctype_t
    {
        u32         cp_id;
        u32         cp_sizeof;
        const char* cp_name;
    };

    static u32 s_cp_store_size(u32 type_count, u32 cp_count, u32 cp_sizeof)
    {
        u32 size = sizeof(cp_store_t);
        size += ((type_count + 3) & ~3) * sizeof(u16);
        size += ((type_count + 1) & ~1) * sizeof(u32);
        size += ((cp_count + 7) & ~7);
        size += ((cp_count * cp_sizeof) + 63) & ~63; // Align to 8 bytes for the components data
        return size;
    }

    static inline u32 s_cp_data_used_num_u64(u32 cp_count) { return ((((cp_count + 7) / 8) + 7) / 8); }

    static u32 s_cp_store_grow(cp_store_t*& src, alloc_t* allocator, u32 cp_sizeof, u16 en_type_id, u32 cp_count)
    {
        u32 const   dst_type_count = (src == nullptr) ? 1 : (src->m_types + 1);
        u32 const   dst_cp_count   = (src == nullptr) ? cp_count : (src->m_size + cp_count);
        cp_store_t* dst            = (cp_store_t*)(allocator->allocate(s_cp_store_size(dst_type_count, dst_cp_count, cp_sizeof) * sizeof(u8)));
        dst->m_size                = dst_cp_count;
        dst->m_types               = dst_type_count;
        u16* a_dst_type_ids        = (u16*)(dst + 1);
        u32* a_dst_type_dof        = (u32*)(a_dst_type_ids + ((dst->m_types + 3) & ~3));
		dst->m_cp_data_used        = (u64*)(a_dst_type_dof + ((dst->m_types + 1) & ~1));
		dst->m_cp_data             = (u8*)(dst->m_cp_data_used + s_cp_data_used_num_u64(dst->m_size));

        u32 const data_offset = src == nullptr ? 0 : src->m_size;
        if (src != nullptr)
        {
            u16* a_src_type_ids = (u16*)(src + 1);
            u32* a_src_type_dof = (u32*)(a_src_type_ids + ((dst->m_types + 3) & ~3));
            s32  c              = dst->m_types;
            while (c > 0)
            {
                *a_dst_type_ids++ = *a_src_type_ids++;
                *a_dst_type_dof++ = *a_src_type_dof++;
            }
            *a_dst_type_ids = en_type_id;
            *a_dst_type_dof = data_offset;

            // Copy 'used' bit array
            u64*       dst_cp_data_used_iter = dst->m_cp_data_used;
            u64*       dst_cp_data_used_end  = dst->m_cp_data_used + s_cp_data_used_num_u64(dst->m_size);
            u64 const* src_cp_data_used_iter = src->m_cp_data_used;
            u64 const* src_cp_data_used_end  = src->m_cp_data_used + s_cp_data_used_num_u64(src->m_size);
            while (src_cp_data_used_iter < src_cp_data_used_end)
                *dst_cp_data_used_iter++ = *src_cp_data_used_iter++;
            // Reset the additional part
            while (dst_cp_data_used_iter < dst_cp_data_used_end)
                *dst_cp_data_used_iter++ = 0;

            // Copy component data array (total size is aligned to 8 bytes, so we can copy 8 bytes at a time)
            u64*       dst_cp_data_iter = (u64*)dst->m_cp_data;
            u64*       dst_cp_data_end  = (u64*)dst->m_cp_data + (dst->m_size * cp_sizeof);
            u64 const* src_cp_data_iter = (u64 const*)src->m_cp_data;
            u64 const* src_cp_data_end  = (u64 const*)src->m_cp_data + (dst->m_size * cp_sizeof);
            while (src_cp_data_iter < src_cp_data_end)
                *dst_cp_data_iter++ = *src_cp_data_iter++;

            //
            allocator->deallocate(src);
        }
        else
        {
            *a_dst_type_ids = en_type_id;
            *a_dst_type_dof = 0;
        }

        src = dst;
        return data_offset;
    }

    static void s_cp_store_exit(cp_store_t*& cp_store, alloc_t* allocator)
    {
        if (cp_store != nullptr)
        {
            allocator->deallocate(cp_store);
            cp_store = nullptr;
        }
    }

    static void s_init(cp_store_mgr_t* cps, alloc_t* allocator)
    {
        g_hbb_init(cps->m_a_cp_hbb, cp_store_mgr_t::COMPONENTS_MAX, 1);
        cps->m_a_cp_type  = (cp_type_t*)allocator->allocate(sizeof(cp_type_t) * cp_store_mgr_t::COMPONENTS_MAX);
        cps->m_a_cp_store = (cp_store_t**)allocator->allocate(sizeof(cp_store_t*) * cp_store_mgr_t::COMPONENTS_MAX);
        x_memset(cps->m_a_cp_store, 0, sizeof(cp_store_t) * cp_store_mgr_t::COMPONENTS_MAX);
    }

    static void s_exit(cp_store_mgr_t* cps, alloc_t* allocator)
    {
        for (u32 i = 0; i < cp_store_mgr_t::COMPONENTS_MAX; ++i)
        {
            s_cp_store_exit(cps->m_a_cp_store[i], allocator);
        }
        allocator->deallocate(cps->m_a_cp_type);
        allocator->deallocate(cps->m_a_cp_store);
        cps->m_a_cp_type  = nullptr;
        cps->m_a_cp_store = nullptr;
        g_hbb_init(cps->m_a_cp_hbb, cp_store_mgr_t::COMPONENTS_MAX, 1);
    }

    static cp_type_t const* s_cp_register_cp_type(cp_store_mgr_t* cps, u32 cp_sizeof, const char* name)
    {
        u32 cp_id;
        if (g_hbb_find(cps->m_a_cp_hbb, cp_id))
        {
            cp_nctype_t* cp_type     = (cp_nctype_t*)&cps->m_a_cp_type[cp_id];
            cp_type[cp_id].cp_id     = cp_id;
            cp_type[cp_id].cp_sizeof = cp_sizeof;
            cp_type[cp_id].cp_name   = name;

            g_hbb_clr(cps->m_a_cp_hbb, cp_id);
            return ((cp_type_t const*)cp_type);
        }
        return nullptr;
    }

    static cp_type_t* s_cp_get_cp_type(cp_store_mgr_t* cps, u32 cp_id)
    {
        cp_type_t* cp_type = (cp_type_t*)&cps->m_a_cp_type[cp_id];
        return cp_type;
    }

    static void s_cp_unregister_cp_type(cp_store_mgr_t* cps, cp_type_t const* cp_type)
    {
        u32 const cp_id = cp_type->cp_id;
        g_hbb_set(cps->m_a_cp_hbb, cp_id);
    }

    static inline u8* s_cp_store_get_cp(u8* cp_store, cp_type_t const& cp_type, u32 cp_offset)
    {
        u8* cp_data = cp_store + (cp_type.cp_sizeof * cp_offset);
        return cp_data;
    }

    static u32 s_cp_store_alloc_cp(cp_store_t*& cp_store, cp_type_t const& cp_type, u16 en_type_id, u32 cp_count, alloc_t* allocator)
    {
        u32 const data_offset = s_cp_store_grow(cp_store, allocator, cp_type.cp_sizeof, en_type_id, cp_count);
        return data_offset;
    }

    static void s_cp_store_dealloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 offset, u32 count)
    {
        // TODO: Tricky since we have no idea how to "remove" a slice out of the whole array without updating
        // other slice owners about an update to their offset.
        // One idea is to have a book keeping array that stores [entity type id, offset], we can then use that
        // to identify which entity type ids need to be informed of an update of their offset.
        return;
    }

    static u32 s_components_alloc(cp_store_mgr_t* cps, cp_type_t const* cp_type, u16 en_type_id, u32 count, alloc_t* allocator)
    {
        cp_store_t* cp_store = cps->m_a_cp_store[cp_type->cp_id];
        return s_cp_store_alloc_cp(cp_store, *cp_type, en_type_id, count, allocator);
    }

    static void s_components_dealloc(cp_store_mgr_t* cps, cp_type_t const* cp_type, u32 offset, u32 count)
    {
        cp_store_t* cp_store = cps->m_a_cp_store[cp_type->cp_id];
        s_cp_store_dealloc_cp(cp_store, *cp_type, offset, count);
    }

    static inline void s_components_set_cp_used(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = cps->m_a_cp_store[cp_type.cp_id];
        u64*        data     = cp_store->m_cp_data_used;
        u64 const   bit      = (u64)1 << (cp_offset & 0x3F);
        data[cp_offset >> 6] |= bit;
    }

    static inline void s_components_set_cp_unused(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = cps->m_a_cp_store[cp_type.cp_id];
        u64*         data     = cp_store->m_cp_data_used;
        u64 const    bit      = (u64)1 << (cp_offset & 0x3F);
        data[cp_offset >> 6] &= ~bit;
    }

    static inline bool s_components_get_cp_used(cp_store_mgr_t const* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = cps->m_a_cp_store[cp_type.cp_id];
        u64 const*   data     = cp_store->m_cp_data_used;
        u64 const    bit      = (u64)1 << (cp_offset & 0x3F);
        return (data[cp_offset >> 6] & bit) != 0;
    }

    static inline u8* s_components_get_cp_data(cp_store_mgr_t* cps, cp_type_t const& cp_type, u32 cp_offset)
    {
        cp_store_t* cp_store = (cp_store_t*)cps->m_a_cp_store[cp_type.cp_id];
		u8*         cp_data = cp_store->m_cp_data;
        return cp_data + (cp_offset * cp_type.cp_sizeof);
    }

} // namespace xcore

#endif