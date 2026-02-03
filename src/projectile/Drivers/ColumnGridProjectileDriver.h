#pragma once

#include "../ProjectileDriver.h"
#include "../../ThreeDUIInterface001.h"  // For VerticalStart, HorizontalStart enums

namespace Projectile {

// Column-major grid arrangement with horizontal scrolling.
// Items are arranged in columns: fill top-to-bottom, then move to next column.
//   Column 0: rows 0, 1, 2, ... then Column 1: rows 0, 1, 2, ...
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
// When there are more columns than fit in the visible width, horizontal scrolling is enabled.
// User grabs a non-anchor item and moves hand left/right to scroll the content.
//
// === Coordinate System ===
// +X = right (scroll direction)
// +Y = forward (into screen)
// +Z = up
// Rows are stacked vertically along Z axis
class ColumnGridProjectileDriver : public ProjectileDriver {
public:
    ColumnGridProjectileDriver();

    // === Layout Configuration ===

    // Distance between columns in the X direction (horizontal spacing)
    void SetColumnSpacing(float spacing) { m_columnSpacing = spacing; }
    float GetColumnSpacing() const { return m_columnSpacing; }

    // Distance between rows in the Z direction (vertical spacing)
    void SetRowSpacing(float spacing) { m_rowSpacing = spacing; }
    float GetRowSpacing() const { return m_rowSpacing; }

    // Number of rows per column (default 1)
    // Items wrap to next column after this many rows
    void SetNumRows(size_t numRows) { m_numRows = std::max<size_t>(1, numRows); }
    size_t GetNumRows() const { return m_numRows; }

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

    // Visible width in game units (default 50)
    // Items within [-width/2, +width/2] are visible
    void SetVisibleWidth(float width) { m_visibleWidth = width; }
    float GetVisibleWidth() const { return m_visibleWidth; }

    // Scroll sensitivity: how much X offset per unit of hand movement
    void SetScrollSensitivity(float sensitivity) { m_scrollSensitivity = sensitivity; }
    float GetScrollSensitivity() const { return m_scrollSensitivity; }

    // === Scroll State Query ===
    bool IsScrolling() const { return m_isScrolling; }
    float GetScrollOffset() const { return m_scrollOffset; }
    bool CanScroll() const;  // Returns true if there are more columns than fit in visible width

    // === Scroll Control ===
    // Set the scroll offset directly (clamped to valid range)
    void SetScrollOffset(float offset);

    // Get maximum valid scroll offset (used for normalized position calculations)
    float GetMaxScrollOffset() const { return ComputeMaxScrollOffset(); }

    // === Visibility (override to handle scroll offset on child count changes) ===
    void SetVisible(bool visible) override;

    // === Legacy Compatibility ===
    // Alias for consistency with other drivers that use item-based naming
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

    // Compute the (column, row) for a given item index (column-major)
    // item 0 -> (col 0, row 0), item 1 -> (col 0, row 1), ...
    std::pair<size_t, size_t> ComputeGridPosition(size_t itemIndex) const;

    // Compute the base X position for an item (before scroll offset)
    float ComputeBaseX(size_t itemIndex) const;

    // Compute the Z position for an item (based on row index)
    float ComputeZ(size_t row) const;

    // Compute local position for an item given its displayed X and row
    RE::NiPoint3 ComputeLocalPosition(float displayedX, size_t row) const;

    // Compute how many columns fit in the visible width
    size_t ComputeVisibleColumns() const;

    // Compute total width of all columns
    float ComputeTotalWidth() const;

    // === Scroll Handling ===
    void StartScrolling(bool isLeftHand);
    void EndScrolling();
    void UpdateScrollFromHand();

    // Transform world position to local coordinates (relative to driver, inverse rotation)
    RE::NiPoint3 WorldToLocal(const RE::NiPoint3& worldPos) const;

    // Transform using fixed rotation captured at scroll start (prevents direction flips)
    RE::NiPoint3 WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const;

    // Compute scroll bounds based on total columns vs visible capacity
    float ComputeMaxScrollOffset() const;

    // Clamp scroll offset to valid range
    void ClampScrollOffset();

    // Check if an X position (after scroll offset) is within visible range
    bool IsXVisible(float displayedX) const;

    // === Layout Configuration ===
    float m_columnSpacing = 15.0f;    // Horizontal distance between columns
    float m_rowSpacing = 12.0f;       // Vertical distance between rows
    size_t m_numRows = 1;             // Number of rows per column
    float m_visibleWidth = 50.0f;     // Width of visible area

    // === Fill Direction Configuration ===
    P3DUI::VerticalFill m_verticalFill = P3DUI::VerticalFill::TopToBottom;     // Default: fill rows downward
    P3DUI::HorizontalFill m_horizontalFill = P3DUI::HorizontalFill::LeftToRight; // Default: fill columns rightward

    // === Origin (Anchor) Configuration ===
    P3DUI::VerticalOrigin m_verticalOrigin = P3DUI::VerticalOrigin::Top;       // Default: top edge at Z=0
    P3DUI::HorizontalOrigin m_horizontalOrigin = P3DUI::HorizontalOrigin::Left; // Default: left edge at X=0

    // === Scroll Configuration ===
    float m_scrollSensitivity = 1.0f; // Units per unit of hand movement

    // === Scroll State ===
    float m_scrollOffset = 0.0f;      // Current X offset
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
