#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_hbb.h"
#include "xecs/x_ecs.h"

namespace xcore
{
    const entity_t g_null_entity = (entity_t)ECS_ENTITY_ID_MASK;

    entity_ver_t g_entity_version(entity_t e) { return {e >> ECS_ENTITY_SHIFT}; }
    entity_id_t  g_entity_identifier(entity_t e) { return {e & ECS_ENTITY_ID_MASK}; }
    entity_t     g_make_entity(entity_id_t id, entity_ver_t version) { return id.id | (version.ver << ECS_ENTITY_SHIFT); }

    void* malloc(xsize_t size) { return nullptr; }
    void* realloc(void* ptr, xsize_t new_size) { return nullptr; }
    void  free(void* ptr) {}

    void memset(void* ptr, u32 c, u32 length) {}
    void memmove(void* dst, void* src, u32 length) {}

    // SPARSE SET

    /*
        storage_map_t:

        How the components mapping set works?
        The main idea comes from ENTT C++ library:
        https://github.com/skypjack/entt
        https://github.com/skypjack/entt/wiki/Crash-Course:-entity-component-system#views
        (Credits to skypjack) for the awesome library.
        We have an mapping array that maps entity identifiers to the dense array indices that contains the full entity.
        mapping array:
        mapping => contains the index in the dense array of an entity identifier (without version)
        that means that the index of this array is the entity identifier (without version) and
        the content is the index of the dense array.
        dense array:
        dense => contains all the entities (entity_t).
        the index is just that, has no meaning here, it's referenced in the mapping.
        the content is the entity_t.
        this allows fast iteration on each entity using the dense array or
        lookup for an entity position in the dense using the mapping array.
        ---------- Example:
        Adding:
        entity_t = 3 => (e3)
        entity_t = 1 => (e1)
        In order to check the entities first in the mapping, we have to retrieve the entity_id_t part of the entity_t.
        The entity_id_t part will be used to index the mapping array.
        The full entity_t will be the value in the dense array.
                               0    1     2    3
        mapping idx:         eid0 eid1  eid2  eid3    this is the array index based on entity_id_t (NO VERSION)
        mapping content:   [ null,   1, null,   0 ]   this is the array content. (index in the dense array)
        dense         idx:    0    1
        dense     content: [ e3,  e2]
    */
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

    cp_type_t g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cpname, const char* cpgroup)
    {
        cp_type_t c(0, 0, cpname, cpgroup);
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
        ECS_CP_INDEX_MASK = 0x0000FFF,
        ECS_CP_GROUP_MASK = 0x00FF000,
        ECS_CP_GROUP_SHIFT = 12,
    };

    static u32 s_get_cp_index(cp_type_t const& cp_type) { return cp_type.cp_id & ECS_CP_INDEX_MASK; }
    static u32 s_get_cp_group(cp_type_t const& cp_type) { return (cp_type.cp_id & ECS_CP_GROUP_MASK)>>ECS_CP_GROUP_SHIFT; }

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
