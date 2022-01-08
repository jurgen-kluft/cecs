#include "xbase/x_target.h"
#include "xbase/x_allocator.h"
#include "xbase/x_debug.h"
#include "xbase/x_integer.h"
#include "xbase/x_memory.h"
#include "xecs/x_ecs.h"

namespace xcore
{
    struct cp_store_t;
    struct entity0_t;

    // primed size list reaching until 8 M
    // 63 items (0x3F)
    static constexpr const u32 s_prime_size_list_count = 63;
    static constexpr const u32 s_prime_size_list[]     = {2,     3,      5,      7,      11,     13,     17,     23,     29,     37,     47,     59,      73,      97,      127,     151,     197,     251,     313,     397,     499,
                                                      631,   797,    1009,   1259,   1597,   2011,   2539,   3203,   4027,   5087,   6421,   8089,    10193,   12853,   16193,   20399,   25717,   32401,   40823,   51437,   64811,
                                                      81649, 102877, 129607, 163307, 205759, 259229, 326617, 411527, 518509, 653267, 823117, 1037059, 1306601, 1646237, 2074129, 2613229, 3292489, 4148279, 5226491, 6584983, 8388607};

    static constexpr const s32 s_entity_array_capacities_count = 40;
    static constexpr const u32 s_entity_array_capacities[]     = {256,    512,    768,    1280,   1792,   2816,   3328,   4352,   5888,   7424,   9472,    12032,   15104,   18688,   24832,   32512,   38656,   50432,   64256,   80128,
                                                              101632, 127744, 161536, 204032, 258304, 322304, 408832, 514816, 649984, 819968, 1030912, 1302272, 1643776, 2070784, 2609408, 3290368, 4145408, 5222144, 6583552, 8294656};

    static constexpr const s32 s_cp_store_capacities_count = 60;
    static constexpr const u32 s_cp_store_capacities[]     = {4,      6,      10,     14,     22,     26,     34,     46,     58,     74,     94,      118,     146,     194,     254,     302,     394,     502,     626,     794,
                                                          998,    1262,   1594,   2018,   2518,   3194,   4022,   5078,   6406,   8054,   10174,   12842,   16178,   20386,   25706,   32386,   40798,   51434,   64802,   81646,
                                                          102874, 129622, 163298, 205754, 259214, 326614, 411518, 518458, 653234, 823054, 1037018, 1306534, 1646234, 2074118, 2613202, 3292474, 4148258, 5226458, 6584978, 8296558};
    struct cp_nctype_t
    {
        u32         cp_id;
        u32         cp_sizeof;
        const char* cp_name;
    };

    struct cp_store_t
    {
        u8* m_cp_data;
        u8* m_entity_ids;
        u32 m_capacity_index;
        u32 m_size;
    };

    struct entity_xo_t
    {
        inline bool get_flag() const { return (m_index & 0x80) != 0; }
        inline void set_flag() { m_index = m_index | 0x80; }
        inline void clr_flag() { m_index = m_index & 0x7F; }

        inline u8   get_index() const { return m_index & 0x7F; }
        inline u32  get_offset() const { return (m_offset_l) | (m_offset_h << 16); }
        inline void set_index(u8 index) { m_index = (m_index & 0x80) | (index & 0x7F); }
        inline void set_offset(u32 offset)
        {
            m_offset_h = (u8)(offset >> 16);
            m_offset_l = (u16)(offset);
        }

        u8  m_index;
        u8  m_offset_h;
        u16 m_offset_l;
    };

    struct entity2_t
    {
        entity_xo_t m_entity_index;     // The entity0 owner of this struct
        u8          m_cp_data_offset[]; // N-size, depends on the the number of bits set in m_cp_bitset aligned to a multiple of 4
    };

    struct entity0_t
    {
        u32 m_en2_index;     // index/offset
        u32 m_cp1_bitset;    // Each bit at level 1 represents 32 shards of components
        u8  m_cp2_bitcnt[4]; // The bitcnt sum of previous shards
        u32 m_cp2_bitset[4]; // this means that an entity can only cover 4 shards and 128 total components
    };
    // NOTE: can have 5 shards, but then we need to use m_cp2_bitcnt as an u32 and use bit logic where a shard is 5 bits in the u32

    static const s32 s_max_num_components = 1024;
    static const s32 s_cp_step_en2_array  = 4;

    static inline u32 s_compute_entity2_struct_size(u16 entity2_ai) { return (entity2_ai * 12) + 4; }

    static inline s8 s_compute_index(u32 const bitset, s8 bit)
    {
        ASSERT(((1 << bit) & bitset) == (1 << bit));
        s8 const i = xcountBits(bitset & ((u32)1 << bit));
        return i;
    }

    struct ecs2_t
    {
        alloc_t* m_allocator;

        u32          m_cp_store_bitset1;
        u32          m_a_cp_store_bitset0[32]; // To identify which component stores are still free (to give out new component id)
        cp_nctype_t* m_a_cp_store_type;        // The type of each store
        cp_store_t*  m_a_cp_store;             // N max number of components

        u8*  m_free_entities_level0;     // 32768 * 256 = 8 M (each byte is an index into a range of 256 entities)
        u32* m_free_entities_level1;     // 1024 * 32 = 32768 bits
        u32  m_free_entities_level2[32]; // 1024 bits
        u32  m_free_entities_level3;     // 32 bits

        u32         m_entity0_size;
        u32         m_entity0_cap;
        entity0_t*  m_a_entity0;
        entity2_t** m_a_a_entity2;
    };

    static inline u32 s_clr_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset & ~(1 << bit);
        return old_bitset;
    }
    static inline u32 s_set_bit_in_u32(u32& bitset, s8 bit)
    {
        u32 const old_bitset = bitset;
        bitset               = bitset | (1 << bit);
        return old_bitset;
    }

    static cp_type_t const* s_cp_register_cp_type(ecs2_t* ecs, u32 cp_sizeof, const char* name)
    {
        if (ecs->m_cp_store_bitset1 == 0)
            return nullptr;

        s8 const o1 = xfindFirstBit(ecs->m_cp_store_bitset1);
        s8 const o0 = xfindFirstBit(ecs->m_a_cp_store_bitset0[o1]);

        u32 const cp_id                         = o1 * 32 + o0;
        ecs->m_a_cp_store_type[cp_id].cp_id     = cp_id;
        ecs->m_a_cp_store_type[cp_id].cp_sizeof = cp_sizeof;
        ecs->m_a_cp_store_type[cp_id].cp_name   = name;

        if (s_clr_bit_in_u32(ecs->m_a_cp_store_bitset0[o1], o0) == 0)
        {
            // No more free items in here, mark upper level
            s_clr_bit_in_u32(ecs->m_cp_store_bitset1, o0);
        }

        return ((cp_type_t const*)&ecs->m_a_cp_store_type[o1 * 32 + o0]);
    }

    static void s_cp_unregister_cp_type(ecs2_t* ecs, cp_type_t const* cp_type)
    {
        s8 const o1 = cp_type->cp_id / 32;
        s8 const o0 = cp_type->cp_id & (32 - 1);
        if (s_set_bit_in_u32(ecs->m_a_cp_store_bitset0[o1], o0) == 0xFFFFFFFF)
        {
            s_set_bit_in_u32(ecs->m_cp_store_bitset1, o1);
        }
    }

    static ecs2_t* s_ecs_create(alloc_t* allocator)
    {
        ecs2_t* ecs      = (ecs2_t*)allocator->allocate(sizeof(ecs2_t));
        ecs->m_allocator = allocator;

        ecs->m_cp_store_bitset1 = 0;
        x_memset(ecs->m_a_cp_store_bitset0, 0xFFFFFFFF, 32 * sizeof(u32));

        ecs->m_a_cp_store_type = (cp_nctype_t*)allocator->allocate(sizeof(cp_nctype_t) * 32 * 32);
        ecs->m_a_cp_store      = (cp_store_t*)allocator->allocate(sizeof(cp_store_t) * 32 * 32);
        x_memset(ecs->m_a_cp_store, 0, sizeof(cp_store_t) * 32 * 32);

        ecs->m_free_entities_level0 = (u8*)allocator->allocate(sizeof(u8) * 32768);
        ecs->m_free_entities_level1 = (u32*)allocator->allocate(sizeof(u32) * 1024);
        x_memset(ecs->m_free_entities_level0, 0x00000000, 32 * 32 * 32 * sizeof(u8));
        x_memset(ecs->m_free_entities_level1, 0xFFFFFFFF, 32 * 32 * sizeof(u32));
        x_memset(ecs->m_free_entities_level2, 0xFFFFFFFF, 32 * sizeof(u32));
        ecs->m_free_entities_level3 = 0xFFFFFFFF;

        ecs->m_entity0_cap  = 0;
        ecs->m_entity0_size = 0;
        u32 const cap_size  = s_entity_array_capacities[ecs->m_entity0_cap];

        // Initialize each 256 entities as a small linked list of free entities
        ecs->m_a_entity0 = (entity0_t*)allocator->allocate(sizeof(entity0_t) * cap_size);
        for (s32 i = 0; i < cap_size; i += 1)
        {
            for (u32 j = 0; j < 255; ++j)
                ecs->m_a_entity0[i++].m_en2_index = ((j + 1) << ECS_ENTITY_SHIFT) | ECS_ENTITY_ID_MASK;

            // Mark the end of the list with the full ECS_ENTITY_VERSION_MASK
            ecs->m_a_entity0[i].m_en2_index = (ECS_ENTITY_VERSION_MASK) | ECS_ENTITY_ID_MASK;
        }

        // The max number of components is 1024
        // Step size of + 4 components per array results in 256 arrays?
        s32 const num_en2_arrays = (s_max_num_components / s_cp_step_en2_array);
        ecs->m_a_a_entity2       = (entity2_t**)allocator->allocate(sizeof(entity2_t*) * num_en2_arrays);
        x_memset(ecs->m_a_a_entity2, 0, num_en2_arrays);

        return ecs;
    }

    static u32 s_create_entity(ecs2_t* ecs)
    {
        // A hierarchical bitset can quickly tell us the lowest free entity
        s8 const o3 = xfindFirstBit(ecs->m_free_entities_level3);
        s8 const o2 = xfindFirstBit(ecs->m_free_entities_level2[o3]);
        s8 const o1 = xfindFirstBit(ecs->m_free_entities_level1[(o3 * 32) + o2]);

        // o0 is the full index into level 0
        u32 const o0 = ((u32)o3 * 32 + (u32)o2) * 32 + (u32)o1;

        // the byte in level 0 is an extra offset on top of o0 that gives us
        // an index of a free entity in the m_a_entity0 array.
        u32 const eo = o0 + ecs->m_free_entities_level0[o0];

        entity0_t& e = ecs->m_a_entity0[eo];
        ASSERT((e.m_en2_index & ECS_ENTITY_ID_MASK) == ECS_ENTITY_ID_MASK);

        // get the next entity that is free in this linked list
        u32 const ne = (e.m_en2_index & ECS_ENTITY_VERSION_MASK) >> ECS_ENTITY_SHIFT;

        // however there could be no more free entities (end of list)
        if (ne == (ECS_ENTITY_VERSION_MASK >> ECS_ENTITY_SHIFT))
        {
            // You can point to any index, but we will be marked as full
            ecs->m_free_entities_level0[o0] = 0;

            if (s_clr_bit_in_u32(ecs->m_free_entities_level1[(o3 * 32) + o2], o1) == 0)
            {
                if (s_clr_bit_in_u32(ecs->m_free_entities_level2[o3], o2) == 0)
                {
                    s_clr_bit_in_u32(ecs->m_free_entities_level3, o3);
                }
            }
        }
        else
        {
            // Update the head of the linked list
            ecs->m_free_entities_level0[o0] = (u8)ne;
        }

        return 0;
    }

    static inline void s_set_u24(u8* base_ptr, u32 i, u32 v)
    {
        base_ptr += (i << 1) + i;
        base_ptr[0] = (u8)(v << 16);
        base_ptr[1] = (u8)(v << 8);
        base_ptr[2] = (u8)(v << 0);
    }

    static inline u32 s_get_u24(u8 const* base_ptr, u32 i)
    {
        base_ptr += (i << 1) + i;
        u32 const v = (base_ptr[0] << 16) | (base_ptr[1] << 8) | (base_ptr[2] << 0);
        return v;
    }

    static void s_cp_store_init(ecs2_t* ecs, cp_store_t* cp_store, cp_type_t const& cp_type)
    {
        cp_store->m_capacity_index = 0;
        cp_store->m_size           = 0;
        u32 const cp_capacity      = s_cp_store_capacities[0];
        cp_store->m_cp_data        = (u8*)ecs->m_allocator->allocate(cp_capacity * cp_type.cp_sizeof);
        cp_store->m_entity_ids     = (u8*)ecs->m_allocator->allocate(cp_capacity * 3); // 3 bytes per entity id
    }

    static void* s_reallocate(alloc_t* allocator, void* current_data, u32 current_datasize_in_bytes, u32 datasize_in_bytes)
    {
        void* data = allocator->allocate(datasize_in_bytes);
        if (current_data != nullptr)
        {
            x_memcpy(data, current_data, current_datasize_in_bytes);
            allocator->deallocate(current_data);
        }
        return data;
    }

    static void s_cp_store_grow(ecs2_t* ecs, cp_store_t* cp_store, cp_type_t const& cp_type)
    {
        cp_store->m_capacity_index += 1;
        cp_store->m_cp_data    = (u8*)s_reallocate(ecs->m_allocator, cp_store->m_cp_data, cp_store->m_size * cp_type.cp_sizeof, s_cp_store_capacities[cp_store->m_capacity_index] * cp_type.cp_sizeof);
        cp_store->m_entity_ids = (u8*)s_reallocate(ecs->m_allocator, cp_store->m_entity_ids, cp_store->m_size * 3, s_cp_store_capacities[cp_store->m_capacity_index] * 3);
    }

    static inline u8* s_cp_store_get_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_index)
    {
        u8* cp_data = cp_store->m_cp_data + (cp_type.cp_sizeof * cp_index);
        return cp_data;
    }

    static void s_cp_store_dealloc_cp(cp_store_t* cp_store, cp_type_t const& cp_type, u32 cp_index, u32& changed_entity_id, u32& new_cp_index)
    {
        // user should check if the entity that is processed is unequal to the 'changed entity id' to
        // see if it needs to update component index.
        u32 const last_cp_index = cp_store->m_size - 1;
        changed_entity_id       = cp_store->m_entity_ids[last_cp_index];
        if (cp_index != last_cp_index)
        {
            // swap remove
            cp_store->m_entity_ids[cp_index] = changed_entity_id;

            // copy component data (slow version)
            // TODO: speed up (align size and struct to u32?)
            u8 const* src_data = s_cp_store_get_cp(cp_store, cp_type, cp_index);
            u8*       dst_data = s_cp_store_get_cp(cp_store, cp_type, last_cp_index);
            s32       n        = 0;
            while (n < cp_type.cp_sizeof)
                *dst_data++ = *src_data++;
        }
        new_cp_index = cp_index;
        cp_store->m_size -= 1;
    }

    static u32 s_cp_store_alloc_cp(ecs2_t* ecs, cp_store_t* cp_store, cp_type_t const& cp_type)
    {
        // return index of newly allocated component
        if (cp_store->m_size == s_cp_store_capacities[cp_store->m_capacity_index])
        {
            s_cp_store_grow(ecs, cp_store, cp_type);
        }
        u32 const new_index = cp_store->m_size++;
        return new_index;
    }

    // --------------------------------------------------------------------------------------------------------
    // entity functionality

    static bool s_entity_has_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp)
    {
        entity0_t* ei = &ecs->m_a_entity0[g_entity_id(e)];

        u8 const shard_bit = cp.cp_id / 32;
        if ((ei->m_cp1_bitset & (1 << shard_bit)) != 0)
        {
            s8 const shard_idx = s_compute_index(ei->m_cp1_bitset, shard_bit);
            return (ei->m_cp2_bitset[shard_idx] & (1 << shard_bit)) != 0;
        }
        return false;
    }

    static void s_entity_set_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        // set/register a component on an entity.
        // Depending on the bit position we need to insert an index into the cp_inf->m_cp_data_offset[] array, this
        // means moving up some entries.
        if (ecs->m_a_cp_store[cp_type.cp_id].m_cp_data == nullptr)
        {
            s_cp_store_init(ecs, &ecs->m_a_cp_store[cp_type.cp_id], cp_type);
        }

        u32 const cp_index = s_cp_store_alloc_cp(ecs, &ecs->m_a_cp_store[cp_type.cp_id], cp_type);
    }

    static void* s_entity_get_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp_type)
    {
        entity0_t* ei = &ecs->m_a_entity0[g_entity_id(e)];

        u8 const shard_bit = cp_type.cp_id / 32;
        if ((ei->m_cp1_bitset & (1 << shard_bit)) != 0)
        {
            u8 const shard_idx = s_compute_index(ei->m_cp1_bitset, shard_bit);
            if ((ei->m_cp2_bitset[shard_idx] & (1 << shard_bit)) != 0)
            {
                // so now we need to retrieve the component data offset from entity2_t
                u16 const  en2_a_i = (ei->m_en2_index & ECS_ENTITY_VERSION_MASK) >> ECS_ENTITY_SHIFT;
                entity2_t* a_en2   = ecs->m_a_a_entity2[en2_a_i];
                u16 const  en2_i   = (ei->m_en2_index & ECS_ENTITY_ID_MASK);
                entity2_t* en2     = &a_en2[en2_i];

                u32 const cp_a_i = ei->m_cp2_bitcnt[(shard_idx - 1) & 0x3] + s_compute_index(ei->m_cp2_bitset[shard_idx], shard_bit);
                u32 const cp_i = en2->m_cp_data_offset[cp_a_i];

                cp_store_t* cp_store = &ecs->m_a_cp_store[cp_type.cp_id];
                ASSERT(cp_store != nullptr);

                return s_cp_store_get_cp(cp_store, cp_type, cp_i);
            }
        }
        return nullptr;
    }

    static bool s_entity_remove_component(ecs2_t* ecs, entity_t e, cp_type_t const& cp) { return false; }

    // If entities and their data are always in-order in their storage, the iterator would only have to
    // increment pointers on each [entity_id,component] array and keeping the entity_id in sync.
    // Also the component data is visited in-order.
    //
    // Also we want something in place which give us the smallest (id) when requesting a free entity id.
    // This means that we need to be able to quickly identify the smallest id that is free.
    // Currently thinking about a hierarchical bitset and at level 0 a byte that indexes into part of the
    // full entity_t array (256 entries).
    // For 4 million entity ids, we would need 32 Kb for the bytes and (512 u64 / 4 Kb) + (8 u64 / 64 b) + u8
    //

    const entity_t g_null_entity = (entity_t)ECS_ENTITY_ID_MASK;

    void* malloc(xsize_t size) { return nullptr; }
    void* realloc(void* ptr, xsize_t new_size) { return nullptr; }
    void  free(void* ptr) {}

    void memset(void* ptr, u32 c, u32 length) {}
    void memmove(void* dst, void* src, u32 length) {}

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
        const entity_id_t eid = g_entity_id(e);
        return (eid < s->sparse_size) && (s->sparse[eid] != g_null_entity);
    }

    static u32 s_sparse_index(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(s_sparse_contains(s, e));
        return s->sparse[g_entity_id(e)];
    }

    static void s_sparse_emplace(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(e != g_null_entity);
        const entity_id_t eid = g_entity_id(e);
        if (eid >= s->sparse_size)
        { // check if we need to realloc
            const u32 new_sparse_size = eid + 1;
            s->sparse                 = (entity_t*)realloc(s->sparse, new_sparse_size * sizeof *s->sparse);
            memset(s->sparse + s->sparse_size, g_null_entity, (new_sparse_size - s->sparse_size) * sizeof *s->sparse);
            s->sparse_size = new_sparse_size;
        }
        s->sparse[eid]          = (entity_t)s->dense_size; // set this eid index to the last dense index (dense_size)
        s->dense                = (entity_t*)realloc(s->dense, (s->dense_size + 1) * sizeof *s->dense);
        s->dense[s->dense_size] = e;
        s->dense_size++;
    }

    static u32 s_sparse_remove(storage_map_t* s, entity_t e)
    {
        ASSERT(s);
        ASSERT(s_sparse_contains(s, e));

        const u32      pos   = s->sparse[g_entity_id(e)];
        const entity_t other = s->dense[s->dense_size - 1];

        s->sparse[g_entity_id(other)] = (entity_t)pos;
        s->dense[pos]                 = other;
        s->sparse[pos]                = g_null_entity;

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

    cp_type_t g_register_component_type(ecs_t* r, u32 cp_sizeof, const char* cpname)
    {
        cp_nctype_t c = {0, 0, cpname};
        return *((cp_type_t*)&c);
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
        const entity_id_t id = g_entity_id(e);
        return id < r->entities_size && r->entities[id] == e;
    }

    static entity_t _s_generate_entity(ecs_t* r)
    {
        // can't create more identifiers entities
        ASSERT(r->entities_size < ECS_ENTITY_ID_MASK);

        // alloc one more element to the entities array
        r->entities = (entity_t*)realloc(r->entities, (r->entities_size + 1) * sizeof(entity_t));

        // create new entity and add it to the array
        const entity_t e              = g_make_entity((u32)r->entities_size, 0);
        r->entities[r->entities_size] = e;
        r->entities_size++;

        return e;
    }

    /* internal function to recycle a non used entity from the linked list */
    static entity_t _s_recycle_entity(ecs_t* r)
    {
        ASSERT(r->available_id != g_null_entity);
        // get the first available entity id
        const entity_id_t curr_id = r->available_id;
        // retrieve the version
        const entity_ver_t curr_ver = g_entity_version(r->entities[curr_id]);
        // point the available_id to the "next" id
        r->available_id = g_entity_id(r->entities[curr_id]);
        // now join the id and version to create the new entity
        const entity_t recycled_e = g_make_entity(curr_id, curr_ver);
        // assign it to the entities array
        r->entities[curr_id] = recycled_e;
        return recycled_e;
    }

    static void _s_release_entity(ecs_t* r, entity_t e, entity_ver_t desired_version)
    {
        const entity_id_t e_id = g_entity_id(e);
        r->entities[e_id]      = g_make_entity(r->available_id, desired_version);
        r->available_id        = e_id;
    }

    entity_t g_create(ecs_t* r)
    {
        ASSERT(r);
        if (r->available_id == g_null_entity)
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
        ECS_CP_INDEX_MASK  = 0x0000FFF,
        ECS_CP_GROUP_MASK  = 0x00FF000,
        ECS_CP_GROUP_SHIFT = 12,
    };

    static u32 s_get_cp_index(cp_type_t const& cp_type) { return cp_type.cp_id & ECS_CP_INDEX_MASK; }
    static u32 s_get_cp_group(cp_type_t const& cp_type) { return (cp_type.cp_id & ECS_CP_GROUP_MASK) >> ECS_CP_GROUP_SHIFT; }

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
        new_version++;
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

        if (r->available_id == g_null_entity)
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
                if (g_entity_id(e) == (i - 1))
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
