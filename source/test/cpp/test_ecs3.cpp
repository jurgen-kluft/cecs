#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "cbase/c_buffer.h"
#include "cecs/c_ecs3.h"

#include "cunittest/cunittest.h"

using namespace ncore;
using namespace ncore::necs3;

namespace ncore
{
    struct position_t
    {
        DECLARE_ECS3_COMPONENT(0);
        f32 x, y, z;
    };

    struct velocity_t
    {
        DECLARE_ECS3_COMPONENT(1);
        f32 x, y, z;
        f32 speed;
    };

    struct physics_state_t
    {
        DECLARE_ECS3_COMPONENT(2);
        bool at_rest;
    };

    struct u8_t
    {
        DECLARE_ECS3_COMPONENT(3);
        u8 value;
    };

    struct enemy_tag_t
    {
        DECLARE_ECS3_TAG(0);
    };

    struct friendly_tag_t
    {
        DECLARE_ECS3_TAG(1);
    };

    struct target_tag_t
    {
        DECLARE_ECS3_TAG(2);
    };

    struct dirty_tag_t
    {
        DECLARE_ECS3_TAG(3);
    };

} // namespace ncore

UNITTEST_SUITE_BEGIN(ecs3)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(create_destroy_ecs)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

            CHECK_TRUE((g_register_component<u8_t>(ecs, 512, "u8")));
            CHECK_TRUE((g_register_component<position_t>(ecs, 512, "position")));
            CHECK_TRUE((g_register_component<velocity_t>(ecs, 512, "velocity")));
            CHECK_TRUE((g_register_component<physics_state_t>(ecs, 512, "physics state")));

            g_unregister_component<physics_state_t>(ecs);
            g_unregister_component<velocity_t>(ecs);
            g_unregister_component<position_t>(ecs);
            g_unregister_component<u8_t>(ecs);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_and_destroy_entities)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

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
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

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
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

            g_register_component<u8_t>(ecs, 512, "u8");
            g_register_component<position_t>(ecs, 512, "position");

            entity_t e01 = g_create_entity(ecs);

            u8_t* cpa1 = g_add_cp<u8_t>(ecs, e01);
            CHECK_NOT_NULL(cpa1);
            CHECK_TRUE(g_has_cp<u8_t>(ecs, e01));
            u8_t* cp1 = g_get_cp<u8_t>(ecs, e01);
            CHECK_NOT_NULL(cp1);
            CHECK_EQUAL(cpa1, cp1);

            position_t* cpa2 = g_add_cp<position_t>(ecs, e01);
            CHECK_NOT_NULL(cpa2);
            CHECK_TRUE(g_has_cp<position_t>(ecs, e01));
            position_t* cp2 = g_get_cp<position_t>(ecs, e01);
            CHECK_NOT_NULL(cp2);
            CHECK_EQUAL(cpa2, cp2);

            g_destroy_entity(ecs, e01);

            g_unregister_component<u8_t>(ecs);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_add_tag)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

            entity_t e01 = g_create_entity(ecs);
            g_add_tag<enemy_tag_t>(ecs, e01);

            CHECK_TRUE(g_has_tag<enemy_tag_t>(ecs, e01));
            g_rem_tag<enemy_tag_t>(ecs, e01);
            CHECK_FALSE(g_has_tag<enemy_tag_t>(ecs, e01));

            g_destroy_entity(ecs, e01);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(iterator_basic)
        {
            ecs_t* ecs = g_create_ecs(Allocator, 1024, 256, 64);

            g_register_component<u8_t>(ecs, 512);
            g_register_component<position_t>(ecs, 512);
            g_register_component<velocity_t>(ecs, 512);

            entity_t e01 = g_create_entity(ecs);
            entity_t e02 = g_create_entity(ecs);
            entity_t e03 = g_create_entity(ecs);
            entity_t e04 = g_create_entity(ecs);

            g_add_cp<u8_t>(ecs, e01);
            g_add_cp<u8_t>(ecs, e03);
            g_add_cp<u8_t>(ecs, e04);

            g_add_cp<position_t>(ecs, e01);
            g_add_cp<position_t>(ecs, e03);

            g_add_cp<velocity_t>(ecs, e01);
            g_add_cp<velocity_t>(ecs, e02);
            g_add_cp<velocity_t>(ecs, e03);
            g_add_cp<velocity_t>(ecs, e04);

            g_add_tag<enemy_tag_t>(ecs, e01);
            g_add_tag<enemy_tag_t>(ecs, e02);
            g_add_tag<enemy_tag_t>(ecs, e03);

            {
                entity_t reference = g_create_entity(ecs);
                g_add_cp<u8_t>(ecs, reference);
                g_add_cp<position_t>(ecs, reference);
                g_add_tag<enemy_tag_t>(ecs, reference);

                en_iterator_t iter(ecs, reference);

                iter.begin();
                while (!iter.end())
                {
                    entity_t e = iter.entity();
                    CHECK_TRUE(e == e01 || e == e03);

                    CHECK_TRUE(g_has_cp<u8_t>(ecs, e));
                    CHECK_TRUE(g_has_cp<position_t>(ecs, e));
                    CHECK_TRUE(g_has_tag<enemy_tag_t>(ecs, e));

                    iter.next();
                }

                g_destroy_entity(ecs, reference);
            }

            {
                entity_t reference = g_create_entity(ecs);
                g_add_cp<velocity_t>(ecs, reference);
                g_add_tag<enemy_tag_t>(ecs, reference);

                en_iterator_t iter(ecs, reference);

                iter.begin();
                while (!iter.end())
                {
                    entity_t e = iter.entity();
                    CHECK_TRUE(e == e01 || e == e02 || e == e03);

                    CHECK_TRUE(g_has_cp<velocity_t>(ecs, e));
                    CHECK_TRUE(g_has_tag<enemy_tag_t>(ecs, e));

                    iter.next();
                }

                g_destroy_entity(ecs, reference);
            }

            {
                en_iterator_t iter(ecs);

                // Iterate all the entities

                s32 index = 0;
                iter.begin();
                while (!iter.end())
                {
                    entity_t e = iter.entity();
                    CHECK_TRUE((e == e01 && index == 0) || (e == e02 && index == 1) || (e == e03 && index == 2) || (e == e04 && index == 3));

                    CHECK_TRUE(g_has_cp<velocity_t>(ecs, e));

                    iter.next();
                    index += 1;
                }
            }

            g_destroy_entity(ecs, e01);
            g_destroy_entity(ecs, e02);
            g_destroy_entity(ecs, e03);
            g_destroy_entity(ecs, e04);

            g_unregister_component<position_t>(ecs);
            g_unregister_component<velocity_t>(ecs);
            g_unregister_component<u8_t>(ecs);

            g_destroy_ecs(ecs);
        }
    }
}
UNITTEST_SUITE_END
