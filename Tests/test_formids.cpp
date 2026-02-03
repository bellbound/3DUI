#include <catch2/catch_all.hpp>

#include "projectile/FormIDs.h"

using namespace Projectile::FormIDs;

// ============================================================================
// FormIDs Tests
// ============================================================================

TEST_CASE("FormIDs Plugin Name", "[formids]") {
    SECTION("Plugin name is correct") {
        REQUIRE(std::string(PluginName) == "3DUI.esp");
    }
}

TEST_CASE("FormIDs Projectile Form IDs", "[formids]") {
    SECTION("Has correct count") {
        REQUIRE(ProjectileFormIDs.size() == 200);
    }

    SECTION("First form ID is correct") {
        // FE00280A -> base form ID 0x80A
        REQUIRE(ProjectileFormIDs[0] == 0x80A);
    }

    SECTION("Last form ID is correct") {
        // 0x943 is the last extended projectile
        REQUIRE(ProjectileFormIDs[199] == 0x943);
    }

    SECTION("All form IDs are within ESL range") {
        // ESL base form IDs should be in range 0x800 - 0xFFF
        for (auto formID : ProjectileFormIDs) {
            REQUIRE(formID >= 0x800);
            REQUIRE(formID <= 0xFFF);
        }
    }
}

TEST_CASE("FormIDs Ammo Form IDs", "[formids]") {
    SECTION("Has correct count") {
        REQUIRE(AmmoFormIDs.size() == 200);
    }

    SECTION("First form ID is correct") {
        // 0200087C -> base form ID 0x87C
        REQUIRE(AmmoFormIDs[0] == 0x87C);
    }

    SECTION("Last form ID is correct") {
        // 0xA7B is the last extended ammo
        REQUIRE(AmmoFormIDs[199] == 0xA7B);
    }

    SECTION("All form IDs are within ESL range") {
        for (auto formID : AmmoFormIDs) {
            REQUIRE(formID >= 0x800);
            REQUIRE(formID <= 0xFFF);
        }
    }
}


TEST_CASE("FormIDs Pool Size Compatibility", "[formids]") {
    SECTION("Projectile and Ammo arrays have same size for 1:1 slot mapping") {
        REQUIRE(ProjectileFormIDs.size() == AmmoFormIDs.size());
    }

    SECTION("Pool size of 200 can use all forms") {
        // This ensures the pool can use all 200 forms without wraparound
        constexpr size_t expectedPoolSize = 200;
        REQUIRE(ProjectileFormIDs.size() == expectedPoolSize);
    }
}
