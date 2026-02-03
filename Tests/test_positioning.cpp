#include <catch2/catch_all.hpp>

#include "projectile/ControlledProjectile.h"
#include "projectile/Anchor.h"
#include "projectile/GameProjectile.h"

using namespace Projectile;

// ============================================================================
// Transform Computation Tests (using Anchor class)
// ============================================================================

namespace {
    // Helper: Compute world transform from local + anchor (mimics Anchor::ToWorld logic)
    // Note: Rotation is now matrix-based, composed via matrix multiplication
    ProjectileTransform ComputeWorldTransform(
        const ProjectileTransform& localTransform,
        const RE::NiTransform& anchorWorld,
        const RE::NiPoint3& offset,
        bool useRotation,
        bool useScale)
    {
        ProjectileTransform worldTransform = localTransform;

        // Transform position: worldPos = anchorPos + (anchorRotation * localPos) + offset
        RE::NiPoint3 rotatedLocal = Anchor::RotatePoint(anchorWorld.rotate, localTransform.position);
        worldTransform.position = anchorWorld.translate + rotatedLocal + offset;

        // Optionally inherit rotation (matrix composition)
        if (useRotation) {
            worldTransform.rotation = MultiplyMatrices(anchorWorld.rotate, localTransform.rotation);
        }

        // Optionally inherit scale
        if (useScale) {
            worldTransform.scale *= anchorWorld.scale;
        }

        return worldTransform;
    }

    // Helper to check if a matrix is identity
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

TEST_CASE("World Transform Computation", "[positioning][transform]") {
    SECTION("Identity anchor transform - position passes through") {
        ProjectileTransform local;
        local.position = RE::NiPoint3(10.0f, 20.0f, 30.0f);
        // local.rotation is identity matrix by default
        local.scale = 1.0f;

        RE::NiTransform anchor;  // Identity by default
        RE::NiPoint3 offset(0.0f, 0.0f, 0.0f);

        auto world = ComputeWorldTransform(local, anchor, offset, false, false);

        // With identity anchor and no offset, position should be unchanged
        REQUIRE(world.position.x == Catch::Approx(10.0f));
        REQUIRE(world.position.y == Catch::Approx(20.0f));
        REQUIRE(world.position.z == Catch::Approx(30.0f));
    }

    SECTION("Anchor translation offsets position") {
        ProjectileTransform local;
        local.position = RE::NiPoint3(0.0f, 0.0f, 0.0f);  // At local origin

        RE::NiTransform anchor;
        anchor.translate = RE::NiPoint3(100.0f, 200.0f, 300.0f);  // Anchor at world position

        RE::NiPoint3 offset(0.0f, 0.0f, 0.0f);

        auto world = ComputeWorldTransform(local, anchor, offset, false, false);

        // Local origin should be at anchor's world position
        REQUIRE(world.position.x == Catch::Approx(100.0f));
        REQUIRE(world.position.y == Catch::Approx(200.0f));
        REQUIRE(world.position.z == Catch::Approx(300.0f));
    }

    SECTION("Local offset from anchor") {
        ProjectileTransform local;
        local.position = RE::NiPoint3(5.0f, 0.0f, 0.0f);  // 5 units in local X

        RE::NiTransform anchor;
        anchor.translate = RE::NiPoint3(100.0f, 100.0f, 100.0f);

        RE::NiPoint3 offset(0.0f, 0.0f, 0.0f);

        auto world = ComputeWorldTransform(local, anchor, offset, false, false);

        // Should be anchor + local offset (with identity rotation)
        REQUIRE(world.position.x == Catch::Approx(105.0f));
        REQUIRE(world.position.y == Catch::Approx(100.0f));
        REQUIRE(world.position.z == Catch::Approx(100.0f));
    }

    SECTION("Additional offset is applied") {
        ProjectileTransform local;
        local.position = RE::NiPoint3(0.0f, 0.0f, 0.0f);

        RE::NiTransform anchor;
        anchor.translate = RE::NiPoint3(50.0f, 50.0f, 50.0f);

        RE::NiPoint3 offset(10.0f, 20.0f, 30.0f);

        auto world = ComputeWorldTransform(local, anchor, offset, false, false);

        REQUIRE(world.position.x == Catch::Approx(60.0f));
        REQUIRE(world.position.y == Catch::Approx(70.0f));
        REQUIRE(world.position.z == Catch::Approx(80.0f));
    }

    SECTION("Scale inheritance") {
        ProjectileTransform local;
        local.scale = 0.5f;

        RE::NiTransform anchor;
        anchor.scale = 2.0f;

        RE::NiPoint3 offset(0.0f, 0.0f, 0.0f);

        auto withoutInherit = ComputeWorldTransform(local, anchor, offset, false, false);
        auto withInherit = ComputeWorldTransform(local, anchor, offset, false, true);

        REQUIRE(withoutInherit.scale == Catch::Approx(0.5f));
        REQUIRE(withInherit.scale == Catch::Approx(1.0f));  // 0.5 * 2.0
    }

    SECTION("Rotation inheritance disabled by default") {
        ProjectileTransform local;
        // local.rotation is identity matrix by default

        RE::NiTransform anchor;
        // Set a 90 degree rotation around Z axis
        anchor.rotate.entry[0][0] = 0.0f;
        anchor.rotate.entry[0][1] = 1.0f;
        anchor.rotate.entry[1][0] = -1.0f;
        anchor.rotate.entry[1][1] = 0.0f;

        RE::NiPoint3 offset(0.0f, 0.0f, 0.0f);

        auto withoutInherit = ComputeWorldTransform(local, anchor, offset, false, false);

        // Without rotation inheritance, local rotation should be unchanged (identity)
        REQUIRE(IsIdentityMatrix(withoutInherit.rotation));
    }
}

// ============================================================================
// Anchor::RotatePoint Tests
// ============================================================================

TEST_CASE("Anchor::RotatePoint", "[positioning][transform]") {
    SECTION("Identity rotation leaves point unchanged") {
        RE::NiMatrix3 identity;  // Default is identity
        RE::NiPoint3 point(1.0f, 2.0f, 3.0f);

        auto result = Anchor::RotatePoint(identity, point);

        REQUIRE(result.x == Catch::Approx(1.0f));
        REQUIRE(result.y == Catch::Approx(2.0f));
        REQUIRE(result.z == Catch::Approx(3.0f));
    }

    SECTION("90 degree rotation around Z axis") {
        RE::NiMatrix3 rotZ90;
        // 90 degrees around Z: [0 -1 0; 1 0 0; 0 0 1]
        rotZ90.entry[0][0] = 0.0f;  rotZ90.entry[0][1] = -1.0f; rotZ90.entry[0][2] = 0.0f;
        rotZ90.entry[1][0] = 1.0f;  rotZ90.entry[1][1] = 0.0f;  rotZ90.entry[1][2] = 0.0f;
        rotZ90.entry[2][0] = 0.0f;  rotZ90.entry[2][1] = 0.0f;  rotZ90.entry[2][2] = 1.0f;

        RE::NiPoint3 point(1.0f, 0.0f, 0.0f);  // Point on X axis

        auto result = Anchor::RotatePoint(rotZ90, point);

        // After 90 degree Z rotation, (1,0,0) becomes (0,1,0)
        REQUIRE(result.x == Catch::Approx(0.0f).margin(0.001f));
        REQUIRE(result.y == Catch::Approx(1.0f).margin(0.001f));
        REQUIRE(result.z == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("Zero point remains zero") {
        RE::NiMatrix3 anyRotation;
        anyRotation.entry[0][0] = 0.5f; anyRotation.entry[0][1] = 0.5f;

        RE::NiPoint3 zero(0.0f, 0.0f, 0.0f);

        auto result = Anchor::RotatePoint(anyRotation, zero);

        REQUIRE(result.x == Catch::Approx(0.0f));
        REQUIRE(result.y == Catch::Approx(0.0f));
        REQUIRE(result.z == Catch::Approx(0.0f));
    }
}

// ============================================================================
// ObjectRefHandle Tests (for anchor by handle functionality)
// ============================================================================

TEST_CASE("ObjectRefHandle for Anchoring", "[positioning][handle]") {
    // Clean up any previous test state
    RE::TESObjectREFR::ClearHandles();

    SECTION("LookupByHandle returns nullptr for invalid handle") {
        RE::ObjectRefHandle invalidHandle;
        invalidHandle.value = 0;

        auto result = RE::TESObjectREFR::LookupByHandle(invalidHandle);
        REQUIRE(result.get() == nullptr);
    }

    SECTION("LookupByHandle returns registered reference") {
        RE::TESObjectREFR ref;
        RE::ObjectRefHandle handle;
        handle.value = 12345;

        RE::TESObjectREFR::RegisterHandle(handle, &ref);

        auto result = RE::TESObjectREFR::LookupByHandle(handle);
        REQUIRE(result.get() == &ref);

        RE::TESObjectREFR::ClearHandles();
    }

    SECTION("LookupByHandle returns nullptr for unregistered handle") {
        RE::ObjectRefHandle handle;
        handle.value = 99999;

        auto result = RE::TESObjectREFR::LookupByHandle(handle);
        REQUIRE(result.get() == nullptr);
    }

    SECTION("TESObjectREFR Get3D returns node") {
        RE::TESObjectREFR ref;
        RE::NiNode node;

        ref.Set3D(&node);
        REQUIRE(ref.Get3D() == &node);
    }
}
