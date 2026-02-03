#include "TransformSmoother.h"
#include <cmath>

namespace Projectile {

void TransformSmoother::SetTarget(const ProjectileTransform& target) {
    m_target = target;

    if (m_mode == TransitionMode::Lerp) {
        m_isTransitioning = true;
    } else {
        // Instant mode - apply immediately
        m_current = target;
        m_isTransitioning = false;
    }
}

void TransformSmoother::SetCurrent(const ProjectileTransform& current) {
    m_current = current;
}

bool TransformSmoother::Update(float deltaTime) {
    if (!m_isTransitioning) {
        return false;
    }

    // Validate deltaTime - game freeze/resume can produce NaN or huge values
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return false;  // Skip this frame, don't corrupt state
    }

    // Cap deltaTime to avoid excessive jumps after long pauses
    constexpr float MAX_DELTA = 0.1f;  // 100ms max
    if (deltaTime > MAX_DELTA) {
        deltaTime = MAX_DELTA;
    }

    // Exponential smoothing - continuously chase the target
    // smoothFactor = 1 - exp(-speed * deltaTime)
    // Higher speed = more responsive (10 = snappy, 2 = floaty)

    float smoothFactor = 1.0f - std::exp(-m_speed * deltaTime);

    // Clamp to valid range [0, 1]
    if (smoothFactor < 0.0f) smoothFactor = 0.0f;
    if (smoothFactor > 1.0f) smoothFactor = 1.0f;

    // Smoothly interpolate position toward target
    m_current.position.x += (m_target.position.x - m_current.position.x) * smoothFactor;
    m_current.position.y += (m_target.position.y - m_current.position.y) * smoothFactor;
    m_current.position.z += (m_target.position.z - m_current.position.z) * smoothFactor;

    // Snap rotation (no interpolation) - proper matrix interpolation would require quaternion slerp
    // For billboard tracking, snapping is fine since rotation updates every frame anyway
    m_current.rotation = m_target.rotation;

    // Smoothly interpolate scale
    m_current.scale += (m_target.scale - m_current.scale) * smoothFactor;

    return true;
}

void TransformSmoother::Reset() {
    m_isTransitioning = false;
    m_target = ProjectileTransform();
    m_current = ProjectileTransform();
}

} // namespace Projectile
