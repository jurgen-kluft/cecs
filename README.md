# xecs library

A entity component system with the following design decisions:

- Entity Type; for grouping use of components and tags
- Component; attach/detach components to entities
- Tag (1 bit); attach/detach a tag/flag to entities
- Iteration; iterate over entities that have specific components and/or tags
- Should not depend on C++ templates

If you manage your entity types well the resulting memory storage is very compact and
exhibits very little waste.

For prototyping you can introduce a single entity type and just work on your game
and once every aspect becomes more crystalized you can then categorize entity types.

```c++

// Entity Component System; create and destroy
ecs_t* ecs = g_create_ecs(alloc_t*);
g_destroy_ecs(ecs_t* ecs);

// Entity Type; 1024 is the maximum amount of entities in that type
en_type_t const* en_type_go = g_register_entity_type(ecs, 1024);

// Create/Delete entity
entity_t e = g_create_entity(ecs, en_type_go);
g_delete_entity(ecs, e);

// Components
struct position_t
{
    float x,y,z;
};

// You can give a name to a component like this (will be visible in cp_type_t)
template <> inline const char* nameof<position_t>() { return "position"; }

cp_type_t* poscp = g_register_component_type<position_t>(ecs);

g_set_cp(ecs, e, poscp);
if (g_has_cp(ecs, e, poscp))
{
    position_t* en_pos = g_get_cp<position_t>(ecs, e, poscp);

    g_rem_cp(ecs, e, poscp);
}


// Tags (1 bit per entity)

struct alerted_t{};

// You can give a name to a tag like this (will be visible in tg_type_t)
template <> inline const char* nameof<alerted_t>() { return "alerted"; }

tg_type_t* alerted = g_register_tag_type<marked_t>(ecs);

g_set_tag(ecs, e, alerted);
if (g_has_tag(ecs, e, alerted))
{
    g_rem_tag(ecs, e, alerted);
}


// Iteration

en_iterator_t iter;
iter.initialize(ecs);        // Iterate over all entity types
//iter.initialize(en_type_go); // Only iterate over entities in this entity type

iter.cp_type(bytecmp);
iter.cp_type(poscmp);
iter.tg_type(enemy);

iter.begin();
while (!iter.end())
{
    entity_t e = iter.item();

    CHECK_TRUE(g_has_cp(ecs, e, bytecmp));
    CHECK_TRUE(g_has_cp(ecs, e, poscmp));
    CHECK_TRUE(g_has_tag(ecs, e, enemy));

    iter.next();
}


```

## Dependencies

- cbase (for alloc_t and hbb_t)
