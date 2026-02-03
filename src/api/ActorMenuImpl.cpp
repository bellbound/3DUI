#include "../ThreeDUIActorMenu.h"
#include "../ThreeDUIInterface001.h"
#include "../InputManager.h"
#include "../MenuChecker.h"
#include "../higgsinterface001.h"
#include "../util/VRNodes.h"
#include "../projectile/InteractionController.h"
#include "../log.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <cstring>

namespace P3DUI {

// =============================================================================
// Internal Types
// =============================================================================

struct ElementRegistration {
    // Config (copied for persistence)
    std::string modId;
    std::string elementId;
    std::string texturePath;
    std::string modelPath;
    uint32_t formID;
    std::wstring tooltip;
    float scale;

    // Callbacks
    ActorMenuEligibilityCallback isEligible;
    ActorMenuActivationCallback onActivate;
    void* userData;

    // Combined key used for 3DUI element ID: "modId:elementId"
    std::string fullId;

    // Copy config into registration
    void CopyConfig(const ActorMenuElementConfig& config) {
        modId = config.modId ? config.modId : "";
        elementId = config.elementId ? config.elementId : "";
        texturePath = config.texturePath ? config.texturePath : "";
        modelPath = config.modelPath ? config.modelPath : "";
        formID = config.formID;
        tooltip = config.tooltip ? config.tooltip : L"";
        scale = config.scale > 0.0f ? config.scale : 1.2f;
        fullId = modId + ":" + elementId;
    }
};

// =============================================================================
// ActorMenuImpl
// =============================================================================

class ActorMenuImpl : public ActorMenuInterface {
public:
    static ActorMenuImpl& Get() {
        static ActorMenuImpl instance;
        return instance;
    }

    bool Initialize() {
        if (m_initialized) return true;

        // Get the 3DUI interface
        m_api = GetInterface001();
        if (!m_api) {
            spdlog::error("ActorMenuImpl: Failed to get Interface001");
            return false;
        }

        // Register trigger button callback
        auto* inputMgr = InputManager::GetSingleton();
        if (!inputMgr || !inputMgr->IsInitialized()) {
            spdlog::error("ActorMenuImpl: InputManager not initialized");
            return false;
        }

        uint64_t triggerMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
        m_callbackId = inputMgr->AddVrButtonCallback(triggerMask,
            [this](bool isLeft, bool isReleased, vr::EVRButtonId) {
                return this->OnTriggerInput(isLeft, isReleased);
            });

        spdlog::info("ActorMenuImpl: Initialized (callback ID: {})", m_callbackId);
        m_initialized = true;
        return true;
    }

    // === ActorMenuInterface ===

    uint32_t GetInterfaceVersion() override {
        return P3DUI_ACTORMENU_VERSION;
    }

    bool RegisterElement(
        const ActorMenuElementConfig& config,
        ActorMenuEligibilityCallback isEligible,
        ActorMenuActivationCallback onActivate,
        void* userData) override
    {
        // Validate required fields
        if (!config.modId || !*config.modId) {
            spdlog::error("ActorMenuImpl::RegisterElement: modId is required");
            return false;
        }
        if (!config.elementId || !*config.elementId) {
            spdlog::error("ActorMenuImpl::RegisterElement: elementId is required");
            return false;
        }
        if (!isEligible) {
            spdlog::error("ActorMenuImpl::RegisterElement: isEligible callback is required");
            return false;
        }
        if (!onActivate) {
            spdlog::error("ActorMenuImpl::RegisterElement: onActivate callback is required");
            return false;
        }

        std::string fullId = std::string(config.modId) + ":" + config.elementId;

        // Check for duplicate
        if (m_registrations.count(fullId)) {
            spdlog::error("ActorMenuImpl::RegisterElement: '{}' already registered", fullId);
            return false;
        }

        // Create registration
        ElementRegistration reg;
        reg.CopyConfig(config);
        reg.isEligible = isEligible;
        reg.onActivate = onActivate;
        reg.userData = userData;

        m_registrations[fullId] = std::move(reg);

        spdlog::info("ActorMenuImpl: Registered element '{}' (tooltip: '{}')",
            fullId, config.tooltip ? "set" : "none");
        return true;
    }

    bool UpdateElement(const ActorMenuElementConfig& config) override {
        if (!config.modId || !config.elementId) {
            spdlog::error("ActorMenuImpl::UpdateElement: modId and elementId required");
            return false;
        }

        std::string fullId = std::string(config.modId) + ":" + config.elementId;
        auto it = m_registrations.find(fullId);
        if (it == m_registrations.end()) {
            spdlog::error("ActorMenuImpl::UpdateElement: '{}' not found", fullId);
            return false;
        }

        // Update visual properties only (not callbacks)
        auto& reg = it->second;
        reg.texturePath = config.texturePath ? config.texturePath : "";
        reg.modelPath = config.modelPath ? config.modelPath : "";
        reg.formID = config.formID;
        reg.tooltip = config.tooltip ? config.tooltip : L"";
        reg.scale = config.scale > 0.0f ? config.scale : 0.3f;

        spdlog::info("ActorMenuImpl: Updated element '{}'", fullId);
        return true;
    }

    void UnregisterElement(const char* modId, const char* elementId) override {
        if (!modId || !elementId) return;

        std::string fullId = std::string(modId) + ":" + elementId;
        auto it = m_registrations.find(fullId);
        if (it != m_registrations.end()) {
            m_registrations.erase(it);
            spdlog::info("ActorMenuImpl: Unregistered element '{}'", fullId);
        }
    }

    void UnregisterMod(const char* modId) override {
        if (!modId) return;

        std::string prefix = std::string(modId) + ":";
        size_t count = 0;

        for (auto it = m_registrations.begin(); it != m_registrations.end(); ) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) {
                it = m_registrations.erase(it);
                ++count;
            } else {
                ++it;
            }
        }

        spdlog::info("ActorMenuImpl: Unregistered {} elements from mod '{}'", count, modId);
    }

    bool IsElementRegistered(const char* modId, const char* elementId) override {
        if (!modId || !elementId) return false;
        std::string fullId = std::string(modId) + ":" + elementId;
        return m_registrations.count(fullId) > 0;
    }

    bool IsMenuOpen() override {
        return m_root && m_root->IsVisible();
    }

    RE::Actor* GetCurrentActor() override {
        return m_currentActor;
    }

    void ForceClose() override {
        HideMenu();
    }

private:
    ActorMenuImpl() = default;

    // === Input Handling ===

    bool OnTriggerInput(bool isLeft, bool isReleased) {
        if (isReleased) {
            return OnTriggerReleased(isLeft);
        } else {
            return OnTriggerPressed(isLeft);
        }
    }

    bool OnTriggerPressed(bool isLeft) {
        // Block input when a game menu is open (inventory, dialogue, etc.)
        if (MenuChecker::IsGameStopped()) {
            return false;
        }

        // Defer to other InteractionControllers if they have a hovered item on this hand.
        // This prevents ActorMenu from opening when another 3DUI menu would handle the input,
        // avoiding double-activation when a mod's menu is open while still holding an NPC.
        if (Widget::AnyControllerHasHoveredItem(isLeft)) {
            spdlog::trace("ActorMenuImpl: Deferring to another controller with hovered item");
            return false;
        }

        // Check if HIGGS interface is available
        if (!g_higgsInterface) {
            return false;
        }

        // Check what the OTHER hand is holding (not the trigger hand)
        bool otherHandIsLeft = !isLeft;
        RE::TESObjectREFR* grabbedObj = g_higgsInterface->GetGrabbedObject(otherHandIsLeft);

        if (!grabbedObj) {
            return false;
        }

        // Check if it's an actor (NPC)
        RE::Actor* actor = grabbedObj->As<RE::Actor>();
        if (!actor) {
            return false;
        }

        spdlog::info("ActorMenuImpl: Detected NPC grab - checking eligibility for '{}'",
            actor->GetName() ? actor->GetName() : "unknown");

        // Collect eligible elements
        std::vector<std::string> eligibleIds;
        for (const auto& [fullId, reg] : m_registrations) {
            if (reg.isEligible && reg.isEligible(actor, reg.userData)) {
                eligibleIds.push_back(fullId);
                spdlog::trace("ActorMenuImpl: Element '{}' is eligible", fullId);
            }
        }

        spdlog::info("ActorMenuImpl: {} eligible elements found", eligibleIds.size());

        if (eligibleIds.empty()) {
            // No eligible elements - don't consume input
            return false;
        }

        if (eligibleIds.size() == 1) {
            // Single eligible element - activate immediately (no menu shown)
            spdlog::info("ActorMenuImpl: Single eligible element '{}' - activating immediately", eligibleIds[0]);
            m_currentActor = actor;  // Set for API consistency during callback
            DoActivation(eligibleIds[0]);
            m_currentActor = nullptr;  // Clear since menu was never shown
            return true;  // Consume input
        }

        // Multiple eligible elements - show tween menu
        ShowMenu(actor, eligibleIds, isLeft);
        return true;  // Consume input
    }

    bool OnTriggerReleased(bool isLeft) {
        if (!IsMenuOpen()) {
            return false;
        }

        // Block input when a game menu is open (inventory, dialogue, etc.)
        // Still close the actor menu, but don't activate any element
        if (MenuChecker::IsGameStopped()) {
            spdlog::info("ActorMenuImpl: Game menu open - closing without activation");
            HideMenu();
            return true;  // Consume input to prevent other handlers
        }

        // Only respond to release from the hand that opened the menu
        if (isLeft != m_menuOpenedByLeftHand) {
            return false;
        }

        // Check if hovering over an element
        Positionable* hovered = m_api->GetHoveredItem(m_menuOpenedByLeftHand);

        if (hovered) {
            const char* id = hovered->GetID();
            if (id && *id) {
                spdlog::info("ActorMenuImpl: Released while hovering '{}' - activating", id);
                ActivateElement(id);
            }
        } else {
            spdlog::info("ActorMenuImpl: Released without hover - canceling");
        }

        // Always hide menu on release
        HideMenu();
        return true;  // Consume input
    }

    // === Menu Management ===

    bool SetupMenu() {
        if (m_root) return true;  // Already setup

        if (!m_api) {
            spdlog::error("ActorMenuImpl::SetupMenu: Interface not available");
            return false;
        }

        spdlog::info("ActorMenuImpl::SetupMenu: Creating menu");

        // Create root with interaction
        RootConfig rootConfig = RootConfig::Default("actormenu_root", "3DUI-ActorMenu");
        rootConfig.interactive = true;
        rootConfig.activationButtonMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
        rootConfig.grabButtonMask = 0;  // No grab repositioning
        rootConfig.eventCallback = nullptr;  // We handle events via GetHoveredItem on release

        m_root = m_api->CreateRoot(rootConfig);
        if (!m_root) {
            spdlog::error("ActorMenuImpl::SetupMenu: Failed to create root");
            return false;
        }

        // Create wheel for elements
        WheelConfig wheelConfig = WheelConfig::Default("actormenu_wheel");
        wheelConfig.itemSpacing = 7.0f;     // 10% reduction from 10.0f
        wheelConfig.ringSpacing = 9.5f;    // 10% reduction from 15.0f (first ring distance)

        m_wheel = m_api->CreateWheel(wheelConfig);
        if (m_wheel) {
            m_root->AddChild(m_wheel);
        }

        // Use HMD anchor for facing
        m_root->SetVRAnchor(VRAnchorType::HMD);
        m_root->SetFacingMode(FacingMode::Full);

        spdlog::info("ActorMenuImpl::SetupMenu: Complete");
        return true;
    }

    void ShowMenu(RE::Actor* actor, const std::vector<std::string>& eligibleIds, bool menuOpenedByLeftHand) {
        // Lazy setup
        if (!SetupMenu()) {
            return;
        }

        m_currentActor = actor;
        m_menuOpenedByLeftHand = menuOpenedByLeftHand;
        m_currentEligibleIds = eligibleIds;

        // Position at the hand that pressed trigger
        PositionMenuAtHand(menuOpenedByLeftHand);

        // Populate wheel with eligible elements
        PopulateWheel();

        // Show
        m_root->SetVisible(true);

        spdlog::info("ActorMenuImpl: Menu opened for '{}' with {} elements",
            actor->GetName() ? actor->GetName() : "unknown",
            eligibleIds.size());
    }

    void HideMenu() {
        if (m_root && m_root->IsVisible()) {
            m_root->SetVisible(false);
            spdlog::info("ActorMenuImpl: Menu hidden");
        }
        m_currentActor = nullptr;
        m_currentEligibleIds.clear();
    }

    void PositionMenuAtHand(bool isLeftHand) {
        if (!m_root) return;

        auto* hand = isLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
        auto* hmd = VRNodes::GetHMD();

        if (!hand || !hmd) {
            spdlog::warn("ActorMenuImpl: Could not get VR nodes for positioning");
            return;
        }

        // Calculate hand position relative to HMD (since VRAnchor is HMD)
        RE::NiPoint3 handWorld = hand->world.translate;
        RE::NiPoint3 hmdWorld = hmd->world.translate;
        RE::NiPoint3 relativePos = handWorld - hmdWorld;

        m_root->SetLocalPosition(relativePos.x, relativePos.y, relativePos.z);
    }

    void PopulateWheel() {
        if (!m_wheel || !m_api) return;

        m_wheel->Clear();

        // Add center orb first (occupies ring 0 at position 0,0,0)
        // This orb does nothing when selected - acts as visual anchor
        ElementConfig orbConfig = ElementConfig::Default("actormenu_center_orb");
        orbConfig.modelPath = "meshes\\3DUI\\orb.nif";
        orbConfig.scale = 1.2f;
        orbConfig.facingMode = FacingMode::None;
        Element* orbElem = m_api->CreateElement(orbConfig);
        if (orbElem) {
            m_wheel->AddChild(orbElem);
        }

        // Add each eligible element
        for (const auto& fullId : m_currentEligibleIds) {
            auto it = m_registrations.find(fullId);
            if (it == m_registrations.end()) continue;

            const auto& reg = it->second;

            ElementConfig config = ElementConfig::Default(fullId.c_str());

            if (!reg.texturePath.empty()) {
                config.texturePath = reg.texturePath.c_str();
            } else if (!reg.modelPath.empty()) {
                config.modelPath = reg.modelPath.c_str();
            }

            if (reg.formID != 0) {
                config.formID = reg.formID;
            }

            if (!reg.tooltip.empty()) {
                config.tooltip = reg.tooltip.c_str();
            }

            config.scale = reg.scale;
            config.facingMode = FacingMode::None;

            Element* elem = m_api->CreateElement(config);
            if (elem) {
                m_wheel->AddChild(elem);
            }
        }

        spdlog::info("ActorMenuImpl::PopulateWheel: Added {} elements", m_currentEligibleIds.size());
    }

    // Unified activation logic used by both single-element immediate activation
    // and multi-element menu selection. Validates actor and invokes callback.
    void DoActivation(const std::string& fullId) {
        if (fullId.empty()) {
            return;
        }

        // Validate actor is still available
        if (!m_currentActor) {
            spdlog::warn("ActorMenuImpl::DoActivation: No current actor set");
            return;
        }

        // Validate actor is still valid (not deleted/unloaded)
        if (m_currentActor->IsDeleted() || !m_currentActor->Is3DLoaded()) {
            spdlog::warn("ActorMenuImpl::DoActivation: Actor '{}' is no longer valid",
                m_currentActor->GetName() ? m_currentActor->GetName() : "unknown");
            return;
        }

        auto it = m_registrations.find(fullId);
        if (it == m_registrations.end()) {
            spdlog::warn("ActorMenuImpl::DoActivation: '{}' not found in registrations", fullId);
            return;
        }

        const auto& reg = it->second;
        if (reg.onActivate) {
            spdlog::info("ActorMenuImpl: Activating '{}' for actor '{}'",
                fullId, m_currentActor->GetName() ? m_currentActor->GetName() : "unknown");

            // Call activation callback with exception handling to catch cross-DLL issues
            try {
                reg.onActivate(
                    m_currentActor,
                    reg.modId.c_str(),
                    reg.elementId.c_str(),
                    reg.userData);
            } catch (const std::exception& e) {
                spdlog::error("ActorMenuImpl: Callback for '{}' threw exception: {}", fullId, e.what());
            } catch (...) {
                spdlog::error("ActorMenuImpl: Callback for '{}' threw unknown exception", fullId);
            }
        }
    }

    // Called when user selects an element from the visible menu
    void ActivateElement(const char* fullId) {
        if (!fullId) return;

        // Ignore center orb - it does nothing when selected
        if (std::strcmp(fullId, "actormenu_center_orb") == 0) {
            return;
        }

        DoActivation(fullId);
    }

    // === State ===

    bool m_initialized = false;
    InputManager::CallbackId m_callbackId = InputManager::InvalidCallbackId;

    Interface001* m_api = nullptr;
    Root* m_root = nullptr;
    Container* m_wheel = nullptr;

    std::unordered_map<std::string, ElementRegistration> m_registrations;

    RE::Actor* m_currentActor = nullptr;
    bool m_menuOpenedByLeftHand = false;
    std::vector<std::string> m_currentEligibleIds;
};

// =============================================================================
// Public Interface Accessor
// =============================================================================

ActorMenuInterface* GetActorMenuInterface() {
    auto& impl = ActorMenuImpl::Get();

    if (!impl.Initialize()) {
        spdlog::error("GetActorMenuInterface: Failed to initialize");
        return nullptr;
    }

    return &impl;
}

} // namespace P3DUI
