#include "ccore/c_target.h"
#include "cbase/c_buffer.h"
#include "cecs/c_ecs.h"

#include "cunittest/cunittest.h"

namespace ncore
{
    // You can make components that are just system types, in this case just a byte

    struct position_t
    {
        f32 x, y, z;
    };

    struct velocity_t
    {
        f32 x, y, z;
        f32 speed;
    };

    struct physics_state_t
    {
        bool at_rest;
    };

} // namespace ncore

using namespace ncore;
using namespace ncore::necs;

UNITTEST_SUITE_BEGIN(necs)
{
    UNITTEST_FIXTURE(ecs)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(create)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_entity_types)
        {
            ecs_t*     ecs  = g_create_ecs(context_t::system_alloc());
            en_type_t* ent0 = g_register_entity_type(ecs, 1024);
            en_type_t* ent1 = g_register_entity_type(ecs, 2048);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            cp_type_t byte_cp_type     = {-1, sizeof(u8), "byte"};
            cp_type_t position_cp_type = {-1, sizeof(position_t), "position"};
            cp_type_t velocity_cp_type = {-1, sizeof(velocity_t), "velocity"};

            g_register_component_type(ecs, &byte_cp_type);
            g_register_component_type(ecs, &position_cp_type);
            g_register_component_type(ecs, &velocity_cp_type);

            CHECK_EQUAL(0, byte_cp_type.cp_id);
            CHECK_EQUAL(1, position_cp_type.cp_id);
            CHECK_EQUAL(2, velocity_cp_type.cp_id);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_tag_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            tg_type_t friendly  = {-1, "friendly"};
            tg_type_t enemy_tag = {-1, "enemy_tag"};
            tg_type_t target    = {-1, "target"};
            tg_type_t dirty     = {-1, "dirty"};

            g_register_tag_type(ecs, &friendly);
            g_register_tag_type(ecs, &target);
            g_register_tag_type(ecs, &dirty);

            CHECK_EQUAL(0, friendly.tg_id);
            CHECK_EQUAL(1, target.tg_id);
            CHECK_EQUAL(2, dirty.tg_id);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);
            entity_t   e01  = g_create_entity(ecs, ent0);
            entity_t   e02  = g_create_entity(ecs, ent0);
            entity_t   e03  = g_create_entity(ecs, ent0);
            entity_t   e04  = g_create_entity(ecs, ent0);

            g_destroy_entity(ecs, e01);
            g_destroy_entity(ecs, e02);
            g_destroy_entity(ecs, e03);
            g_destroy_entity(ecs, e04);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_delete_many_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);

            entity_t entities[512];
            for (s32 i = 0; i < 512; ++i)
            {
                entities[i] = g_create_entity(ecs, ent0);
            }
            for (s32 i = 0; i < 512; ++i)
            {
                g_destroy_entity(ecs, entities[i]);
            }

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_set_component)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);

            cp_type_t byte_cp_type     = {-1, sizeof(u8), "byte"};

            g_register_component_type(ecs, &byte_cp_type);

            entity_t e01 = g_create_entity(ecs, ent0);
            g_set_cp(ecs, e01, &byte_cp_type);

            CHECK_TRUE(g_has_cp(ecs, e01, &byte_cp_type));

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        struct enemy_tag_t
        {
        };

        UNITTEST_TEST(create_entity_and_set_tag)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            tg_type_t enemy_tag = {-1, "enemy_tag"};

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);
            g_register_tag_type(ecs, &enemy_tag);

            entity_t e01 = g_create_entity(ecs, ent0);
            g_set_tag(ecs, e01, &enemy_tag);

            CHECK_TRUE(g_has_tag(ecs, e01, &enemy_tag));

            g_rem_tag(ecs, e01, &enemy_tag);

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(iterator_basic)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            cp_type_t byte_cp_type     = {-1, sizeof(u8), "byte"};
            cp_type_t position_cp_type = {-1, sizeof(position_t), "position"};
            cp_type_t velocity_cp_type = {-1, sizeof(velocity_t), "velocity"};

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);
            g_register_component_type(ecs, &byte_cp_type);
            g_register_component_type(ecs, &position_cp_type);
            g_register_component_type(ecs, &velocity_cp_type);

            tg_type_t enemy_tag = {-1, "enemy_tag"};
            g_register_tag_type(ecs, &enemy_tag);

            entity_t e01 = g_create_entity(ecs, ent0);
            entity_t e02 = g_create_entity(ecs, ent0);
            entity_t e03 = g_create_entity(ecs, ent0);
            entity_t e04 = g_create_entity(ecs, ent0);

            g_set_cp(ecs, e01, &byte_cp_type);
            g_set_cp(ecs, e03, &byte_cp_type);
            g_set_cp(ecs, e04, &byte_cp_type);

            g_set_cp(ecs, e01, &position_cp_type);
            g_set_cp(ecs, e03, &position_cp_type);

            g_set_cp(ecs, e01, &velocity_cp_type);
            g_set_cp(ecs, e03, &velocity_cp_type);

            g_set_tag(ecs, e01, &enemy_tag);
            g_set_tag(ecs, e02, &enemy_tag);
            g_set_tag(ecs, e03, &enemy_tag);

            en_iterator_t iter;
            iter.initialize(ecs);

            iter.cp_type(&byte_cp_type);
            iter.cp_type(&position_cp_type);
            iter.tg_type(&enemy_tag);

            iter.begin();
            while (!iter.end())
            {
                entity_t e = iter.item();

                CHECK_TRUE(g_has_cp(ecs, e, &byte_cp_type));
                CHECK_TRUE(g_has_cp(ecs, e, &position_cp_type));
                CHECK_TRUE(g_has_tag(ecs, e, &enemy_tag));

                iter.next();
            }

            g_destroy_entity(ecs, e01);
            g_destroy_entity(ecs, e02);
            g_destroy_entity(ecs, e03);
            g_destroy_entity(ecs, e04);
            g_destroy_ecs(ecs);
        }
    }
}
UNITTEST_SUITE_END