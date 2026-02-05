#include "WrapperTypes.h"
#include "../projectile/ProjectileSubsystem.h"
#include "../projectile/DriverUpdateManager.h"
#include "../util/Haptics.h"
#include "../log.h"

namespace P3DUI {

// =============================================================================
// Interface001 Implementation
// =============================================================================
// Main entry point for the 3D UI API. Provides:
// - Factory methods for creating UI nodes (Root, Element, Text, containers)
// - Global query methods (IsHovering, GetHoveredItem)
//
// All created objects are owned by WrapperRegistry. Callers receive raw
// pointers but do not own the memory.

class Interface001Impl : public Interface001 {
public:
    uint32_t GetInterfaceVersion() override { return P3DUI_INTERFACE_VERSION; }

    // =========================================================================
    // Root Management
    // =========================================================================

    Root* GetOrCreateRoot(const RootConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::GetOrCreateRoot: ID is required");
            return nullptr;
        }
        if (!config.modId || !*config.modId) {
            spdlog::error("P3DUI::GetOrCreateRoot: modId is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();

        // Return existing root if present
        auto it = registry.roots.find(config.id);
        if (it != registry.roots.end()) {
            spdlog::trace("[{}] GetOrCreateRoot: Returning existing root '{}'", config.modId, config.id);
            return it->second.get();
        }

        // Create new root
        auto wrapper = std::make_unique<RootWrapper>(config);
        auto* ptr = wrapper.get();
        registry.roots[config.id] = std::move(wrapper);

        spdlog::info("[{}] Created menu '{}'", config.modId, config.id);
        return ptr;
    }

    Root* GetRoot(const char* id) override {
        if (!id) return nullptr;
        auto& registry = WrapperRegistry::Get();
        auto it = registry.roots.find(id);
        return it != registry.roots.end() ? it->second.get() : nullptr;
    }

    // =========================================================================
    // Factory Methods
    // =========================================================================

    Element* CreateElement(const ElementConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateElement: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.elements.count(config.id)) {
            spdlog::error("P3DUI::CreateElement: Element '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<ElementWrapper>(config);
        auto* ptr = wrapper.get();
        registry.elements[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateElement: Created element '{}'", config.id);
        return ptr;
    }

    Text* CreateText(const TextConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateText: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.texts.count(config.id)) {
            spdlog::error("P3DUI::CreateText: Text '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<TextWrapper>(config);
        auto* ptr = wrapper.get();
        registry.texts[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateText: Created text '{}'", config.id);
        return ptr;
    }

    Container* CreateScrollWheel(const ScrollWheelConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateScrollWheel: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.scrollWheels.count(config.id)) {
            spdlog::error("P3DUI::CreateScrollWheel: ScrollWheel '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<ScrollWheelWrapper>(config);
        auto* ptr = wrapper.get();
        registry.scrollWheels[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateScrollWheel: Created scroll wheel '{}'", config.id);
        return ptr;
    }

    Container* CreateWheel(const WheelConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateWheel: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.wheels.count(config.id)) {
            spdlog::error("P3DUI::CreateWheel: Wheel '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<WheelWrapper>(config);
        auto* ptr = wrapper.get();
        registry.wheels[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateWheel: Created wheel '{}'", config.id);
        return ptr;
    }

    ScrollableContainer* CreateColumnGrid(const ColumnGridConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateColumnGrid: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.columnGrids.count(config.id)) {
            spdlog::error("P3DUI::CreateColumnGrid: ColumnGrid '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<ColumnGridWrapper>(config);
        auto* ptr = wrapper.get();
        registry.columnGrids[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateColumnGrid: Created column grid '{}'", config.id);
        return ptr;
    }

    ScrollableContainer* CreateRowGrid(const RowGridConfig& config) override {
        if (!config.id || !*config.id) {
            spdlog::error("P3DUI::CreateRowGrid: ID is required");
            return nullptr;
        }

        auto& registry = WrapperRegistry::Get();
        if (registry.rowGrids.count(config.id)) {
            spdlog::error("P3DUI::CreateRowGrid: RowGrid '{}' already exists", config.id);
            return nullptr;
        }

        auto wrapper = std::make_unique<RowGridWrapper>(config);
        auto* ptr = wrapper.get();
        registry.rowGrids[config.id] = std::move(wrapper);

        spdlog::trace("P3DUI::CreateRowGrid: Created row grid '{}'", config.id);
        return ptr;
    }

    // =========================================================================
    // Input State Query
    // =========================================================================

    bool IsHovering(bool leftHand, bool anyHand) override {
        auto& registry = WrapperRegistry::Get();
        for (const auto& [id, root] : registry.roots) {
            if (!root) continue;
            if (anyHand) {
                // Check both hands
                if (root->IsHandInteracting(true) || root->IsHandInteracting(false)) {
                    return true;
                }
            } else {
                // Check specific hand
                if (root->IsHandInteracting(leftHand)) {
                    return true;
                }
            }
        }
        return false;
    }

    Positionable* GetHoveredItem(bool isLeftHand) override {
        auto& registry = WrapperRegistry::Get();
        for (const auto& [id, root] : registry.roots) {
            if (!root) continue;
            if (auto* hovered = root->GetHoveredItem(isLeftHand)) {
                return hovered;
            }
        }
        return nullptr;
    }
};

// =============================================================================
// Singleton Accessor
// =============================================================================
// First call initializes all 3D UI subsystems:
// - ProjectileSubsystem: manages projectile lifecycle and rendering
// - DriverUpdateManager: coordinates per-frame updates
// - Haptics: VR controller vibration feedback

static bool g_initialized = false;

Interface001* GetInterface001() {
    static Interface001Impl instance;

    if (!g_initialized) {
        // Initialize ProjectileSubsystem
        auto* projSubsystem = Projectile::ProjectileSubsystem::GetSingleton();
        if (!projSubsystem->IsInitialized()) {
            if (!projSubsystem->Initialize()) {
                spdlog::error("P3DUI::GetInterface001: Failed to initialize ProjectileSubsystem");
                return nullptr;
            }
        }

        // Initialize DriverUpdateManager
        auto& driverMgr = Widget::DriverUpdateManager::GetSingleton();
        if (!driverMgr.IsInitialized()) {
            driverMgr.Initialize(projSubsystem);
        }

        // Initialize Haptics (requires InputManager to be initialized first)
        auto& haptics = Projectile::Haptics::GetSingleton();
        if (!haptics.IsInitialized()) {
            haptics.Initialize();
            if (!haptics.IsInitialized()) {
                spdlog::error("P3DUI::GetInterface001: Haptics failed to initialize - VR haptic feedback will be unavailable");
            }
        }

        g_initialized = true;
        spdlog::info("P3DUI::GetInterface001: 3D UI subsystems initialized (haptics: {})",
            haptics.IsInitialized() ? "enabled" : "DISABLED");
    }

    return &instance;
}

} // namespace P3DUI
