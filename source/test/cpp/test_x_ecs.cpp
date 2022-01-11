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
            ecs2_t* ecs = g_ecs_create(context_t::system_alloc());

            // entity_t e = g_create_entity(ecs);
            // g_delete_entity(ecs, e);

            g_ecs_destroy(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs2_t* ecs = g_ecs_create(context_t::system_alloc());

            cp_type_t const* bytecmp = g_register_component_type<u8>(ecs);
            cp_type_t const* poscmp  = g_register_component_type<position_t>(ecs);
            cp_type_t const* velcmp  = g_register_component_type<velocity_t>(ecs);

            entity_type_t const* ent0 = g_register_entity_type(ecs, 1024);

            entity_t e = g_create_entity(ecs, ent0);
            g_delete_entity(ecs, e);

            g_ecs_destroy(ecs);
        }

        UNITTEST_TEST(create_entity)
        {
            ecs2_t* ecs = g_ecs_create(context_t::system_alloc());

            // entity_t e = g_create_entity(ecs);
            // g_delete_entity(ecs, e);

            g_ecs_destroy(ecs);
        }

        UNITTEST_TEST(create_entity_and_attach_component)
        {
            ecs2_t* ecs = g_ecs_create(context_t::system_alloc());

            // entity_t e = g_create_entity(ecs);

            // cp_type_t const* bytecmp = g_register_component_type<u8>(ecs);
            // g_attach_component(ecs, e, bytecmp);

            // g_delete_entity(ecs, e);

            g_ecs_destroy(ecs);
        }
    }
}
UNITTEST_SUITE_END