#pragma once

#include "../ProjectileDriver.h"

namespace Projectile {

// Concentric ring arrangement
// Projectiles are arranged in concentric rings on a flat plane that can be oriented toward an anchor
// Ring 0 is the center (1 item), Ring N is at radius N * rowDistance
// Items in each ring are spaced evenly around the circumference, with spacing adjusted to cover the full circle
class RadialProjectileDriver : public ProjectileDriver {
public:
    RadialProjectileDriver();

    // === Ring Configuration ===
    // Target distance between items within a ring (actual distance is adjusted to fill circle evenly)
    void SetItemSpacing(float spacing) { m_itemSpacing = spacing; }
    float GetItemSpacing() const { return m_itemSpacing; }

    // Distance between concentric rings
    void SetRowDistance(float distance) { m_rowDistance = distance; }
    float GetRowDistance() const { return m_rowDistance; }

    // Ring spacing (alias for row distance - space between rings)
    void SetRingSpacing(float spacing) { m_rowDistance = spacing; }
    float GetRingSpacing() const { return m_rowDistance; }

    // Legacy compatibility
    void SetSpacing(float spacing) { m_itemSpacing = spacing; }
    float GetSpacing() const { return m_itemSpacing; }

    // Note: SetFacingAnchor, GetFacingAnchor, HasFacingAnchor are inherited from ProjectileDriver

protected:
    void UpdateLayout(float deltaTime) override;

    // Compute the local position for a projectile at the given index (relative to center)
    // Returns position in local coordinate space (X-Y plane)
    // Scene graph handles rotation inheritance automatically
    RE::NiPoint3 ComputeRingLocalPosition(size_t index) const;

private:
    // Compute number of items that fit in a ring at the given radius
    size_t ComputeItemsInRing(size_t ringIndex) const;

    // Get the starting angle offset based on item count
    // 2 items: π (horizontal left/right)
    // 3+ items: π/2 (start from top)
    float GetStartAngle(size_t count) const;

    static constexpr float TWO_PI = 6.28318530718f;
    static constexpr float HALF_PI = 1.57079632679f;

    float m_itemSpacing = 15.0f;    // Target distance between items in a ring
    float m_rowDistance = 15.0f;    // Distance between concentric rings
    mutable size_t m_visibleItemCount = 0;  // Cached during UpdateLayout for even distribution
    // Note: m_facingAnchor is inherited from ProjectileDriver
};

} // namespace Projectile
