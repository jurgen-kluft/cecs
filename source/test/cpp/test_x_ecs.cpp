#include "xbase/x_target.h"
#include "xbase/x_buffer.h"
#include "xecs/x_ecs.h"

#include "xunittest/xunittest.h"


namespace xcore
{
    struct global_cp_group_t
    {
        static inline const char* name() { return "global"; }
        inline static u32 get_id() { return s_id; }
        inline static void set_id(u32 id) { s_id = id; }
        static u32 s_id;
    };
    u32 global_cp_group_t::s_id = 0xffffffff;

    struct physics_cp_group_t
    {
        static inline const char* name() { return "physics"; }
        inline static u32 get_id() { return s_id; }
        inline static void set_id(u32 id) { s_id = id; }
        static u32 s_id;
    };
    u32 physics_cp_group_t::s_id = 0xffffffff;

    struct position_t
    {
        f32 x, y, z;
    };

    template<>
    class cp_info_t<position_t> 
    {
    public:
        static inline const char* name() { return "position"; }
        inline static u32 get_id() { return s_id; }
        inline static void set_id(u32 id) { s_id = id; }
        static u32 s_id;
    };
    u32 cp_info_t<position_t>::s_id = 0xffffffff;

    struct velocity_t
    {
        f32 x, y, z;
        f32 speed;
    };

    template<>
    class cp_info_t<velocity_t> 
    {
    public:
        static inline const char* name() { return "velocity"; }
        inline static u32 get_id() { return s_id; }
        inline static void set_id(u32 id) { s_id = id; }
        static u32 s_id;
    };
    u32 cp_info_t<velocity_t>::s_id = 0xffffffff;

    struct physics_state_t
    {
        bool at_rest;
    };

    template<>
    class cp_info_t<physics_state_t> 
    {
    public:
        static inline const char* name() { return "physics-state"; }
        inline static u32 get_id() { return s_id; }
        inline static void set_id(u32 id) { s_id = id; }
        static u32 s_id ;
    };
    u32 cp_info_t<physics_state_t>::s_id = 0xffffffff;

}


using namespace xcore;

UNITTEST_SUITE_BEGIN(ecs)
{
    UNITTEST_FIXTURE(ecs)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}
        

        UNITTEST_TEST(create)
        {
            ecs_t*    ecs              = g_ecs_create();

            cp_type_t poscmp           = g_register_component_type<position_t>(ecs);
            cp_type_t velcmp           = g_register_component_type<velocity_t>(ecs);
            cp_type_t physics_statecmp = g_register_component_type<physics_state_t, physics_cp_group_t>(ecs);
        }
    }
}
UNITTEST_SUITE_END