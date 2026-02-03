#pragma once

#include "../ProjectileDriver.h"

namespace Projectile {

// Grid arrangement that stacks children vertically along the local Z axis
// Each child (driver or projectile) is positioned at a vertical offset from the grid center
class GridProjectileDriver : public ProjectileDriver {
public:
    GridProjectileDriver() = default;

    // === Grid Configuration ===
    // Spacing between rows (vertical offset per child)
    void SetRowSpacing(float spacing) { m_rowSpacing = spacing; }
    float GetRowSpacing() const { return m_rowSpacing; }

    // Note: SetFacingAnchor is inherited from ProjectileDriver
    // No override needed - scene graph handles rotation inheritance automatically
    // Child drivers inherit world rotation via GetWorldRotation()

protected:
    void UpdateLayout(float deltaTime) override;

private:
    float m_rowSpacing = 30.0f;  // Vertical spacing between rows
    // Note: m_facingAnchor is inherited from ProjectileDriver
};

} // namespace Projectile
