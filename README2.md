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

// Entity Component System; create and destroy
ecs_t* ecs = g_create_ecs(alloc_t*, 1024);
g_destroy_ecs(ecs_t* ecs);

// Component Group
cp_group_t* cp_group_go = g_register_cp_group(ecs);

// Create/Delete entity
entity_t e = g_create_entity(ecs);
g_destroy_entity(ecs, e);

// Components
struct position_t
{
    float x,y,z;
};

// Register a component type in a component group
cp_type_t* cp_type_byte = g_register_cp_type(ecs, cp_group_go, "byte", sizeof(char), alignof(char));
cp_type_t* cp_type_pos = g_register_cp_type(ecs, cp_group_go, "position", sizeof(position_t), alignof(position_t));

// Attach/detach a component to an entity
g_set_cp(ecs, e, cp_type_pos);
if (g_has_cp(ecs, e, cp_type_pos))
{
    position_t* en_pos = g_get_cp<position_t>(ecs, e, cp_type_pos);
    g_rem_cp(ecs, e, cp_type_pos);
}

// Tags (1 bit per entity)

cp_type_t* alerted = g_register_tg_type(ecs, cp_group_go, "alerted");

g_set_tag(ecs, e, alerted);
if (g_has_tag(ecs, e, alerted))
{
    g_rem_tag(ecs, e, alerted);
}

// Iteration

en_iterator_t iter;
iter.initialize(ecs);        // Iterate over all entity types

iter.cp_type(cp_type_byte);
iter.cp_type(cp_type_pos);
iter.tg_type(alerted);

iter.begin();
while (!iter.end())
{
    entity_t e = iter.item();

    CHECK_TRUE(g_has_tag(ecs, e, alerted));
    CHECK_TRUE(g_has_cp(ecs, e, cp_type_byte));
    CHECK_TRUE(g_has_cp(ecs, e, cp_type_pos));

    iter.next();
}


```

## Dependencies

- cbase (for alloc_t and hbb_t)

## Buy me a Coffee

If you like my work and want to support me. Please consider to buy me a [coffee!](https://www.buymeacoffee.com/Jur93n)
<img src="bmacoffee.png" width="100">
