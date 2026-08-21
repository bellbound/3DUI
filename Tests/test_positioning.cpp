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

// ============================================================================
// Curve Warp Tests
// ============================================================================
// The curve that bends a menu's flat plane toward the player. Pure maths on
// local coordinates, so it can be checked exactly.

TEST_CASE("CurveWarp bends a flat plane toward the player", "[curve]") {

    SECTION("A radius of zero is flat") {
        CurveWarp warp;  // radius defaults to 0
        REQUIRE_FALSE(warp.Active());

        const RE::NiPoint3 local(40.0f, 0.0f, 12.0f);
        const RE::NiPoint3 warped = warp.WarpPosition(local);

        REQUIRE(warped.x == Catch::Approx(local.x));
        REQUIRE(warped.y == Catch::Approx(local.y));
        REQUIRE(warped.z == Catch::Approx(local.z));
    }

    SECTION("The centre of the plane does not move") {
        CurveWarp warp;
        warp.radius = 90.0f;
        REQUIRE(warp.Active());

        const RE::NiPoint3 warped = warp.WarpPosition(RE::NiPoint3(0.0f, 0.0f, 0.0f));

        REQUIRE(warped.x == Catch::Approx(0.0f).margin(1e-5f));
        REQUIRE(warped.y == Catch::Approx(0.0f).margin(1e-5f));
        REQUIRE(warped.z == Catch::Approx(0.0f).margin(1e-5f));
    }

    SECTION("An edge comes forward and inward - the whole point of the curve") {
        CurveWarp warp;
        warp.radius = 90.0f;

        const RE::NiPoint3 local(45.0f, 0.0f, 0.0f);
        const RE::NiPoint3 warped = warp.WarpPosition(local);

        // Forward is +Y, toward the player.
        REQUIRE(warped.y > 0.0f);
        // Inward: |x| shrinks, because arc length 45 subtends a chord shorter than it.
        REQUIRE(warped.x < local.x);
        REQUIRE(warped.x > 0.0f);
        // And closer to the origin overall, which is what shortens the reach.
        const float flatDistance = std::sqrt(local.x * local.x + local.y * local.y);
        const float curvedDistance = std::sqrt(warped.x * warped.x + warped.y * warped.y);
        REQUIRE(curvedDistance < flatDistance);
    }

    SECTION("Both edges bend the same way, mirrored") {
        CurveWarp warp;
        warp.radius = 90.0f;

        const RE::NiPoint3 right = warp.WarpPosition(RE::NiPoint3(45.0f, 0.0f, 0.0f));
        const RE::NiPoint3 left = warp.WarpPosition(RE::NiPoint3(-45.0f, 0.0f, 0.0f));

        REQUIRE(left.x == Catch::Approx(-right.x));
        REQUIRE(left.y == Catch::Approx(right.y));
    }

    SECTION("Height is untouched by a horizontal-only curve") {
        CurveWarp warp;
        warp.radius = 90.0f;
        warp.horizontal = true;
        warp.vertical = false;

        const RE::NiPoint3 warped = warp.WarpPosition(RE::NiPoint3(30.0f, 0.0f, 25.0f));
        REQUIRE(warped.z == Catch::Approx(25.0f));
    }

    SECTION("A vertical curve bends the other axis instead") {
        CurveWarp warp;
        warp.radius = 90.0f;
        warp.horizontal = false;
        warp.vertical = true;

        const RE::NiPoint3 warped = warp.WarpPosition(RE::NiPoint3(30.0f, 0.0f, 45.0f));

        REQUIRE(warped.x == Catch::Approx(30.0f));   // untouched
        REQUIRE(warped.z < 45.0f);                    // pulled in
        REQUIRE(warped.y > 0.0f);                     // and forward
    }

    SECTION("maxAngle stops the plane wrapping back round behind the player") {
        CurveWarp warp;
        warp.radius = 10.0f;   // tiny radius, so a modest x would wrap far
        warp.maxAngle = 1.2f;

        // Far past the clamp: 500 / 10 = 50 radians unclamped.
        const RE::NiPoint3 warped = warp.WarpPosition(RE::NiPoint3(500.0f, 0.0f, 0.0f));

        // Held at the clamp angle rather than spiralling.
        REQUIRE(warped.x == Catch::Approx(10.0f * std::sin(1.2f)));
        REQUIRE(warped.y == Catch::Approx(10.0f * (1.0f - std::cos(1.2f))));
        // Never behind the plane's own depth: y stays under the diameter.
        REQUIRE(warped.y < 2.0f * warp.radius);
    }

    SECTION("Spacing is preserved along the arc, so layouts need no retuning") {
        CurveWarp warp;
        warp.radius = 90.0f;

        // Two neighbours 10 apart at the centre, and two 10 apart out at the edge.
        const RE::NiPoint3 nearA = warp.WarpPosition(RE::NiPoint3(0.0f, 0.0f, 0.0f));
        const RE::NiPoint3 nearB = warp.WarpPosition(RE::NiPoint3(10.0f, 0.0f, 0.0f));
        const RE::NiPoint3 farA = warp.WarpPosition(RE::NiPoint3(40.0f, 0.0f, 0.0f));
        const RE::NiPoint3 farB = warp.WarpPosition(RE::NiPoint3(50.0f, 0.0f, 0.0f));

        auto chord = [](const RE::NiPoint3& a, const RE::NiPoint3& b) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        };

        // Chords are a touch shorter than the 10 units of arc they span, and the two
        // pairs agree with each other - no bunching at the edges.
        REQUIRE(chord(nearA, nearB) == Catch::Approx(chord(farA, farB)).epsilon(0.01f));
        REQUIRE(chord(nearA, nearB) == Catch::Approx(10.0f).epsilon(0.01f));
    }

    SECTION("Tilting off leaves the rotation alone") {
        CurveWarp warp;
        warp.radius = 90.0f;
        warp.tiltElements = false;

        const RE::NiMatrix3 rot = warp.WarpRotation(RE::NiPoint3(45.0f, 0.0f, 0.0f));
        const RE::NiMatrix3 identity = IdentityMatrix();

        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                REQUIRE(rot.entry[i][j] == Catch::Approx(identity.entry[i][j]).margin(1e-5f));
            }
        }
    }

    SECTION("Tilting turns an edge element by the angle it was carried through") {
        CurveWarp warp;
        warp.radius = 90.0f;
        warp.tiltElements = true;

        const float expectedYaw = 45.0f / 90.0f;   // arc length / radius
        const RE::NiMatrix3 rot = warp.WarpRotation(RE::NiPoint3(45.0f, 0.0f, 0.0f));
        const RE::NiPoint3 euler = MatrixToEuler(rot);

        REQUIRE(euler.z == Catch::Approx(expectedYaw).margin(1e-4f));  // yaw
        REQUIRE(euler.x == Catch::Approx(0.0f).margin(1e-4f));         // no pitch
    }

    SECTION("The centre element is not tilted at all") {
        CurveWarp warp;
        warp.radius = 90.0f;

        const RE::NiPoint3 euler = MatrixToEuler(warp.WarpRotation(RE::NiPoint3(0.0f, 0.0f, 0.0f)));
        REQUIRE(euler.x == Catch::Approx(0.0f).margin(1e-5f));
        REQUIRE(euler.z == Catch::Approx(0.0f).margin(1e-5f));
    }
}
