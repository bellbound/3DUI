#include "RadialProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../log.h"
#include <cmath>

namespace Projectile {

RadialProjectileDriver::RadialProjectileDriver() {
    // Default to smooth transitions for ring layout
    SetTransitionMode(TransitionMode::Lerp);
    // Note: FacingStrategy is NOT set here - parent driver handles facing
    // via scene graph rotation inheritance. Only root drivers should set facing.
}

size_t RadialProjectileDriver::ComputeItemsInRing(size_t ringIndex) const {
    if (ringIndex == 0) {
        return 1;  // Center ring always has 1 item
    }

    float radius = static_cast<float>(ringIndex) * m_rowDistance;
    float circumference = TWO_PI * radius;

    // Calculate how many items fit, rounding to nearest integer (minimum 1)
    size_t count = static_cast<size_t>(std::round(circumference / m_itemSpacing));
    return std::max<size_t>(1, count);
}

// Note: ComputeRotatedLocalPosition and ComputeRingWorldPosition removed
// Scene graph now handles rotation - children inherit parent's world rotation
// via GetWorldPosition() which applies parent rotation to local positions

void RadialProjectileDriver::UpdateLayout(float deltaTime) {
    // THREAD SAFETY: Copy children before iterating - main thread may modify while VR thread updates
    auto children = GetChildrenMutable();  // Copy, not reference

    // First pass: count visible items for even distribution calculation
    m_visibleItemCount = 0;
    for (const auto& child : children) {
        if (child && child->IsVisible()) {
            ++m_visibleItemCount;
        }
    }

    // Second pass: position items
    size_t validIndex = 0;
    for (auto& child : children) {
        if (!child || !child->IsVisible()) continue;

        // Set local position in X-Z plane (scene graph applies parent rotation automatically)
        child->SetLocalPosition(ComputeRingLocalPosition(validIndex));
        // Scale uses baseScale from projectile (applied in GetWorldScale)

        ++validIndex;
    }
}

float RadialProjectileDriver::GetStartAngle(size_t count) const {
    // 2 items: start from left (π) for horizontal left/right arrangement
    // 3+ items: start from top (π/2) for triangle/polygon with vertex at top
    if (count == 2) {
        return TWO_PI / 2.0f;  // π = 180° = left
    }
    return HALF_PI;  // π/2 = 90° = top
}

RE::NiPoint3 RadialProjectileDriver::ComputeRingLocalPosition(size_t index) const {
    // Concentric ring positioning with even distribution for sparse wheels
    // When items don't fill ring 1, spread them evenly around the circle
    // Skyrim coordinates: +X=right, +Y=forward, +Z=up
    // Layout in X-Z plane (vertical wall facing player, Y=0)

    size_t totalItems = m_visibleItemCount;
    size_t ring1Capacity = ComputeItemsInRing(1);

    // Single item stays at center
    if (totalItems <= 1) {
        return RE::NiPoint3(0, 0, 0);
    }

    // Even distribution mode: when items fit in ring 1, spread evenly
    // Item 0 is always at center, remaining items distributed around ring 1
    // This gives intuitive layouts:
    //   2 items: center + right
    //   3 items: center + left/right
    //   4 items: center + triangle with point at top
    if (totalItems <= ring1Capacity + 1) {  // +1 because item 0 is at center
        // Item 0 goes to center
        if (index == 0) {
            return RE::NiPoint3(0, 0, 0);
        }

        // Remaining items distributed around ring 1
        size_t outerItems = totalItems - 1;  // Items excluding center
        size_t outerIndex = index - 1;       // Position among outer items

        float radius = m_rowDistance;  // Use ring 1 radius
        float startAngle = GetStartAngle(outerItems);

        // Clockwise distribution from start angle
        float angle = startAngle - (static_cast<float>(outerIndex) / static_cast<float>(outerItems)) * TWO_PI;

        float localX = radius * std::cos(angle);
        float localY = 0.0f;
        float localZ = radius * std::sin(angle);

        return RE::NiPoint3(localX, localY, localZ);
    }

    // Standard concentric ring mode for when items overflow ring 1
    // Ring 0: center (1 item)
    // Ring N: at radius N * rowDistance, items evenly spaced around circumference

    if (index == 0) {
        // Center position
        return RE::NiPoint3(0, 0, 0);
    }

    // Find which ring this index belongs to
    size_t cumulativeItems = 1;  // Ring 0 has 1 item
    size_t ringIndex = 1;

    while (true) {
        size_t itemsInThisRing = ComputeItemsInRing(ringIndex);

        if (index < cumulativeItems + itemsInThisRing) {
            // Found the ring - compute position within it
            size_t positionInRing = index - cumulativeItems;

            float radius = static_cast<float>(ringIndex) * m_rowDistance;

            // Evenly distribute items around the ring
            float angle = (static_cast<float>(positionInRing) / static_cast<float>(itemsInThisRing)) * TWO_PI;

            float localX = radius * std::cos(angle);
            float localY = 0.0f;  // Flat surface facing forward (+Y direction)
            float localZ = radius * std::sin(angle);

            return RE::NiPoint3(localX, localY, localZ);
        }

        cumulativeItems += itemsInThisRing;
        ++ringIndex;
    }
}

} // namespace Projectile
