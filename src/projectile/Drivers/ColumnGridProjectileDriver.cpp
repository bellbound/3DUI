#include "ColumnGridProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../util/VRNodes.h"
#include "../../log.h"
#include <cmath>
#include <algorithm>

namespace Projectile {

ColumnGridProjectileDriver::ColumnGridProjectileDriver() {
    // Default to smooth transitions for grid layout
    SetTransitionMode(TransitionMode::Lerp);
    // Note: FacingStrategy is NOT set here - parent driver handles facing
    // via scene graph rotation inheritance. Only root drivers should set facing.
}

void ColumnGridProjectileDriver::SetVisible(bool visible) {
    bool wasVisible = IsVisible();

    // When becoming visible, check if scroll offset is still valid
    // (children may have been added/removed while hidden)
    if (visible && !wasVisible) {
        float maxOffset = ComputeMaxScrollOffset();
        if (m_scrollOffset > maxOffset) {
            spdlog::info("ColumnGridProjectileDriver::SetVisible - Scroll offset {:.3f} exceeds max {:.3f}, resetting to 0",
                m_scrollOffset, maxOffset);
            m_scrollOffset = 0.0f;
        }
    }

    // Call base class implementation
    ProjectileDriver::SetVisible(visible);
}

// =============================================================================
// Fill Direction Configuration
// =============================================================================

void ColumnGridProjectileDriver::SetFillDirection(P3DUI::VerticalFill verticalFill, P3DUI::HorizontalFill horizontalFill) {
    m_verticalFill = verticalFill;
    m_horizontalFill = horizontalFill;
}

// =============================================================================
// Origin (Anchor) Configuration
// =============================================================================

void ColumnGridProjectileDriver::SetOrigin(P3DUI::VerticalOrigin verticalOrigin, P3DUI::HorizontalOrigin horizontalOrigin) {
    m_verticalOrigin = verticalOrigin;
    m_horizontalOrigin = horizontalOrigin;
}

// =============================================================================
// Grid Position Computation (Column-Major)
// =============================================================================

std::pair<size_t, size_t> ColumnGridProjectileDriver::ComputeGridPosition(size_t itemIndex) const {
    // Column-major: fill top-to-bottom in each column, then move to next column
    // item 0 -> (col 0, row 0), item 1 -> (col 0, row 1), ...
    // m_numRows defines how many items per column before wrapping to next column
    size_t column = itemIndex / m_numRows;
    size_t row = itemIndex % m_numRows;
    return {column, row};
}

float ColumnGridProjectileDriver::ComputeBaseX(size_t itemIndex) const {
    auto [column, row] = ComputeGridPosition(itemIndex);

    // Columns are spaced by m_columnSpacing
    // HorizontalFill determines which direction columns grow:
    // - LeftToRight: column 0 at left, grows toward player's RIGHT (UI's -X due to mirror)
    // - RightToLeft: column 0 at right, grows toward player's LEFT (UI's +X)
    float x = static_cast<float>(column) * m_columnSpacing;

    // Since UI faces the player, coordinates are mirrored from viewer's perspective.
    // For LeftToRight fill: negate X so items grow toward player's right.
    if (m_horizontalFill == P3DUI::HorizontalFill::LeftToRight) {
        x = -x;
    }

    return x;
}

float ColumnGridProjectileDriver::ComputeZ(size_t row) const {
    // Rows are stacked vertically
    // VerticalFill determines which direction rows stack:
    // - TopToBottom: Row 0 at top (Z=0), subsequent rows go downward (negative Z)
    // - BottomToTop: Row 0 at bottom (Z=0), subsequent rows go upward (positive Z)
    float z;

    if (m_verticalFill == P3DUI::VerticalFill::TopToBottom) {
        // TopToBottom: Row 0 at highest Z (0), higher rows are below (negative Z)
        z = -static_cast<float>(row) * m_rowSpacing;
    } else {
        // BottomToTop: Row 0 is at lowest Z, higher rows are above (positive Z)
        z = static_cast<float>(row) * m_rowSpacing;
    }

    return z;
}

RE::NiPoint3 ColumnGridProjectileDriver::ComputeLocalPosition(float displayedX, size_t row) const {
    float z = ComputeZ(row);
    return RE::NiPoint3(displayedX, 0.0f, z);
}

size_t ColumnGridProjectileDriver::ComputeVisibleColumns() const {
    if (m_columnSpacing <= 0.0f) {
        return 1;
    }
    // Number of columns that fit in the visible width
    // Add 1 to ensure we don't cut off columns at the edge
    return static_cast<size_t>(m_visibleWidth / m_columnSpacing) + 1;
}

float ColumnGridProjectileDriver::ComputeTotalWidth() const {
    size_t totalItems = GetChildren().size();
    if (totalItems == 0) {
        return 0.0f;
    }

    // Total columns needed (column-major)
    size_t totalColumns = (totalItems + m_numRows - 1) / m_numRows;
    if (totalColumns == 0) {
        return 0.0f;
    }

    // Width spans from column 0 to column (totalColumns-1)
    return static_cast<float>(totalColumns - 1) * m_columnSpacing;
}

// =============================================================================
// Scroll Bounds
// =============================================================================

bool ColumnGridProjectileDriver::CanScroll() const {
    float totalWidth = ComputeTotalWidth();
    return totalWidth > m_visibleWidth;
}

float ColumnGridProjectileDriver::ComputeMaxScrollOffset() const {
    if (!CanScroll()) {
        return 0.0f;
    }

    float totalWidth = ComputeTotalWidth();
    // Maximum scroll is when the last column is at the LEFT edge of visible area
    // This ensures the full visible window is still populated with items
    float maxOffset = totalWidth - m_visibleWidth;

    // Minimum is 0 (first column at left edge)
    return (std::max)(0.0f, maxOffset);
}

void ColumnGridProjectileDriver::ClampScrollOffset() {
    // Guard against NaN/infinity
    if (!std::isfinite(m_scrollOffset)) {
        m_scrollOffset = 0.0f;
        return;
    }

    float maxOffset = ComputeMaxScrollOffset();

    // Hard clamp - no overscroll allowed
    if (m_scrollOffset < 0.0f) {
        m_scrollOffset = 0.0f;
    } else if (m_scrollOffset > maxOffset) {
        m_scrollOffset = maxOffset;
    }
}

void ColumnGridProjectileDriver::SetScrollOffset(float offset) {
    m_scrollOffset = offset;
    ClampScrollOffset();
    // Layout will be recomputed on next UpdateLayout call
}

bool ColumnGridProjectileDriver::IsXVisible(float displayedX) const {
    // Visible range depends on HorizontalOrigin anchor:
    // - Left: anchor at X=0, visible area extends rightward (player's right = UI's -X) [−visibleWidth, 0]
    // - Center: anchor at X=0, visible area is centered [-visibleWidth/2, visibleWidth/2]
    // - Right: anchor at X=0, visible area extends leftward (player's left = UI's +X) [0, visibleWidth]
    switch (m_horizontalOrigin) {
        case P3DUI::HorizontalOrigin::Left:
            return displayedX >= -m_visibleWidth && displayedX <= 0.0f;
        case P3DUI::HorizontalOrigin::Center:
            return displayedX >= -m_visibleWidth / 2.0f && displayedX <= m_visibleWidth / 2.0f;
        case P3DUI::HorizontalOrigin::Right:
        default:
            return displayedX >= 0.0f && displayedX <= m_visibleWidth;
    }
}

// =============================================================================
// Scroll State Management
// =============================================================================

void ColumnGridProjectileDriver::StartScrolling(bool isLeftHand) {
    if (!CanScroll()) {
        return;
    }

    m_isScrolling = true;
    m_scrollHandIsLeft = isLeftHand;

    // Capture the driver's rotation at scroll start - use this fixed reference frame
    // throughout the scroll to prevent direction flips when the driver rotates
    m_scrollStartRotation = GetWorldRotation();

    // Initialize previous position for per-frame delta tracking
    RE::NiAVObject* hand = isLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
    if (hand) {
        m_scrollPrevLocalPos = WorldToLocalFixedFrame(hand->world.translate);
    }

    spdlog::info("ColumnGridProjectileDriver: Started scrolling (hand: {})",
        isLeftHand ? "left" : "right");
}

void ColumnGridProjectileDriver::EndScrolling() {
    if (!m_isScrolling) {
        return;
    }

    m_isScrolling = false;

    // Snap to nearest column position if within threshold
    constexpr float SNAP_THRESHOLD = 3.0f;  // Game units

    float maxOffset = ComputeMaxScrollOffset();

    // Find nearest column position
    float nearestIdeal = 0.0f;
    float minDistance = std::abs(m_scrollOffset);  // Distance to offset 0

    // Check distance to maxOffset
    float distToMax = std::abs(m_scrollOffset - maxOffset);
    if (distToMax < minDistance) {
        minDistance = distToMax;
        nearestIdeal = maxOffset;
    }

    // Check all column positions as ideal snap points
    size_t totalItems = GetChildren().size();
    size_t totalColumns = (totalItems + m_numRows - 1) / m_numRows;
    for (size_t col = 0; col < totalColumns; ++col) {
        float colOffset = static_cast<float>(col) * m_columnSpacing;
        if (colOffset >= 0.0f && colOffset <= maxOffset) {
            float distance = std::abs(m_scrollOffset - colOffset);
            if (distance < minDistance) {
                minDistance = distance;
                nearestIdeal = colOffset;
            }
        }
    }

    // Snap if within threshold
    if (minDistance <= SNAP_THRESHOLD) {
        spdlog::info("ColumnGridProjectileDriver: Snapping from {:.3f} to ideal {:.3f} (dist={:.3f})",
            m_scrollOffset, nearestIdeal, minDistance);
        m_scrollOffset = nearestIdeal;
    }

    spdlog::info("ColumnGridProjectileDriver: Ended scrolling at offset {:.3f}",
        m_scrollOffset);
}

RE::NiPoint3 ColumnGridProjectileDriver::WorldToLocal(const RE::NiPoint3& worldPos) const {
    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    RE::NiMatrix3 worldRot = GetWorldRotation();
    RE::NiMatrix3 invRot = InverseRotationMatrix(worldRot);

    return RotatePoint(invRot, relative);
}

RE::NiPoint3 ColumnGridProjectileDriver::WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const {
    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    RE::NiMatrix3 invRot = InverseRotationMatrix(m_scrollStartRotation);

    return RotatePoint(invRot, relative);
}

void ColumnGridProjectileDriver::UpdateScrollFromHand() {
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

    // For horizontal scrolling, we care about X movement
    // Moving hand left (negative X) scrolls content right (shows later items)
    // Moving hand right (positive X) scrolls content left (shows earlier items)
    // So we SUBTRACT the delta to get intuitive "drag" behavior
    float distance = -delta.x;

    // Guard against NaN/infinity
    if (!std::isfinite(distance)) {
        return;
    }

    // Accumulate scroll offset
    m_scrollOffset += distance * m_scrollSensitivity;
    ClampScrollOffset();

    // Update previous position for next frame
    m_scrollPrevLocalPos = handLocal;
}

// =============================================================================
// Event Handling
// =============================================================================

bool ColumnGridProjectileDriver::OnEvent(InputEvent& event) {
    // Handle non-anchor grabs for scrolling
    if (event.type == InputEventType::GrabStart) {
        auto* proj = dynamic_cast<ControlledProjectile*>(event.source);
        if (proj && !proj->IsAnchorHandle()) {
            // Non-anchor grab -> start scrolling (if scrollable)
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

void ColumnGridProjectileDriver::Update(float deltaTime) {
    // Track hand movement while scrolling
    if (m_isScrolling) {
        UpdateScrollFromHand();
    }

    // Call base class update (handles facing, UpdateLayout, child updates)
    ProjectileDriver::Update(deltaTime);
}

void ColumnGridProjectileDriver::UpdateLayout(float deltaTime) {
    // THREAD SAFETY: Copy children before iterating
    auto children = GetChildrenMutable();  // Copy, not reference
    size_t totalItems = children.size();

    if (totalItems == 0) {
        return;
    }

    // Ensure visibility tracking vector is correct size
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
    bool scrollingEnabled = CanScroll();

    // Compute X offset for positioning (horizontal scrolling)
    // This combines two independent concepts:
    // 1. Origin offset: shifts grid so the specified anchor point is at X=0
    // 2. Scroll offset: shifts content to reveal items during scrolling

    float totalWidth = ComputeTotalWidth();
    float horizontalOriginOffset = 0.0f;
    float scrollAdjustment = 0.0f;

    // Compute horizontal origin offset based on fill direction and origin setting
    if (m_horizontalFill == P3DUI::HorizontalFill::LeftToRight) {
        // Content spans from X=0 to X=-totalWidth (leftward in UI coords)
        // To center/anchor, we shift RIGHT (positive X) to bring the anchor point to X=0
        switch (m_horizontalOrigin) {
            case P3DUI::HorizontalOrigin::Left:
                horizontalOriginOffset = totalWidth;  // Shift right so leftmost item is at X=0
                break;
            case P3DUI::HorizontalOrigin::Center:
                horizontalOriginOffset = totalWidth / 2.0f;  // Shift right so center is at X=0
                break;
            case P3DUI::HorizontalOrigin::Right:
                horizontalOriginOffset = 0.0f;  // Rightmost (X=0) is already at origin
                break;
            default:
                break;
        }
        // Scrolling shifts content left (+X in UI coords) to reveal later columns
        if (scrollingEnabled) {
            scrollAdjustment = m_scrollOffset;
        }
    } else {
        // RightToLeft: Content spans from X=0 leftward (in player view, so UI's +X direction)
        switch (m_horizontalOrigin) {
            case P3DUI::HorizontalOrigin::Left:
                horizontalOriginOffset = totalWidth;  // Shift right so left edge is at X=0
                break;
            case P3DUI::HorizontalOrigin::Center:
                horizontalOriginOffset = totalWidth / 2.0f;  // Shift right so center is at X=0
                break;
            case P3DUI::HorizontalOrigin::Right:
                horizontalOriginOffset = 0.0f;  // Right is already at X=0
                break;
            default:
                break;
        }
        // Scrolling shifts content right (-X in UI coords) to reveal later columns
        if (scrollingEnabled) {
            scrollAdjustment = -m_scrollOffset;
        }
    }

    float centerOffset = horizontalOriginOffset + scrollAdjustment;

    // Compute vertical origin offset (no scrolling for column grid)
    float totalHeight = static_cast<float>(m_numRows - 1) * m_rowSpacing;
    float verticalOriginOffset = 0.0f;

    if (m_verticalFill == P3DUI::VerticalFill::TopToBottom) {
        // Content spans from Z=0 (row 0) down to Z=-totalHeight
        switch (m_verticalOrigin) {
            case P3DUI::VerticalOrigin::Top:
                verticalOriginOffset = 0.0f;
                break;
            case P3DUI::VerticalOrigin::Center:
                verticalOriginOffset = totalHeight / 2.0f;
                break;
            case P3DUI::VerticalOrigin::Bottom:
                verticalOriginOffset = totalHeight;
                break;
            default:
                break;
        }
    } else {
        // BottomToTop: Content spans from Z=0 (row 0) up to Z=+totalHeight
        switch (m_verticalOrigin) {
            case P3DUI::VerticalOrigin::Top:
                verticalOriginOffset = -totalHeight;
                break;
            case P3DUI::VerticalOrigin::Center:
                verticalOriginOffset = -totalHeight / 2.0f;
                break;
            case P3DUI::VerticalOrigin::Bottom:
                verticalOriginOffset = 0.0f;
                break;
            default:
                break;
        }
    }

    // One-time layout logging
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
        spdlog::trace("=== ColumnGridProjectileDriver Initial Layout Snapshot ===");
        spdlog::trace("Total items: {}, NumRows: {}, VisibleWidth: {}, ScrollingEnabled: {}",
            totalItems, m_numRows, m_visibleWidth, scrollingEnabled);
        float windowMin, windowMax;
        switch (m_horizontalOrigin) {
            case P3DUI::HorizontalOrigin::Left:
                windowMin = -m_visibleWidth; windowMax = 0.0f; break;
            case P3DUI::HorizontalOrigin::Center:
                windowMin = -m_visibleWidth / 2.0f; windowMax = m_visibleWidth / 2.0f; break;
            default: // Right
                windowMin = 0.0f; windowMax = m_visibleWidth; break;
        }
        spdlog::trace("ScrollOffset: {:.3f}, HorizOriginOffset: {:.3f}, VertOriginOffset: {:.3f}, Window: [{:.3f}, {:.3f}]",
            m_scrollOffset, horizontalOriginOffset, verticalOriginOffset, windowMin, windowMax);
    }

    for (size_t i = 0; i < totalItems; ++i) {
        auto& child = children[i];
        if (!child) continue;

        auto* proj = dynamic_cast<ControlledProjectile*>(child.get());
        bool isValidProjectile = proj && proj->IsValid();

        auto [column, row] = ComputeGridPosition(i);
        float baseX = ComputeBaseX(i);
        float baseZ = ComputeZ(row);

        // Apply origin and scroll offsets
        float displayedX = baseX + centerOffset;
        float displayedZ = baseZ + verticalOriginOffset;

        bool shouldBeVisible = IsXVisible(displayedX);
        RE::NiPoint3 localPos(displayedX, 0.0f, displayedZ);

        // One-time layout logging for each item
        if (shouldLogLayout && isValidProjectile) {
            const std::string itemText = proj->GetUUID().ToString();
            spdlog::trace("[{}] '{}' | col:{} row:{} baseX:{:.3f} dispX:{:.3f} baseZ:{:.3f} dispZ:{:.3f} | pos:({:.1f},{:.1f},{:.1f}) | visible:{}",
                i, itemText, column, row, baseX, displayedX, baseZ, displayedZ,
                localPos.x, localPos.y, localPos.z,
                shouldBeVisible ? "Y" : "N");
        }

        // Set visibility
        if (shouldBeVisible) {
            child->SetVisible(true);
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

        // Position updates only for valid projectiles
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
