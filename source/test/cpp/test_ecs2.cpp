#include "ccore/c_target.h"
#include "cbase/c_buffer.h"
#include "cecs/c_ecs2.h"

#include "cunittest/cunittest.h"
#include "cecs/test_allocator.h"

using namespace ncore;
using namespace ncore::necs2;

namespace ncore
{
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

    struct enemy_tag_t
    {
    };

    struct friendly_tag_t
    {
    };

    struct target_tag_t
    {
    };

    struct dirty_tag_t
    {
    };

    struct base_group_t
    {
    };

    REGISTER_ECS_GROUP(base_group_t);
    DECLARE_ECS_GROUP(base_group_t);
    REGISTER_ECS_COMPONENT(position_t);
    DECLARE_ECS_COMPONENT(position_t);
    REGISTER_ECS_COMPONENT(velocity_t);
    DECLARE_ECS_COMPONENT(velocity_t);
    REGISTER_ECS_COMPONENT(physics_state_t);
    DECLARE_ECS_COMPONENT_NAMED(physics_state_t, "physics state");
    REGISTER_ECS_COMPONENT(u8);
    DECLARE_ECS_COMPONENT(u8);
    REGISTER_ECS_TAG(enemy_tag_t);
    DECLARE_ECS_TAG_NAMED(enemy_tag_t, "enemy tag");
    REGISTER_ECS_TAG(friendly_tag_t);
    DECLARE_ECS_TAG_NAMED(friendly_tag_t, "friendly tag");
    REGISTER_ECS_TAG(target_tag_t);
    DECLARE_ECS_TAG_NAMED(target_tag_t, "target tag");
    REGISTER_ECS_TAG(dirty_tag_t);
    DECLARE_ECS_TAG_NAMED(dirty_tag_t, "dirty tag");

} // namespace ncore

UNITTEST_SUITE_BEGIN(ecs2)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(create_destroy_ecs)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            g_register_cp_group<base_group_t>(ecs, 1024);

            g_register_component<base_group_t, u8>();
            g_register_component<base_group_t, position_t>();
            g_register_component<base_group_t, velocity_t>();
            g_register_component<base_group_t, physics_state_t>();

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_tag_types)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            g_register_cp_group<base_group_t>(ecs, 1024);

            g_register_tag<base_group_t, enemy_tag_t>();
            g_register_tag<base_group_t, friendly_tag_t>();
            g_register_tag<base_group_t, target_tag_t>();
            g_register_tag<base_group_t, dirty_tag_t>();

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_and_destroy_entities)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            entity_t e01 = g_create_entity(ecs);
            entity_t e02 = g_create_entity(ecs);
            entity_t e03 = g_create_entity(ecs);
            entity_t e04 = g_create_entity(ecs);

            g_destroy_entity(ecs, e01);
            g_destroy_entity(ecs, e02);
            g_destroy_entity(ecs, e03);
            g_destroy_entity(ecs, e04);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_destroy_many_entities)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            entity_t entities[512];
            for (s32 i = 0; i < 512; ++i)
            {
                entities[i] = g_create_entity(ecs);
            }

            for (s32 i = 0; i < 512; ++i)
            {
                g_destroy_entity(ecs, entities[i]);
            }

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_add_component)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            g_register_cp_group<base_group_t>(ecs, 1024);
            g_register_component<base_group_t, u8>();

            entity_t e01 = g_create_entity(ecs);
            g_add_cp<u8>(e01);

            CHECK_TRUE(g_has_cp<u8>(e01));

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_add_tag)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            g_register_cp_group<base_group_t>(ecs, 1024);
            g_register_tag<base_group_t, enemy_tag_t>();

            entity_t e01 = g_create_entity(ecs);
            g_add_tag<enemy_tag_t>(e01);

            CHECK_TRUE(g_has_tag<enemy_tag_t>(e01));
            g_rem_tag<enemy_tag_t>(e01);
            CHECK_FALSE(g_has_tag<enemy_tag_t>(e01));

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(iterator_basic)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024);

            g_register_cp_group<base_group_t>(ecs, 1024);

            g_register_component<base_group_t, u8>();
            g_register_component<base_group_t, position_t>();
            g_register_component<base_group_t, velocity_t>();

            g_register_tag<base_group_t, enemy_tag_t>();

            entity_t e01 = g_create_entity(ecs);
            entity_t e02 = g_create_entity(ecs);
            entity_t e03 = g_create_entity(ecs);
            entity_t e04 = g_create_entity(ecs);

            g_add_cp<u8>(e01);
            g_add_cp<u8>(e03);
            g_add_cp<u8>(e04);

            g_add_cp<position_t>(e01);
            g_add_cp<position_t>(e03);

            g_add_cp<velocity_t>(e01);
            g_add_cp<velocity_t>(e03);

            g_add_tag<enemy_tag_t>(e01);
            g_add_tag<enemy_tag_t>(e02);
            g_add_tag<enemy_tag_t>(e03);

            en_iterator_t iter(ecs);

            iter.set_cp_type<u8>();
            iter.set_cp_type<position_t>();
            iter.set_tg_type<enemy_tag_t>();

            iter.begin();
            while (!iter.end())
            {
                entity_t e = iter.entity();
                CHECK_TRUE(e == e01 || e == e03);

                CHECK_TRUE(g_has_cp<u8>(e));
                CHECK_TRUE(g_has_cp<position_t>(e));
                CHECK_TRUE(g_has_tag<enemy_tag_t>(e));

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
