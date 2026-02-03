#pragma once

#include "../ProjectileDriver.h"
#include <optional>
#include <utility>

namespace Projectile {

// Half-wheel (semicircle) arrangement with optional scrolling support.
// Similar to RadialProjectileDriver but only uses the upper half of each ring.
// Item 0 is at the center position - ALWAYS visible and unaffected by scrolling.
// Items 1+ are arranged in nested semicircles using a serpentine pattern for spatial locality.
// Ring 0: center (1 item at index 0)
// Ring N: semicircular arc at radius N * rowDistance, items evenly spaced from 0 to π
//
// === Scrolling Behavior ===
// When MaxRings is set and there are more items than fit, scrolling is enabled.
// User grabs a non-anchor item and moves hand left/right to rotate the "disk".
// Items rotate around the center axis (perpendicular to rings).
// Items that rotate past the visible semicircle edges become hidden.
// Default scroll position is fully left-aligned (scroll offset = 0).
class HalfWheelProjectileDriver : public ProjectileDriver {
public:
    HalfWheelProjectileDriver();

    // === Ring Configuration ===
    // Target distance between items within a ring (actual distance is adjusted to fill arc evenly)
    void SetItemSpacing(float spacing) { m_itemSpacing = spacing; }
    float GetItemSpacing() const { return m_itemSpacing; }

    // Distance between concentric rings
    void SetRowDistance(float distance) { m_rowDistance = distance; }
    float GetRowDistance() const { return m_rowDistance; }

    // Ring spacing (alias for row distance - space between rings)
    void SetRingSpacing(float spacing) { m_rowDistance = spacing; }
    float GetRingSpacing() const { return m_rowDistance; }

    // Distance from center to first ring (optional, defaults to m_rowDistance if unset)
    // When set, all rings shift outward: center <firstRingSpacing> ring1 <rowDistance> ring2 ...
    void SetFirstRingSpacing(float spacing) { m_firstRingSpacing = spacing; }
    float GetFirstRingSpacing() const { return m_firstRingSpacing.value_or(m_rowDistance); }
    bool HasFirstRingSpacing() const { return m_firstRingSpacing.has_value(); }
    void ClearFirstRingSpacing() { m_firstRingSpacing.reset(); }

    // === Scroll Configuration ===
    // Maximum number of visible rings (0 = unlimited, no scrolling)
    // When set, items beyond MaxRings are hidden and can be scrolled into view.
    void SetMaxRings(size_t maxRings) { m_maxRings = maxRings; }
    size_t GetMaxRings() const { return m_maxRings; }

    // Scroll sensitivity: how much rotation (radians) per unit of hand movement
    void SetScrollSensitivity(float sensitivity) { m_scrollSensitivity = sensitivity; }
    float GetScrollSensitivity() const { return m_scrollSensitivity; }

    // Legacy compatibility
    void SetSpacing(float spacing) { m_itemSpacing = spacing; }
    float GetSpacing() const { return m_itemSpacing; }

    // === Scroll State Query ===
    bool IsScrolling() const { return m_isScrolling; }
    float GetScrollOffset() const { return m_scrollOffset; }
    bool CanScroll() const;  // Returns true if there are more items than visible slots

    // === Visibility (override to handle scroll offset on child count changes) ===
    void SetVisible(bool visible) override;

protected:
    void UpdateLayout(float deltaTime) override;

    // Override to intercept non-anchor grabs for scrolling
    bool OnEvent(InputEvent& event) override;

    // Override Update to handle scroll tracking
    void Update(float deltaTime) override;

private:
    // === Layout Computation ===
    // Compute number of items that fit in a half-ring at the given radius
    size_t ComputeItemsInHalfRing(size_t ringIndex) const;

    // Compute the radius for a given ring index (accounts for FirstRingSpacing)
    float ComputeRingRadius(size_t ringIndex) const;

    // Compute ring and angle together using racing algorithm
    // Racing keeps all rings progressing at similar rates for clean leading edge
    // Returns {ringIndex (1-based for rings, 0 for center), baseAngle}
    std::pair<size_t, float> ComputeRacingPosition(size_t itemIndex) const;

    // Convenience wrappers that use racing algorithm
    float ComputeBaseAngle(size_t itemIndex) const;
    size_t ComputeRingForItem(size_t itemIndex) const;

    // Compute static (non-scrolling) position: fills rings sequentially from inner to outer
    // Returns {ringIndex (1-based for rings, 0 for center), angle}
    // If the outermost ring with items is partial, items are centered around π/2 (top)
    std::pair<size_t, float> ComputeStaticPosition(size_t itemIndex, size_t totalItems) const;

    // Compute total items that fit in MaxRings (including center)
    size_t ComputeMaxVisibleItems() const;

    // Compute the local position for a projectile given its base angle and ring
    RE::NiPoint3 ComputePositionFromAngle(float angle, size_t ringIndex) const;

    // Legacy: Compute position by visible index (still used internally)
    RE::NiPoint3 ComputeHalfRingLocalPosition(size_t index) const;

    // === Scroll Handling ===
    void StartScrolling(bool isLeftHand);
    void EndScrolling();
    void UpdateScrollFromHand();

    // Transform world position to local coordinates (relative to driver, inverse rotation)
    RE::NiPoint3 WorldToLocal(const RE::NiPoint3& worldPos) const;

    // Transform using fixed rotation captured at scroll start (prevents direction flips)
    RE::NiPoint3 WorldToLocalFixedFrame(const RE::NiPoint3& worldPos) const;

    // Compute scroll bounds based on total items vs visible capacity
    float ComputeMaxScrollOffset() const;

    // Clamp scroll offset to valid range (with overscroll resistance)
    void ClampScrollOffset();

    // Check if an angle (after scroll offset) is within visible range [0, π]
    bool IsAngleVisible(float angle) const;

    static constexpr float PI = 3.14159265359f;
    static constexpr float TWO_PI = 2.0f * PI;

    // === Ring Configuration ===
    float m_itemSpacing = 15.0f;                // Target distance between items in a ring
    float m_rowDistance = 15.0f;                // Distance between concentric rings
    std::optional<float> m_firstRingSpacing;    // Optional: center to ring 1 distance

    // === Scroll Configuration ===
    size_t m_maxRings = 2;                      // 0 = unlimited (no scrolling)
    float m_scrollSensitivity = 0.045f;         // Radians per unit of hand movement

    // === Scroll State ===
    float m_scrollOffset = 0.0f;                // Current angular offset (radians)
    bool m_isScrolling = false;                 // Currently being scrolled by user
    bool m_scrollHandIsLeft = false;            // Which hand initiated the scroll
    RE::NiPoint3 m_scrollPrevLocalPos;          // Previous frame's hand position (for per-frame delta)
    RE::NiMatrix3 m_scrollStartRotation;        // Driver rotation when scroll started (fixed reference frame)

    // === Visibility Tracking ===
    // Track previous visibility to only call SetVisible on changes
    std::vector<bool> m_previousVisibility;

    // === Debug ===
    bool m_hasLoggedInitialLayout = false;  // One-time layout snapshot logging
};

} // namespace Projectile
