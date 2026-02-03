#pragma once

#include "../projectile/ControlledProjectile.h"
#include "../projectile/IPositionable.h"
#include "../projectile/ProjectileDriver.h"
#include "../InputManager.h"
#include "openvr.h"
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace Widget {

// Which hand to track for interaction detection
enum class HandTrackingMode {
    AnyHand,    // Track both hands independently (default)
    LeftHand,   // Track left hand only
    RightHand   // Track right hand only
};

// Per-hand interaction state
// Uses weak_ptr to safely handle projectile destruction without dangling pointers
struct HandInteractionState {
    Projectile::ControlledProjectileWeakPtr hoveredProjectile;
    Projectile::ControlledProjectileWeakPtr previousHoveredProjectile;
    Projectile::ControlledProjectileWeakPtr grabbedProjectile;
    Projectile::ControlledProjectileWeakPtr activatedProjectile;  // Track for ActivateUp
    bool isGrabbing = false;

    // Debounce state - pending hover target waiting for confirmation
    Projectile::ControlledProjectileWeakPtr pendingHover;
    float pendingHoverTimer = 0.0f;  // seconds elapsed since pending started

    void Clear() {
        hoveredProjectile.reset();
        previousHoveredProjectile.reset();
        grabbedProjectile.reset();
        activatedProjectile.reset();
        isGrabbing = false;
        pendingHover.reset();
        pendingHoverTimer = 0.0f;
    }
};

// Handles hover detection and input for projectile interactions
// Discovers projectiles by traversing the driver hierarchy - no registration needed
// Tracks each hand independently - activation only works for the hand that is hovering
class InteractionController {
public:
    InteractionController() = default;
    ~InteractionController() noexcept;

    // Non-copyable
    InteractionController(const InteractionController&) = delete;
    InteractionController& operator=(const InteractionController&) = delete;

    // === Configuration ===
    void SetHoverThreshold(float distance) { m_hoverThreshold = distance; }
    float GetHoverThreshold() const { return m_hoverThreshold; }

    void SetHoverScale(float scale) { m_hoverScale = scale; }
    float GetHoverScale() const { return m_hoverScale; }

    void SetHoverTransitionSpeed(float speed) { m_hoverTransitionSpeed = speed; }

    void SetHandTrackingMode(HandTrackingMode mode) { m_handTrackingMode = mode; }
    HandTrackingMode GetHandTrackingMode() const { return m_handTrackingMode; }

    // Enable/disable tooltip display on hover (default: true)
    void SetDisplayTooltip(bool display) { m_displayTooltip = display; }
    bool GetDisplayTooltip() const { return m_displayTooltip; }

    // === Input Configuration ===
    void SetActivationButtons(uint64_t buttonMask);
    void SetGrabButtons(uint64_t buttonMask);

    // === Root Driver ===
    // Set the root driver to traverse for projectiles
    // Projectiles are discovered by traversing the hierarchy each frame
    void SetRoot(Projectile::ProjectileDriver* root) { m_root = root; }
    Projectile::ProjectileDriver* GetRoot() const { return m_root; }

    // === Callbacks ===
    void SetCloseCallback(std::function<void()> callback) { m_closeCallback = std::move(callback); }

    // === State Query ===
    // Get hovered projectile for a specific hand (returns locked shared_ptr, may be null if destroyed)
    Projectile::ControlledProjectilePtr GetHoveredProjectile(bool isLeft) const {
        return isLeft ? m_leftHand.hoveredProjectile.lock() : m_rightHand.hoveredProjectile.lock();
    }
    // Legacy: returns either hand's hovered projectile (left preferred)
    Projectile::ControlledProjectilePtr GetHoveredProjectile() const {
        auto left = m_leftHand.hoveredProjectile.lock();
        return left ? left : m_rightHand.hoveredProjectile.lock();
    }
    bool HasHoveredItem() const {
        return !m_leftHand.hoveredProjectile.expired() || !m_rightHand.hoveredProjectile.expired();
    }
    bool IsGrabbing() const {
        return m_leftHand.isGrabbing || m_rightHand.isGrabbing;
    }

    // Per-hand interaction state query (for input blocking across DLLs)
    // Returns true if the specified hand is currently hovering OR grabbing a 3DUI element
    bool IsHandInteracting(bool isLeft) const {
        const auto& hand = isLeft ? m_leftHand : m_rightHand;
        return !hand.hoveredProjectile.expired() || hand.isGrabbing;
    }

    // Clear hover state
    void Clear();

    // Remove input callbacks
    void RemoveCallbacks();

    // === Update (called each frame by Widget) ===
    void Update(float deltaTime);

private:
    // Collect all projectiles from the hierarchy
    void CollectProjectiles(Projectile::IPositionable* node,
                           std::vector<Projectile::ControlledProjectilePtr>& out);

    void UpdateHover(float deltaTime);
    void UpdateHoverForHand(bool isLeft, RE::NiAVObject* handNode,
                            const std::vector<Projectile::ControlledProjectilePtr>& projectiles,
                            float deltaTime);
    void CommitHoverChange(bool isLeft, RE::NiAVObject* handNode,
                           Projectile::ControlledProjectilePtr newHovered);
    void UpdateScaleAnimation(float deltaTime);

    bool OnActivationInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);
    bool OnGrabInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    HandInteractionState& GetHandState(bool isLeft) { return isLeft ? m_leftHand : m_rightHand; }
    const HandInteractionState& GetHandState(bool isLeft) const { return isLeft ? m_leftHand : m_rightHand; }

    // Root driver for hierarchy traversal
    Projectile::ProjectileDriver* m_root = nullptr;

    // Per-hand interaction state
    HandInteractionState m_leftHand;
    HandInteractionState m_rightHand;

    // Hover scale tracking per projectile (by pointer, rebuilt each frame)
    std::unordered_map<Projectile::ControlledProjectile*, float> m_currentScales;

    // Configuration
    float m_hoverThreshold = 10.0f;
    float m_hoverScale = 1.2f;
    float m_hoverTransitionSpeed = 100.0f;
    uint64_t m_activationButtons = 0;
    uint64_t m_grabButtons = 0;
    HandTrackingMode m_handTrackingMode = HandTrackingMode::AnyHand;
    bool m_displayTooltip = true;

    // Callbacks
    std::function<void()> m_closeCallback;
};

// =============================================================================
// Global Query Functions
// =============================================================================

// Query if ANY visible InteractionController has a hovered item for the specified hand.
// Used by ActorMenu to defer to other 3DUI menus when they would handle trigger input.
// This prevents double-activation when a mod's menu is open over a grabbed NPC.
bool AnyControllerHasHoveredItem(bool isLeft);

} // namespace Widget
