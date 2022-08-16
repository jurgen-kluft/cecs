#include "cbase/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_debug.h"
#include "cbase/c_hbb.h"
#include "cbase/c_integer.h"
#include "cbase/c_memory.h"

#include "cecs/c_ecs.h"

namespace ncore
{
    const entity_t g_null_entity = (entity_t)0xFFFFFFFF;

    typedef u16 entity_ver_t;
    typedef u16 entity_type_id_t;
    typedef u32 entity_id_t;

    const u32 ECS_ENTITY_ID_MASK       = (0x0000FFFF); // Mask to use to get the entity number out of an identifier
    const u32 ECS_ENTITY_TYPE_MASK     = (0x00FF0000); // Mask to use to get the entity type out of an identifier
    const u32 ECS_ENTITY_TYPE_MAX      = (0x000000FF); // Maximum type
    const u32 ECS_ENTITY_VERSION_MASK  = (0xFF000000); // Mask to use to get the version out of an identifier
    const u32 ECS_ENTITY_VERSION_MAX   = (0x000000FF); // Maximum version
    const s16 ECS_ENTITY_TYPE_SHIFT    = (16);         // Extent of the entity id within an identifier
    const s8  ECS_ENTITY_VERSION_SHIFT = (24);         // Extent of the entity id + type within an identifier

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // type definitions and utility functions

    struct cp_type_t;

    // clang-format off
    static inline entity_ver_t     g_entity_version(entity_t e) { return ((u32)e & ECS_ENTITY_VERSION_MASK)>>ECS_ENTITY_VERSION_SHIFT; }
    static inline entity_type_id_t g_entity_type_id(entity_t e) { return ((u32)e & ECS_ENTITY_TYPE_MASK) >> ECS_ENTITY_TYPE_SHIFT; }
    static inline entity_id_t      g_entity_id(entity_t e)      { return (u32)e & ECS_ENTITY_ID_MASK; }
    static inline entity_t         g_make_entity(entity_ver_t ev, entity_type_id_t et, entity_id_t id) { return (u32)id | ((u32)et<<ECS_ENTITY_TYPE_SHIFT) | ((u32)ev<<ECS_ENTITY_VERSION_SHIFT); }
    // clang-format on

    // [index:12-bit, offset:20-bit]
    struct index_t
    {
        // clang-format off
        enum { NILL_VALUE = 0xFFFFFFFF, INDEX_MASK = 0xFFF00000, INDEX_SHIFT = 20, OFFSET_MASK = 0x000FFFFF };
        inline index_t() : m_value(NILL_VALUE) {}
        inline index_t(u16 index, u32 offset) : m_value((offset & OFFSET_MASK) | ((index << INDEX_SHIFT) & INDEX_MASK)) {}
        // clang-format on

        inline bool is_null() const { return m_value == NILL_VALUE; }
        inline u16  get_index() const { return ((m_value & INDEX_MASK) >> INDEX_SHIFT); }
        inline u32  get_offset() const { return m_value & OFFSET_MASK; }
        inline void set_index(u16 index) { m_value = (m_value & OFFSET_MASK) | ((index << INDEX_SHIFT) & INDEX_MASK); }
        inline void set_offset(u32 offset) { m_value = (m_value & INDEX_MASK) | (offset & OFFSET_MASK); }
        u32         m_value;
    };

    struct cp_type_mgr_t
    {
        enum
        {
            COMPONENTS_MAX = 1024,
        };
        u32        m_a_cp_hbb[35]; // To identify which component stores are still free (to give out new component id)
        cp_type_t* m_a_cp_type;    // The type information attached to each store
    };

    struct tg_type_mgr_t
    {
        enum
        {
            TAGS_MAX = 128,
        };
        u32        m_a_tg_hbb[7]; // To identify which tag stores are still free (to give out new tag id)
        tg_type_t* m_a_tg_type;   // The type information attached to each tag store
    };

    // Entity Type, (8 + 8 + sizeof(u32)*max-number-of-components) ~4Kb
    // When an entity type registers a component it will allocate component data from the specific store for N entities
    // and it will keep it there. Of course each entity can mark if it actually uses the component, but that is
    // just a single bit. So if only 50% of your entities of this type use this component you might be better of
    // registering another entity type.
    struct en_type_t
    {
        index_t m_type_id_and_size; // 4 bytes
        u32     m_cp_hbb[35];       // 140 bytes, hbb to indicate which components are used by this entity type
        hbb_t*  m_a_cp_store_hbb;   // 8192 bytes
        u8**    m_a_cp_store;       // 8192 bytes
        u32     m_tg_hbb[7];        // 28 bytes, which tag is registered by the entity type (max 128)
        hbb_t*  m_a_tg_hbb;         // 8 bytes, every registered tag has a hbb
        hbb_t   m_entity_free_hbb;  // 8 bytes, which entity is free
        hbb_t   m_entity_used_hbb;  // 8 bytes, which entity is used
        u8*     m_a_entity;         // 8 bytes, just versions
    };

    struct en_type_mgr_t
    {
        enum
        {
            ENTITY_TYPE_MAX = 256, // (related to ECS_ENTITY_TYPE_MASK)
        };

        u32         m_entity_type_used_hbb[11];
        u32         m_entity_type_free_hbb[11];
        en_type_t** m_entity_type_array;
    };

    struct ecs_t
    {
        alloc_t*      m_allocator;
        cp_type_mgr_t m_component_store;
        tg_type_mgr_t m_tag_type_store;
        en_type_mgr_t m_entity_type_store;
    };

    static inline s8 s_compute_index(u32 const bitset, u32 bit)
    {
        ASSERT((bit & bitset) == bit);
        s8 const i = xcountBits(bitset & (bit - 1));
        return i;
    }

    static inline u32 s_clr_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset & ~(1 << bit);
        return old_bitset;
    }
    static inline u32 s_set_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset | (1 << bit);
        return old_bitset;
    }

    struct u24
    {
        u8 b[3];
    };

    static inline void s_set_u24(u8* ptr, u32 i, u32 v)
    {
        ptr += (i << 1) + i;
        ptr[0] = (u8)(v << 16);
        ptr[1] = (u8)(v << 8);
        ptr[2] = (u8)(v << 0);
    }

    static inline u32 s_get_u24(u8 const* ptr, u32 i)
    {
        ptr += (i << 1) + i;
        u32 const v = (ptr[0] << 16) | (ptr[1] << 8) | (ptr[2] << 0);
        return v;
    }

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // component type, component type manager
    struct cp_nctype_t
    {
        u16         cp_id;
        u32         cp_sizeof;
        const char* cp_name;
    };

    static void s_init(cp_type_mgr_t* cps, alloc_t* allocator)
    {
        g_hbb_init(cps->m_a_cp_hbb, cp_type_mgr_t::COMPONENTS_MAX, 1);
        cps->m_a_cp_type = (cp_type_t*)allocator->allocate(sizeof(cp_type_t) * cp_type_mgr_t::COMPONENTS_MAX);
    }

    static void s_exit(cp_type_mgr_t* cps, alloc_t* allocator)
    {
        allocator->deallocate(cps->m_a_cp_type);
        cps->m_a_cp_type = nullptr;
        g_hbb_init(cps->m_a_cp_hbb, cp_type_mgr_t::COMPONENTS_MAX, 1);
    }

    static cp_type_t* s_register_cp_type(cp_type_mgr_t* cps, u32 cp_sizeof, const char* name)
    {
        u32 cp_id;
        if (g_hbb_find(cps->m_a_cp_hbb, cp_id))
        {
            cp_nctype_t* cp_type = (cp_nctype_t*)&cps->m_a_cp_type[cp_id];
            cp_type->cp_id       = cp_id;
            cp_type->cp_sizeof   = cp_sizeof;
            cp_type->cp_name     = name;

            g_hbb_clr(cps->m_a_cp_hbb, cp_id);
            return ((cp_type_t*)cp_type);
        }
        return nullptr;
    }

    static cp_type_t* s_get_cp_type(cp_type_mgr_t* cps, u32 cp_id)
    {
        cp_type_t* cp_type = (cp_type_t*)&cps->m_a_cp_type[cp_id];
        return cp_type;
    }

    static void s_unregister_cp_type(cp_type_mgr_t* cps, cp_type_t* cp_type)
    {
        u32 const cp_id = cp_type->cp_id;
        g_hbb_set(cps->m_a_cp_hbb, cp_id);
    }

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // tag type, tag type manager

    struct tg_nctype_t
    {
        u32         tg_id;
        const char* tg_name;
    };

    static void s_init(tg_type_mgr_t* ts, alloc_t* allocator)
    {
        g_hbb_init(ts->m_a_tg_hbb, tg_type_mgr_t::TAGS_MAX, 1);
        ts->m_a_tg_type = (tg_type_t*)allocator->allocate(sizeof(tg_type_t) * tg_type_mgr_t::TAGS_MAX);
    }

    static void s_exit(tg_type_mgr_t* ts, alloc_t* allocator)
    {
        allocator->deallocate(ts->m_a_tg_type);
        ts->m_a_tg_type = nullptr;
        g_hbb_init(ts->m_a_tg_hbb, tg_type_mgr_t::TAGS_MAX, 1);
    }

    static tg_type_t* s_register_tag_type(tg_type_mgr_t* ts, const char* name)
    {
        u32 tg_id;
        if (g_hbb_find(ts->m_a_tg_hbb, tg_id))
        {
            tg_nctype_t* tg_type = (tg_nctype_t*)&ts->m_a_tg_type[tg_id];
            tg_type->tg_id       = tg_id;
            tg_type->tg_name     = name;

            g_hbb_clr(ts->m_a_tg_hbb, tg_id);
            return ((tg_type_t*)tg_type);
        }
        return nullptr;
    }

    static tg_type_t* s_get_tg_type(tg_type_mgr_t* cps, u32 tg_id)
    {
        tg_type_t* cp_type = (tg_type_t*)&cps->m_a_tg_type[tg_id];
        return cp_type;
    }

    static void s_unregister_tg_type(tg_type_mgr_t* cps, tg_type_t* cp_type)
    {
        u32 const tg_id = cp_type->tg_id;
        g_hbb_set(cps->m_a_tg_hbb, tg_id);
    }

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // entity type, entity type manager

    static void s_clear(en_type_t* et)
    {
        et->m_type_id_and_size = index_t();
        et->m_a_cp_store_hbb   = nullptr;
        et->m_a_cp_store       = nullptr;
        et->m_a_tg_hbb         = nullptr;
        et->m_entity_free_hbb  = nullptr;
        et->m_entity_used_hbb  = nullptr;
        et->m_a_entity         = nullptr;
        g_hbb_init(et->m_cp_hbb, 1024, 0);
        g_hbb_init(et->m_tg_hbb, 128, 0);
    }

    static inline bool s_is_registered(en_type_t const* et) { return !et->m_type_id_and_size.is_null(); }

    static void s_init(en_type_mgr_t* es, alloc_t* allocator)
    {
        g_hbb_init(es->m_entity_type_free_hbb, en_type_mgr_t::ENTITY_TYPE_MAX, 1);
        g_hbb_init(es->m_entity_type_used_hbb, en_type_mgr_t::ENTITY_TYPE_MAX, 0);

        es->m_entity_type_array = (en_type_t**)allocator->allocate(sizeof(en_type_t*) * (en_type_mgr_t::ENTITY_TYPE_MAX));
        for (s32 i = 0; i < en_type_mgr_t::ENTITY_TYPE_MAX; ++i)
        {
            es->m_entity_type_array[i] = nullptr;
        }
    }

    static en_type_t*& s_get_entity_type(en_type_mgr_t* es, u32 entity_type_id) { return es->m_entity_type_array[entity_type_id]; }
    static bool        s_has_component(en_type_t const* et, u32 cp_id) { return g_hbb_is_set((hbb_t)et->m_cp_hbb, cp_id); }

    static en_type_t* s_register_entity_type(en_type_mgr_t* es, u32 max_entities, alloc_t* allocator)
    {
        u32 entity_type_id = 0;
        if (g_hbb_find(es->m_entity_type_free_hbb, entity_type_id))
        {
            g_hbb_clr(es->m_entity_type_free_hbb, entity_type_id);
            g_hbb_set(es->m_entity_type_used_hbb, entity_type_id);

            en_type_t*& et = s_get_entity_type(es, entity_type_id);
            et             = (en_type_t*)allocator->allocate(sizeof(en_type_t));

            et->m_type_id_and_size.set_index(entity_type_id);
            et->m_type_id_and_size.set_offset(max_entities);

            et->m_a_cp_store_hbb = (hbb_t*)allocator->allocate(sizeof(hbb_t) * cp_type_mgr_t::COMPONENTS_MAX);
            et->m_a_cp_store     = (u8**)allocator->allocate(sizeof(u8*) * cp_type_mgr_t::COMPONENTS_MAX);
            for (s32 i = 0; i < cp_type_mgr_t::COMPONENTS_MAX; ++i)
            {
                et->m_a_cp_store[i]     = nullptr;
                et->m_a_cp_store_hbb[i] = nullptr;
            }

            et->m_a_tg_hbb = (hbb_t*)allocator->allocate(sizeof(hbb_t) * tg_type_mgr_t::TAGS_MAX);
            for (s32 i = 0; i < tg_type_mgr_t::TAGS_MAX; ++i)
            {
                et->m_a_tg_hbb[i] = nullptr;
            }

            et->m_a_entity = (u8*)allocator->allocate(sizeof(u8) * max_entities);
            for (u32 i = 0; i < max_entities; ++i)
            {
                et->m_a_entity[i] = 0;
            }

            g_hbb_init(et->m_tg_hbb, tg_type_mgr_t::TAGS_MAX, 0);
            g_hbb_init(et->m_cp_hbb, cp_type_mgr_t::COMPONENTS_MAX, 0);

            g_hbb_init(et->m_entity_free_hbb, max_entities, 1, allocator);
            g_hbb_init(et->m_entity_used_hbb, max_entities, 0, allocator);

            return et;
        }
        return nullptr;
    }

    static entity_t s_create_entity(en_type_t const* et)
    {
        if (s_is_registered(et))
        {
            u32 entity_id = 0;
            if (g_hbb_find(et->m_entity_free_hbb, entity_id))
            {
                g_hbb_clr(et->m_entity_free_hbb, entity_id);
                g_hbb_set(et->m_entity_used_hbb, entity_id);

                u8&              eVER  = et->m_a_entity[entity_id];
                entity_type_id_t eTYPE = et->m_type_id_and_size.get_index();
                eVER += 1;
                return g_make_entity(eVER, eTYPE, entity_id);
            }
        }
        return g_null_entity;
    }

    static void s_delete_entity(cp_type_mgr_t* cps, en_type_t* et, entity_t e)
    {
        if (s_is_registered(et))
        {
            u32 const entity_id = g_entity_id(e);
            if (!g_hbb_is_set(et->m_entity_free_hbb, entity_id))
            {
                g_hbb_set(et->m_entity_free_hbb, entity_id);
                g_hbb_clr(et->m_entity_used_hbb, entity_id);

                // NOTE: For all components for this entity type and mark them as unused for this entity

                // Bruteforce (should use hbb iterator here)
                for (s32 i = 0; i < cp_type_mgr_t::COMPONENTS_MAX; ++i)
                {
                    if (et->m_a_cp_store[i] == nullptr)
                        continue;

                    g_hbb_clr(et->m_a_cp_store_hbb[i], g_entity_id(e));
                }
            }
        }
    }

    static void s_unregister_entity_type(en_type_mgr_t* es, en_type_t const* _et, alloc_t* allocator)
    {
        if (_et == nullptr)
            return;

        u32 const   entity_type_id = _et->m_type_id_and_size.get_index();
        en_type_t*& et             = es->m_entity_type_array[entity_type_id];
        if (et != nullptr)
        {
            g_hbb_set(es->m_entity_type_free_hbb, entity_type_id);
            g_hbb_clr(es->m_entity_type_used_hbb, entity_type_id);

            for (s32 i = 0; i < cp_type_mgr_t::COMPONENTS_MAX; ++i)
            {
                if (et->m_a_cp_store[i] == nullptr)
                    continue;
                allocator->deallocate(et->m_a_cp_store[i]);
                allocator->deallocate(et->m_a_cp_store_hbb[i]);
                et->m_a_cp_store[i]     = nullptr;
                et->m_a_cp_store_hbb[i] = nullptr;
            }

            for (s32 i = 0; i < tg_type_mgr_t::TAGS_MAX; ++i)
            {
                if (et->m_a_tg_hbb[i] == nullptr)
                    continue;
                g_hbb_release((hbb_t&)et->m_a_tg_hbb[i], allocator);
            }

            allocator->deallocate(et->m_a_cp_store);
            allocator->deallocate(et->m_a_cp_store_hbb);
            allocator->deallocate(et->m_a_tg_hbb);
            allocator->deallocate(et->m_a_entity);

            g_hbb_release((hbb_t&)et->m_entity_free_hbb, allocator);
            g_hbb_release((hbb_t&)et->m_entity_used_hbb, allocator);

            s_clear(et);
            allocator->deallocate(et);
            et = nullptr;
        }
    }

    static void s_exit(en_type_mgr_t* es, alloc_t* allocator)
    {
        for (s32 i = 0; i < en_type_mgr_t::ENTITY_TYPE_MAX; ++i)
        {
            en_type_t* et = es->m_entity_type_array[i];
            s_unregister_entity_type(es, et, allocator);
        }
        allocator->deallocate(es->m_entity_type_array);
    }

    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------
    // entity component system, create and destroy

    ecs_t* g_create_ecs(alloc_t* allocator)
    {
        ecs_t* ecs       = (ecs_t*)allocator->allocate(sizeof(ecs_t));
        ecs->m_allocator = allocator;
        s_init(&ecs->m_component_store, allocator);
        s_init(&ecs->m_tag_type_store, allocator);
        s_init(&ecs->m_entity_type_store, allocator);
        return ecs;
    }

    void g_destroy_ecs(ecs_t* ecs)
    {
        alloc_t* allocator = ecs->m_allocator;
        s_exit(&ecs->m_entity_type_store, allocator);
        s_exit(&ecs->m_tag_type_store, allocator);
        s_exit(&ecs->m_component_store, allocator);
        allocator->deallocate(ecs);
    }

    en_type_t* g_register_entity_type(ecs_t* r, u32 max_entities) { return s_register_entity_type(&r->m_entity_type_store, max_entities, r->m_allocator); }
    void       g_unregister_entity_type(ecs_t* r, en_type_t* et) { s_unregister_entity_type(&r->m_entity_type_store, et, r->m_allocator); }

    cp_type_t* g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cp_name) { return s_register_cp_type(&r->m_component_store, cp_sizeof, cp_name); }
    tg_type_t* g_register_tag_type(ecs_t* r, const char* tg_name) { return s_register_tag_type(&r->m_tag_type_store, tg_name); }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs_t* ecs, entity_t e, cp_type_t& cp_type)
    {
        entity_type_id_t const en_type_id  = g_entity_type_id(e);
        en_type_t const*       entity_type = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        return g_hbb_is_set(entity_type->m_a_cp_store_hbb[cp_type.cp_id], g_entity_id(e));
    }

    static void* s_entity_get_component(ecs_t* ecs, entity_t e, cp_type_t& cp_type)
    {
        entity_type_id_t const en_type_id    = g_entity_type_id(e);
        en_type_t const*       entity_type   = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        u8*                    cp_store_data = entity_type->m_a_cp_store[cp_type.cp_id];
        u32 const              cp_offset     = g_entity_id(e);
        return cp_store_data + (cp_offset * cp_type.cp_sizeof);
    }

    static void s_entity_set_component(ecs_t* ecs, entity_t e, cp_type_t& cp_type)
    {
        entity_type_id_t const en_type_id    = g_entity_type_id(e);
        en_type_t*             entity_type   = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        u8*&                   cp_store_data = entity_type->m_a_cp_store[cp_type.cp_id];
        if (cp_store_data == nullptr)
        {
            u32 const count   = entity_type->m_type_id_and_size.get_offset();
            cp_store_data     = (u8*)ecs->m_allocator->allocate(count * cp_type.cp_sizeof);
            u32* cp_store_hbb = (u32*)ecs->m_allocator->allocate(sizeof(u32) * g_sizeof_hbb(count));
            g_hbb_init(cp_store_hbb, count, 0);
            entity_type->m_a_cp_store_hbb[cp_type.cp_id] = cp_store_hbb;
            g_hbb_set(entity_type->m_cp_hbb, cp_type.cp_id);
        }
        // Now set the mark for this entity that he has attached this component
        g_hbb_set(entity_type->m_a_cp_store_hbb[cp_type.cp_id], g_entity_id(e));
    }

    // Remove/detach component from the entity
    static void s_entity_rem_component(ecs_t* ecs, entity_t e, cp_type_t& cp_type)
    {
        entity_type_id_t const en_type_id    = g_entity_type_id(e);
        en_type_t*             entity_type   = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        u8*&                   cp_store_data = entity_type->m_a_cp_store[cp_type.cp_id];
        if (cp_store_data == nullptr)
            return;
        // Now set the mark for this entity that he has attached this component
        g_hbb_set(entity_type->m_a_cp_store_hbb[cp_type.cp_id], g_entity_id(e));
    }

    static bool s_entity_has_tag(ecs_t* ecs, entity_t e, tg_type_t& tg_type)
    {
        entity_type_id_t const en_type_id  = g_entity_type_id(e);
        en_type_t const*       entity_type = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        return g_hbb_is_set(entity_type->m_a_tg_hbb[tg_type.tg_id], g_entity_id(e));
    }

    static void s_entity_set_tag(ecs_t* ecs, entity_t e, tg_type_t& tg_type)
    {
        entity_type_id_t const en_type_id  = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        u32*&                  tag_hbb     = entity_type->m_a_tg_hbb[tg_type.tg_id];
        if (tag_hbb == nullptr)
        {
            u32 const count = entity_type->m_type_id_and_size.get_offset();
            g_hbb_init(tag_hbb, count, 0, ecs->m_allocator);
            entity_type->m_a_tg_hbb[tg_type.tg_id] = tag_hbb;
            g_hbb_set(entity_type->m_tg_hbb, tg_type.tg_id);
        }
        // Now set the mark for this entity to indicate the tag is attached
        g_hbb_set(entity_type->m_a_tg_hbb[tg_type.tg_id], g_entity_id(e));
    }

    static void s_entity_rem_tag(ecs_t* ecs, entity_t e, tg_type_t& tg_type)
    {
        entity_type_id_t const en_type_id  = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        u32*&                  tag_hbb     = entity_type->m_a_tg_hbb[tg_type.tg_id];
        if (tag_hbb == nullptr)
            return;
        // Now clear the mark for this entity that he has attached this tag
        g_hbb_clr(entity_type->m_a_tg_hbb[tg_type.tg_id], g_entity_id(e));
    }

    entity_t g_create_entity(ecs_t* es, en_type_t* et) { return s_create_entity(et); }

    void g_delete_entity(ecs_t* ecs, entity_t e)
    {
        entity_type_id_t const en_type_id  = g_entity_type_id(e);
        en_type_t*             entity_type = s_get_entity_type(&ecs->m_entity_type_store, en_type_id);
        s_delete_entity(&ecs->m_component_store, entity_type, e);
    }

    bool  g_has_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_has_component(ecs, entity, *cp_type); }
    void  g_set_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_set_component(ecs, entity, *cp_type); }
    void  g_rem_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { s_entity_rem_component(ecs, entity, *cp_type); }
    void* g_get_cp(ecs_t* ecs, entity_t entity, cp_type_t* cp_type) { return s_entity_get_component(ecs, entity, *cp_type); }

    bool g_has_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { return s_entity_has_tag(ecs, entity, *tg_type); }
    void g_set_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { s_entity_set_tag(ecs, entity, *tg_type); }
    void g_rem_tag(ecs_t* ecs, entity_t entity, tg_type_t* tg_type) { s_entity_rem_tag(ecs, entity, *tg_type); }

    //////////////////////////////////////////////////////////////////////////
    // en_iterator_t

    void en_iterator_t::initialize(ecs_t* ecs)
    {
        m_ecs         = ecs;
        m_en_type     = nullptr;
        m_en_id       = 0;
        m_cp_type_cnt = 0;
        m_tg_type_cnt = 0;
    }

    void en_iterator_t::initialize(en_type_t* en_type)
    {
        m_ecs         = nullptr;
        m_en_type     = en_type;
        m_en_id       = 0;
        m_cp_type_cnt = 0;
        m_tg_type_cnt = 0;
    }

    // Mark the things you want to iterate on
    void en_iterator_t::cp_type(cp_type_t* cp) { m_cp_type_arr[m_cp_type_cnt++] = (u16)cp->cp_id; }
    void en_iterator_t::tg_type(tg_type_t* tg) { m_tg_type_arr[m_tg_type_cnt++] = (u8)tg->tg_id; }

    static inline u32 s_first_entity(en_type_t* en_type)
    {
        u32 index;
        if (!g_hbb_find(en_type->m_entity_used_hbb, index))
            return en_type->m_type_id_and_size.get_offset();
        return index;
    }

    static inline u32 s_next_entity(en_type_t* en_type, u32 en_id)
    {
        u32 index;
        if (!g_hbb_upper(en_type->m_entity_used_hbb, en_id, index))
            return en_type->m_type_id_and_size.get_offset();
        return index;
    }

    static inline en_type_t* s_first_entity_type(ecs_t* ecs)
    {
        if (ecs != nullptr)
        {
            u32 index;
            if (g_hbb_find(ecs->m_entity_type_store.m_entity_type_used_hbb, index))
                return ecs->m_entity_type_store.m_entity_type_array[index];
        }
        return nullptr;
    }

    static inline en_type_t* s_next_entity_type(ecs_t* ecs, en_type_t* en_type)
    {
        if (ecs != nullptr)
        {
            u32 index;
            if (g_hbb_upper(ecs->m_entity_type_store.m_entity_type_used_hbb, en_type->m_type_id_and_size.get_index(), index))
                return ecs->m_entity_type_store.m_entity_type_array[index];
        }
        return nullptr;
    }

    static en_type_t* s_search_matching_entity_type(en_iterator_t& iter)
    {
    iter_next_entity_type:
        while (iter.m_en_type != nullptr)
        {
            // Until we encounter an entity type that has all the required components/tags
            for (s16 i = 0; i < iter.m_tg_type_cnt; ++i)
            {
                if (!g_hbb_is_set(iter.m_en_type->m_tg_hbb, iter.m_tg_type_arr[i]))
                {
                    iter.m_en_type = s_next_entity_type(iter.m_ecs, iter.m_en_type);
                    goto iter_next_entity_type;
                }
            }
            for (s16 i = 0; i < iter.m_cp_type_cnt; ++i)
            {
                if (!g_hbb_is_set(iter.m_en_type->m_cp_hbb, iter.m_cp_type_arr[i]))
                {
                    iter.m_en_type = s_next_entity_type(iter.m_ecs, iter.m_en_type);
                    goto iter_next_entity_type;
                }
            }
            return iter.m_en_type;
        }
        return nullptr;
    }

    static s32 s_search_matching_entity(en_iterator_t& iter)
    {
        while (iter.m_en_type != nullptr)
        {
        iter_next_entity:
            while (iter.m_en_id < iter.m_en_type->m_type_id_and_size.get_offset())
            {
                // Until we encounter an entity that has all the required components/tags
                for (s16 i = 0; i < iter.m_tg_type_cnt; ++i)
                {
                    if (!g_hbb_is_set(iter.m_en_type->m_a_tg_hbb[iter.m_tg_type_arr[i]], iter.m_en_id))
                    {
                        iter.m_en_id = s_next_entity(iter.m_en_type, iter.m_en_id);
                        goto iter_next_entity;
                    }
                }
                for (s16 i = 0; i < iter.m_cp_type_cnt; ++i)
                {
                    if (!g_hbb_is_set(iter.m_en_type->m_a_cp_store_hbb[iter.m_cp_type_arr[i]], iter.m_en_id))
                    {
                        iter.m_en_id = s_next_entity(iter.m_en_type, iter.m_en_id);
                        goto iter_next_entity;
                    }
                }
                return iter.m_en_id;
            }

            iter.m_en_type = s_next_entity_type(iter.m_ecs, iter.m_en_type);
            iter.m_en_type = s_search_matching_entity_type(iter);
        }
        return -1;
    }

    void en_iterator_t::begin()
    {
        if (m_ecs != nullptr)
        {
            m_en_type = s_first_entity_type(m_ecs);
            m_en_type = s_search_matching_entity_type(*this);
            if (m_en_type != nullptr)
            {
                m_en_id = s_first_entity(m_en_type);
                m_en_id = s_search_matching_entity(*this);
            }
        }
        else if (m_en_type != nullptr)
        {
            m_en_type = s_search_matching_entity_type(*this);
            m_en_id   = s_first_entity(m_en_type);
        }
    }

    entity_t en_iterator_t::item() const { return g_make_entity(m_en_type->m_a_entity[m_en_id], m_en_type->m_type_id_and_size.get_index(), m_en_id); }

    void en_iterator_t::next()
    {
        m_en_id = s_next_entity(m_en_type, m_en_id);
        m_en_id = s_search_matching_entity(*this);
    }

    bool en_iterator_t::end() const { return m_en_type == nullptr; }

} // namespace ncore
