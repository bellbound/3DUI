#include "InteractionController.h"
#include "TooltipTextDisplayManager.h"
#include "../MenuChecker.h"
#include "../util/VRNodes.h"
#include "../util/HapticPulses.h"
#include "../InputManager.h"
#include "../log.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {
    // All registered interaction controllers - checked in order for input handling
    std::vector<Widget::InteractionController*> g_registeredControllers;

    // Global callback IDs - only register once across all controllers
    InputManager::CallbackId g_activationCallbackId = InputManager::InvalidCallbackId;
    InputManager::CallbackId g_grabCallbackId = InputManager::InvalidCallbackId;

    // Hover debounce configuration
    // When enabled, hover changes are delayed until the new target remains closest for DEBOUNCE_MS
    // This prevents rapid tooltip show/hide cycles when hand moves quickly between items
    constexpr bool ENABLE_DEBOUNCE = true;
    constexpr float DEBOUNCE_MS = 50.0f;
}

namespace Widget {

InteractionController::~InteractionController() noexcept {
    try {
        RemoveCallbacks();
        // Remove from registered controllers
        auto it = std::find(g_registeredControllers.begin(), g_registeredControllers.end(), this);
        if (it != g_registeredControllers.end()) {
            g_registeredControllers.erase(it);
        }
    } catch (...) {
        // Destructors must not throw - silently absorb any exceptions
    }
}

void InteractionController::RemoveCallbacks() {
    // Global callbacks are shared across all controllers, so we don't remove them here.
    // They will persist for the lifetime of the plugin.
    // Individual controllers are removed from g_registeredControllers in the destructor.
}

void InteractionController::SetActivationButtons(uint64_t buttonMask) {
    m_activationButtons = buttonMask;

    // Register this controller if not already in the list
    if (std::find(g_registeredControllers.begin(), g_registeredControllers.end(), this) == g_registeredControllers.end()) {
        g_registeredControllers.push_back(this);
    }

    if (g_activationCallbackId == InputManager::InvalidCallbackId && buttonMask != 0) {
        auto* inputMgr = InputManager::GetSingleton();
        if (inputMgr && inputMgr->IsInitialized()) {
            g_activationCallbackId = inputMgr->AddVrButtonCallback(buttonMask,
                [](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) -> bool {
                    // Block input when a game menu is open
                    if (MenuChecker::IsGameStopped()) {
                        return false;
                    }
                    // THREAD SAFETY: Copy before iterating - main thread may modify during VR callback
                    auto controllersCopy = g_registeredControllers;
                    // Check all registered controllers, handle with first visible one that has a hovered item
                    for (auto* controller : controllersCopy) {
                        if (controller && controller->GetRoot() && controller->GetRoot()->IsVisible()) {
                            if (controller->OnActivationInput(isLeft, isReleased, buttonId)) {
                                return true;
                            }
                        }
                    }
                    return false;
                });

            spdlog::info("InteractionController: Registered global activation callback for mask 0x{:X}", buttonMask);
        }
    }
}

void InteractionController::SetGrabButtons(uint64_t buttonMask) {
    m_grabButtons = buttonMask;

    // Register this controller if not already in the list
    if (std::find(g_registeredControllers.begin(), g_registeredControllers.end(), this) == g_registeredControllers.end()) {
        g_registeredControllers.push_back(this);
    }

    // Only register callback once globally (not per-controller)
    if (g_grabCallbackId == InputManager::InvalidCallbackId && buttonMask != 0) {
        auto* inputMgr = InputManager::GetSingleton();
        if (inputMgr && inputMgr->IsInitialized()) {
            g_grabCallbackId = inputMgr->AddVrButtonCallback(buttonMask,
                [](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) -> bool {
                    // Block input when a game menu is open
                    if (MenuChecker::IsGameStopped()) {
                        return false;
                    }
                    // THREAD SAFETY: Copy before iterating - main thread may modify during VR callback
                    auto controllersCopy = g_registeredControllers;
                    // Check all registered controllers, handle with first visible one
                    for (auto* controller : controllersCopy) {
                        if (controller && controller->GetRoot() && controller->GetRoot()->IsVisible()) {
                            if (controller->OnGrabInput(isLeft, isReleased, buttonId)) {
                                return true;
                            }
                        }
                    }
                    return false;
                });

            spdlog::info("InteractionController: Registered global grab callback for mask 0x{:X}", buttonMask);
        }
    }
}

void InteractionController::Clear() {
    // Fire hover exit for both hands and hide tooltips
    for (bool isLeft : {true, false}) {
        auto& handState = GetHandState(isLeft);
        if (auto hovered = handState.hoveredProjectile.lock()) {
            // Hide tooltip for the exited item (ID-based)
            if (m_displayTooltip) {
                TooltipTextDisplayManager::GetSingleton()->HideTooltip(
                    isLeft, hovered->GetUUID().ToString());
            }
        } else if (m_displayTooltip) {
            // Projectile was destroyed (weak_ptr expired) but tooltip may still be visible
            // Use ForceHideTooltip to ensure cleanup regardless of ID matching
            TooltipTextDisplayManager::GetSingleton()->ForceHideTooltip(isLeft);
        }
        handState.Clear();
    }

    m_currentScales.clear();
}

void InteractionController::CollectProjectiles(Projectile::IPositionable* node,
                                               std::vector<Projectile::ControlledProjectilePtr>& out) {
    if (!node) return;

    // Check if this node is a ControlledProjectile
    if (auto* proj = dynamic_cast<Projectile::ControlledProjectile*>(node)) {
        if (proj->IsValid()) {
            // Get shared_ptr via enable_shared_from_this
            out.push_back(proj->shared_from_this());
        }
        return;  // Projectiles don't have children
    }

    // Check if this is a driver with children
    if (auto* driver = dynamic_cast<Projectile::ProjectileDriver*>(node)) {
        for (const auto& child : driver->GetChildren()) {
            CollectProjectiles(child.get(), out);
        }
    }
}

bool InteractionController::OnActivationInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
    if (!m_root || !m_root->IsVisible()) {
        return false;
    }

    auto& handState = GetHandState(isLeft);
    RE::NiAVObject* handNode = isLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();

    if (isReleased) {
        // Key up - fire ActivateUp if we were activating
        auto activated = handState.activatedProjectile.lock();
        if (!activated) {
            return false;  // No activation in progress for this hand
        }

        spdlog::trace("[Interaction] ActivateUp: hand={} item='{}' UUID={}",
            isLeft ? "left" : "right", activated->GetID(), activated->GetUUID().ToString());

        auto event = Projectile::InputEvent::ActivateUp(activated.get(), handNode, isLeft);
        activated->DispatchEvent(event);

        // Trigger haptic pulse after dispatch
        Projectile::TriggerPulseForEvent(event);

        handState.activatedProjectile.reset();
        return true;
    } else {
        // Key down - fire ActivateDown
        auto hovered = handState.hoveredProjectile.lock();
        if (!hovered) {
            return false;  // This hand isn't hovering anything
        }

        spdlog::trace("[Interaction] ActivateDown: hand={} item='{}' UUID={} closeOnActivate={}",
            isLeft ? "left" : "right", hovered->GetID(), hovered->GetUUID().ToString(),
            hovered->ShouldCloseOnActivate());

        // Track for ActivateUp
        handState.activatedProjectile = hovered;

        // Dispatch ActivateDown event through hierarchy
        auto event = Projectile::InputEvent::ActivateDown(
            hovered.get(), handNode, isLeft,
            hovered->ShouldCloseOnActivate());
        hovered->DispatchEvent(event);

        // Trigger haptic pulse after dispatch
        Projectile::TriggerPulseForEvent(event);

        // Fire close callback if needed (based on projectile's closeOnActivate flag)
        if (hovered->ShouldCloseOnActivate() && m_closeCallback) {
            spdlog::trace("[Interaction] Item '{}' has closeOnActivate, firing close callback", hovered->GetID());

            // Hide tooltip BEFORE close callback - the callback may destroy the menu/projectiles,
            // which would leave the tooltip stuck visible if we don't hide it first
            if (m_displayTooltip) {
                TooltipTextDisplayManager::GetSingleton()->HideTooltip(
                    isLeft, hovered->GetUUID().ToString());
            }

            m_closeCallback();
            return true;
        }

        return true;
    }
}

bool InteractionController::OnGrabInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
    if (!m_root || !m_root->IsVisible()) {
        return false;
    }

    auto& handState = GetHandState(isLeft);
    RE::NiAVObject* handNode = isLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();

    if (isReleased) {
        auto grabbed = handState.grabbedProjectile.lock();
        if (!handState.isGrabbing || !grabbed) {
            return false;
        }

        spdlog::trace("[Interaction] GrabEnd: hand={} item='{}' UUID={}",
            isLeft ? "left" : "right", grabbed->GetID(), grabbed->GetUUID().ToString());

        // Dispatch GrabEnd event
        auto event = Projectile::InputEvent::GrabEnd(grabbed.get(), handNode, isLeft);
        grabbed->DispatchEvent(event);

        // Trigger haptic pulse after dispatch
        Projectile::TriggerPulseForEvent(event);

        handState.isGrabbing = false;
        handState.grabbedProjectile.reset();
        return true;
    } else {
        // Press - require this hand to be hovering an item
        auto hovered = handState.hoveredProjectile.lock();
        if (!hovered) {
            return false;
        }

        spdlog::trace("[Interaction] GrabStart: hand={} item='{}' UUID={} isAnchorHandle={}",
            isLeft ? "left" : "right", hovered->GetID(), hovered->GetUUID().ToString(),
            hovered->IsAnchorHandle());

        handState.isGrabbing = true;
        handState.grabbedProjectile = hovered;

        // Dispatch GrabStart event
        auto event = Projectile::InputEvent::GrabStart(hovered.get(), handNode, isLeft);
        hovered->DispatchEvent(event);

        // Trigger haptic pulse after dispatch
        Projectile::TriggerPulseForEvent(event);

        return true;
    }
}

void InteractionController::Update(float deltaTime) {
    // Auto-disable when driver is hidden - no separate enabled state needed
    if (!m_root || !m_root->IsVisible()) {
        return;
    }

    UpdateHover(deltaTime);
    UpdateScaleAnimation(deltaTime);
}

void InteractionController::CommitHoverChange(bool isLeft, RE::NiAVObject* handNode,
                                               Projectile::ControlledProjectilePtr newHovered) {
    auto& handState = GetHandState(isLeft);

    // Log the hover change with full context
    auto prevHovered = handState.hoveredProjectile.lock();
    std::string prevId = prevHovered ? prevHovered->GetID() : "(none)";
    std::string prevUUID = prevHovered ? prevHovered->GetUUID().ToString() : "";
    std::string newId = newHovered ? newHovered->GetID() : "(none)";
    std::string newUUID = newHovered ? newHovered->GetUUID().ToString() : "";
    spdlog::trace("[Interaction] HoverChange: hand={} '{}' [{}] -> '{}' [{}]",
        isLeft ? "left" : "right", prevId, prevUUID, newId, newUUID);

    // Exit previous hover
    if (prevHovered) {
        // Dispatch HoverExit event
        auto event = Projectile::InputEvent::HoverExit(prevHovered.get(), handNode, isLeft);
        prevHovered->DispatchEvent(event);

        // Trigger haptic pulse after dispatch (event.sendHapticPulse may have been modified)
        Projectile::TriggerPulseForEvent(event);

        // Hide tooltip for the exited item
        if (m_displayTooltip) {
            TooltipTextDisplayManager::GetSingleton()->HideTooltip(isLeft, prevHovered->GetUUID().ToString());
        }
    }

    // Enter new hover
    if (newHovered) {
        // Dispatch HoverEnter event
        auto event = Projectile::InputEvent::HoverEnter(newHovered.get(), handNode, isLeft);
        newHovered->DispatchEvent(event);

        // Trigger haptic pulse after dispatch (event.sendHapticPulse may have been modified)
        Projectile::TriggerPulseForEvent(event);

        // Show tooltip for the new item (only if text is set)
        const auto& text = newHovered->GetText();
        if (m_displayTooltip && !text.empty()) {
            spdlog::trace("[Interaction] HoverEnter: showing tooltip for '{}' [{}], text length={}",
                newHovered->GetID(), newHovered->GetUUID().ToString(), text.size());
            TooltipTextDisplayManager::GetSingleton()->ShowTooltip(isLeft, newHovered->GetUUID().ToString(), text);
        }
    }

    handState.previousHoveredProjectile = handState.hoveredProjectile;
    handState.hoveredProjectile = newHovered;  // shared_ptr implicitly converts to weak_ptr
}

void InteractionController::UpdateHoverForHand(bool isLeft, RE::NiAVObject* handNode,
                                                const std::vector<Projectile::ControlledProjectilePtr>& projectiles,
                                                float deltaTime) {
    if (!handNode) return;

    auto& handState = GetHandState(isLeft);

    // Lock current hover state for comparisons
    auto currentHovered = handState.hoveredProjectile.lock();

    // Defensive check: if tooltip is visible but we have no valid hover target,
    // the hovered element was destroyed without proper cleanup (e.g., driver->Clear()).
    // Force-hide the orphaned tooltip to prevent it from getting stuck.
    if (!currentHovered && m_displayTooltip) {
        if (TooltipTextDisplayManager::GetSingleton()->IsTooltipVisible(isLeft)) {
            TooltipTextDisplayManager::GetSingleton()->ForceHideTooltip(isLeft);
            handState.hoveredProjectile.reset();
            handState.pendingHover.reset();
            handState.pendingHoverTimer = 0.0f;
        }
    }

    auto currentPending = handState.pendingHover.lock();

    // Helper to get effective threshold for a projectile (override or controller default)
    auto getThreshold = [this](const Projectile::ControlledProjectilePtr& proj) -> float {
        return proj->HasHoverThresholdOverride() ? proj->GetHoverThresholdOverride() : m_hoverThreshold;
    };

    // Hysteresis: exit threshold is 1.01x enter threshold to prevent flickering
    // For per-projectile thresholds, we calculate this per-item
    float currentExitThreshold = m_hoverThreshold * 1.01f;  // Updated below if we have a hovered item

    // Find closest projectile that is within its own threshold
    Projectile::ControlledProjectilePtr newHovered = nullptr;
    float closestDist = (std::numeric_limits<float>::max)();
    float currentHoveredDist = (std::numeric_limits<float>::max)();

    RE::NiPoint3 handPos = handNode->world.translate;

    for (const auto& proj : projectiles) {
        if (!proj || !proj->IsEffectivelyVisible())
            continue;

        RE::NiPoint3 projPos = proj->GetWorldPosition();

        float dx = projPos.x - handPos.x;
        float dy = projPos.y - handPos.y;
        float dz = projPos.z - handPos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Track distance to currently hovered item for hysteresis check
        if (proj == currentHovered) {
            currentHoveredDist = dist;
            currentExitThreshold = getThreshold(proj) * 1.01f;
        }

        // Check if this projectile is within its own threshold
        float projThreshold = getThreshold(proj);
        if (dist < projThreshold && dist < closestDist) {
            closestDist = dist;
            newHovered = proj;
        }
    }

    // Hysteresis: if currently hovering an item that's within its exit threshold,
    // keep hovering it even if another item is now slightly closer
    if (currentHovered && currentHoveredDist <= currentExitThreshold && newHovered != currentHovered) {
        // Current item is still within exit threshold - maintain hover
        // unless new item is significantly closer (within enter threshold)
        if (!newHovered || closestDist >= currentHoveredDist) {
            newHovered = currentHovered;
        }
    }

    // Handle hover state changes with optional debounce
    if (newHovered != currentHovered) {
        // Skip debounce when:
        // 1. Exiting hover entirely (newHovered == nullptr)
        // 2. Current hovered item became invisible (must exit immediately)
        bool forceImmediate = (newHovered == nullptr) ||
            (currentHovered && !currentHovered->IsEffectivelyVisible());

        if constexpr (ENABLE_DEBOUNCE) {
            if (forceImmediate) {
                // Immediate change - no debounce for hover end or invisible items
                CommitHoverChange(isLeft, handNode, newHovered);
                handState.pendingHover.reset();
                handState.pendingHoverTimer = 0.0f;
            } else if (newHovered == currentPending) {
                // Same pending target - accumulate time
                handState.pendingHoverTimer += deltaTime;

                if (handState.pendingHoverTimer >= (DEBOUNCE_MS / 1000.0f)) {
                    // Debounce passed - commit the change
                    CommitHoverChange(isLeft, handNode, newHovered);
                    handState.pendingHover.reset();
                    handState.pendingHoverTimer = 0.0f;
                }
                // else: keep waiting
            } else {
                // Different target - reset pending state
                handState.pendingHover = newHovered;
                handState.pendingHoverTimer = deltaTime;  // start counting from this frame
            }
        } else {
            // No debounce - immediate change
            CommitHoverChange(isLeft, handNode, newHovered);
        }
    } else {
        // Same as current hover - clear any pending
        handState.pendingHover.reset();
        handState.pendingHoverTimer = 0.0f;
    }
}

void InteractionController::UpdateHover(float deltaTime) {
    // Collect all projectiles from hierarchy
    std::vector<Projectile::ControlledProjectilePtr> projectiles;
    CollectProjectiles(m_root, projectiles);

    // Get hand nodes
    RE::NiAVObject* leftHand = VRNodes::GetLeftHand();
    RE::NiAVObject* rightHand = VRNodes::GetRightHand();

    // Update hover state for each hand independently based on tracking mode
    if (m_handTrackingMode == HandTrackingMode::AnyHand) {
        UpdateHoverForHand(true, leftHand, projectiles, deltaTime);
        UpdateHoverForHand(false, rightHand, projectiles, deltaTime);
    } else if (m_handTrackingMode == HandTrackingMode::LeftHand) {
        UpdateHoverForHand(true, leftHand, projectiles, deltaTime);
        // Clear right hand state if mode changed
        if (!m_rightHand.hoveredProjectile.expired()) {
            m_rightHand.Clear();
        }
    } else {
        UpdateHoverForHand(false, rightHand, projectiles, deltaTime);
        // Clear left hand state if mode changed
        if (!m_leftHand.hoveredProjectile.expired()) {
            m_leftHand.Clear();
        }
    }
}

void InteractionController::UpdateScaleAnimation(float deltaTime) {
    if (!m_root) return;

    // Collect all projectiles
    std::vector<Projectile::ControlledProjectilePtr> projectiles;
    CollectProjectiles(m_root, projectiles);

    // Lock hover state for comparisons
    auto leftHovered = m_leftHand.hoveredProjectile.lock();
    auto rightHovered = m_rightHand.hoveredProjectile.lock();

    float lerpFactor = m_hoverTransitionSpeed * deltaTime;
    if (lerpFactor > 1.0f) lerpFactor = 1.0f;

    for (const auto& proj : projectiles) {
        // Skip scale animation if element is not activateable (non-interactive display)
        // Still update hover state for such elements, but no visual feedback
        if (!proj->IsActivateable()) {
            proj->SetHoverScale(1.0f);
            continue;
        }

        // Item is hovered if EITHER hand is hovering it
        bool isHovered = (proj == leftHovered) || (proj == rightHovered);
        float targetScale = isHovered ? m_hoverScale : 1.0f;

        // Get or initialize current scale (use raw ptr as map key)
        auto* rawPtr = proj.get();
        auto it = m_currentScales.find(rawPtr);
        float currentScale = (it != m_currentScales.end()) ? it->second : 1.0f;

        // Lerp toward target
        currentScale = currentScale + (targetScale - currentScale) * lerpFactor;
        m_currentScales[rawPtr] = currentScale;

        // Apply hover scale
        proj->SetHoverScale(currentScale);
    }

    // Clean up scales for projectiles that no longer exist
    // (This is O(n*m) but both are typically small)
    for (auto it = m_currentScales.begin(); it != m_currentScales.end(); ) {
        bool found = false;
        for (const auto& proj : projectiles) {
            if (proj.get() == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_currentScales.erase(it);
        } else {
            ++it;
        }
    }
}

// =============================================================================
// Global Query Functions
// =============================================================================

bool AnyControllerHasHoveredItem(bool isLeft) {
    for (auto* controller : g_registeredControllers) {
        if (controller && controller->GetRoot() && controller->GetRoot()->IsVisible()) {
            // Use the public API to check hover state
            if (controller->GetHoveredProjectile(isLeft) != nullptr) {
                return true;
            }
        }
    }
    return false;
}

} // namespace Widget
