#pragma once

#include "GameProjectile.h"

namespace Projectile {

// How position changes are applied
enum class TransitionMode {
    Instant,        // Position changes immediately (default)
    Lerp            // Position interpolates over time
};

// Handles smooth interpolation of transform changes over time.
// Uses exponential smoothing for responsive, natural-feeling transitions.
class TransformSmoother {
public:
    TransformSmoother() = default;

    // === Mode ===
    void SetMode(TransitionMode mode) { m_mode = mode; }
    TransitionMode GetMode() const { return m_mode; }

    // === Smoothing Speed ===
    // Higher = more responsive (10 = snappy, 2 = floaty)
    void SetSpeed(float speed) { m_speed = speed; }
    float GetSpeed() const { return m_speed; }

    // === Target ===
    // Set the target transform to smooth towards
    void SetTarget(const ProjectileTransform& target);
    const ProjectileTransform& GetTarget() const { return m_target; }

    // === Current Value ===
    // Get the current smoothed transform
    const ProjectileTransform& GetCurrent() const { return m_current; }

    // Initialize current value (skips smoothing for first frame)
    void SetCurrent(const ProjectileTransform& current);

    // === Update ===
    // Advance smoothing by deltaTime. Returns true if value changed.
    bool Update(float deltaTime);

    // Check if currently transitioning
    bool IsTransitioning() const { return m_isTransitioning; }

    // Reset transition state
    void Reset();

private:
    TransitionMode m_mode = TransitionMode::Lerp;
    float m_speed = 13.0f;
    bool m_isTransitioning = false;
    ProjectileTransform m_target;
    ProjectileTransform m_current;
};

} // namespace Projectile
