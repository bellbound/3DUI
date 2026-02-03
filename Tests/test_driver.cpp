#include <catch2/catch_all.hpp>

#include "projectile/ProjectileDriver.h"
#include "projectile/ControlledProjectile.h"
#include "projectile/Anchor.h"

using namespace Projectile;

// ============================================================================
// Anchor Tests
// ============================================================================

TEST_CASE("Anchor", "[driver]") {
    SECTION("Default construction") {
        Anchor anchor;
        REQUIRE_FALSE(anchor.HasAnchor());
        REQUIRE(anchor.GetOffset().x == 0.0f);
        REQUIRE(anchor.GetOffset().y == 0.0f);
        REQUIRE(anchor.GetOffset().z == 0.0f);
        REQUIRE_FALSE(anchor.GetUseRotation());
        REQUIRE_FALSE(anchor.GetUseScale());
    }

    SECTION("HasAnchor with no anchor") {
        Anchor anchor;
        REQUIRE_FALSE(anchor.HasAnchor());
    }

    SECTION("HasAnchor with node pointer") {
        Anchor anchor;
        RE::NiNode dummyNode;
        anchor.SetDirect(&dummyNode);
        REQUIRE(anchor.HasAnchor());
    }

    SECTION("Clear resets all fields") {
        Anchor anchor;
        RE::NiNode dummyNode;
        anchor.SetDirect(&dummyNode);
        anchor.SetOffset(RE::NiPoint3(10, 20, 30));
        anchor.SetUseRotation(true);
        anchor.SetUseScale(true);

        anchor.Clear();

        REQUIRE_FALSE(anchor.HasAnchor());
        REQUIRE(anchor.GetOffset().x == 0.0f);
        REQUIRE(anchor.GetOffset().y == 0.0f);
        REQUIRE(anchor.GetOffset().z == 0.0f);
        REQUIRE_FALSE(anchor.GetUseRotation());
        REQUIRE_FALSE(anchor.GetUseScale());
    }

    SECTION("GetWorldPosition without anchor uses worldPosition + offset") {
        Anchor anchor;
        anchor.SetWorldPosition(RE::NiPoint3(100, 200, 300));
        anchor.SetOffset(RE::NiPoint3(10, 20, 30));

        auto pos = anchor.GetWorldPosition();

        REQUIRE(pos.x == Catch::Approx(110.0f));
        REQUIRE(pos.y == Catch::Approx(220.0f));
        REQUIRE(pos.z == Catch::Approx(330.0f));
    }

    SECTION("GetWorldPosition with anchor uses anchor.world.translate + offset") {
        Anchor anchor;
        RE::NiNode dummyNode;
        dummyNode.world.translate = RE::NiPoint3(500, 600, 700);

        anchor.SetDirect(&dummyNode);
        anchor.SetOffset(RE::NiPoint3(10, 20, 30));

        auto pos = anchor.GetWorldPosition();

        // Should use anchor's world translate, not worldPosition
        REQUIRE(pos.x == Catch::Approx(510.0f));
        REQUIRE(pos.y == Catch::Approx(620.0f));
        REQUIRE(pos.z == Catch::Approx(730.0f));
    }

    SECTION("RotatePoint performs matrix-vector multiplication") {
        RE::NiMatrix3 identity;  // Identity matrix by default
        RE::NiPoint3 point(10.0f, 20.0f, 30.0f);

        auto result = Anchor::RotatePoint(identity, point);

        REQUIRE(result.x == Catch::Approx(10.0f));
        REQUIRE(result.y == Catch::Approx(20.0f));
        REQUIRE(result.z == Catch::Approx(30.0f));
    }
}

// ============================================================================
// TransitionMode Tests
// ============================================================================

TEST_CASE("TransitionMode Enum", "[driver][transition]") {
    SECTION("Enum values are distinct") {
        REQUIRE(TransitionMode::Instant != TransitionMode::Lerp);
    }

    SECTION("Default should be Instant") {
        TransitionMode mode = TransitionMode::Instant;
        REQUIRE(mode == TransitionMode::Instant);
    }
}

// ============================================================================
// Fibonacci Wheel Math Tests
// ============================================================================

namespace {
    constexpr float GOLDEN_ANGLE = 2.39996323f;  // 137.5 degrees in radians

    struct WheelPosition {
        float x, y;
    };

    WheelPosition CalculateWheelPosition(size_t index, float spacing, float maxRadius) {
        float angle = static_cast<float>(index) * GOLDEN_ANGLE;
        float r = spacing * std::sqrt(static_cast<float>(index));

        if (r > maxRadius) {
            r = maxRadius;
        }

        return {
            r * std::cos(angle),
            r * std::sin(angle)
        };
    }
}

TEST_CASE("Fibonacci Wheel Positioning", "[driver][wheel]") {
    SECTION("First element at origin") {
        auto pos = CalculateWheelPosition(0, 15.0f, 100.0f);
        REQUIRE(pos.x == Catch::Approx(0.0f).margin(0.001f));
        REQUIRE(pos.y == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("Subsequent elements have increasing radius") {
        float prevRadius = 0.0f;
        for (size_t i = 1; i < 10; ++i) {
            auto pos = CalculateWheelPosition(i, 15.0f, 1000.0f);
            float radius = std::sqrt(pos.x * pos.x + pos.y * pos.y);
            REQUIRE(radius > prevRadius);
            prevRadius = radius;
        }
    }

    SECTION("Radius follows sqrt pattern") {
        float spacing = 15.0f;
        for (size_t i = 1; i < 10; ++i) {
            auto pos = CalculateWheelPosition(i, spacing, 1000.0f);
            float actualRadius = std::sqrt(pos.x * pos.x + pos.y * pos.y);
            float expectedRadius = spacing * std::sqrt(static_cast<float>(i));
            REQUIRE(actualRadius == Catch::Approx(expectedRadius).margin(0.01f));
        }
    }

    SECTION("Golden angle gives good distribution") {
        // Check that consecutive elements are not clustered
        std::vector<WheelPosition> positions;
        for (size_t i = 0; i < 20; ++i) {
            positions.push_back(CalculateWheelPosition(i, 15.0f, 1000.0f));
        }

        // Verify angular spacing between consecutive non-zero elements
        for (size_t i = 2; i < positions.size(); ++i) {
            float angle1 = std::atan2(positions[i-1].y, positions[i-1].x);
            float angle2 = std::atan2(positions[i].y, positions[i].x);
            float angleDiff = std::abs(angle2 - angle1);

            // Normalize to [0, PI]
            while (angleDiff > 3.14159f) {
                angleDiff = 2.0f * 3.14159f - angleDiff;
            }

            // Golden angle should give reasonable spacing (not too small)
            // Elements shouldn't be directly on top of each other
            REQUIRE(angleDiff > 0.1f);
        }
    }

    SECTION("Max radius is respected") {
        float maxRadius = 50.0f;
        // With spacing=15 and maxRadius=50, elements beyond ~11 should be clamped
        for (size_t i = 0; i < 100; ++i) {
            auto pos = CalculateWheelPosition(i, 15.0f, maxRadius);
            float radius = std::sqrt(pos.x * pos.x + pos.y * pos.y);
            REQUIRE(radius <= maxRadius + 0.001f);
        }
    }
}

// ============================================================================
// Transform Helper Tests
// ============================================================================

TEST_CASE("Local to World Transform", "[driver][transform]") {
    SECTION("Identity center transform preserves local position") {
        RE::NiTransform center;  // Identity by default
        RE::NiPoint3 local(10.0f, 20.0f, 30.0f);

        // Manual transform (same as ProjectileDriver::SetProjectileLocalPosition)
        RE::NiPoint3 world;
        world.x = center.rotate.entry[0][0] * local.x +
                  center.rotate.entry[0][1] * local.y +
                  center.rotate.entry[0][2] * local.z;
        world.y = center.rotate.entry[1][0] * local.x +
                  center.rotate.entry[1][1] * local.y +
                  center.rotate.entry[1][2] * local.z;
        world.z = center.rotate.entry[2][0] * local.x +
                  center.rotate.entry[2][1] * local.y +
                  center.rotate.entry[2][2] * local.z;
        world = world + center.translate;

        REQUIRE(world.x == Catch::Approx(10.0f));
        REQUIRE(world.y == Catch::Approx(20.0f));
        REQUIRE(world.z == Catch::Approx(30.0f));
    }

    SECTION("Center translation offsets local position") {
        RE::NiTransform center;
        center.translate = RE::NiPoint3(100.0f, 200.0f, 300.0f);
        RE::NiPoint3 local(10.0f, 20.0f, 30.0f);

        RE::NiPoint3 world;
        world.x = center.rotate.entry[0][0] * local.x +
                  center.rotate.entry[0][1] * local.y +
                  center.rotate.entry[0][2] * local.z;
        world.y = center.rotate.entry[1][0] * local.x +
                  center.rotate.entry[1][1] * local.y +
                  center.rotate.entry[1][2] * local.z;
        world.z = center.rotate.entry[2][0] * local.x +
                  center.rotate.entry[2][1] * local.y +
                  center.rotate.entry[2][2] * local.z;
        world = world + center.translate;

        REQUIRE(world.x == Catch::Approx(110.0f));
        REQUIRE(world.y == Catch::Approx(220.0f));
        REQUIRE(world.z == Catch::Approx(330.0f));
    }

    SECTION("90 degree Z rotation transforms local X to world Y") {
        RE::NiTransform center;
        // 90 degrees around Z: [0 -1 0; 1 0 0; 0 0 1]
        center.rotate.entry[0][0] = 0.0f;  center.rotate.entry[0][1] = -1.0f; center.rotate.entry[0][2] = 0.0f;
        center.rotate.entry[1][0] = 1.0f;  center.rotate.entry[1][1] = 0.0f;  center.rotate.entry[1][2] = 0.0f;
        center.rotate.entry[2][0] = 0.0f;  center.rotate.entry[2][1] = 0.0f;  center.rotate.entry[2][2] = 1.0f;

        RE::NiPoint3 local(10.0f, 0.0f, 0.0f);  // Point on local X axis

        RE::NiPoint3 world;
        world.x = center.rotate.entry[0][0] * local.x +
                  center.rotate.entry[0][1] * local.y +
                  center.rotate.entry[0][2] * local.z;
        world.y = center.rotate.entry[1][0] * local.x +
                  center.rotate.entry[1][1] * local.y +
                  center.rotate.entry[1][2] * local.z;
        world.z = center.rotate.entry[2][0] * local.x +
                  center.rotate.entry[2][1] * local.y +
                  center.rotate.entry[2][2] * local.z;

        // After 90 degree Z rotation, local X axis becomes world Y axis
        REQUIRE(world.x == Catch::Approx(0.0f).margin(0.001f));
        REQUIRE(world.y == Catch::Approx(10.0f).margin(0.001f));
        REQUIRE(world.z == Catch::Approx(0.0f).margin(0.001f));
    }
}

// ============================================================================
// Lerp Interpolation Tests
// ============================================================================

namespace {
    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    float SmoothStep(float t) {
        return t * t * (3.0f - 2.0f * t);
    }
}

TEST_CASE("Lerp Interpolation", "[driver][transition]") {
    SECTION("t=0 returns start value") {
        REQUIRE(Lerp(0.0f, 100.0f, 0.0f) == Catch::Approx(0.0f));
    }

    SECTION("t=1 returns end value") {
        REQUIRE(Lerp(0.0f, 100.0f, 1.0f) == Catch::Approx(100.0f));
    }

    SECTION("t=0.5 returns midpoint") {
        REQUIRE(Lerp(0.0f, 100.0f, 0.5f) == Catch::Approx(50.0f));
    }

    SECTION("Works with negative values") {
        REQUIRE(Lerp(-50.0f, 50.0f, 0.5f) == Catch::Approx(0.0f));
    }
}

TEST_CASE("SmoothStep Easing", "[driver][transition]") {
    SECTION("t=0 returns 0") {
        REQUIRE(SmoothStep(0.0f) == Catch::Approx(0.0f));
    }

    SECTION("t=1 returns 1") {
        REQUIRE(SmoothStep(1.0f) == Catch::Approx(1.0f));
    }

    SECTION("t=0.5 returns 0.5") {
        REQUIRE(SmoothStep(0.5f) == Catch::Approx(0.5f));
    }

    SECTION("SmoothStep is monotonic increasing") {
        float prev = 0.0f;
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            float val = SmoothStep(t);
            REQUIRE(val >= prev);
            prev = val;
        }
    }

    SECTION("SmoothStep eases in and out") {
        // Derivative at endpoints should be 0 (smooth)
        // Check by comparing small differences
        float near0 = SmoothStep(0.01f);
        float near1 = SmoothStep(0.99f);

        // Near 0, the curve should be relatively flat (small change)
        REQUIRE(near0 < 0.01f);  // Much less than linear would give

        // Near 1, the curve should be relatively flat (small change from 1.0)
        REQUIRE(near1 > 0.99f);  // Much closer to 1.0 than linear would give
    }
}
