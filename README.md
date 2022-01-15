# xecs library

A entity component system with the following design decisions:

- Component Types should not rely on template magic
- Tag Types should not rely on template magic
- Iteration should be straightforward and not rely on template magic
- User should be able to have some control over the organization/grouping of entities and components
- Component data for each entity type is in one array and NOT in a global array for that specific component type
- Component data for each entity should be in forward order as we iterate over entities and their components
- Tags should really be just one bit

```c++

// Entity Component System, create and destroy
ecs_t* ecs = g_create_ecs(alloc_t*);
g_destroy_ecs(ecs_t* ecs);

// Entity Type, 1024 is the maximum amount of entities in that type
en_type_t const* en_type_go = g_register_entity_type(ecs, 1024);

// Create / Delete entity
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

// TODO, currently in progress


```

## Dependencies

- xbase (for alloc_t and hbb_t)
