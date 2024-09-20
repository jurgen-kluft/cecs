# cecs library

Version 2

An entity component system with the following design decisions:

- Component Group; for grouping components and tags
- Component; attach/detach components to entities
- Tag (1 bit); attach/detach a tag/flag to entities
- Iteration; iterate over entities that have specific components and/or tags
- No C++ templates

If you manage your entity types well the resulting memory storage is very compact and
exhibits very little waste.

For prototyping you can not care about organizing component groups and just work on your game
and once every aspect becomes more crystalized you can then analyze the current usage.

NOTE: This library has been mostly a development out of curiousity and figuring out how
to come up with something simple and efficient. The primary focus was memory consumption 
and performance.  

```c++
struct base_group_t
{
};
REGISTER_ECS_GROUP(base_group_t);
DECLARE_ECS_GROUP(base_group_t);

// Entity Component System; create and destroy
ecs_t* ecs = g_create_ecs(alloc_t*, 1024);
g_destroy_ecs(ecs_t* ecs);

// Component Group
g_register_cp_group<base_group_t>(ecs);

// Create/Delete entity
entity_t e = g_create_entity(ecs);
g_destroy_entity(ecs, e);

// Components
struct position_t
{
    float x,y,z;
};
REGISTER_ECS_COMPONENT(position_t);
DECLARE_ECS_COMPONENT(position_t);

// Register a component type in a component group
g_register_component<base_group_t, position_t>();

// Attach/detach a component to an entity
g_add_cp<position_t>(e);
if (g_has_cp(ecs, e, cp_type_pos))
{
    position_t* en_pos = g_get_cp<position_t>(e);
    g_rem_cp<position_t>(e);
}

// Tags (1 bit per entity)
struct alerted_t
{
};
REGISTER_ECS_TAG(alerted_t);
DECLARE_ECS_TAG_NAMED(alerted_t, "alerted tag");

g_register_tag<base_group_t, alerted_t>();

g_set_tag<alerted_t>(e);
if (g_has_tag<alerted_t>(e))
{
    g_rem_tag<alerted_t>(e);
}

// Iteration

en_iterator_t iter(ecs);

iter.set_cp_type<position_t>();
iter.set_tg_type<alerted_t>();

iter.begin();
while (!iter.end())
{
    entity_t e = iter.item();

    CHECK_TRUE(g_has_tag<alerted_t>(e));
    CHECK_TRUE(g_has_cp<position_t>(e));

    iter.next();
}


```

## Dependencies

- cbase (for alloc_t and hbb_t)

## Buy me a Coffee

If you like my work and want to support me. Please consider to buy me a [coffee!](https://www.buymeacoffee.com/Jur93n)
<img src="bmacoffee.png" width="100">
