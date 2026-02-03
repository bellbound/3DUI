#pragma once

#include "Anchor.h"
#include "ControlledProjectile.h"
#include "ProjectileSubsystem.h"
#include "IPositionable.h"
#include "FacingStrategy.h"
#include <vector>
#include <memory>

namespace Widget {
    class InteractionController;  // Forward declaration
}

namespace Projectile {

// Abstract base class for projectile arrangement drivers
// Drivers position children (projectiles or sub-drivers) relative to a center point.
// Implements IPositionable to support composable driver hierarchies.
class ProjectileDriver : public IPositionable {
public:
    ProjectileDriver() = default;
    ~ProjectileDriver() override;  // Defined in .cpp where InteractionController is complete

    // Non-copyable, movable
    ProjectileDriver(const ProjectileDriver&) = delete;
    ProjectileDriver& operator=(const ProjectileDriver&) = delete;
    ProjectileDriver(ProjectileDriver&& other) noexcept;
    ProjectileDriver& operator=(ProjectileDriver&& other) noexcept;

    // === Initialization ===
    bool IsInitialized() const { return m_subsystem != nullptr; }
    ProjectileSubsystem* GetSubsystem() const { return m_subsystem; }

    // === Update (call each frame) ===
    void Update(float deltaTime);

    // === Child Management ===
    // Add any IPositionable child (projectile or sub-driver)
    // Sets this driver as the parent of the child
    virtual void AddChild(IPositionablePtr child);

    // Clear all children (destroys projectiles, releases sub-drivers)
    virtual void Clear();

    // Get children
    const std::vector<IPositionablePtr>& GetChildren() const { return m_children; }

    // === Layout ===
    // Derived classes override UpdateLayout() to implement specific layouts (circle, wheel, grid, etc.)

    // === Visibility (IPositionable overrides) ===
    void SetVisible(bool visible) override;
    bool IsVisible() const override { return m_localVisible; }
    void OnParentHide() override;

    // === Center Point ===
    // Set center to a fixed world position
    void SetCenter(const RE::NiPoint3& worldPos);

    // Set center anchor to a direct NiAVObject pointer
    void SetAnchor(RE::NiAVObject* node, bool useRotation = false, bool useScale = false);

    // Get the current center world position
    RE::NiPoint3 GetCenterPosition() const { return m_anchor.GetWorldPosition(); }

    // === Facing Anchor (for look-at-player behavior) ===
    // Set an anchor node that the layout will orient toward (e.g., HMD/player head)
    void SetFacingAnchor(RE::NiAVObject* anchor) { m_facingAnchor = anchor; }
    RE::NiAVObject* GetFacingAnchor() const { return m_facingAnchor; }
    bool HasFacingAnchor() const { return m_facingAnchor != nullptr; }

    // Set the facing strategy (determines how the layout rotates toward the anchor)
    void SetFacingStrategy(IFacingStrategy* strategy) { m_facingStrategy = strategy; }

    // IPositionable override: compute world position from anchor + parent chain
    // Scene graph: if we have a parent, our position is rotated by parent's world rotation
    RE::NiPoint3 GetWorldPosition() const override {
        if (m_parent) {
            // Child driver: apply parent's rotation to our local position
            RE::NiMatrix3 parentWorldRot = m_parent->GetWorldRotation();
            RE::NiPoint3 rotatedLocalPos = RotatePoint(parentWorldRot, m_localPosition);
            return m_parent->GetWorldPosition() + rotatedLocalPos;
        }
        // Root driver: position is anchor + local offset
        RE::NiPoint3 anchorPos = m_anchor.GetWorldPosition();
        return m_localPosition + anchorPos;
    }

    // IPositionable override: event handling for drivers
    // Base implementation handles anchor handle grabs by forwarding to StartDriverPositioning
    // Returns false to let events bubble up by default
    bool OnEvent(InputEvent& event) override;

    // === Interaction Controller ===
    // Set the interaction controller for this driver (takes ownership)
    // Only root drivers should have an interaction controller
    void SetInteractionController(std::unique_ptr<Widget::InteractionController> controller);

    // Get the interaction controller (traverses hierarchy to root if needed)
    Widget::InteractionController* GetInteractionController();
    const Widget::InteractionController* GetInteractionController() const;

    // === Grab Control ===
    // Start driver positioning - temporarily anchors the entire driver to the grabbing hand
    // Resolves hand node fresh each frame to survive VR controller reconnects/tracking loss
    // If grabbedProjectile is provided, offsets anchor so that projectile appears at hand position
    virtual void StartDriverPositioning(bool isLeftHand,
                                        ControlledProjectile* grabbedProjectile = nullptr);

    // End driver positioning - restores previous anchor but updates offset to current position
    virtual void EndDriverPositioning();

    // Check if currently being grabbed
    bool IsGrabbing() const { return m_isGrabbing; }

    // === Haptic Feedback ===
    // When false, suppresses haptic pulses for events bubbling through this container
    void SetUseHapticFeedback(bool enabled) { m_useHapticFeedback = enabled; }
    bool GetUseHapticFeedback() const { return m_useHapticFeedback; }

    // Update the grabbed projectile reference (call after populating if StartDriverPositioning
    // was called before items existed). Searches for first anchor handle if none set.
    void UpdateGrabbedProjectile(ControlledProjectile* proj = nullptr);

    // === Transition Settings ===
    void SetTransitionMode(TransitionMode mode);
    void SetSmoothingSpeed(float speed);

protected:
    // Initialize the driver. Gets subsystem from DriverUpdateManager automatically.
    // Root drivers are auto-registered with DriverUpdateManager for frame updates.
    // Child drivers (those with a parent) are NOT auto-registered.
    // Protected: use RootDriver::Spawn() to start the initialization chain.
    virtual void Initialize();

    // Override to implement custom layout update logic
    // Note: Children should be positioned in LOCAL coordinates only.
    // Parent rotation is automatically applied via scene graph (GetWorldPosition).
    virtual void UpdateLayout(float deltaTime);

    // Access to children for derived classes (mutable)
    std::vector<IPositionablePtr>& GetChildrenMutable() { return m_children; }

    ProjectileSubsystem* m_subsystem = nullptr;

    // Transition settings (accessible to derived classes for projectile configuration)
    TransitionMode m_transitionMode = TransitionMode::Lerp;
    float m_smoothingSpeed = 12.0f;

private:
    std::vector<IPositionablePtr> m_children;  // Owned children (projectiles and/or sub-drivers)
    Anchor m_anchor;
    // Note: m_localVisible is inherited from IPositionable

    // Facing anchor and strategy (for look-at-player behavior)
    RE::NiAVObject* m_facingAnchor = nullptr;
    IFacingStrategy* m_facingStrategy = nullptr;

    // Grab state
    bool m_isGrabbing = false;
    bool m_grabbingIsLeftHand = false;  // Which hand initiated the grab (for fresh node lookup each frame)
    bool m_useHapticFeedback = true;    // When false, suppresses haptics for events bubbling through
    Anchor m_previousAnchor;
    ControlledProjectileWeakPtr m_grabbedProjectile;  // Weak ref for drift compensation (survives projectile destruction)

    // Interaction controller (only root drivers typically have one)
    std::unique_ptr<Widget::InteractionController> m_interactionController;
};

} // namespace Projectile
