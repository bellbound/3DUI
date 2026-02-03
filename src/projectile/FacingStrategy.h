#pragma once

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <cmath>

namespace Projectile {

// Strategy interface for computing facing rotation matrices
// Different strategies produce different rotation behaviors (full 3D, yaw-only, etc.)
class IFacingStrategy {
public:
    virtual ~IFacingStrategy() = default;

    // Compute a rotation matrix that orients a layout to face from centerPos toward anchorPos
    virtual RE::NiMatrix3 ComputeRotation(
        const RE::NiPoint3& centerPos,
        const RE::NiPoint3& anchorPos) const = 0;
};

// Full 3D facing - the layout tilts in all axes to face the anchor
// Suitable for flat layouts like wheel rings that should face the player directly
class FullFacingStrategy : public IFacingStrategy {
public:
    static FullFacingStrategy& Instance() {
        static FullFacingStrategy instance;
        return instance;
    }

    RE::NiMatrix3 ComputeRotation(
        const RE::NiPoint3& centerPos,
        const RE::NiPoint3& anchorPos) const override
    {
        RE::NiMatrix3 rotation;

        RE::NiPoint3 forward = anchorPos - centerPos;
        float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);

        if (length < 0.001f) {
            return rotation;  // Identity
        }

        forward.x /= length;
        forward.y /= length;
        forward.z /= length;

        RE::NiPoint3 worldUp(0, 0, 1);
        float dot = forward.x * worldUp.x + forward.y * worldUp.y + forward.z * worldUp.z;

        RE::NiPoint3 up;
        if (std::abs(dot) > 0.999f) {
            up = RE::NiPoint3(0, 1, 0);
        } else {
            up = worldUp;
        }

        // right = forward × up (order matters for handedness!)
        // For right-handed system with Y=forward, Z=up: X = Y × Z
        RE::NiPoint3 right;
        right.x = forward.y * up.z - forward.z * up.y;
        right.y = forward.z * up.x - forward.x * up.z;
        right.z = forward.x * up.y - forward.y * up.x;

        length = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        right.x /= length;
        right.y /= length;
        right.z /= length;

        // Recompute up = right × forward (for right-handed: Z = X × Y)
        up.x = right.y * forward.z - right.z * forward.y;
        up.y = right.z * forward.x - right.x * forward.z;
        up.z = right.x * forward.y - right.y * forward.x;

        // Skyrim coordinate convention: +X=right, +Y=forward, +Z=up
        // Map local Y to forward (toward target), local Z to up
        rotation.entry[0][0] = right.x;   rotation.entry[0][1] = forward.x;   rotation.entry[0][2] = up.x;
        rotation.entry[1][0] = right.y;   rotation.entry[1][1] = forward.y;   rotation.entry[1][2] = up.y;
        rotation.entry[2][0] = right.z;   rotation.entry[2][1] = forward.z;   rotation.entry[2][2] = up.z;

        return rotation;
    }

private:
    FullFacingStrategy() = default;
};

// Yaw-only facing - rotates around Z axis only, keeping the layout horizontal
// Suitable for circular arcs that should stay parallel to the ground
class YawOnlyFacingStrategy : public IFacingStrategy {
public:
    static YawOnlyFacingStrategy& Instance() {
        static YawOnlyFacingStrategy instance;
        return instance;
    }

    RE::NiMatrix3 ComputeRotation(
        const RE::NiPoint3& centerPos,
        const RE::NiPoint3& anchorPos) const override
    {
        RE::NiMatrix3 rotation;

        // Project forward direction onto horizontal plane (zero Z)
        // This ensures the layout only yaws but never pitches or rolls
        RE::NiPoint3 forward = anchorPos - centerPos;
        forward.z = 0.0f;

        float length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        if (length < 0.001f) {
            return rotation;  // Identity - can't determine facing
        }

        forward.x /= length;
        forward.y /= length;

        // Skyrim coordinate convention: +X=right, +Y=forward, +Z=up
        // For a horizontal layout:
        // - Local X maps to world "right" direction (perpendicular to forward in XY plane)
        // - Local Y maps to world "forward" direction (toward anchor in XY plane)
        // - Local Z maps to world Z (up, unchanged)
        //
        // right = (-forward.y, forward.x, 0) - perpendicular in XY plane
        RE::NiPoint3 right(-forward.y, forward.x, 0);

        // Matrix columns: [right | forward | up]
        rotation.entry[0][0] = right.x;     rotation.entry[0][1] = forward.x;   rotation.entry[0][2] = 0;
        rotation.entry[1][0] = right.y;     rotation.entry[1][1] = forward.y;   rotation.entry[1][2] = 0;
        rotation.entry[2][0] = 0;           rotation.entry[2][1] = 0;           rotation.entry[2][2] = 1;

        return rotation;
    }

private:
    YawOnlyFacingStrategy() = default;
};

} // namespace Projectile
