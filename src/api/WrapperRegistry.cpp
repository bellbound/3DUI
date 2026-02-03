#include "WrapperTypes.h"
#include "../log.h"

namespace P3DUI {

// =============================================================================
// WrapperRegistry::Destroy
// =============================================================================
// Removes a wrapper from the registry and cleans up associated resources.
// This is called when a container Clear()s its children.
//
// The wrapper classes use the "tombstone" pattern:
// 1. MarkDestroyed() is called first (invalidates the wrapper)
// 2. Destroy() removes it from the registry (releases memory)
//
// This two-phase approach prevents issues where a consumer holds a stale
// pointer that gets reused - the tombstone ensures any calls return safely.

void WrapperRegistry::Destroy(Positionable* wrapper) {
    if (!wrapper) return;

    const char* id = wrapper->GetID();
    if (!id || !*id) return;

    std::string idStr(id);

    // Determine type for logging and get impl pointer for unmapping
    const char* typeStr = "unknown";
    Projectile::IPositionable* impl = nullptr;

    if (auto* elem = dynamic_cast<ElementWrapper*>(wrapper)) {
        impl = elem->GetImpl().get();
        typeStr = "Element";
    } else if (auto* text = dynamic_cast<TextWrapper*>(wrapper)) {
        impl = text->GetImpl().get();
        typeStr = "Text";
    } else if (auto* scrollWheel = dynamic_cast<ScrollWheelWrapper*>(wrapper)) {
        impl = scrollWheel->GetImpl().get();
        typeStr = "ScrollWheel";
    } else if (auto* wheel = dynamic_cast<WheelWrapper*>(wrapper)) {
        impl = wheel->GetImpl().get();
        typeStr = "Wheel";
    } else if (auto* colGrid = dynamic_cast<ColumnGridWrapper*>(wrapper)) {
        impl = colGrid->GetImpl().get();
        typeStr = "ColumnGrid";
    } else if (auto* rowGrid = dynamic_cast<RowGridWrapper*>(wrapper)) {
        impl = rowGrid->GetImpl().get();
        typeStr = "RowGrid";
    }

    spdlog::trace("[Registry] Destroying {} '{}'", typeStr, idStr);

    // Unregister impl->wrapper mapping first
    if (impl) {
        UnregisterMapping(impl);
    }

    // Remove from storage maps - unique_ptr will handle cleanup
    // Only one of these will succeed (wrapper exists in exactly one map)
    if (elements.erase(idStr) > 0) return;
    if (texts.erase(idStr) > 0) return;
    if (scrollWheels.erase(idStr) > 0) return;
    if (wheels.erase(idStr) > 0) return;
    if (columnGrids.erase(idStr) > 0) return;
    if (rowGrids.erase(idStr) > 0) return;
}

} // namespace P3DUI
