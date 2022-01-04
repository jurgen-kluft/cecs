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

        template <typename ECS_TYPE> class ecs_unique_cp_index_t
        {
        public:
            static inline u32 get_id() { return s_id++; }
        protected:
            static u32 s_id;
        };

        
        struct global_cp_group_t
        {
            static inline const char* name() { return "global"; }
        };

        struct physics_cp_group_t
        {
            static inline const char* name() { return "physics"; }
        };

        struct position_t
        {
            static inline const char* name() { return "position"; }
            static inline u32 id() { static u32 id = ecs_unique_cp_index_t<position_t>::get_id(); return id; }

            f32 x, y, z;
        };



        template <typename CP_TYPE, typename ECS_TYPE> class ecs_cp_info
        {
        public:
            const char* name() const { return CP_TYPE::name(); }
            const char* group() const { return ECS_TYPE::name(); }
            inline u32 size() const { return sizeof(CP_TYPE); }
            inline u32 get_id() const { return CP_TYPE::id(); }
        };

        struct velocity_t
        {
            static inline const char* name() { return "velocity"; }

            f32 x, y, z;
            f32 speed;
        };

        struct physics_state_t
        {
            static inline const char* name() { return "state"; }

            bool at_rest;
        };

        UNITTEST_TEST(create)
        {
            ecs_t*    ecs              = g_ecs_create();

            ecs_cp_info<position_t, global_cp_group_t> position_cp;
            ecs_cp_info<physics_state_t, physics_cp_group_t> physicsstate_cp;

            cp_type_t poscmp           = g_register_component_type(ecs, position_cp.name(), position_cp.size());
            cp_type_t velcmp           = g_register_component_type(ecs, "velocity", sizeof(velocity_t));
            cp_type_t physics_statecmp = g_register_component_type(ecs, physicsstate_cp.group(), physicsstate_cp.name(), physicsstate_cp.size());
        }
    }
}
UNITTEST_SUITE_END