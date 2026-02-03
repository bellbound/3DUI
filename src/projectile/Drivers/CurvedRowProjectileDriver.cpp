#include "CurvedRowProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../log.h"
#include <cmath>
#include <algorithm>

namespace Projectile {

CurvedRowProjectileDriver::CurvedRowProjectileDriver() {
    // Note: FacingStrategy is NOT set here - parent driver handles facing
    // via scene graph rotation inheritance. Only root drivers should set facing.
}

void CurvedRowProjectileDriver::SetForwardDirection(const RE::NiPoint3& direction) {
    float length = std::sqrt(direction.x * direction.x +
                             direction.y * direction.y +
                             direction.z * direction.z);
    if (length > 0.001f) {
        m_forwardDirection.x = direction.x / length;
        m_forwardDirection.y = direction.y / length;
        m_forwardDirection.z = direction.z / length;
    }
}

void CurvedRowProjectileDriver::Clear() {
    ProjectileDriver::Clear();
    m_baseAngleOffset = 0.0f;
    m_baseAngleInitialized = false;
}

RE::NiPoint3 CurvedRowProjectileDriver::ComputeCircleLocalPosition(float angle) const {
    // Skyrim coordinates: +X=right, +Y=forward, +Z=up
    // Layout in X-Z plane (vertical arc facing player, Y=0)
    float localX = m_radius * std::cos(angle);
    float localZ = m_radius * std::sin(angle);
    return RE::NiPoint3(localX, 0.0f, localZ);
}

// Note: ComputeRotatedLocalPosition and ComputeCircleWorldPosition removed
// Scene graph now handles rotation - children inherit parent's world rotation
// via GetWorldPosition() which applies parent rotation to local positions

float CurvedRowProjectileDriver::ComputeAngleFromWorldPosition(const RE::NiPoint3& worldPos) const {
    // Use scene graph world rotation to transform world position to local space
    RE::NiPoint3 centerPos = GetCenterPosition();
    RE::NiMatrix3 rotation = GetWorldRotation();

    // Extract basis vectors from rotation matrix
    // Skyrim convention: column 0=right(X), column 1=forward(Y), column 2=up(Z)
    RE::NiPoint3 right = {rotation.entry[0][0], rotation.entry[1][0], rotation.entry[2][0]};
    RE::NiPoint3 up = {rotation.entry[0][2], rotation.entry[1][2], rotation.entry[2][2]};

    RE::NiPoint3 toPos = worldPos - centerPos;

    // Project onto local X-Z plane (layout plane)
    float x = toPos.x * right.x + toPos.y * right.y + toPos.z * right.z;
    float z = toPos.x * up.x + toPos.y * up.y + toPos.z * up.z;

    return std::atan2(z, x);
}

float CurvedRowProjectileDriver::ComputeForwardAngleLocal() const {
    // Use scene graph world rotation
    RE::NiMatrix3 rotation = GetWorldRotation();

    // Skyrim convention: column 0=right(X), column 1=forward(Y), column 2=up(Z)
    RE::NiPoint3 right = {rotation.entry[0][0], rotation.entry[1][0], rotation.entry[2][0]};
    RE::NiPoint3 up = {rotation.entry[0][2], rotation.entry[1][2], rotation.entry[2][2]};

    // Project forward direction onto local X-Z plane
    float x = m_forwardDirection.x * right.x + m_forwardDirection.y * right.y + m_forwardDirection.z * right.z;
    float z = m_forwardDirection.x * up.x + m_forwardDirection.y * up.y + m_forwardDirection.z * up.z;

    if (std::abs(x) < 0.001f && std::abs(z) < 0.001f) {
        return 0.0f;
    }

    return std::atan2(z, x);
}

float CurvedRowProjectileDriver::NormalizeAngle(float angle) {
    while (angle > PI) angle -= TWO_PI;
    while (angle < -PI) angle += TWO_PI;
    return angle;
}

float CurvedRowProjectileDriver::AngleDistance(float a, float b) {
    float diff = NormalizeAngle(b - a);
    return std::abs(diff);
}

bool CurvedRowProjectileDriver::OnEvent(InputEvent& event) {
    if (event.type == InputEventType::GrabStart) {
        if (auto* proj = dynamic_cast<ControlledProjectile*>(event.source)) {
            if (proj->IsAnchorHandle()) {
                return ProjectileDriver::OnEvent(event);
            }
        }

        if (event.handNode) {
            m_grabNode = event.handNode;
            RE::NiPoint3 grabPos = event.handNode->world.translate;
            m_grabStartAngle = ComputeAngleFromWorldPosition(grabPos);
            m_grabStartBaseOffset = m_baseAngleOffset;
            m_isSlideGrabbing = true;

            spdlog::info("CurvedRowProjectileDriver::OnEvent - GrabStart slide grab: startAngle={:.2f}, baseOffset={:.2f}",
                m_grabStartAngle, m_grabStartBaseOffset);
            return true;
        }
        return false;
    }

    if (event.type == InputEventType::GrabEnd) {
        if (m_isSlideGrabbing) {
            spdlog::info("CurvedRowProjectileDriver::OnEvent - GrabEnd slide grab, final offset {:.2f}", m_baseAngleOffset);
            m_isSlideGrabbing = false;
            m_grabNode = nullptr;
            return true;
        }
        return ProjectileDriver::OnEvent(event);
    }

    return ProjectileDriver::OnEvent(event);
}

void CurvedRowProjectileDriver::UpdateLayout(float deltaTime) {
    // Guard against division by zero - radius must be positive
    if (m_radius < 0.001f) {
        spdlog::warn("CurvedRowProjectileDriver::UpdateLayout - radius is zero or negative, skipping layout");
        return;
    }

    // THREAD SAFETY: Copy children before iterating - main thread may modify while VR thread updates
    auto children = GetChildrenMutable();  // Copy, not reference

    // Count visible children
    size_t validCount = 0;
    for (auto& child : children) {
        if (child && child->IsVisible()) validCount++;
    }

    float anglePerItem = m_itemOffset / m_radius;
    float forwardAngle = ComputeForwardAngleLocal();
    float halfVisibleArc = m_visibleItemRange * PI;

    // Initialize base angle offset to center items on forward direction
    if (!m_baseAngleInitialized && validCount > 0) {
        // Center items around forward angle (not left-aligned)
        float totalSpan = static_cast<float>(validCount - 1) * anglePerItem;
        m_baseAngleOffset = forwardAngle - totalSpan / 2.0f;
        m_baseAngleInitialized = true;
        spdlog::info("CurvedRowProjectileDriver: Initialized baseAngleOffset to {:.2f} (forwardAngle {:.2f}, {} items centered)",
            m_baseAngleOffset, forwardAngle, validCount);
    }

    // Handle slide grab
    if (m_isSlideGrabbing && m_grabNode) {
        RE::NiPoint3 grabPos = m_grabNode->world.translate;
        float currentAngle = ComputeAngleFromWorldPosition(grabPos);
        float angleDelta = NormalizeAngle(currentAngle - m_grabStartAngle);
        float newOffset = m_grabStartBaseOffset + angleDelta;

        if (validCount > 0) {
            float maxOffset = forwardAngle + halfVisibleArc;
            float minOffset = forwardAngle - halfVisibleArc - (static_cast<float>(validCount) - 1.0f) * anglePerItem;

            if (minOffset < maxOffset) {
                newOffset = (std::max)(minOffset, (std::min)(maxOffset, newOffset));
            }
        }

        m_baseAngleOffset = newOffset;
    }

    // Compute center offset to align forward direction edge to origin (in X-Z plane)
    float centerOffsetX = -m_radius * std::cos(forwardAngle);
    float centerOffsetZ = -m_radius * std::sin(forwardAngle);

    // Position children in local X-Z plane (scene graph applies parent rotation automatically)
    size_t validIndex = 0;
    for (auto& child : children) {
        if (!child || !child->IsVisible()) continue;

        float itemAngle = m_baseAngleOffset + static_cast<float>(validIndex) * anglePerItem;

        // Compute local position with center offset
        RE::NiPoint3 localPos = ComputeCircleLocalPosition(itemAngle);
        localPos.x += centerOffsetX;
        localPos.z += centerOffsetZ;
        child->SetLocalPosition(localPos);

        ++validIndex;
    }
}

} // namespace Projectile
