#include "xbase/x_target.h"
#include "xbase/x_buffer.h"
#include "xecs/x_ecs.h"

#include "xunittest/xunittest.h"

namespace xcore
{
    // You can make components that are just system types, in this case just a byte
    template <> inline const char* nameof<u8>() { return "u8"; }

    struct position_t
    {
        f32 x, y, z;
    };
    template <> inline const char* nameof<position_t>() { return "position"; }

    struct velocity_t
    {
        f32 x, y, z;
        f32 speed;
    };
    template <> inline const char* nameof<velocity_t>() { return "velocity"; }

    struct physics_state_t
    {
        bool at_rest;
    };
    template <> inline const char* nameof<physics_state_t>() { return "physics-state"; }

} // namespace xcore

using namespace xcore;

UNITTEST_SUITE_BEGIN(ecs)
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
            ecs_t*           ecs  = g_create_ecs(context_t::system_alloc());
            en_type_t const* ent0 = g_register_entity_type(ecs, 1024);
            en_type_t const* ent1 = g_register_entity_type(ecs, 2048);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            cp_type_t const* bytecmp = g_register_component_type<u8>(ecs);
            cp_type_t const* poscmp  = g_register_component_type<position_t>(ecs);
            cp_type_t const* velcmp  = g_register_component_type<velocity_t>(ecs);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t const* ent0 = g_register_entity_type(ecs, 1024);
            entity_t         e01  = g_create_entity(ecs, ent0);
            entity_t         e02  = g_create_entity(ecs, ent0);
            entity_t         e03  = g_create_entity(ecs, ent0);
            entity_t         e04  = g_create_entity(ecs, ent0);

            g_delete_entity(ecs, e01);
            g_delete_entity(ecs, e02);
            g_delete_entity(ecs, e03);
            g_delete_entity(ecs, e04);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_set_component)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t const* ent0 = g_register_entity_type(ecs, 1024);
            entity_t         e01  = g_create_entity(ecs, ent0);

            cp_type_t const* bytecmp = g_register_component_type<u8>(ecs);
            g_set_cp(ecs, e01, bytecmp);

            CHECK_TRUE(g_has_cp(ecs, e01, bytecmp));

            g_delete_entity(ecs, e01);

            g_destroy_ecs(ecs);
        }

        struct enemy_tag_t
        {
        };

        UNITTEST_TEST(create_entity_and_set_tag)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t const* ent0 = g_register_entity_type(ecs, 1024);
            entity_t         e01  = g_create_entity(ecs, ent0);

            tg_type_t const* enemy = g_register_tag_type<enemy_tag_t>(ecs);
            g_set_tag(ecs, e01, enemy);

            CHECK_TRUE(g_has_tag(ecs, e01, enemy));

            g_delete_entity(ecs, e01);

            g_destroy_ecs(ecs);
        }
    }
}
UNITTEST_SUITE_END