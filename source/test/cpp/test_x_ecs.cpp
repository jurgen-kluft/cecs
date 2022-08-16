#include "cbase/c_target.h"
#include "cbase/c_buffer.h"
#include "cecs/c_ecs.h"

#include "cunittest/xunittest.h"

namespace ncore
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

} // namespace ncore

using namespace ncore;

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
            ecs_t*     ecs  = g_create_ecs(context_t::system_alloc());
            en_type_t* ent0 = g_register_entity_type(ecs, 1024);
            en_type_t* ent1 = g_register_entity_type(ecs, 2048);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(register_component_types)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            cp_type_t* bytecmp = g_register_component_type<u8>(ecs);
            cp_type_t* poscmp  = g_register_component_type<position_t>(ecs);
            cp_type_t* velcmp  = g_register_component_type<velocity_t>(ecs);

			CHECK_EQUAL(0, bytecmp->cp_id);
			CHECK_EQUAL(1, poscmp->cp_id);
			CHECK_EQUAL(2, velcmp->cp_id);

            g_destroy_ecs(ecs);
        }

		struct friendly {};
		struct target {};
		struct dirty {};

		UNITTEST_TEST(register_tag_types)
		{
			ecs_t* ecs = g_create_ecs(context_t::system_alloc());

			tg_type_t* acmp  = g_register_tag_type<friendly>(ecs);
			tg_type_t* bcmp  = g_register_tag_type<target>(ecs);
			tg_type_t* ccmp  = g_register_tag_type<dirty>(ecs);

			CHECK_EQUAL(0, acmp->tg_id);
			CHECK_EQUAL(1, bcmp->tg_id);
			CHECK_EQUAL(2, ccmp->tg_id);

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

            g_delete_entity(ecs, e01);
            g_delete_entity(ecs, e02);
            g_delete_entity(ecs, e03);
            g_delete_entity(ecs, e04);

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_delete_many_entities_in_one_entity_type)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0 = g_register_entity_type(ecs, 1024);

            entity_t entities[512];
            for (s32 i=0; i<512; ++i)
            {
                entities[i] = g_create_entity(ecs, ent0);
            }
            for (s32 i=0; i<512; ++i)
            {
                g_delete_entity(ecs, entities[i]);
            }

            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(create_entity_and_set_component)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0    = g_register_entity_type(ecs, 1024);
            cp_type_t* bytecmp = g_register_component_type<u8>(ecs);

            entity_t e01 = g_create_entity(ecs, ent0);
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

            en_type_t* ent0  = g_register_entity_type(ecs, 1024);
            tg_type_t* enemy = g_register_tag_type<enemy_tag_t>(ecs);

            entity_t e01 = g_create_entity(ecs, ent0);
            g_set_tag(ecs, e01, enemy);

            CHECK_TRUE(g_has_tag(ecs, e01, enemy));

            g_rem_tag(ecs, e01, enemy);

            g_delete_entity(ecs, e01);
            g_destroy_ecs(ecs);
        }

        UNITTEST_TEST(iterator_basic)
        {
            ecs_t* ecs = g_create_ecs(context_t::system_alloc());

            en_type_t* ent0  = g_register_entity_type(ecs, 1024);
            cp_type_t* bytecmp = g_register_component_type<u8>(ecs);
            cp_type_t* poscmp  = g_register_component_type<position_t>(ecs);
            cp_type_t* velcmp  = g_register_component_type<velocity_t>(ecs);
            tg_type_t* enemy = g_register_tag_type<enemy_tag_t>(ecs);

            entity_t   e01  = g_create_entity(ecs, ent0);
            entity_t   e02  = g_create_entity(ecs, ent0);
            entity_t   e03  = g_create_entity(ecs, ent0);
            entity_t   e04  = g_create_entity(ecs, ent0);

            g_set_cp(ecs, e01, bytecmp);
            g_set_cp(ecs, e03, bytecmp);
            g_set_cp(ecs, e04, bytecmp);

            g_set_cp(ecs, e01, poscmp);
            g_set_cp(ecs, e03, poscmp);

            g_set_cp(ecs, e01, velcmp);
            g_set_cp(ecs, e03, velcmp);

            g_set_tag(ecs, e01, enemy);
            g_set_tag(ecs, e02, enemy);
            g_set_tag(ecs, e03, enemy);

            en_iterator_t iter;
            iter.initialize(ecs);

            iter.cp_type(bytecmp);
            iter.cp_type(poscmp);
            iter.tg_type(enemy);

            iter.begin();
            while (!iter.end())
            {
                entity_t e = iter.item();

                CHECK_TRUE(g_has_cp(ecs, e, bytecmp));
                CHECK_TRUE(g_has_cp(ecs, e, poscmp));
                CHECK_TRUE(g_has_tag(ecs, e, enemy));

                iter.next();
            }

            g_delete_entity(ecs, e01);
            g_delete_entity(ecs, e02);
            g_delete_entity(ecs, e03);
            g_delete_entity(ecs, e04);
            g_destroy_ecs(ecs);
        }
    }
}
UNITTEST_SUITE_END