#pragma once

#include "../ProjectileDriver.h"

namespace Projectile {

// Curved row arrangement displaying items in a circle with visibility zones
// Only items within a specified slice of the circle are visible at full scale
// Grabbing non-anchor items slides all items around the circle
class CurvedRowProjectileDriver : public ProjectileDriver {
public:
    CurvedRowProjectileDriver();

    // === Circle Configuration ===
    void SetRadius(float radius) { m_radius = radius; }
    float GetRadius() const { return m_radius; }

    // Fraction of full circle where items display (0-1, 1 = complete circle)
    // Used for clamping slide grab range
    void SetVisibleItemRange(float range) { m_visibleItemRange = std::clamp(range, 0.0f, 1.0f); }
    float GetVisibleItemRange() const { return m_visibleItemRange; }

    // Spacing between items in game units (arc length along the circle)
    void SetItemOffset(float offset) { m_itemOffset = offset; }
    float GetItemOffset() const { return m_itemOffset; }

    // Set the forward direction - center of the visible slice (world space)
    void SetForwardDirection(const RE::NiPoint3& direction);
    RE::NiPoint3 GetForwardDirection() const { return m_forwardDirection; }

    // Note: SetFacingAnchor, GetFacingAnchor, HasFacingAnchor are inherited from ProjectileDriver

    // Clear children and reset base angle initialization
    void Clear() override;


    // IPositionable event handling - handles slide grab for non-anchor items
    bool OnEvent(InputEvent& event) override;

protected:
    void UpdateLayout(float deltaTime) override;

    // Compute the local position for a projectile at the given angle (relative to center)
    // Returns position in local coordinate space (X-Y plane)
    RE::NiPoint3 ComputeCircleLocalPosition(float angle) const;

    // Compute the angle of a world position relative to the circle center
    // Uses scene graph world rotation to transform world position to local space
    float ComputeAngleFromWorldPosition(const RE::NiPoint3& worldPos) const;

    // Compute the forward angle in local circle space
    float ComputeForwardAngleLocal() const;

    // Normalize angle to [-PI, PI]
    static float NormalizeAngle(float angle);

    // Compute angular distance (always positive, in [0, PI])
    static float AngleDistance(float a, float b);

private:
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;

    float m_radius = 60.0f;                    // Circle radius in game units
    float m_visibleItemRange = 0.35f;          // Fraction of circle visible (0-1)
    float m_itemOffset = 15.0f;                // Arc length spacing between items
    RE::NiPoint3 m_forwardDirection{0, 1, 0};  // World space forward direction
    // Note: m_facingAnchor is inherited from ProjectileDriver

    // Base angle offset - modified by sliding grab
    float m_baseAngleOffset = 0.0f;
    bool m_baseAngleInitialized = false;

    // Slide grab state
    bool m_isSlideGrabbing = false;
    RE::NiAVObject* m_grabNode = nullptr;
    float m_grabStartAngle = 0.0f;
    float m_grabStartBaseOffset = 0.0f;

    // Driver positioning state
    bool m_isDriverPositioning = false;
    RE::NiAVObject* m_positioningGrabNode = nullptr;

    // Capacity buffer - extra items rendered at scale 0 beyond circle capacity
    // Prevents pop-in when scrolling
    static constexpr size_t CAPACITY_BUFFER = 3;
};

} // namespace Projectile
