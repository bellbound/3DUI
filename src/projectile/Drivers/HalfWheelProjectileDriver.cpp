#include "HalfWheelProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../util/VRNodes.h"
#include "../../log.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace Projectile {

HalfWheelProjectileDriver::HalfWheelProjectileDriver() {
    // Default to smooth transitions for ring layout
    SetTransitionMode(TransitionMode::Lerp);
    // Note: FacingStrategy is NOT set here - parent driver handles facing
    // via scene graph rotation inheritance. Only root drivers should set facing.
}

void HalfWheelProjectileDriver::SetVisible(bool visible) {
    bool wasVisible = IsVisible();

    // When becoming visible, check if scroll offset is still valid
    // (children may have been added/removed while hidden)
    if (visible && !wasVisible) {
        float maxOffset = ComputeMaxScrollOffset();
        if (m_scrollOffset > maxOffset) {
            spdlog::info("HalfWheelProjectileDriver::SetVisible - Scroll offset {:.3f} exceeds max {:.3f}, resetting to 0",
                m_scrollOffset, maxOffset);
            m_scrollOffset = 0.0f;
        }
    }

    // Call base class implementation
    ProjectileDriver::SetVisible(visible);
}

// =============================================================================
// Ring Geometry Computation
// =============================================================================

float HalfWheelProjectileDriver::ComputeRingRadius(size_t ringIndex) const {
    if (ringIndex == 0) {
        return 0.0f;  // Center has no radius
    }

    // First ring uses firstRingSpacing if set, otherwise rowDistance
    float firstRing = m_firstRingSpacing.value_or(m_rowDistance);

    if (ringIndex == 1) {
        return firstRing;
    }

    // Ring 2+: firstRingSpacing + (ringIndex - 1) * rowDistance
    return firstRing + static_cast<float>(ringIndex - 1) * m_rowDistance;
}

size_t HalfWheelProjectileDriver::ComputeItemsInHalfRing(size_t ringIndex) const {
    if (ringIndex == 0) {
        return 1;  // Center ring always has 1 item
    }

    float radius = ComputeRingRadius(ringIndex);
    // Half circumference (semicircle arc length)
    float arcLength = PI * radius;

    // Calculate how many items fit, rounding to nearest integer (minimum 1)
    size_t count = static_cast<size_t>(std::round(arcLength / m_itemSpacing));
    return std::max<size_t>(1, count);
}

size_t HalfWheelProjectileDriver::ComputeMaxVisibleItems() const {
    if (m_maxRings == 0) {
        return SIZE_MAX;  // Unlimited
    }

    size_t total = 1;  // Center item
    for (size_t ring = 1; ring <= m_maxRings; ++ring) {
        total += ComputeItemsInHalfRing(ring);
    }
    return total;
}

bool HalfWheelProjectileDriver::CanScroll() const {
    if (m_maxRings == 0) {
        return false;  // No max rings = no scrolling
    }

    size_t totalItems = GetChildren().size();
    size_t maxVisible = ComputeMaxVisibleItems();
    return totalItems > maxVisible;
}

// =============================================================================
// Racing Algorithm for Ring/Angle Assignment
// =============================================================================
// Instead of serpentine (which creates uneven density at scroll edges),
// we use a "racing" algorithm that keeps all rings progressing at similar rates:
// 1. Each ring tracks its current angle (progress)
// 2. For each item, assign to the ring that's furthest behind (lowest angle)
// 3. Advance that ring by its angular step
// Result: All rings fill at similar angular rates → clean leading edge when scrolling

std::pair<size_t, float> HalfWheelProjectileDriver::ComputeRacingPosition(size_t itemIndex) const {
    // Center item
    if (itemIndex == 0) {
        return {0, 0.0f};
    }

    // Determine number of rings to use
    size_t numRings = (m_maxRings > 0) ? m_maxRings : 1;

    // Fallback for no rings configured
    if (numRings == 0) {
        numRings = 1;
    }

    // Compute step size for each ring (angular distance between consecutive items in that ring)
    std::vector<float> steps(numRings);
    for (size_t r = 0; r < numRings; ++r) {
        size_t itemsInRing = ComputeItemsInHalfRing(r + 1);
        // Step = π / (items - 1) for multiple items, or π for single item
        steps[r] = (itemsInRing > 1) ? PI / static_cast<float>(itemsInRing - 1) : PI;
    }

    // Track current angle for each ring (all start at 0)
    std::vector<float> angles(numRings, 0.0f);

    // Simulate the racing assignment for items 1 through itemIndex
    size_t resultRing = 1;
    float resultAngle = 0.0f;

    for (size_t item = 1; item <= itemIndex; ++item) {
        // Find the ring that's furthest behind (lowest angle)
        // Tie-breaker: prefer inner rings (lower index) for consistent ordering
        size_t behindRing = 0;
        float minAngle = angles[0];
        for (size_t r = 1; r < numRings; ++r) {
            if (angles[r] < minAngle) {
                minAngle = angles[r];
                behindRing = r;
            }
        }

        // If this is the item we're looking for, record its position
        if (item == itemIndex) {
            resultRing = behindRing + 1;  // Convert to 1-based ring index
            resultAngle = angles[behindRing];
        }

        // Advance the behind ring by its step
        angles[behindRing] += steps[behindRing];
    }

    return {resultRing, resultAngle};
}

// Convenience wrapper: get just the base angle
float HalfWheelProjectileDriver::ComputeBaseAngle(size_t itemIndex) const {
    return ComputeRacingPosition(itemIndex).second;
}

// Convenience wrapper: get just the ring index
size_t HalfWheelProjectileDriver::ComputeRingForItem(size_t itemIndex) const {
    return ComputeRacingPosition(itemIndex).first;
}

// Static positioning: fills rings sequentially from inner to outer
// Used when there are not enough items to scroll
std::pair<size_t, float> HalfWheelProjectileDriver::ComputeStaticPosition(size_t itemIndex, size_t totalItems) const {
    // Center item
    if (itemIndex == 0) {
        return {0, PI / 2.0f};  // Center at top
    }

    // Determine which ring this item belongs to by counting capacity
    size_t cumulativeCapacity = 1;  // Start with center item
    size_t targetRing = 1;

    while (true) {
        size_t ringCapacity = ComputeItemsInHalfRing(targetRing);
        if (itemIndex < cumulativeCapacity + ringCapacity) {
            // Item belongs to this ring
            break;
        }
        cumulativeCapacity += ringCapacity;
        targetRing++;

        // Safety: don't go beyond reasonable ring count
        if (targetRing > 100) {
            break;
        }
    }

    // Position within this ring (0-based index within the ring)
    size_t positionInRing = itemIndex - cumulativeCapacity;
    size_t ringCapacity = ComputeItemsInHalfRing(targetRing);

    // Count how many items are actually in this ring
    size_t itemsInThisRing;
    size_t nextRingStart = cumulativeCapacity + ringCapacity;
    if (totalItems <= nextRingStart) {
        // This is the outermost ring with items (may be partial)
        itemsInThisRing = totalItems - cumulativeCapacity;
    } else {
        // Ring is full
        itemsInThisRing = ringCapacity;
    }

    // Compute angle
    float angle;
    if (itemsInThisRing == 1) {
        // Single item: center at top (π/2)
        angle = PI / 2.0f;
    } else if (itemsInThisRing == ringCapacity) {
        // Full ring: evenly space from 0 to π
        float step = PI / static_cast<float>(ringCapacity - 1);
        angle = static_cast<float>(positionInRing) * step;
    } else {
        // Partial ring: center around π/2 (top)
        // Calculate the step (same spacing as if ring were full)
        float step = PI / static_cast<float>(ringCapacity - 1);
        // Total angular span for our items
        float totalSpan = step * static_cast<float>(itemsInThisRing - 1);
        // Start angle to center the group around π/2
        float startAngle = (PI - totalSpan) / 2.0f;
        angle = startAngle + static_cast<float>(positionInRing) * step;
    }

    return {targetRing, angle};
}

// =============================================================================
// Position Computation
// =============================================================================

RE::NiPoint3 HalfWheelProjectileDriver::ComputePositionFromAngle(float angle, size_t ringIndex) const {
    if (ringIndex == 0) {
        return RE::NiPoint3(0, 0, 0);  // Center
    }

    float radius = ComputeRingRadius(ringIndex);

    // Skyrim coordinates: +X=right, +Y=forward, +Z=up
    // Layout in X-Z plane (vertical wall facing player, Y=0)
    // cos(0) = 1 → X+ (right), cos(π) = -1 → X- (left)
    // sin(0) = 0, sin(π/2) = 1 → Z+ (up)
    float localX = radius * std::cos(angle);
    float localY = 0.0f;
    float localZ = radius * std::sin(angle);

    return RE::NiPoint3(localX, localY, localZ);
}

RE::NiPoint3 HalfWheelProjectileDriver::ComputeHalfRingLocalPosition(size_t index) const {
    // Legacy method - compute position for a given visible index
    // Now delegates to angle-based computation
    if (index == 0) {
        return RE::NiPoint3(0, 0, 0);
    }

    size_t ringIndex = ComputeRingForItem(index);
    float baseAngle = ComputeBaseAngle(index);

    return ComputePositionFromAngle(baseAngle, ringIndex);
}

// =============================================================================
// Scroll Angle Visibility
// =============================================================================

bool HalfWheelProjectileDriver::IsAngleVisible(float angle) const {
    // Visible range is [0, π] (the semicircle)
    // Apply scroll offset: displayed angle = base angle + scroll offset
    // Item is visible if displayed angle is in [0, π]

    // Note: We DON'T apply scroll offset here - it's applied in UpdateLayout
    // This checks if the final angle is in visible range
    return angle >= 0.0f && angle <= PI;
}

// =============================================================================
// Scroll Bounds
// =============================================================================

float HalfWheelProjectileDriver::ComputeMaxScrollOffset() const {
    if (m_maxRings == 0 || !CanScroll()) {
        return 0.0f;
    }

    // With the sliding window model, we need to find the maximum baseAngle
    // and compute how far we need to scroll for it to appear at the left edge (π position)
    //
    // Visible window = [scrollOffset, scrollOffset + π]
    // For an item at baseAngle to appear at the left edge: baseAngle = scrollOffset + π
    // Therefore: scrollOffset = baseAngle - π
    //
    // Max scroll = (largest baseAngle among all items) - π

    size_t totalItems = GetChildren().size();
    if (totalItems <= 1) {
        return 0.0f;  // Only center item
    }

    // Find the maximum baseAngle (last item has highest angle due to serpentine continuation)
    float maxBaseAngle = 0.0f;
    for (size_t i = 1; i < totalItems; ++i) {  // Skip center (index 0)
        float angle = ComputeBaseAngle(i);
        if (angle > maxBaseAngle) {
            maxBaseAngle = angle;
        }
    }

    // Max scroll offset = maxBaseAngle - π (so the last item appears at left edge)
    // But don't go below 0 (no need to scroll if everything fits)
    float maxOffset = maxBaseAngle - PI;
    return (maxOffset > 0.0f) ? maxOffset : 0.0f;
}

void HalfWheelProjectileDriver::ClampScrollOffset() {
    // Guard against NaN/infinity from bad calculations (e.g., after game freeze/resume)
    if (!std::isfinite(m_scrollOffset)) {
        m_scrollOffset = 0.0f;
        return;
    }

    float maxOffset = ComputeMaxScrollOffset();

    // Hard clamp - no overscroll allowed
    // Default position is fully left-aligned (offset = 0)
    if (m_scrollOffset < 0.0f) {
        m_scrollOffset = 0.0f;
    } else if (m_scrollOffset > maxOffset) {
        m_scrollOffset = maxOffset;
    }
}

// =============================================================================
// Scroll State Management
// =============================================================================

void HalfWheelProjectileDriver::StartScrolling(bool isLeftHand) {
    if (!CanScroll()) {
        return;
    }

    m_isScrolling = true;
    m_scrollHandIsLeft = isLeftHand;

    // Capture the driver's rotation at scroll start - use this fixed reference frame
    // throughout the scroll to prevent direction flips when the driver rotates (facing strategy)
    m_scrollStartRotation = GetWorldRotation();

    // Initialize previous position for per-frame delta tracking
    // Use the fixed rotation for consistent local coordinates
    RE::NiAVObject* hand = isLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
    if (hand) {
        m_scrollPrevLocalPos = WorldToLocalFixedFrame(hand->world.translate);
    }

    spdlog::info("HalfWheelProjectileDriver: Started scrolling (hand: {})",
        isLeftHand ? "left" : "right");
}

void HalfWheelProjectileDriver::EndScrolling() {
    if (!m_isScrolling) {
        return;
    }

    m_isScrolling = false;

    // Snap to nearest ideal position if within threshold
    // Ideal positions = scroll offsets where an item is exactly at right edge (displayed angle 0)
    // This creates a "grid" feel when scrolling small amounts
    constexpr float SNAP_DEGREES = 12.0f;
    constexpr float SNAP_RADIANS = SNAP_DEGREES * PI / 180.0f;

    float maxOffset = ComputeMaxScrollOffset();

    // Find nearest ideal position
    // Start with the boundary positions as candidates
    float nearestIdeal = 0.0f;
    float minDistance = std::abs(m_scrollOffset);  // Distance to offset 0

    // Check distance to maxOffset
    float distToMax = std::abs(m_scrollOffset - maxOffset);
    if (distToMax < minDistance) {
        minDistance = distToMax;
        nearestIdeal = maxOffset;
    }

    // Check all item base angles as ideal positions
    // scrollOffset == baseAngle puts that item at displayed angle 0 (right edge)
    size_t totalItems = GetChildren().size();
    for (size_t i = 1; i < totalItems; ++i) {  // Skip center (index 0)
        float baseAngle = ComputeBaseAngle(i);
        // Only consider if it's a valid scroll position
        if (baseAngle > 0.0f && baseAngle < maxOffset) {
            float distance = std::abs(m_scrollOffset - baseAngle);
            if (distance < minDistance) {
                minDistance = distance;
                nearestIdeal = baseAngle;
            }
        }
    }

    // Snap if within threshold
    if (minDistance <= SNAP_RADIANS) {
        spdlog::info("HalfWheelProjectileDriver: Snapping from {:.3f} to ideal {:.3f} ({:.1f}deg, dist={:.3f}rad)",
            m_scrollOffset, nearestIdeal, nearestIdeal * 180.0f / PI, minDistance);
        m_scrollOffset = nearestIdeal;
    }

    spdlog::info("HalfWheelProjectileDriver: Ended scrolling at offset {:.3f} ({:.1f}deg)",
        m_scrollOffset, m_scrollOffset * 180.0f / PI);
}

RE::NiPoint3 HalfWheelProjectileDriver::WorldToLocal(const RE::NiPoint3& worldPos) const {
    // Transform world position to local coordinates
    // Local = InverseRotation * (World - DriverWorldPos)

    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    // Apply inverse of driver's world rotation
    RE::NiMatrix3 worldRot = GetWorldRotation();
    RE::NiMatrix3 invRot = InverseRotationMatrix(worldRot);

    return RotatePoint(invRot, relative);
}

RE::NiPoint3 HalfWheelProjectileDriver::WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const {
    // Transform world position to local coordinates using the FIXED rotation
    // captured at scroll start. This prevents direction flips when the driver
    // rotates during scrolling (e.g., due to facing strategy).

    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    // Apply inverse of the FIXED rotation from scroll start
    RE::NiMatrix3 invRot = InverseRotationMatrix(m_scrollStartRotation);

    return RotatePoint(invRot, relative);
}

void HalfWheelProjectileDriver::UpdateScrollFromHand() {
    if (!m_isScrolling) {
        return;
    }

    RE::NiAVObject* hand = m_scrollHandIsLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
    if (!hand) {
        return;
    }

    // Get current hand position in local coordinates using FIXED reference frame
    RE::NiPoint3 handLocal = WorldToLocalFixedFrame(hand->world.translate);

    // Compute per-frame delta from previous position
    RE::NiPoint3 delta;
    delta.x = handLocal.x - m_scrollPrevLocalPos.x;
    delta.y = handLocal.y - m_scrollPrevLocalPos.y;
    delta.z = handLocal.z - m_scrollPrevLocalPos.z;

    // Direction: determined by X component (left = negative X = scroll backward)
    // Using a small threshold to avoid jitter when hand is nearly stationary
    constexpr float directionThreshold = 0.01f;
    float direction = 0.0f;
    if (delta.x < -directionThreshold) {
        direction = -1.0f;  // Moving left → scroll backward (decrease offset)
    } else if (delta.x > directionThreshold) {
        direction = 1.0f;   // Moving right → scroll forward (increase offset)
    }

    // Magnitude: full 3D distance traveled (makes diagonal movement = horizontal movement)
    float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

    // Guard against NaN/infinity from bad hand position data (e.g., after game freeze/resume)
    if (!std::isfinite(distance)) {
        return;
    }

    // Accumulate scroll offset
    m_scrollOffset += direction * distance * m_scrollSensitivity;
    ClampScrollOffset();

    // Update previous position for next frame
    m_scrollPrevLocalPos = handLocal;
}

// =============================================================================
// Event Handling
// =============================================================================

bool HalfWheelProjectileDriver::OnEvent(InputEvent& event) {
    // Handle non-anchor grabs for scrolling
    if (event.type == InputEventType::GrabStart) {
        auto* proj = dynamic_cast<ControlledProjectile*>(event.source);
        if (proj && !proj->IsAnchorHandle()) {
            // Non-anchor grab → start scrolling (if scrollable)
            if (CanScroll()) {
                StartScrolling(event.isLeftHand);
                return true;  // Consume event
            }
        }
    }

    if (event.type == InputEventType::GrabEnd) {
        if (m_isScrolling) {
            EndScrolling();
            return true;  // Consume event
        }
    }

    // Let base class handle anchor grabs and other events
    return ProjectileDriver::OnEvent(event);
}

// =============================================================================
// Update Loop
// =============================================================================

void HalfWheelProjectileDriver::Update(float deltaTime) {
    // Track hand movement while scrolling
    if (m_isScrolling) {
        UpdateScrollFromHand();
    }

    // Call base class update (handles facing, UpdateLayout, child updates)
    ProjectileDriver::Update(deltaTime);
}

void HalfWheelProjectileDriver::UpdateLayout(float deltaTime) {
    // THREAD SAFETY: Copy children before iterating - main thread may modify while VR thread updates
    auto children = GetChildrenMutable();  // Copy, not reference
    size_t totalItems = children.size();

    if (totalItems == 0) {
        return;
    }

    // Ensure visibility tracking vector is correct size
    // Initialize to match actual child visibility state to avoid spurious SetVisible calls
    if (m_previousVisibility.size() != totalItems) {
        m_previousVisibility.resize(totalItems);
        for (size_t i = 0; i < totalItems; ++i) {
            if (children[i]) {
                m_previousVisibility[i] = children[i]->IsVisible();
            } else {
                m_previousVisibility[i] = false;
            }
        }
    }

    // Compute visibility and position for each item
    size_t maxVisible = ComputeMaxVisibleItems();
    bool scrollingEnabled = (m_maxRings > 0) && (totalItems > maxVisible);

    // One-time layout logging - delay until all projectiles are valid
    // Count valid projectiles to ensure we don't log during initialization
    size_t validCount = 0;
    if (!m_hasLoggedInitialLayout) {
        for (size_t i = 0; i < totalItems; ++i) {
            if (auto* p = dynamic_cast<ControlledProjectile*>(children[i].get())) {
                if (p->IsValid()) ++validCount;
            }
        }
    }
    bool shouldLogLayout = !m_hasLoggedInitialLayout && validCount == totalItems && totalItems > 0;
    if (shouldLogLayout) {
        spdlog::trace("=== HalfWheelProjectileDriver Initial Layout Snapshot ===");
        spdlog::trace("Total items: {}, MaxVisible: {}, MaxRings: {}, ScrollingEnabled: {}",
            totalItems, maxVisible, m_maxRings, scrollingEnabled);
        spdlog::trace("ScrollOffset: {:.3f}, Window: [{:.3f}, {:.3f}]",
            m_scrollOffset, m_scrollOffset, m_scrollOffset + PI);
    }

    for (size_t i = 0; i < totalItems; ++i) {
        auto& child = children[i];
        if (!child) continue;

        // Check if child is a ControlledProjectile
        auto* proj = dynamic_cast<ControlledProjectile*>(child.get());
        bool isValidProjectile = proj && proj->IsValid();

        bool shouldBeVisible;
        RE::NiPoint3 localPos;
        size_t ringIndex = 0;
        float baseAngle = 0.0f;
        float displayedAngle = 0.0f;
        bool isOverflow = false;

        if (i == 0) {
            // Center item: ALWAYS visible, never affected by scroll
            shouldBeVisible = true;
            localPos = RE::NiPoint3(0, 0, 0);
        } else {
            if (scrollingEnabled) {
                // Ring items when scrolling: use racing algorithm for even distribution
                // Ring index is used only for radius calculation (wraps for overflow items)
                ringIndex = ComputeRingForItem(i);
                baseAngle = ComputeBaseAngle(i);
                isOverflow = (i >= maxVisible);

                // Sliding window model: visible window = [scrollOffset, scrollOffset + π]
                // No normalization - this correctly handles multiple cycles of overflow items
                // Item is visible if its baseAngle falls within the current window
                float windowStart = m_scrollOffset;
                float windowEnd = m_scrollOffset + PI;

                shouldBeVisible = (baseAngle >= windowStart) && (baseAngle <= windowEnd);

                // Always compute position (even for hidden items) so they're ready when visible
                // Position relative to window start: 0 = right edge, π = left edge
                displayedAngle = baseAngle - m_scrollOffset;
                localPos = ComputePositionFromAngle(displayedAngle, ringIndex);
            } else {
                // No scrolling - use static positioning: fill rings from inner to outer
                // Partial outermost ring is centered around π/2 (top)
                auto [staticRing, staticAngle] = ComputeStaticPosition(i, totalItems);
                ringIndex = staticRing;
                displayedAngle = staticAngle;
                baseAngle = staticAngle;  // For logging consistency
                shouldBeVisible = true;   // All items visible when not scrolling
                localPos = ComputePositionFromAngle(staticAngle, ringIndex);
            }
        }

        // One-time layout logging for each item (only for valid projectiles)
        if (shouldLogLayout && isValidProjectile) {
            // Use UUID for logging
            const std::string itemText = proj->GetUUID().ToString();

            spdlog::trace("[{}] '{}' | ring:{} baseAngle:{:.3f} ({:.1f}deg) dispAngle:{:.3f} | pos:({:.1f},{:.1f},{:.1f}) | visible:{} overflow:{}",
                i,
                itemText,
                ringIndex,
                baseAngle,
                baseAngle * 180.0f / PI,
                displayedAngle,
                localPos.x, localPos.y, localPos.z,
                shouldBeVisible ? "Y" : "N",
                isOverflow ? "Y" : "N");
        }

        // Set visibility - works even before Initialize() by setting m_localVisible
        // This ensures items that should be hidden don't fire when Initialize() runs
        if (shouldBeVisible) {
            child->SetVisible(true);
            // Only set scale on valid projectiles
            if (isValidProjectile) {
                child->SetLocalScale(1.0f);
            }
        } else {
            child->SetVisible(false);
            if (isValidProjectile) {
                child->SetLocalScale(0.0f);
            }
        }
        m_previousVisibility[i] = shouldBeVisible;

        // Position/scale updates only for valid projectiles
        if (isValidProjectile) {
            child->SetLocalPosition(localPos);
        }
    }

    // Mark logging as done
    if (shouldLogLayout) {
        spdlog::trace("=== End Layout Snapshot ===");
        m_hasLoggedInitialLayout = true;
    }
}

} // namespace Projectile
