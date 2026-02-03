#include <catch2/catch_all.hpp>

// TestStubs.h is included via the projectile headers when TEST_ENVIRONMENT is defined
// This allows us to test the REAL implementation with stubbed RE/SKSE types

#include "projectile/GameProjectile.h"
// Note: ControlledProjectile.h excluded - depends on ProjectileSubsystem
#include "util/UUID.h"

using namespace Projectile;
using namespace Util;

// ============================================================================
// ProjectileTransform Tests
// ============================================================================

// Helper to check if a matrix is identity
namespace {
    bool IsIdentityMatrix(const RE::NiMatrix3& mat) {
        const float epsilon = 0.001f;
        return std::abs(mat.entry[0][0] - 1.0f) < epsilon &&
               std::abs(mat.entry[1][1] - 1.0f) < epsilon &&
               std::abs(mat.entry[2][2] - 1.0f) < epsilon &&
               std::abs(mat.entry[0][1]) < epsilon &&
               std::abs(mat.entry[0][2]) < epsilon &&
               std::abs(mat.entry[1][0]) < epsilon &&
               std::abs(mat.entry[1][2]) < epsilon &&
               std::abs(mat.entry[2][0]) < epsilon &&
               std::abs(mat.entry[2][1]) < epsilon;
    }
}

TEST_CASE("ProjectileTransform", "[projectile]") {
    SECTION("Default construction") {
        ProjectileTransform t;
        REQUIRE(t.position.x == 0.0f);
        REQUIRE(t.position.y == 0.0f);
        REQUIRE(t.position.z == 0.0f);
        // Rotation is now a matrix, should be identity by default
        REQUIRE(IsIdentityMatrix(t.rotation));
        REQUIRE(t.scale == 1.0f);
    }

    SECTION("Position assignment") {
        ProjectileTransform t;
        t.position = RE::NiPoint3(100.0f, 200.0f, 300.0f);
        REQUIRE(t.position.x == 100.0f);
        REQUIRE(t.position.y == 200.0f);
        REQUIRE(t.position.z == 300.0f);
    }

    SECTION("Scale assignment") {
        ProjectileTransform t;
        t.scale = 0.5f;
        REQUIRE(t.scale == 0.5f);
    }
}

// ============================================================================
// GameProjectile Tests (Real Implementation)
// ============================================================================

TEST_CASE("GameProjectile", "[projectile][game]") {
    SECTION("Default state") {
        GameProjectile gp;
        REQUIRE_FALSE(gp.IsBound());
        REQUIRE(gp.GetProjectile() == nullptr);
        REQUIRE(gp.IsVisible());
        REQUIRE_FALSE(gp.IsMarkedForDeletion());
    }

    SECTION("Transform manipulation") {
        GameProjectile gp;

        ProjectileTransform t;
        t.position = RE::NiPoint3(100, 200, 300);
        // Rotation is now a matrix - set a non-identity rotation for testing
        // This is a 90-degree rotation around Z axis
        t.rotation.entry[0][0] = 0.0f;  t.rotation.entry[0][1] = -1.0f; t.rotation.entry[0][2] = 0.0f;
        t.rotation.entry[1][0] = 1.0f;  t.rotation.entry[1][1] = 0.0f;  t.rotation.entry[1][2] = 0.0f;
        t.rotation.entry[2][0] = 0.0f;  t.rotation.entry[2][1] = 0.0f;  t.rotation.entry[2][2] = 1.0f;
        t.scale = 0.5f;

        gp.SetTransform(t);

        auto result = gp.GetTargetTransform();
        REQUIRE(result.position.x == 100.0f);
        REQUIRE(result.position.y == 200.0f);
        REQUIRE(result.position.z == 300.0f);
        REQUIRE(result.scale == 0.5f);
        // Verify rotation matrix was preserved
        REQUIRE(result.rotation.entry[0][0] == Catch::Approx(0.0f));
        REQUIRE(result.rotation.entry[1][0] == Catch::Approx(1.0f));
    }

    SECTION("Visibility control") {
        GameProjectile gp;

        REQUIRE(gp.IsVisible());

        gp.SetVisible(false);
        REQUIRE_FALSE(gp.IsVisible());

        gp.SetVisible(true);
        REQUIRE(gp.IsVisible());
    }

    SECTION("Mark for deletion") {
        GameProjectile gp;

        REQUIRE_FALSE(gp.IsMarkedForDeletion());

        gp.MarkForDeletion();
        REQUIRE(gp.IsMarkedForDeletion());
    }

    SECTION("Model path") {
        GameProjectile gp;

        gp.SetModelPath("Magic\\TestOrb.nif");
        REQUIRE(gp.GetModelPath() == "Magic\\TestOrb.nif");
    }

    SECTION("Assignment time") {
        GameProjectile gp;

        gp.SetAssignmentTime(12345);
        REQUIRE(gp.GetAssignmentTime() == 12345);
    }

    SECTION("Bind and unbind (with nullptr)") {
        GameProjectile gp;

        // Binding nullptr should still work
        gp.BindToProjectile(nullptr);
        REQUIRE_FALSE(gp.IsBound());  // nullptr means not bound

        gp.Unbind();
        REQUIRE_FALSE(gp.IsBound());
    }
}

// ============================================================================
// NiPoint3 Operations Tests
// ============================================================================

TEST_CASE("NiPoint3 Operations", "[projectile][math]") {
    SECTION("Addition") {
        RE::NiPoint3 a(1.0f, 2.0f, 3.0f);
        RE::NiPoint3 b(4.0f, 5.0f, 6.0f);
        auto c = a + b;

        REQUIRE(c.x == 5.0f);
        REQUIRE(c.y == 7.0f);
        REQUIRE(c.z == 9.0f);
    }

    SECTION("Subtraction") {
        RE::NiPoint3 a(5.0f, 7.0f, 9.0f);
        RE::NiPoint3 b(1.0f, 2.0f, 3.0f);
        auto c = a - b;

        REQUIRE(c.x == 4.0f);
        REQUIRE(c.y == 5.0f);
        REQUIRE(c.z == 6.0f);
    }

    SECTION("Scalar multiplication") {
        RE::NiPoint3 a(2.0f, 3.0f, 4.0f);
        auto b = a * 2.0f;

        REQUIRE(b.x == 4.0f);
        REQUIRE(b.y == 6.0f);
        REQUIRE(b.z == 8.0f);
    }

    SECTION("Length calculation") {
        RE::NiPoint3 a(3.0f, 4.0f, 0.0f);
        REQUIRE(a.Length() == Catch::Approx(5.0f));

        RE::NiPoint3 b(1.0f, 2.0f, 2.0f);
        REQUIRE(b.Length() == Catch::Approx(3.0f));
    }

    SECTION("Equality") {
        RE::NiPoint3 a(1.0f, 2.0f, 3.0f);
        RE::NiPoint3 b(1.0f, 2.0f, 3.0f);
        RE::NiPoint3 c(1.0f, 2.0f, 4.0f);

        REQUIRE(a == b);
        REQUIRE_FALSE(a == c);
    }
}

// ============================================================================
// RE Stub Types Tests
// ============================================================================

TEST_CASE("RE Stub Types", "[projectile][stubs]") {
    SECTION("TESDataHandler singleton") {
        auto* handler1 = RE::TESDataHandler::GetSingleton();
        auto* handler2 = RE::TESDataHandler::GetSingleton();
        REQUIRE(handler1 == handler2);
    }

    SECTION("TESDataHandler form lookup") {
        auto* handler = RE::TESDataHandler::GetSingleton();
        handler->ClearForms();

        RE::BGSProjectile proj;
        proj.formID = 0x800;

        handler->RegisterForm("TestPlugin.esp", 0x800, &proj);

        auto* found = handler->LookupForm(0x800, "TestPlugin.esp");
        REQUIRE(found == &proj);

        auto* notFound = handler->LookupForm(0x999, "TestPlugin.esp");
        REQUIRE(notFound == nullptr);

        handler->ClearForms();
    }

    SECTION("PlayerCharacter singleton") {
        auto* player1 = RE::PlayerCharacter::GetSingleton();
        auto* player2 = RE::PlayerCharacter::GetSingleton();
        REQUIRE(player1 == player2);
    }

    SECTION("Projectile stub has expected members") {
        RE::Projectile proj;
        RE::NiNode node;

        proj.Set3D(&node);
        REQUIRE(proj.Get3D() == &node);

        proj.GetPosition() = RE::NiPoint3(1, 2, 3);
        REQUIRE(proj.GetPosition().x == 1.0f);

        proj.GetVelocity() = RE::NiPoint3(4, 5, 6);
        REQUIRE(proj.GetVelocity().y == 5.0f);
    }

    SECTION("BGSTextureSet stub") {
        RE::BGSTextureSet texSet;
        texSet.texturePaths[0].str = "textures/test.dds";
        REQUIRE(texSet.texturePaths[0].str == "textures/test.dds");
    }
}

// ============================================================================
// GameProjectile Texture Tests
// ============================================================================

TEST_CASE("GameProjectile Texture Support", "[projectile][texture]") {
    GameProjectile gp;

    SECTION("Default texture state") {
        REQUIRE(gp.GetTexturePath().empty());
        REQUIRE(gp.GetBorderColor().empty());
        REQUIRE_FALSE(gp.NeedsTextureSet());
    }

    SECTION("Setting texture path sets needs texture flag") {
        gp.SetTexturePath("Interface/MyMod/icon.dds");

        REQUIRE(gp.GetTexturePath() == "Interface/MyMod/icon.dds");
        REQUIRE(gp.NeedsTextureSet());
    }

    SECTION("Empty texture path does not set flag") {
        gp.SetTexturePath("");

        REQUIRE(gp.GetTexturePath().empty());
        REQUIRE_FALSE(gp.NeedsTextureSet());
    }

    SECTION("Clear texture set flag") {
        gp.SetTexturePath("Interface/test.dds");
        REQUIRE(gp.NeedsTextureSet());

        gp.ClearTextureSetFlag();
        REQUIRE_FALSE(gp.NeedsTextureSet());
        REQUIRE(gp.GetTexturePath() == "Interface/test.dds");  // Path still preserved
    }

    SECTION("Border color") {
        gp.SetBorderColor("ff0000");
        REQUIRE(gp.GetBorderColor() == "ff0000");

        gp.SetBorderColor("00ff00");
        REQUIRE(gp.GetBorderColor() == "00ff00");
    }

    SECTION("Combined texture and border") {
        gp.SetTexturePath("Interface/icons/sword.dds");
        gp.SetBorderColor("0000ff");

        REQUIRE(gp.GetTexturePath() == "Interface/icons/sword.dds");
        REQUIRE(gp.GetBorderColor() == "0000ff");
        REQUIRE(gp.NeedsTextureSet());
    }
}

TEST_CASE("GameProjectile Move Semantics with Texture", "[projectile][texture]") {
    SECTION("Move constructor preserves texture state") {
        GameProjectile gp1;
        gp1.SetTexturePath("Interface/test.dds");
        gp1.SetBorderColor("ff00ff");
        gp1.SetModelPath("Magic\\BasicPicture.nif");

        GameProjectile gp2(std::move(gp1));

        REQUIRE(gp2.GetTexturePath() == "Interface/test.dds");
        REQUIRE(gp2.GetBorderColor() == "ff00ff");
        REQUIRE(gp2.GetModelPath() == "Magic\\BasicPicture.nif");
        REQUIRE(gp2.NeedsTextureSet());

        // Original should be cleared
        REQUIRE_FALSE(gp1.NeedsTextureSet());
    }

    SECTION("Move assignment preserves texture state") {
        GameProjectile gp1;
        gp1.SetTexturePath("Interface/icon.dds");
        gp1.SetBorderColor("ffffff");

        GameProjectile gp2;
        gp2 = std::move(gp1);

        REQUIRE(gp2.GetTexturePath() == "Interface/icon.dds");
        REQUIRE(gp2.GetBorderColor() == "ffffff");
        REQUIRE(gp2.NeedsTextureSet());
    }
}
