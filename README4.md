# cecs library

Version 4

An entity component system with the following design decisions:

- Component; attach/detach components to entities
- Tag (1/32 bits); attach/detach a tag/flag to entities
- Iteration; iterate over entities that have specific components and/or tags
- No C++ templates, only some helpers for syntactic sugar
- Component bins use virtual memory to minimize memory consumption and also
  avoid to need to specify max component count up front. They are tracked by
  index, so removing a component will swap-remove the last component in the bin
  into the removed component's slot, and update the entity->component mapping
  accordingly.
- Archetype; an Archetype holds a fixed maximum number of entities (65536), a set
  of components and tags.

