#ifndef __XECS_ECS_H__
#define __XECS_ECS_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    // Opaque 32 bits entity identifier.
    //
    // A 32 bits entity identifier guarantees:
    //   - 22 bits for the entity count (4 million)
    //   - 10 bit for the version(resets in[0 - 1023]).

    // Use the functions g_entity_version and g_entity_id to retrieve each part of an entity_t.

    // clang-format off
    typedef u32 entity_t;
    typedef u32 entity_ver_t;
    typedef u32 entity_id_t;

    #define ECS_ENTITY_ID_MASK       ((u32)0x003FFFFF)   // Mask to use to get the entity number out of an identifier
    #define ECS_ENTITY_VERSION_MASK  ((u32)0xFFC00000)   // Mask to use to get the version out of an identifier
    #define ECS_ENTITY_SHIFT         ((s8)20)            // Extent of the entity number within an identifier
    inline entity_ver_t g_entity_version(entity_t e)                        { return ((u32)e & ECS_ENTITY_VERSION_MASK)>>ECS_ENTITY_SHIFT; }
    inline entity_id_t  g_entity_id(entity_t e)                             { return (u32)e & ECS_ENTITY_ID_MASK; }
    inline entity_t     g_make_entity(entity_id_t id, entity_ver_t version) { return (u32)id | ((u32)version<<ECS_ENTITY_SHIFT); }

    extern const entity_t g_null_entity;

    // clang-format on

    struct ecs_t;
    ecs_t* g_ecs_create();
    void   g_ecs_destroy(ecs_t* r);

    // Component Type identifier information.
    struct cp_type_t
    {
        u32 const         cp_id;
        u32 const         cp_sizeof;
        const char* const cp_name;
    };

    // Registers a component type and returns its type information
    cp_type_t g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cpname);

    template <typename T> inline const char* nameof() { return "?"; }

    template <typename T> cp_type_t g_register_component_type(ecs_t* r) { return g_register_component_type(r, sizeof(T), nameof<T>()); }

    // Creates a new entity and returns it
    // The identifier can be:
    //  - New identifier in case no entities have been previously destroyed
    //  - Recycled identifier with an update version.
    entity_t g_create(ecs_t* r);

    //    Destroys an entity.
    //    When an entity is destroyed, its version is updated and the identifier
    //    can be recycled when needed.
    //    Warning:
    //    Undefined behavior if the entity is not valid.
    void g_destroy(ecs_t* r, entity_t e);

    //    Checks if an entity identifier is a valid one.
    //    The entity e can be a valid or an invalid one.
    //    Teturns true if the identifier is valid, false otherwise
    bool g_valid(ecs_t* r, entity_t e);

    //    Removes all the components from an entity and makes it orphaned (no components in it)
    //    the entity remains alive and valid without components.
    //    Warning: attempting to use invalid entity results in undefined behavior
    void g_remove_all(ecs_t* r, entity_t e);

    //    Assigns a given component type to an entity and returns it.
    //    A new memory instance of component type cp_type is allocated for the
    //    entity e and returned.
    //    Note: the memory returned is only allocated not initialized.
    //    Warning: use an invalid entity or assigning a component to a valid
    //    entity that currently has this component instance id results in
    //    undefined behavior.
    void* g_emplace(ecs_t* r, entity_t e, cp_type_t cp_type);

    //    Removes the given component from the entity.
    //    Warning: attempting to use invalid entity results in undefined behavior
    void g_remove(ecs_t* r, entity_t e, cp_type_t cp_type);

    //    Checks if the entity has the given component
    //    Warning: using an invalid entity results in undefined behavior.
    bool g_has(ecs_t* r, entity_t e, cp_type_t cp_type);

    //    Returns the pointer to the given component type data for the entity
    //    Warning: Using an invalid entity or get a component from an entity
    //    that doesn't own it results in undefined behavior.
    //    Note: This is the fastest way of retrieveing component data but
    //    has no checks. This means that you are 100% sure that the entity e
    //    has the component emplaced. Use g_try_get to check if you want checks
    void* g_get(ecs_t* r, entity_t e, cp_type_t cp_type);

    //    Returns the pointer to the given component type data for the entity
    //    or nullptr if the entity doesn't have this component.
    //    Warning: Using an invalid entity results in undefined behavior.
    //    Note: This is safer but slower than g_get.
    void* g_try_get(ecs_t* r, entity_t e, cp_type_t cp_type);

    //    Iterates all the entities that are still in use and calls
    //    the function pointer for each one.
    //    This is a fairly slow operation and should not be used frequently.
    //    However it's useful for iterating all the entities still in use,
    //    regarding their components.
    void g_each(ecs_t* r, void (*fun)(ecs_t*, entity_t, void*), void* udata);

    //    Returns true if an entity has no components assigned, false otherwise.
    //    Warning: Using an invalid entity results in undefined behavior.
    bool g_orphan(ecs_t* r, entity_t e);

    //    Iterates all the entities that are orphans (no component in it) and calls
    //    the function pointer for each one.
    //    This is a fairly slow operation and should not be used frequently.
    //    However it's useful for iterating all the entities still in use,
    //    regarding their components.
    void g_orphans_each(ecs_t* r, void (*fun)(ecs_t*, entity_t, void*), void* udata);

    struct ecs_cp_store_t;

    // Use this view to iterate entities that have the component type specified.
    struct ecs_view_single_t
    {
        ecs_cp_store_t* storage;
        u32             current_entity_index;
        entity_t        entity;
    };

    ecs_view_single_t g_create_view_single(ecs_t* r, cp_type_t cp_type);
    bool              g_view_single_valid(ecs_view_single_t* v);
    entity_t          g_view_single_entity(ecs_view_single_t* v);
    void*             g_view_single_get(ecs_view_single_t* v);
    void              g_view_single_next(ecs_view_single_t* v);

    // ecs_view_t
    //     Use this view to iterate entities that have multiple component types specified.
    //
    //     Note: You don't need to destroy the view because it doesn't allocate and it
    //     is not recommended that you save a view. Just use g_create_view each time.
    //     It's a "cheap" operation.
    //
    //     Example usage with two components:
    //
    //     for (ecs_view_t v = g_create_view(r, 2, (cp_type_t[2]) {transform_type, velocity_type }); g_view_valid(&v); g_view_next(&v)) {
    //         entity_t e = g_view_entity(&v);
    //         transform* tr = g_view_get(&v, transform_type);
    //         velocity* tc = g_view_get(&v, velocity_type);
    //         printf("transform  entity: %d => x=%d, y=%d, z=%d\n", g_entity_id(e).id, tr->x, tr->y, tr->z);
    //         printf("velocity  entity: %d => w=%f\n", g_entity_id(e).id, tc->v);
    //     }
    struct ecs_view_t
    {
        enum
        {
            MAX_VIEW_COMPONENTS = 16
        };

        // value is the component id, index is where is located in the all_pools array
        u32             to_pool_index[MAX_VIEW_COMPONENTS];
        ecs_cp_store_t* all_pools[MAX_VIEW_COMPONENTS];
        u32             pool_count;
        ecs_cp_store_t* pool;
        u32             current_entity_index;
        entity_t        current_entity;
    };

    ecs_view_t g_create_view(ecs_t* r, u32 cp_count, cp_type_t* cp_types);
    bool       g_view_valid(ecs_view_t* v);
    entity_t   g_view_entity(ecs_view_t* v);
    void*      g_view_get(ecs_view_t* v, cp_type_t cp_type);
    void*      g_view_get_by_index(ecs_view_t* v, u32 pool_index);
    void       g_view_next(ecs_view_t* v);

} // namespace xcore

#endif
