#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xecs/x_ecs.h"

namespace xcore
{
    const entity_t g_null_entity = (entity_t)DE_ENTITY_ID_MASK;

    entity_ver_t g_entity_version(entity_t e) { return {e >> DE_ENTITY_SHIFT}; }
    entity_id_t  g_entity_identifier(entity_t e) { return {e & DE_ENTITY_ID_MASK}; }
    entity_t     g_make_entity(entity_id_t id, entity_ver_t version) { return id.id | (version.ver << DE_ENTITY_SHIFT); }

    void* malloc(size_t size) { return nullptr; }
    void* realloc(void* ptr, size_t new_size) { return nullptr; }
    void  free(void* ptr) {}

    void memset(void* ptr, u32 c, u32 length) {}
    void memmove(void* dst, void* src, u32 length) {}

    // SPARSE SET

    /*
        sparse_t:

        How the components sparse set works?
        The main idea comes from ENTT C++ library:
        https://github.com/skypjack/entt
        https://github.com/skypjack/entt/wiki/Crash-Course:-entity-component-system#views
        (Credits to skypjack) for the awesome library.
        We have an sparse array that maps entity identifiers to the dense array indices that contains the full entity.
        sparse array:
        sparse => contains the index in the dense array of an entity identifier (without version)
        that means that the index of this array is the entity identifier (without version) and
        the content is the index of the dense array.
        dense array:
        dense => contains all the entities (entity_t).
        the index is just that, has no meaning here, it's referenced in the sparse.
        the content is the entity_t.
        this allows fast iteration on each entity using the dense array or
        lookup for an entity position in the dense using the sparse array.
        ---------- Example:
        Adding:
        entity_t = 3 => (e3)
        entity_t = 1 => (e1)
        In order to check the entities first in the sparse, we have to retrieve the entity_id_t part of the entity_t.
        The entity_id_t part will be used to index the sparse array.
        The full entity_t will be the value in the dense array.
                               0    1     2    3
        sparse idx:         eid0 eid1  eid2  eid3    this is the array index based on entity_id_t (NO VERSION)
        sparse content:   [ null,   1, null,   0 ]   this is the array content. (index in the dense array)
        dense         idx:    0    1
        dense     content: [ e3,  e2]
    */
    struct sparse_t
    {
        sparse_t()
            : sparse(nullptr)
            , sparse_size(0)
            , dense(nullptr)
            , dense_size(0)
        {
        }

        //  sparse entity identifiers indices array.
        //  - index is the entity_id_t. (without version)
        //  - value is the index of the dense array
        entity_t* sparse;
        u32       sparse_size;

        // Dense entities array.
        // - index is linked with the sparse value.
        // - value is the full entity_t
        entity_t* dense;
        u32       dense_size;
    };

    static sparse_t* s_sparse_init(sparse_t* s)
    {
        if (s)
        {
            *s        = sparse_t();
            s->sparse = 0;
            s->dense  = 0;
        }
        return s;
    }

    static sparse_t* s_sparse_new()
    {
        sparse_t* sparse = (sparse_t*)malloc(sizeof(sparse_t));
        return s_sparse_init(sparse);
    }

    static void s_sparse_destroy(sparse_t* s)
    {
        if (s)
        {
            free(s->sparse);
            free(s->dense);
        }
    }

    static void s_sparse_delete(sparse_t* s)
    {
        s_sparse_destroy(s);
        free(s);
    }

    static bool s_sparse_contains(sparse_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        const entity_id_t eid = g_entity_identifier(e);
        return (eid.id < s->sparse_size) && (s->sparse[eid.id] != g_null_entity);
    }

    static u32 s_sparse_index(sparse_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(s_sparse_contains(s, e));
        return s->sparse[g_entity_identifier(e).id];
    }

    static void s_sparse_emplace(sparse_t* s, entity_t e)
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

    static u32 s_sparse_remove(sparse_t* s, entity_t e)
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

    // STORAGE FUNCTIONS

    /*
        ecs_storage_t

        handles the raw component data aligned with a sparse_t.
        stores packed component data elements for each entity in the sparse set.
        the packed component elements data is aligned always with the dense array from the sparse set.
        adding/removing an entity to the storage will:
            - add/remove from the sparse
            - use the sparse_set dense array position to move the components data aligned.
        Example:
                      idx:    0    1    2
        dense     content: [ e3,  e2,  e1]
        cp_data   content: [e3c, e2c, e1c] contains component data for the entity in the corresponding index
        If now we remove from the storage the entity e2:
                      idx:    0    1    2
        dense     content: [ e3,  e1]
        cp_data   content: [e3c, e1c] contains component data for the entity in the corresponding index
        note that the alignment to the index in the dense and in the cp_data is always preserved.
        This allows fast iteration for each component and having the entities accessible aswell.
        for (i = 0; i < dense_size; i++) {  // mental example, wrong syntax
            entity_t e = dense[i];
            void*   ecp = cp_data[i];
        }
    */

    struct ecs_storage_t
    {
        ecs_storage_t()
            : cp_id(0)
            , cp_data(nullptr)
            , cp_data_size(0)
            , cp_sizeof(0)
        {
        }
        u32      cp_id;        // component id for this storage
        void*    cp_data;      // packed component elements array. aligned with sparse->dense
        u32      cp_data_size; // number of elements in the cp_data array
        u32      cp_sizeof;    // sizeof for each cp_data element
        sparse_t sparse;
    };

    static ecs_storage_t* s_storage_init(ecs_storage_t* s, u32 cp_size, u32 cp_id)
    {
        if (s)
        {
            *s = ecs_storage_t();
            s_sparse_init(&s->sparse);
            s->cp_sizeof = cp_size;
            s->cp_id     = cp_id;
        }
        return s;
    }

    static ecs_storage_t* s_storage_new(u32 cp_size, u32 cp_id) { return s_storage_init((ecs_storage_t*)malloc(sizeof(ecs_storage_t)), cp_size, cp_id); }

    static void s_storage_destroy(ecs_storage_t* s)
    {
        if (s)
        {
            s_sparse_destroy(&s->sparse);
            free(s->cp_data);
        }
    }

    static void s_storage_delete(ecs_storage_t* s)
    {
        s_storage_destroy(s);
        free(s);
    }

    static void* s_storage_emplace(ecs_storage_t* s, entity_t e)
    {
        ASSERT(s);
        // now allocate the data for the new component at the end of the array
        s->cp_data = realloc(s->cp_data, (s->cp_data_size + 1) * sizeof(char) * s->cp_sizeof);
        s->cp_data_size++;

        // return the component data pointer (last position)
        void* cp_data_ptr = &((char*)s->cp_data)[(s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof];

        // then add the entity to the sparse set
        s_sparse_emplace(&s->sparse, e);

        return cp_data_ptr;
    }

    static void s_storage_remove(ecs_storage_t* s, entity_t e)
    {
        ASSERT(s);
        u32 pos_to_remove = s_sparse_remove(&s->sparse, e);

        // swap (memmove because if cp_data_size 1 it will overlap dst and source.
        memmove(&((char*)s->cp_data)[pos_to_remove * sizeof(char) * s->cp_sizeof], &((char*)s->cp_data)[(s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof], s->cp_sizeof);

        // and pop
        s->cp_data = realloc(s->cp_data, (s->cp_data_size - 1) * sizeof(char) * s->cp_sizeof);
        s->cp_data_size--;
    }

    static void* g_storage_get_by_index(ecs_storage_t* s, u32 index)
    {
        ASSERT(s);
        ASSERT(index < s->cp_data_size);
        return &((char*)s->cp_data)[index * sizeof(char) * s->cp_sizeof];
    }

    static void* s_storage_get(ecs_storage_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return g_storage_get_by_index(s, s_sparse_index(&s->sparse, e));
    }

    static void* s_storage_try_get(ecs_storage_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return s_sparse_contains(&s->sparse, e) ? s_storage_get(s, e) : 0;
    }

    static bool s_storage_contains(ecs_storage_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        return s_sparse_contains(&s->sparse, e);
    }

    // ecs_t
    // Is the global context that holds each storage for each component types and the entities.
    struct ecs_t
    {
        ecs_storage_t** storages;      /* array to pointers to storage */
        u32             storages_size; /* size of the storages array */
        u32             entities_size;
        entity_t*       entities;     /* contains all the created entities */
        entity_id_t     available_id; /* first index in the list to recycle */
    };

    ecs_t* g_ecs_create()
    {
        ecs_t* r = (ecs_t*)malloc(sizeof(ecs_t));
        if (r)
        {
            r->storages        = 0;
            r->storages_size   = 0;
            r->available_id.id = g_null_entity;
            r->entities_size   = 0;
            r->entities        = 0;
        }
        return r;
    }

    void g_ecs_destroy(ecs_t* r)
    {
        if (r)
        {
            if (r->storages)
            {
                for (u32 i = 0; i < r->storages_size; i++)
                {
                    s_storage_delete(r->storages[i]);
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
        ASSERT(r->entities_size < DE_ENTITY_ID_MASK);

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

    ecs_storage_t* g_assure(ecs_t* r, cp_type_t cp_type)
    {
        ASSERT(r);
        ecs_storage_t* storage_found = 0;

        for (u32 i = 0; i < r->storages_size; i++)
        {
            if (r->storages[i]->cp_id == cp_type.cp_id)
            {
                storage_found = r->storages[i];
            }
        }

        if (storage_found)
        {
            return storage_found;
        }
        else
        {
            ecs_storage_t* storage_new    = s_storage_new(cp_type.cp_sizeof, cp_type.cp_id);
            r->storages                   = (ecs_storage_t**)realloc(r->storages, (r->storages_size + 1) * sizeof *r->storages);
            r->storages[r->storages_size] = storage_new;
            r->storages_size++;
            return storage_new;
        }
    }

    void g_remove_all(ecs_t* r, entity_t e)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));

        for (u32 i = r->storages_size; i; --i)
        {
            if (r->storages[i - 1] && s_sparse_contains(&r->storages[i - 1]->sparse, e))
            {
                s_storage_remove(r->storages[i - 1], e);
            }
        }
    }

    void g_remove(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(false);
        ASSERT(g_valid(r, e));
        s_storage_remove(g_assure(r, cp_type), e);
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
        ASSERT(g_assure(r, cp_type));
        return s_storage_contains(g_assure(r, cp_type), e);
    }

    void* g_emplace(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(g_assure(r, cp_type));
        return s_storage_emplace(g_assure(r, cp_type), e);
    }

    void* g_get(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(g_assure(r, cp_type));
        return s_storage_get(g_assure(r, cp_type), e);
    }

    void* g_try_get(ecs_t* r, entity_t e, cp_type_t cp_type)
    {
        ASSERT(r);
        ASSERT(g_valid(r, e));
        ASSERT(g_assure(r, cp_type));
        return s_storage_try_get(g_assure(r, cp_type), e);
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
        for (u32 pool_i = 0; pool_i < r->storages_size; pool_i++)
        {
            if (r->storages[pool_i])
            {
                if (s_storage_contains(r->storages[pool_i], e))
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
        v.pool              = g_assure(r, cp_type);
        ASSERT(v.pool);

        ecs_storage_t* pool = (ecs_storage_t*)v.pool;
        if (pool->cp_data_size != 0)
        {
            // get the last entity of the pool
            v.current_entity_index = pool->cp_data_size - 1;
            v.entity               = pool->sparse.dense[v.current_entity_index];
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
        return g_storage_get_by_index(v->pool, v->current_entity_index);
    }

    void g_view_single_next(ecs_view_single_t* v)
    {
        ASSERT(v);
        if (v->current_entity_index)
        {
            v->current_entity_index--;
            v->entity = ((ecs_storage_t*)v->pool)->sparse.dense[v->current_entity_index];
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
                v->current_entity = ((ecs_storage_t*)v->pool)->sparse.dense[v->current_entity_index];
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
            v.all_pools[i] = g_assure(r, cp_types[i]);
            ASSERT(v.all_pools[i]);
            if (!v.pool)
            {
                v.pool = v.all_pools[i];
            }
            else
            {
                if (((ecs_storage_t*)v.all_pools[i])->cp_data_size < ((ecs_storage_t*)v.pool)->cp_data_size)
                {
                    v.pool = v.all_pools[i];
                }
            }
            v.to_pool_index[i] = cp_types[i].cp_id;
        }

        if (v.pool && ((ecs_storage_t*)v.pool)->cp_data_size != 0)
        {
            v.current_entity_index = ((ecs_storage_t*)v.pool)->cp_data_size - 1;
            v.current_entity       = ((ecs_storage_t*)v.pool)->sparse.dense[v.current_entity_index];
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
        return ((ecs_storage_t*)v->pool)->sparse.dense[v->current_entity_index];
    }

} // namespace xcore
