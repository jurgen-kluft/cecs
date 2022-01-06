#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_hbb.h"
#include "xecs/x_ecs.h"

namespace xcore
{
    // ecs, max 256 components
    // ecs, max 16 M entities
    // entity, max 256 components

    struct entity_info_t
    {
        u32 m_groups; // each bit tells us if we also exist in group1_t/group2_t/.../group16_t/.../group32_t
        u32 m_index;  // the group/entity index
    };

    // Each group covers 8 components, so for 256 components we will need 32 groups.
    // 32 bytes covering potentially 8 components
    struct group_t
    {
        u32 m_index;         // For remove/swap, this indexes into the previous group
        u8  m_cp_bits;       // Which of the 8 cp_data[] are used
        u8  m_cp_group;      // Index of this group
        u8  m_cp_group_next; // Index of next group
        u8  m_dummy;
        u16 m_cp_data_l[8]; // The lower 2 bytes of each offset
        u8  m_cp_data_h[8]; // The high bytes of each offset in cp_data (max 16 M entries)
    };

    // when a component is removed we can easily swap it with the last one
    // and update the information in the specific group_t.
    struct component_store_t
    {
        void* component_data;
        u32*  entity_ids; // each one is a group-index/entity-index
        u32   size;
        u32   cap;
    };

    static void remove_from_component_store(u32 index, u32& update_index, u32& update_offset) {}

    struct ecs2_t
    {
        component_store_t component_stores[256];
        u32               entity_group_size[32];
        u32               entity_group_cap[32];
        group_t*          entity_groups[32];
        u32               entity_array_size;
        u32               entity_array_cap;
        entity_info_t*    entity_array;
    };

    static bool entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        entity_info_t* ei  = &ecs->entity_array[e];
        s32 const      ggi = cp.cp_id >> 3;
        if ((ei->m_groups & (1 << ggi)) != 0)
        {
            u8       gi      = (ei->m_index >> 24) & 0xFF;
            u32      entityi = (ei->m_index & 0xFFFFFF);
            group_t* group   = ecs->entity_groups[gi];
            while (ggi != gi)
            {
                group   = &group[entityi];
                gi      = group->m_cp_group_next;
                entityi = group->m_index;
                group   = ecs->entity_group[gi];
            }
            u8 const cp_bits = (group->m_cp_bits & (1 << (cp.cp_id & 0x7)));
            if (cp_bits != 0)
            {
                // s8 const cpi = xfindFirstBit(cp_bits);
                return true;
            }
        }
        return false;
    }

    static bool entity_remove_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp) {}

    // The primary goals of this ecs:
    //   1) iterators are easy and fast
    //   2) creating an entity is fast
    //   3) adding components is fast
    //   4) removal of a component is fast, but a negative impact on iteration (can be fixed by consolidation call)
    //   5) destroying an entity is fast

    // Should we impose constraints like:
    //   - Maximum number of components
    //   - Maximum number of entities
    //   - Maximum number of components per entity

    // Entity Functions
    // ----------------
    //
    // How to implement the following as well:
    //   - Entity::HasAnyComponent()
    //   - Entity::HasComponent(cp_type_t)
    //   - Entity::RemoveComponent(entity_t, cp_type_t)
    //

    // Entity Systems
    // --------------
    //
    // Furthermore, how would we be able to collect entities that get a component X attached.
    // For example, if a 'system' wants to keep track of entities that have certain components
    // they would need to be able to receive specific events.
    //

    // Entities+Components Iterators
    // -----------------------------
    //
    // 1. Iterators are easy and fast:
    //
    //    Iterating over entities that have one or more the same components:
    //    e.g.
    //            ecs::iterator it = begin<position_t, velocity_t, scale_t>();
    //            ecs::iterator it = begin<position_t, velocity_t, scale_t>(ecs_not<threat_t>(ecs));
    //            while (it.valid())
    //            {
    //                entity_t ent = *it;
    //                position_t* pos = it.get_cp<position_t>();
    //                velocity_t* vel = it.get_cp<velocity_t>();
    //                ++it;
    //            }
    //
    struct iterator_t
    {
        entity_t* cp_entity[32];
        void*     cp_data[32];
        u32       cp_sizes[32];
        u32       cp_count;
        entity_t  current;

        void begin() {}

        void next() {}
    };

    // If entities and their data are always in-order in their storage, the iterator would only have to
    // increment pointers on each [entity_id,component] array and keeping the entity_id in sync.
    // Also the component data is visited in-order.
    //
    // Also we want something in place which give us the smallest (id) when requesting a free entity id.
    // This means that we need to be able to quickly identify the smallest id that is free.
    // Currently thinking about a hierarchical bitset and at level 0 a byte that indexes into part of the
    // full entity_t array (256 entries).
    // For 4 million entity ids, we would need 32 Kb for the bytes and (512 u64 / 4 Kb) + (8 u64 / 64 b) + u8
    //

    const entity_t g_null_entity = (entity_t)ECS_ENTITY_ID_MASK;

    entity_ver_t g_entity_version(entity_t e) { return {e >> ECS_ENTITY_SHIFT}; }
    entity_id_t  g_entity_identifier(entity_t e) { return {e & ECS_ENTITY_ID_MASK}; }
    entity_t     g_make_entity(entity_id_t id, entity_ver_t version) { return id.id | (version.ver << ECS_ENTITY_SHIFT); }

    void* malloc(xsize_t size) { return nullptr; }
    void* realloc(void* ptr, xsize_t new_size) { return nullptr; }
    void  free(void* ptr) {}

    void memset(void* ptr, u32 c, u32 length) {}
    void memmove(void* dst, void* src, u32 length) {}

    struct storage_map_t
    {
        storage_map_t()
            : sparse(nullptr)
            , dense(nullptr)
            , cpset(nullptr)
            , sparse_size(0)
            , dense_size(0)
        {
        }

        //  mapping entity identifiers indices array.
        //  - index is the entity id. (without version)
        //  - value is the index into the dense array
        entity_t* sparse;

        //  has component bitfield per entity
        //  - each bit index value is the index into the cp data array
        //  = is dense
        u8* cpset;

        // Dense entities array.
        // - index is linked with the mapping value.
        // - value is the full entity_t
        entity_t* dense;

        u32 sparse_size;
        u32 dense_size;
    };

    static storage_map_t* s_sparse_new()
    {
        storage_map_t* mapping = (storage_map_t*)malloc(sizeof(storage_map_t));
        mapping->sparse        = nullptr;
        mapping->sparse_size   = 0;
        mapping->dense         = nullptr;
        mapping->dense_size    = 0;
        return mapping;
    }

    static void s_sparse_destroy(storage_map_t* s)
    {
        if (s)
        {
            free(s->sparse);
            free(s->dense);
        }
    }

    static void s_sparse_delete(storage_map_t* s)
    {
        s_sparse_destroy(s);
        free(s);
    }

    static bool s_sparse_contains(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        const entity_id_t eid = g_entity_identifier(e);
        return (eid.id < s->sparse_size) && (s->sparse[eid.id] != g_null_entity);
    }

    static u32 s_sparse_index(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(s_sparse_contains(s, e));
        return s->sparse[g_entity_identifier(e).id];
    }

    static void s_sparse_emplace(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        const entity_id_t eid = g_entity_identifier(e);
        if (eid.id >= s->sparse_size)
        { // check if we need to realloc
            const u32 new_sparse_size = eid.id + 1;
            s->sparse                 = (entity_t*)realloc(s->sparse, new_sparse_size * sizeof *s->sparse);
            memset(s->sparse + s->sparse_size, g_null_entity, (new_sparse_size - s->sparse_size) * sizeof *s->sparse);
            s->sparse_size = new_sparse_size;
        }
        s->sparse[eid.id]       = (entity_t)s->dense_size; // set this eid index to the last dense index (dense_size)
        s->dense                = (entity_t*)realloc(s->dense, (s->dense_size + 1) * sizeof *s->dense);
        s->dense[s->dense_size] = e;
        s->dense_size++;
    }

    static u32 s_sparse_remove(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(s_sparse_contains(s, e));

        const u32      pos   = s->sparse[g_entity_identifier(e).id];
        const entity_t other = s->dense[s->dense_size - 1];

        s->sparse[g_entity_identifier(other).id] = (entity_t)pos;
        s->dense[pos]                            = other;
        s->sparse[pos]                           = g_null_entity;

        s->dense = (entity_t*)realloc(s->dense, (s->dense_size - 1) * sizeof *s->dense);
        s->dense_size--;

        return pos;
    }

    struct ecs_cp_store_t
    {
        u32           cp_id;        // component id for this storage
        void*         cp_data;      // packed component elements array. aligned with mapping->dense
        u32           cp_data_size; // number of elements in the cp_data array
        u32           cp_sizeof;    // sizeof for each cp_data element
        storage_map_t mapping;
    };

    static ecs_cp_store_t* s_storage_new(u32 cp_size, u32 cp_id)
    {
        ecs_cp_store_t* s = (ecs_cp_store_t*)malloc(sizeof(ecs_cp_store_t));
        s->cp_id          = cp_id;
        s->cp_data        = nullptr;
        s->cp_data_size   = 0;
        s->cp_sizeof      = cp_size;
        return s;
    }

    static void s_storage_destroy(ecs_cp_store_t* s)
    {
        if (s)
        {
            s_sparse_destroy(&s->mapping);
            free(s->cp_data);
        }
    }

    static void s_storage_delete(ecs_cp_store_t* s)
    {
        s_storage_destroy(s);
        free(s);
    }

    static void* s_storage_emplace(ecs_cp_store_t* s, entity_t e)
    {
        ASSERT(s);

        // now allocate the data for the new component at the end of the array
        s->cp_data = realloc(s->cp_data, (s->cp_data_size + 1) * sizeof(char) * s->cp_sizeof);
        s->cp_data_size++;

        // return the component data pointer (last position)
        void* cp_data_ptr = &((char*)s->cp_data)[(s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof];

        // then add the entity to the mapping set
        s_sparse_emplace(&s->mapping, e);

        return cp_data_ptr;
    }

    static void s_storage_remove(ecs_cp_store_t* s, entity_t e)
    {
        ASSERT(s);
        u32 pos_to_remove = s_sparse_remove(&s->mapping, e);

        // swap (memmove because if cp_data_size 1 it will overlap dst and source.
        memmove(&((char*)s->cp_data)[pos_to_remove * sizeof(char) * s->cp_sizeof], &((char*)s->cp_data)[(s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof], s->cp_sizeof);

        // and pop
        s->cp_data = realloc(s->cp_data, (s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof);
        s->cp_data_size--;
    }

    static void* g_storage_get_by_index(ecs_cp_store_t* s, u32 index)
    {
        ASSERT(s);
        ASSERT(index < s->cp_data_size);
        return &((char*)s->cp_data)[index * sizeof(char) * s->cp_sizeof];
    }

    static void* s_storage_get(ecs_cp_store_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return g_storage_get_by_index(s, s_sparse_index(&s->mapping, e));
    }

    static void* s_storage_try_get(ecs_cp_store_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return s_sparse_contains(&s->mapping, e) ? s_storage_get(s, e) : 0;
    }

    static bool s_storage_contains(ecs_cp_store_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return s_sparse_contains(&s->mapping, e);
    }

    // ecs_t
    // Is the global context that holds each storage for each component types and the entities.
    struct ecs_t
    {
        ecs_cp_store_t** global_cp_array;      // array to pointers to storage
        u32              global_cp_array_size; //

        ecs_cp_store_t** tagged_cp_array;      //
        u32              tagged_cp_array_size; //

        u32         entities_size;
        entity_t*   entities; // contains all the allocated entities
        entity_id_t available_id;
        // hbb_t     entities_unused; // '1' means active, '0' means unused/free

        u32          unique_cp_id;
        u32          unique_tag_id;
        u32          num_unique_cps;
        u32          max_unique_cps;
        const char** unique_cps; // sorted by pointer
        u32          num_unique_tags;
        u32          max_unique_tags;
        const char** unique_tags; // sorted by pointer
    };

    static u32 s_ecs_unique_cp_id(ecs_t* r) { return r->unique_cp_id++; }
    static u32 s_ecs_unique_group_id(ecs_t* r) { return r->unique_tag_id++; }

    cp_type_t g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cpname)
    {
        cp_type_t c(0, 0, cpname);
        return c;
    }

    ecs_t* g_ecs_create()
    {
        ecs_t* r = (ecs_t*)malloc(sizeof(ecs_t));
        if (r)
        {
            r->global_cp_array      = 0;
            r->global_cp_array_size = 0;
            r->entities             = (entity_t*)malloc(sizeof(entity_t));
            r->unique_cp_id         = 0;
            r->unique_tag_id        = 1;
        }
        return r;
    }

    void g_ecs_destroy(ecs_t* r)
    {
        if (r)
        {
            if (r->global_cp_array)
            {
                for (u32 i = 0; i < r->global_cp_array_size; i++)
                {
                    s_storage_delete(r->global_cp_array[i]);
                }
            }
            free(r->entities);
        }
        free(r);
    }

    bool g_valid(ecs_t* r, entity_t e)
    {
        ASSERT(r);
        const entity_id_t id = g_entity_identifier(e);
        return id.id < r->entities_size && r->entities[id.id] == e;
    }

    static entity_t _s_generate_entity(ecs_t* r)
    {
        // can't create more identifiers entities
        ASSERT(r->entities_size < ECS_ENTITY_ID_MASK);

        // alloc one more element to the entities array
        r->entities = (entity_t*)realloc(r->entities, (r->entities_size + 1) * sizeof(entity_t));

        // create new entity and add it to the array
        const entity_t e              = g_make_entity({(u32)r->entities_size}, {0});
        r->entities[r->entities_size] = e;
        r->entities_size++;

        return e;
    }

    /* internal function to recycle a non used entity from the linked list */
    static entity_t _s_recycle_entity(ecs_t* r)
    {
        ASSERT(r->available_id.id != g_null_entity);
        // get the first available entity id
        const entity_id_t curr_id = r->available_id;
        // retrieve the version
        const entity_ver_t curr_ver = g_entity_version(r->entities[curr_id.id]);
        // point the available_id to the "next" id
        r->available_id = g_entity_identifier(r->entities[curr_id.id]);
        // now join the id and version to create the new entity
        const entity_t recycled_e = g_make_entity(curr_id, curr_ver);
        // assign it to the entities array
        r->entities[curr_id.id] = recycled_e;
        return recycled_e;
    }

    static void _s_release_entity(ecs_t* r, entity_t e, entity_ver_t desired_version)
    {
        const entity_id_t e_id = g_entity_identifier(e);
        r->entities[e_id.id]   = g_make_entity(r->available_id, desired_version);
        r->available_id        = e_id;
    }

    entity_t g_create(ecs_t* r)
    {
        ASSERT(r);
        if (r->available_id.id == g_null_entity)
        {
            return _s_generate_entity(r);
        }
        else
        {
            return _s_recycle_entity(r);
        }
    }

    enum
    {
        ECS_CP_INDEX_MASK  = 0x0000FFF,
        ECS_CP_GROUP_MASK  = 0x00FF000,
        ECS_CP_GROUP_SHIFT = 12,
    };

    static u32 s_get_cp_index(cp_type_t const& cp_type) { return cp_type.cp_id & ECS_CP_INDEX_MASK; }
    static u32 s_get_cp_group(cp_type_t const& cp_type) { return (cp_type.cp_id & ECS_CP_GROUP_MASK) >> ECS_CP_GROUP_SHIFT; }

    static ecs_cp_store_t* s_get_cp_storage(ecs_t* r, cp_type_t const& cp_type)
    {
        ASSERT(r);
        u32 const       cp_index   = s_get_cp_index(cp_type);
        ecs_cp_store_t* cp_storage = r->global_cp_array[cp_index];
        if (cp_storage == nullptr)
        {
            cp_storage                   = s_storage_new(cp_type.cp_sizeof, cp_type.cp_id);
            r->global_cp_array[cp_index] = cp_storage;
        }
        return cp_storage;
    }

    void g_remove_all(ecs_t* r, entity_t e)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));

        for (u32 i = r->global_cp_array_size; i; --i)
        {
            if (r->global_cp_array[i - 1] && s_sparse_contains(&r->global_cp_array[i - 1]->mapping, e))
            {
                s_storage_remove(r->global_cp_array[i - 1], e);
            }
        }
    }

    void g_remove(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(false);
        ASSERT(g_valid(r, e));
        s_storage_remove(s_get_cp_storage(r, cp_type), e);
    }

    void g_destroy(ecs_t* r, entity_t e)
    {
        ASSERT(r);
        ASSERT(e != g_null_entity);

        // 1) remove all the components of the entity
        g_remove_all(r, e);

        // 2) release_entity with a desired new version
        entity_ver_t new_version = g_entity_version(e);
        new_version.ver++;
        _s_release_entity(r, e, new_version);
    }

    bool g_has(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(s_get_cp_storage(r, cp_type));
        return s_storage_contains(s_get_cp_storage(r, cp_type), e);
    }

    void* g_emplace(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(s_get_cp_storage(r, cp_type));
        return s_storage_emplace(s_get_cp_storage(r, cp_type), e);
    }

    void* g_get(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(s_get_cp_storage(r, cp_type));
        return s_storage_get(s_get_cp_storage(r, cp_type), e);
    }

    void* g_try_get(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(s_get_cp_storage(r, cp_type));
        return s_storage_try_get(s_get_cp_storage(r, cp_type), e);
    }

    void g_each(ecs_t* r, void (*fun)(ecs_t*, entity_t, void*), void* udata)
    {
        ASSERT(r);
        if (!fun)
        {
            return;
        }

        if (r->available_id.id == g_null_entity)
        {
            for (u32 i = r->entities_size; i; --i)
            {
                fun(r, r->entities[i - 1], udata);
            }
        }
        else
        {
            for (u32 i = r->entities_size; i; --i)
            {
                const entity_t e = r->entities[i - 1];
                if (g_entity_identifier(e).id == (i - 1))
                {
                    fun(r, e, udata);
                }
            }
        }
    }

    bool g_orphan(ecs_t* r, entity_t e)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        for (u32 pool_i = 0; pool_i < r->global_cp_array_size; pool_i++)
        {
            if (r->global_cp_array[pool_i])
            {
                if (s_storage_contains(r->global_cp_array[pool_i], e))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /* Internal function to iterate orphans*/
    struct orphans_fun_data_t
    {
        void* orphans_udata;
        void (*orphans_fun)(ecs_t*, entity_t, void*);
    };

    static void _s_orphans_each_executor(ecs_t* r, entity_t e, void* udata)
    {
        orphans_fun_data_t* orphans_data = (orphans_fun_data_t*)udata;
        if (g_orphan(r, e))
        {
            orphans_data->orphans_fun(r, e, orphans_data->orphans_udata);
        }
    }

    void g_orphans_each(ecs_t* r, void (*fun)(ecs_t*, entity_t, void*), void* udata)
    {
        orphans_fun_data_t ofd = {udata, fun};
        g_each(r, _s_orphans_each_executor, &ofd);
    }

    // VIEW SINGLE COMPONENT

    ecs_view_single_t g_create_view_single(ecs_t* r, cp_type_t cp_type)
    {
        ASSERT(r);
        ecs_view_single_t v = {0};
        v.storage           = s_get_cp_storage(r, cp_type);
        ASSERT(v.storage);

        ecs_cp_store_t* storage = (ecs_cp_store_t*)v.storage;
        if (storage->cp_data_size != 0)
        {
            // get the last entity of the storage
            v.current_entity_index = storage->cp_data_size - 1;
            v.entity               = storage->mapping.dense[v.current_entity_index];
        }
        else
        {
            v.current_entity_index = 0;
            v.entity               = g_null_entity;
        }
        return v;
    }

    bool g_view_single_valid(ecs_view_single_t* v)
    {
        ASSERT(v);
        return (v->entity != g_null_entity);
    }

    entity_t g_view_single_entity(ecs_view_single_t* v)
    {
        ASSERT(v);
        return v->entity;
    }

    void* g_view_single_get(ecs_view_single_t* v)
    {
        ASSERT(v);
        return g_storage_get_by_index(v->storage, v->current_entity_index);
    }

    void g_view_single_next(ecs_view_single_t* v)
    {
        ASSERT(v);
        if (v->current_entity_index)
        {
            v->current_entity_index--;
            v->entity = ((ecs_cp_store_t*)v->storage)->mapping.dense[v->current_entity_index];
        }
        else
        {
            v->entity = g_null_entity;
        }
    }

    /// VIEW MULTI COMPONENTS

    bool g_view_entity_contained(ecs_view_t* v, entity_t e)
    {
        ASSERT(v);
        ASSERT(g_view_valid(v));

        for (u32 pool_id = 0; pool_id < v->pool_count; pool_id++)
        {
            if (!s_storage_contains(v->all_pools[pool_id], e))
            {
                return false;
            }
        }
        return true;
    }

    u32 g_view_get_index(ecs_view_t* v, cp_type_t cp_type)
    {
        ASSERT(v);
        for (u32 i = 0; i < v->pool_count; i++)
        {
            if (v->to_pool_index[i] == cp_type.cp_id)
            {
                return i;
            }
        }
        ASSERT(0); // FIX (dani) cp not found in the view pools
        return 0;
    }

    void* g_view_get(ecs_view_t* v, cp_type_t cp_type) { return g_view_get_by_index(v, g_view_get_index(v, cp_type)); }

    void* g_view_get_by_index(ecs_view_t* v, u32 pool_index)
    {
        ASSERT(v);
        ASSERT(pool_index >= 0 && pool_index < ecs_view_t::MAX_VIEW_COMPONENTS);
        ASSERT(g_view_valid(v));
        return s_storage_get(v->all_pools[pool_index], v->current_entity);
    }

    void g_view_next(ecs_view_t* v)
    {
        ASSERT(v);
        ASSERT(g_view_valid(v));
        // find the next contained entity that is inside all pools
        do
        {
            if (v->current_entity_index)
            {
                v->current_entity_index--;
                v->current_entity = ((ecs_cp_store_t*)v->pool)->mapping.dense[v->current_entity_index];
            }
            else
            {
                v->current_entity = g_null_entity;
            }
        } while ((v->current_entity != g_null_entity) && !g_view_entity_contained(v, v->current_entity));
    }

    ecs_view_t g_create_view(ecs_t* r, u32 cp_count, cp_type_t* cp_types)
    {
        ASSERT(r);
        ASSERT(cp_count < ecs_view_t::MAX_VIEW_COMPONENTS);

        ecs_view_t v;
        v.pool_count = cp_count;
        // setup pools pointer and find the smallest pool that we
        // use for iterations
        for (u32 i = 0; i < cp_count; i++)
        {
            v.all_pools[i] = s_get_cp_storage(r, cp_types[i]);
            ASSERT(v.all_pools[i]);
            if (!v.pool)
            {
                v.pool = v.all_pools[i];
            }
            else
            {
                if (((ecs_cp_store_t*)v.all_pools[i])->cp_data_size < ((ecs_cp_store_t*)v.pool)->cp_data_size)
                {
                    v.pool = v.all_pools[i];
                }
            }
            v.to_pool_index[i] = cp_types[i].cp_id;
        }

        if (v.pool && ((ecs_cp_store_t*)v.pool)->cp_data_size != 0)
        {
            v.current_entity_index = ((ecs_cp_store_t*)v.pool)->cp_data_size - 1;
            v.current_entity       = ((ecs_cp_store_t*)v.pool)->mapping.dense[v.current_entity_index];
            // now check if this entity is contained in all the pools
            if (!g_view_entity_contained(&v, v.current_entity))
            {
                // if not, search the next entity contained
                g_view_next(&v);
            }
        }
        else
        {
            v.current_entity_index = 0;
            v.current_entity       = g_null_entity;
        }
        return v;
    }

    bool g_view_valid(ecs_view_t* v)
    {
        ASSERT(v);
        return v->current_entity != g_null_entity;
    }

    entity_t g_view_entity(ecs_view_t* v)
    {
        ASSERT(v);
        ASSERT(g_view_valid(v));
        return ((ecs_cp_store_t*)v->pool)->mapping.dense[v->current_entity_index];
    }

} // namespace xcore
