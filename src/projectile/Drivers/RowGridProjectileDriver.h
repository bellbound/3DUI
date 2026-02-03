#pragma once

#include "../ProjectileDriver.h"
#include "../../ThreeDUIInterface001.h"  // For VerticalStart, HorizontalStart enums

namespace Projectile {

// Row-major grid arrangement with vertical scrolling.
// Items are arranged in rows: fill left-to-right, then move to next row.
//   Row 0: cols 0, 1, 2, ... then Row 1: cols 0, 1, 2, ...
//
// === Configuration (DECOUPLED) ===
// SetFillDirection() controls item ordering:
// - VerticalFill: TopToBottom or BottomToTop (which way rows stack)
// - HorizontalFill: LeftToRight or RightToLeft (which way columns fill)
//
// SetOrigin() controls anchor point (where local 0,0,0 is):
// - VerticalOrigin: Top/Center/Bottom
// - HorizontalOrigin: Left/Center/Right
//
// These are independent! You can fill top-to-bottom but anchor at center.
//
// === Scrolling ===
// When there are more rows than fit in the visible height, vertical scrolling is enabled.
// User grabs a non-anchor item and moves hand up/down to scroll the content.
//
// === Coordinate System ===
// +X = right
// +Y = forward (into screen)
// +Z = up (scroll direction)
// Columns are arranged horizontally along X axis
class RowGridProjectileDriver : public ProjectileDriver {
public:
    RowGridProjectileDriver();

    // === Layout Configuration ===

    // Distance between columns in the X direction (horizontal spacing)
    void SetColumnSpacing(float spacing) { m_columnSpacing = spacing; }
    float GetColumnSpacing() const { return m_columnSpacing; }

    // Distance between rows in the Z direction (vertical spacing)
    void SetRowSpacing(float spacing) { m_rowSpacing = spacing; }
    float GetRowSpacing() const { return m_rowSpacing; }

    // Number of columns per row (default 1)
    // Items wrap to next row after this many columns
    void SetNumColumns(size_t numColumns) { m_numColumns = std::max<size_t>(1, numColumns); }
    size_t GetNumColumns() const { return m_numColumns; }

    // === Fill Direction Configuration ===
    // Controls the order in which items are laid out in the grid.
    // - verticalFill: TopToBottom fills rows downward, BottomToTop fills upward
    // - horizontalFill: LeftToRight fills columns rightward, RightToLeft fills leftward
    void SetFillDirection(P3DUI::VerticalFill verticalFill, P3DUI::HorizontalFill horizontalFill);
    P3DUI::VerticalFill GetVerticalFill() const { return m_verticalFill; }
    P3DUI::HorizontalFill GetHorizontalFill() const { return m_horizontalFill; }

    // === Origin (Anchor) Configuration ===
    // Controls where the grid's local (0,0,0) point is relative to its content.
    // This is INDEPENDENT from fill direction.
    // - verticalOrigin: Top/Center/Bottom - which vertical position is at Z=0
    // - horizontalOrigin: Left/Center/Right - which horizontal position is at X=0
    // Example: SetOrigin(Center, Center) centers the grid at the driver's position
    void SetOrigin(P3DUI::VerticalOrigin verticalOrigin, P3DUI::HorizontalOrigin horizontalOrigin);
    P3DUI::VerticalOrigin GetVerticalOrigin() const { return m_verticalOrigin; }
    P3DUI::HorizontalOrigin GetHorizontalOrigin() const { return m_horizontalOrigin; }

    // === Scroll Configuration ===

    // Visible height in game units (default 50)
    // Items within [-height/2, +height/2] are visible
    void SetVisibleHeight(float height) { m_visibleHeight = height; }
    float GetVisibleHeight() const { return m_visibleHeight; }

    // Scroll sensitivity: how much Z offset per unit of hand movement
    void SetScrollSensitivity(float sensitivity) { m_scrollSensitivity = sensitivity; }
    float GetScrollSensitivity() const { return m_scrollSensitivity; }

    // === Scroll State Query ===
    bool IsScrolling() const { return m_isScrolling; }
    float GetScrollOffset() const { return m_scrollOffset; }
    bool CanScroll() const;  // Returns true if there are more rows than fit in visible height

    // === Scroll Control ===
    // Set the scroll offset directly (clamped to valid range)
    void SetScrollOffset(float offset);

    // Get maximum valid scroll offset (used for normalized position calculations)
    float GetMaxScrollOffset() const { return ComputeMaxScrollOffset(); }

    // === Visibility (override to handle scroll offset on child count changes) ===
    void SetVisible(bool visible) override;

    // === Legacy Compatibility (for wrapper code) ===
    void SetItemSpacing(float spacing) { SetColumnSpacing(spacing); }
    float GetItemSpacing() const { return GetColumnSpacing(); }

protected:
    void UpdateLayout(float deltaTime) override;

    // Override to intercept non-anchor grabs for scrolling
    bool OnEvent(InputEvent& event) override;

    // Override Update to handle scroll tracking
    void Update(float deltaTime) override;

private:
    // === Layout Computation ===

    // Compute the (column, row) for a given item index (row-major)
    // item 0 -> (col 0, row 0), item 1 -> (col 1, row 0), ...
    std::pair<size_t, size_t> ComputeGridPosition(size_t itemIndex) const;

    // Compute the X position for an item (based on column index)
    float ComputeX(size_t column) const;

    // Compute the base Z position for an item (before scroll offset)
    float ComputeBaseZ(size_t itemIndex) const;

    // Compute local position for an item given its column and displayed Z
    RE::NiPoint3 ComputeLocalPosition(size_t column, float displayedZ) const;

    // Compute how many rows fit in the visible height
    size_t ComputeVisibleRows() const;

    // Compute total height of all rows
    float ComputeTotalHeight() const;

    // === Scroll Handling ===
    void StartScrolling(bool isLeftHand);
    void EndScrolling();
    void UpdateScrollFromHand();

    // Transform world position to local coordinates (relative to driver, inverse rotation)
    RE::NiPoint3 WorldToLocal(const RE::NiPoint3& worldPos) const;

    // Transform using fixed rotation captured at scroll start (prevents direction flips)
    RE::NiPoint3 WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const;

    // Compute scroll bounds based on total rows vs visible capacity
    float ComputeMaxScrollOffset() const;

    // Clamp scroll offset to valid range
    void ClampScrollOffset();

    // Check if a Z position (after scroll offset) is within visible range
    bool IsZVisible(float displayedZ) const;

    // === Layout Configuration ===
    float m_columnSpacing = 15.0f;    // Horizontal distance between columns
    float m_rowSpacing = 12.0f;       // Vertical distance between rows
    size_t m_numColumns = 1;          // Number of columns per row
    float m_visibleHeight = 50.0f;    // Height of visible area

    // === Fill Direction Configuration ===
    P3DUI::VerticalFill m_verticalFill = P3DUI::VerticalFill::TopToBottom;     // Default: fill rows downward
    P3DUI::HorizontalFill m_horizontalFill = P3DUI::HorizontalFill::LeftToRight; // Default: fill columns rightward

    // === Origin (Anchor) Configuration ===
    P3DUI::VerticalOrigin m_verticalOrigin = P3DUI::VerticalOrigin::Top;       // Default: top edge at Z=0
    P3DUI::HorizontalOrigin m_horizontalOrigin = P3DUI::HorizontalOrigin::Left; // Default: left edge at X=0

    // === Scroll Configuration ===
    float m_scrollSensitivity = 1.0f; // Units per unit of hand movement

    // === Scroll State ===
    float m_scrollOffset = 0.0f;      // Current Z offset
    bool m_isScrolling = false;       // Currently being scrolled by user
    bool m_scrollHandIsLeft = false;  // Which hand initiated the scroll
    RE::NiPoint3 m_scrollPrevLocalPos;     // Previous frame's hand position
    RE::NiMatrix3 m_scrollStartRotation;   // Driver rotation when scroll started

    // === Visibility Tracking ===
    std::vector<bool> m_previousVisibility;

    // === Debug ===
    bool m_hasLoggedInitialLayout = false;
};

} // namespace Projectile
