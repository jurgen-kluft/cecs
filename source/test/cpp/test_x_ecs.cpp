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
            ecs_t* ecs = g_ecs_create();

            cp_type_t bytecmp          = g_register_component_type<u8>(ecs);
            cp_type_t poscmp           = g_register_component_type<position_t>(ecs);
            cp_type_t velcmp           = g_register_component_type<velocity_t>(ecs);
        }
    }
}
UNITTEST_SUITE_END