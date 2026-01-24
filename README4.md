# cecs library

Version 4

An entity component system with the following design decisions:

- Component; attach/detach components to entities
- Tag (1 bit); attach/detach a tag/flag to entities
- Iteration; iterate over entities that have specific components and/or tags
- No C++ templates, only some helpers for syntactic sugar
- Component stores use virtual memory to minimize memory consumption and also
  avoid to need to specify max component count up front.
- Entity Shard; an entity shard holds a fixed maximum number of entities (65536),
  this allows to use smaller indices in component containers, reducing memory consumption.

