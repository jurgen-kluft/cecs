#include "xbase/x_target.h"
#include "xbase/x_buffer.h"
#include "xecs/x_ecs.h"

#include "xunittest/xunittest.h"

using namespace xcore;

UNITTEST_SUITE_BEGIN(ecs)
{
    UNITTEST_FIXTURE(ecs)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        struct position_t
        {
            f32 x, y, z;
        };

        template <typename ECS_TYPE> class ecs_unique_cp_index_t
        {
        public:
            static inline u32 get_id() { return s_id++; }
        protected:
            static u32 s_id;
        };

        template <typename CP_TYPE, typename ECS_TYPE> class ecs_cp_info
        {
        public:
            ecs_type_info() : s_id(ecs_unique_cp_index_t<ECS_TYPE>::get_id()) {}

            const char* name() const { return "position"; }
            inline u32 get_id() const { return s_id; }

        protected:
            static const u32 s_id;
        };

        struct global_ecs_t
        {

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

        UNITTEST_TEST(create)
        {
            ecs_t*    ecs              = g_ecs_create();

            ecs_cp_info<position_t, 

            cb_type_t poscmp           = g_register_component_type(ecs, "position", sizeof(position_t));
            cb_type_t velcmp           = g_register_component_type(ecs, "velocity", sizeof(velocity_t));
            cb_type_t physics_statecmp = g_register_component_type(ecs, "physics", "physics-state", sizeof(physics_state));
        }
    }
}
UNITTEST_SUITE_END