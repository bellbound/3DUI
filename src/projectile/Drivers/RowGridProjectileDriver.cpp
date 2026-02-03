#include "RowGridProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../util/VRNodes.h"
#include "../../log.h"
#include <cmath>
#include <algorithm>

namespace Projectile {

RowGridProjectileDriver::RowGridProjectileDriver() {
    // Default to smooth transitions for grid layout
    SetTransitionMode(TransitionMode::Lerp);
    // Note: FacingStrategy is NOT set here - parent driver handles facing
    // via scene graph rotation inheritance. Only root drivers should set facing.
}

void RowGridProjectileDriver::SetVisible(bool visible) {
    bool wasVisible = IsVisible();

    // When becoming visible, check if scroll offset is still valid
    // (children may have been added/removed while hidden)
    if (visible && !wasVisible) {
        float maxOffset = ComputeMaxScrollOffset();
        if (m_scrollOffset > maxOffset) {
            spdlog::info("RowGridProjectileDriver::SetVisible - Scroll offset {:.3f} exceeds max {:.3f}, resetting to 0",
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

void RowGridProjectileDriver::SetFillDirection(P3DUI::VerticalFill verticalFill, P3DUI::HorizontalFill horizontalFill) {
    m_verticalFill = verticalFill;
    m_horizontalFill = horizontalFill;
}

// =============================================================================
// Origin (Anchor) Configuration
// =============================================================================

void RowGridProjectileDriver::SetOrigin(P3DUI::VerticalOrigin verticalOrigin, P3DUI::HorizontalOrigin horizontalOrigin) {
    m_verticalOrigin = verticalOrigin;
    m_horizontalOrigin = horizontalOrigin;
}

// =============================================================================
// Grid Position Computation (Row-Major)
// =============================================================================

std::pair<size_t, size_t> RowGridProjectileDriver::ComputeGridPosition(size_t itemIndex) const {
    // Row-major: fill left-to-right in each row, then move to next row
    // item 0 -> (col 0, row 0), item 1 -> (col 1, row 0), ...
    // m_numColumns defines how many items per row before wrapping to next row
    size_t row = itemIndex / m_numColumns;
    size_t column = itemIndex % m_numColumns;
    return {column, row};
}

float RowGridProjectileDriver::ComputeX(size_t column) const {
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

float RowGridProjectileDriver::ComputeBaseZ(size_t itemIndex) const {
    auto [column, row] = ComputeGridPosition(itemIndex);

    // Rows are spaced by m_rowSpacing
    // VerticalFill determines which direction rows stack:
    // - TopToBottom: Row 0 at top (Z=0), subsequent rows go downward (negative Z)
    // - BottomToTop: Row 0 at bottom (Z=0), subsequent rows go upward (positive Z)
    float z;

    if (m_verticalFill == P3DUI::VerticalFill::TopToBottom) {
        // TopToBottom: Row 0 at highest Z (0), higher rows are below (negative Z)
        z = -static_cast<float>(row) * m_rowSpacing;
    } else {
        // BottomToTop: Row 0 at lowest Z (0), higher rows are above (positive Z)
        z = static_cast<float>(row) * m_rowSpacing;
    }

    return z;
}

RE::NiPoint3 RowGridProjectileDriver::ComputeLocalPosition(size_t column, float displayedZ) const {
    float x = ComputeX(column);
    return RE::NiPoint3(x, 0.0f, displayedZ);
}

size_t RowGridProjectileDriver::ComputeVisibleRows() const {
    if (m_rowSpacing <= 0.0f) {
        return 1;
    }
    // Number of rows that fit in the visible height
    // Add 1 to ensure we don't cut off rows at the edge
    return static_cast<size_t>(m_visibleHeight / m_rowSpacing) + 1;
}

float RowGridProjectileDriver::ComputeTotalHeight() const {
    size_t totalItems = GetChildren().size();
    if (totalItems == 0) {
        return 0.0f;
    }

    // Total rows needed (row-major)
    size_t totalRows = (totalItems + m_numColumns - 1) / m_numColumns;
    if (totalRows == 0) {
        return 0.0f;
    }

    // Height spans from row 0 to row (totalRows-1)
    return static_cast<float>(totalRows - 1) * m_rowSpacing;
}

// =============================================================================
// Scroll Bounds
// =============================================================================

bool RowGridProjectileDriver::CanScroll() const {
    float totalHeight = ComputeTotalHeight();
    return totalHeight > m_visibleHeight;
}

float RowGridProjectileDriver::ComputeMaxScrollOffset() const {
    if (!CanScroll()) {
        return 0.0f;
    }

    float totalHeight = ComputeTotalHeight();
    // Maximum scroll is when the last row is at the edge of visible area
    float maxOffset = totalHeight - m_visibleHeight;

    // Minimum is 0 (first row at edge)
    return (std::max)(0.0f, maxOffset);
}

void RowGridProjectileDriver::ClampScrollOffset() {
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

void RowGridProjectileDriver::SetScrollOffset(float offset) {
    m_scrollOffset = offset;
    ClampScrollOffset();
    // Layout will be recomputed on next UpdateLayout call
}

bool RowGridProjectileDriver::IsZVisible(float displayedZ) const {
    // Visible range depends on VerticalOrigin anchor:
    // - Bottom: anchor at Z=0, visible area extends upward [0, visibleHeight]
    // - Center: anchor at Z=0, visible area is centered [-visibleHeight/2, visibleHeight/2]
    // - Top: anchor at Z=0, visible area extends downward [-visibleHeight, 0]
    switch (m_verticalOrigin) {
        case P3DUI::VerticalOrigin::Bottom:
            return displayedZ >= 0.0f && displayedZ <= m_visibleHeight;
        case P3DUI::VerticalOrigin::Center:
            return displayedZ >= -m_visibleHeight / 2.0f && displayedZ <= m_visibleHeight / 2.0f;
        case P3DUI::VerticalOrigin::Top:
        default:
            return displayedZ >= -m_visibleHeight && displayedZ <= 0.0f;
    }
}

// =============================================================================
// Scroll State Management
// =============================================================================

void RowGridProjectileDriver::StartScrolling(bool isLeftHand) {
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

    spdlog::info("RowGridProjectileDriver: Started scrolling (hand: {})",
        isLeftHand ? "left" : "right");
}

void RowGridProjectileDriver::EndScrolling() {
    if (!m_isScrolling) {
        return;
    }

    m_isScrolling = false;

    // Snap to nearest row position if within threshold
    constexpr float SNAP_THRESHOLD = 3.0f;  // Game units

    float maxOffset = ComputeMaxScrollOffset();

    // Find nearest row position
    float nearestIdeal = 0.0f;
    float minDistance = std::abs(m_scrollOffset);  // Distance to offset 0

    // Check distance to maxOffset
    float distToMax = std::abs(m_scrollOffset - maxOffset);
    if (distToMax < minDistance) {
        minDistance = distToMax;
        nearestIdeal = maxOffset;
    }

    // Check all row positions as ideal snap points
    size_t totalItems = GetChildren().size();
    size_t totalRows = (totalItems + m_numColumns - 1) / m_numColumns;
    for (size_t row = 0; row < totalRows; ++row) {
        float rowOffset = static_cast<float>(row) * m_rowSpacing;
        if (rowOffset >= 0.0f && rowOffset <= maxOffset) {
            float distance = std::abs(m_scrollOffset - rowOffset);
            if (distance < minDistance) {
                minDistance = distance;
                nearestIdeal = rowOffset;
            }
        }
    }

    // Snap if within threshold
    if (minDistance <= SNAP_THRESHOLD) {
        spdlog::info("RowGridProjectileDriver: Snapping from {:.3f} to ideal {:.3f} (dist={:.3f})",
            m_scrollOffset, nearestIdeal, minDistance);
        m_scrollOffset = nearestIdeal;
    }

    spdlog::info("RowGridProjectileDriver: Ended scrolling at offset {:.3f}",
        m_scrollOffset);
}

RE::NiPoint3 RowGridProjectileDriver::WorldToLocal(const RE::NiPoint3& worldPos) const {
    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    RE::NiMatrix3 worldRot = GetWorldRotation();
    RE::NiMatrix3 invRot = InverseRotationMatrix(worldRot);

    return RotatePoint(invRot, relative);
}

RE::NiPoint3 RowGridProjectileDriver::WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const {
    RE::NiPoint3 driverWorldPos = GetWorldPosition();
    RE::NiPoint3 relative = worldPos - driverWorldPos;

    RE::NiMatrix3 invRot = InverseRotationMatrix(m_scrollStartRotation);

    return RotatePoint(invRot, relative);
}

void RowGridProjectileDriver::UpdateScrollFromHand() {
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

    // For vertical scrolling, we care about Z movement
    // Moving hand up (positive Z) scrolls content down (shows later rows)
    // Moving hand down (negative Z) scrolls content up (shows earlier rows)
    // So we SUBTRACT the delta to get intuitive "drag" behavior
    float distance = -delta.z;

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

bool RowGridProjectileDriver::OnEvent(InputEvent& event) {
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

void RowGridProjectileDriver::Update(float deltaTime) {
    // Track hand movement while scrolling
    if (m_isScrolling) {
        UpdateScrollFromHand();
    }

    // Call base class update (handles facing, UpdateLayout, child updates)
    ProjectileDriver::Update(deltaTime);
}

void RowGridProjectileDriver::UpdateLayout(float deltaTime) {
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

    // Compute Z offset for positioning
    // This combines two independent concepts:
    // 1. Origin offset: shifts grid so the specified anchor point is at Z=0
    // 2. Scroll offset: shifts content to reveal items during scrolling

    float totalHeight = ComputeTotalHeight();
    float originOffset = 0.0f;
    float scrollAdjustment = 0.0f;

    // Compute origin offset based on fill direction and origin setting
    if (m_verticalFill == P3DUI::VerticalFill::TopToBottom) {
        // Content spans from Z=0 (row 0) down to Z=-totalHeight
        switch (m_verticalOrigin) {
            case P3DUI::VerticalOrigin::Top:
                originOffset = 0.0f;  // Top is already at Z=0
                break;
            case P3DUI::VerticalOrigin::Center:
                originOffset = totalHeight / 2.0f;  // Shift up so center is at Z=0
                break;
            case P3DUI::VerticalOrigin::Bottom:
                originOffset = totalHeight;  // Shift up so bottom is at Z=0
                break;
            default:
                break;
        }
        // Scrolling shifts content up (+Z) to reveal later rows
        if (scrollingEnabled) {
            scrollAdjustment = m_scrollOffset;
        }
    } else {
        // BottomToTop: Content spans from Z=0 (row 0) up to Z=+totalHeight
        switch (m_verticalOrigin) {
            case P3DUI::VerticalOrigin::Top:
                originOffset = -totalHeight;  // Shift down so top is at Z=0
                break;
            case P3DUI::VerticalOrigin::Center:
                originOffset = -totalHeight / 2.0f;  // Shift down so center is at Z=0
                break;
            case P3DUI::VerticalOrigin::Bottom:
                originOffset = 0.0f;  // Bottom is already at Z=0
                break;
            default:
                break;
        }
        // Scrolling shifts content down (-Z) to reveal later rows
        if (scrollingEnabled) {
            scrollAdjustment = -m_scrollOffset;
        }
    }

    float centerOffset = originOffset + scrollAdjustment;

    // Compute horizontal origin offset based on ACTUAL content width, not configured columns
    // This ensures proper centering when the grid isn't fully filled
    size_t columnsUsed;
    if (totalItems == 0) {
        columnsUsed = 0;
    } else if (totalItems >= m_numColumns) {
        columnsUsed = m_numColumns;  // Full width - all columns used (multi-row)
    } else {
        columnsUsed = totalItems;    // Partial first row - only these columns exist
    }
    float totalWidth = (columnsUsed > 0) ? static_cast<float>(columnsUsed - 1) * m_columnSpacing : 0.0f;
    float horizontalOriginOffset = 0.0f;

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
        spdlog::trace("=== RowGridProjectileDriver Initial Layout Snapshot ===");
        spdlog::trace("Total items: {}, NumColumns: {}, VisibleHeight: {}, ScrollingEnabled: {}",
            totalItems, m_numColumns, m_visibleHeight, scrollingEnabled);
        float windowMin, windowMax;
        switch (m_verticalOrigin) {
            case P3DUI::VerticalOrigin::Bottom:
                windowMin = 0.0f; windowMax = m_visibleHeight; break;
            case P3DUI::VerticalOrigin::Center:
                windowMin = -m_visibleHeight / 2.0f; windowMax = m_visibleHeight / 2.0f; break;
            default: // Top
                windowMin = -m_visibleHeight; windowMax = 0.0f; break;
        }
        spdlog::trace("ScrollOffset: {:.3f}, OriginOffset: {:.3f}, HorizOriginOffset: {:.3f}, Window: [{:.3f}, {:.3f}]",
            m_scrollOffset, originOffset, horizontalOriginOffset, windowMin, windowMax);
    }

    for (size_t i = 0; i < totalItems; ++i) {
        auto& child = children[i];
        if (!child) continue;

        auto* proj = dynamic_cast<ControlledProjectile*>(child.get());
        bool isValidProjectile = proj && proj->IsValid();

        auto [column, row] = ComputeGridPosition(i);
        float baseZ = ComputeBaseZ(i);
        float baseX = ComputeX(column);

        // Apply origin and scroll offsets
        float displayedZ = baseZ + centerOffset;
        float displayedX = baseX + horizontalOriginOffset;

        bool shouldBeVisible = IsZVisible(displayedZ);
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
