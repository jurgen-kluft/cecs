#ifndef __XECS_ECS_TAG_STORE_H__
#define __XECS_ECS_TAG_STORE_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xecs/private/x_ecs_types.h"

namespace xcore
{
    struct tg_nctype_t
    {
        u32         cp_id;
        const char* cp_name;
    };

    // struct tag_store_mgr_t
    // {
    //     enum
    //     {
    //         TAGS_MAX = 256,
    //     };
    //     u32          m_a_tg_hbb[35]; // To identify which component stores are still free (to give out new component id)
    //     tg_type_t*   m_a_tg_type;    // The type information attached to each store
    //     tg_store_t** m_a_tg_store;   // N max number of components (1024 * 8 = 8192 bytes)
    // };

    static void s_tg_store_init(hbb_t& ts, u32 count, alloc_t* allocator) { g_hbb_resize(ts, count, 0, allocator); }
    static void s_tg_store_exit(hbb_t& ts, alloc_t* allocator) { g_hbb_release(ts, allocator); }

    static void s_init(tag_store_mgr_t* ts, alloc_t* allocator)
    {
        g_hbb_init(ts->m_a_tg_hbb, tag_store_mgr_t::TAGS_MAX, 1);
        ts->m_a_tg_type  = (tg_type_t*)allocator->allocate(sizeof(tg_type_t) * tag_store_mgr_t::TAGS_MAX);
        ts->m_a_tg_store = (hbb_t*)allocator->allocate(sizeof(hbb_t) * tag_store_mgr_t::TAGS_MAX);
        x_memset(ts->m_a_tg_store, 0, sizeof(hbb_t) * tag_store_mgr_t::TAGS_MAX);
    }

    static void s_exit(tag_store_mgr_t* ts, alloc_t* allocator)
    {
        for (u32 i = 0; i < tag_store_mgr_t::TAGS_MAX; ++i)
        {
            s_tg_store_exit(ts->m_a_tg_store[i], allocator);
        }
        allocator->deallocate(ts->m_a_tg_type);
        allocator->deallocate(ts->m_a_tg_store);
        ts->m_a_tg_type  = nullptr;
        ts->m_a_tg_store = nullptr;
        g_hbb_init(ts->m_a_tg_hbb, tag_store_mgr_t::TAGS_MAX, 1);
    }

    static tg_type_t const* s_register_tag_type(tag_store_mgr_t* store, const char* name) { return nullptr; }

} // namespace xcore

#endif // __XECS_ECS_TAG_STORE_H__