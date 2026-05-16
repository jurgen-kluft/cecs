# Entity Component System (ECS) Implementations Analysis

## Overview

The cecs library contains 4 different ECS implementations, each with different design trade-offs and architectural philosophies. This document analyzes their approaches, strengths, and use cases.

## Design Evolution Pattern

The four implementations represent a spectrum from **type-centric** → **group-based** → **query-based** → **archetype-based** designs. This progression shows thoughtful iterative exploration of ECS trade-offs.

## Detailed Comparison

### ECS1 (c_ecs.h) — Type-Centric

**Entity Structure**: type (8b) + id (16b) + version (8b)

- Entity types pre-define which components can be attached
- Memory layout is naturally grouped (all soldiers together, all trees together)
- Up to 256 entity types supported
- Components registered per entity type

**Strengths**:
- Most intuitive for developers thinking in entity class hierarchies
- Compact memory layout with natural grouping
- Simple mental model: "this entity is a soldier, so it has these components"

**Trade-offs**:
- Limited flexibility; can't mix component sets freely across types
- Requires predefined entity types upfront

**Best for**: Games with clear, stable entity class hierarchies

---

### ECS2 (c_ecs2.h) — Grouped Flat Index

**Entity Structure**: generation (8b) + index (24b)

- Introduces "component groups" as intermediate organizational layer
- Up to 64 component groups per ECS
- Enum-based registration with static dispatch (compile-time indices)
- Entities can span multiple groups via `m_group_cp_mask`

**Strengths**:
- More flexible than ECS1 (cross-group entity composition)
- Logical clustering of related components
- Still maintains decent memory locality

**Trade-offs**:
- Iterator logic with group masks adds conceptual complexity
- More registration boilerplate than ECS1

**Best for**: Mid-complexity systems with logical component clustering

---

### ECS3 (c_ecs3.h) — Query-by-Example/Reference-Based

**Entity Structure**: generation (8b) + index (24b)

- Most radical rethink: uses a "blueprint entity" to define the query shape
- Components registered globally with per-component allocators
- Implicit querying based on entity reference matching
- Clean, minimal registration overhead

**Strengths**:
- Elegant API: create an entity with desired components, use it as a query template
- Minimal boilerplate for common cases
- Very expressive: entities can have arbitrary component combinations

**Trade-offs**:
- Implicit querying less obvious than explicit filter lists
- Extra indirection per query (following reference entity)
- Reference entity adds memory overhead per query

**Best for**: Prototyping, smaller ECS systems, flexibility-first designs

---

### ECS4 (c_ecs4.h) — True Archetypes + Bitmask Occupancy

**Entity Structure**: generation (8b) + archetype (8b) + index (16b)

- Implements the "archetype" pattern properly
- Entities pinned to archetypes at creation time
- Occupancy bitmasks (`cp_occupancy`, `tag_occupancy`) for efficient iteration
- Avoids dense iteration over component arrays you don't care about

**Strengths**:
- Best cache locality and performance characteristics
- True archetype semantics match modern ECS literature
- Bitmask queries are extremely efficient
- Up to 256 archetypes, 256 component types, 32 tag types

**Trade-offs**:
- Steepest learning curve
- Requires explicit bitmask marking before iteration
- Less flexible than query-by-example (entities must commit to archetype)

**Best for**: High-performance systems, data-oriented design, cache-critical applications

---

## Notable Cross-Implementation Design Choices

1. **No Heavy C++ Templates**: All implementations use enums and indices rather than template metaprogramming. This is pragmatic for debugging ease and binary size control.

2. **Tags as 1-Bit Flags**: Across all versions, tags are stored as bits, not full components. Very memory-efficient for boolean state.

3. **Explicit Registration**: Unlike modern systems (Bevy, flecs) with auto-registration, these require upfront declaration. More boilerplate, but completely predictable and explicit.

4. **Iterator Design Progression**:
   - ECS1: Explicit filter list + entity type
   - ECS2: Group membership masks
   - ECS3: Entity reference matching
   - ECS4: Bitmask occupancy (most efficient)

---

## Recommendation Matrix

| Goal | Best Choice | Reason |
|------|-------------|--------|
| Memory efficiency | ECS4 | Occupancy bitmasks avoid scanning irrelevant components |
| Simplicity/Prototyping | ECS3 | Reference entity pattern is elegant once understood |
| Natural grouping | ECS1 | Type-based organization matches domain thinking |
| Flexibility | ECS3 or ECS4 | Either reference-based or archetype-based |
| Production code with mixed patterns | ECS1 or ECS2 | Explicit organization scales better |
| High-performance game engine | ECS4 | Cache-aware, modern archetype semantics |

---

## Memory Overhead Comparison

- **ECS1 Iterator**: 186 bytes (largest; includes type ptr, arrays for up to 64 components, 32 tags)
- **ECS2 Iterator**: Similar to ECS1 but with group mask instead
- **ECS3 Iterator**: Compact (entity reference + index tracking)
- **ECS4 Iterator**: Fixed size with archetype pointer + occupancy masks

---

## Conclusion

This collection demonstrates mature exploration of ECS design space. Rather than a single "correct" answer, each implementation optimizes for different constraints:

- **Constraints on memory**: ECS4 (bitmask queries)
- **Constraints on development time**: ECS3 (reference-based)
- **Constraints on understanding**: ECS1 (type-centric)
- **Constraints on flexibility**: ECS3 or ECS4

The library's stated primary goal was memory consumption with secondary focus on performance—by that metric, **ECS4** is the logical endpoint, while **ECS1** achieves good memory density through natural grouping, and **ECS3** sacrifices some memory for API elegance.
