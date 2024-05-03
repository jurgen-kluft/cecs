#include "ccore/c_target.h"
#include "cbase/c_buffer.h"
#include "cecs/c_ecs2.h"

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
using namespace ncore::necs2;

UNITTEST_SUITE_BEGIN(necs2)
{
    UNITTEST_FIXTURE(ecs)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(create)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

            cp_group_t* cp_base_group = g_register_cp_group(ecs);

            cp_type_t* byte_cp_type     = g_register_cp_type(ecs, cp_base_group, "byte", sizeof(u8));
            cp_type_t* position_cp_type = g_register_cp_type(ecs, cp_base_group, "position", sizeof(position_t));
            cp_type_t* velocity_cp_type = g_register_cp_type(ecs, cp_base_group, "velocity", sizeof(velocity_t));

            CHECK_NOT_NULL(byte_cp_type);
            CHECK_NOT_NULL(position_cp_type);
            CHECK_NOT_NULL(velocity_cp_type);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_tag_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

            cp_group_t* cp_base_group = g_register_cp_group(ecs);

            cp_type_t* friendly  = g_register_tg_type(ecs, cp_base_group, "friendly");
            cp_type_t* enemy_tag = g_register_tg_type(ecs, cp_base_group, "enemy_tag");
            cp_type_t* target    = g_register_tg_type(ecs, cp_base_group, "target");
            cp_type_t* dirty     = g_register_tg_type(ecs, cp_base_group, "dirty");

            CHECK_NOT_NULL(friendly);
            CHECK_NOT_NULL(target);
            CHECK_NOT_NULL(dirty);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

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

        UNITTEST_TEST(create_delete_many_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

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

        UNITTEST_TEST(create_entity_and_set_component)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

            cp_group_t* cp_base_group = g_register_cp_group(ecs);
            cp_type_t*  byte_cp_type  = g_register_cp_type(ecs, cp_base_group, "byte", sizeof(u8));

            entity_t e01 = g_create_entity(ecs);
            g_set_cp(ecs, e01, byte_cp_type);

            CHECK_TRUE(g_has_cp(ecs, e01, byte_cp_type));

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        struct enemy_tag_t
        {
        };

        UNITTEST_TEST(create_entity_and_set_tag)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

            cp_group_t* cp_base_group = g_register_cp_group(ecs);
            cp_type_t * enemy_tag = g_register_tg_type(ecs, cp_base_group, "enemy_tag");

            entity_t e01 = g_create_entity(ecs);
            g_set_tag(ecs, e01, enemy_tag);

            CHECK_TRUE(g_has_tag(ecs, e01, enemy_tag));
            g_rem_tag(ecs, e01, enemy_tag);
            CHECK_FALSE(g_has_tag(ecs, e01, enemy_tag));

            g_destroy_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(iterator_basic)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc(), 1024);

            cp_group_t* cp_base_group = g_register_cp_group(ecs);

            cp_type_t* byte_cp_type     = g_register_cp_type(ecs, cp_base_group, "byte", sizeof(u8));
            cp_type_t* position_cp_type = g_register_cp_type(ecs, cp_base_group, "position", sizeof(position_t));
            cp_type_t* velocity_cp_type = g_register_cp_type(ecs, cp_base_group, "velocity", sizeof(velocity_t));

            cp_type_t *enemy_tag = g_register_tg_type(ecs, cp_base_group, "enemy_tag");

            entity_t e01 = g_create_entity(ecs);
            entity_t e02 = g_create_entity(ecs);
            entity_t e03 = g_create_entity(ecs);
            entity_t e04 = g_create_entity(ecs);

            g_set_cp(ecs, e01, byte_cp_type);
            g_set_cp(ecs, e03, byte_cp_type);
            g_set_cp(ecs, e04, byte_cp_type);

            g_set_cp(ecs, e01, position_cp_type);
            g_set_cp(ecs, e03, position_cp_type);

            g_set_cp(ecs, e01, velocity_cp_type);
            g_set_cp(ecs, e03, velocity_cp_type);

            g_set_tag(ecs, e01, enemy_tag);
            g_set_tag(ecs, e02, enemy_tag);
            g_set_tag(ecs, e03, enemy_tag);

            en_iterator_t iter;
            iter.initialize(ecs);

            iter.cp_type(byte_cp_type);
            iter.cp_type(position_cp_type);
            iter.tg_type(enemy_tag);

            iter.begin();
            while (!iter.end())
            {
                entity_t e = iter.entity();
                CHECK_TRUE(e == e01 || e == e03);

                CHECK_TRUE(g_has_cp(ecs, e, byte_cp_type));
                CHECK_TRUE(g_has_cp(ecs, e, position_cp_type));
                CHECK_TRUE(g_has_tag(ecs, e, enemy_tag));

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