#include "ProjectileDriver.h"
#include "InteractionController.h"
#include "DriverUpdateManager.h"
#include "../util/VRNodes.h"
#include "../log.h"

namespace Projectile {

// =============================================================================
// ProjectileDriver Base Class
// =============================================================================

// Destructor must be defined in .cpp where InteractionController is complete
// (unique_ptr needs full type to call destructor)
ProjectileDriver::~ProjectileDriver() {
    // Unregister from DriverUpdateManager if we were a root driver
    if (!m_parent) {
        Widget::DriverUpdateManager::GetSingleton().Unregister(this);
    }
}

ProjectileDriver::ProjectileDriver(ProjectileDriver&& other) noexcept
    : IPositionable(std::move(other))  // Handles m_localVisible, m_localPosition, m_localScale
    , m_subsystem(other.m_subsystem)
    , m_children(std::move(other.m_children))
    , m_anchor(other.m_anchor)
    , m_transitionMode(other.m_transitionMode)
    , m_smoothingSpeed(other.m_smoothingSpeed)
    , m_isGrabbing(other.m_isGrabbing)
    , m_useHapticFeedback(other.m_useHapticFeedback)
    , m_previousAnchor(other.m_previousAnchor)
    , m_grabbedProjectile(std::move(other.m_grabbedProjectile))
    , m_interactionController(std::move(other.m_interactionController))
{
    other.m_subsystem = nullptr;
    other.m_anchor.Clear();
    other.m_isGrabbing = false;
    other.m_previousAnchor.Clear();

    // Update interaction controller's root pointer to this driver
    if (m_interactionController) {
        m_interactionController->SetRoot(this);
    }
}

ProjectileDriver& ProjectileDriver::operator=(ProjectileDriver&& other) noexcept {
    if (this != &other) {
        IPositionable::operator=(std::move(other));  // Handles m_localVisible, m_localPosition, m_localScale
        m_subsystem = other.m_subsystem;
        m_children = std::move(other.m_children);
        m_anchor = other.m_anchor;
        m_transitionMode = other.m_transitionMode;
        m_smoothingSpeed = other.m_smoothingSpeed;
        m_isGrabbing = other.m_isGrabbing;
        m_useHapticFeedback = other.m_useHapticFeedback;
        m_previousAnchor = other.m_previousAnchor;
        m_grabbedProjectile = std::move(other.m_grabbedProjectile);
        m_interactionController = std::move(other.m_interactionController);

        other.m_subsystem = nullptr;
        other.m_anchor.Clear();
        other.m_isGrabbing = false;
        other.m_previousAnchor.Clear();

        // Update interaction controller's root pointer to this driver
        if (m_interactionController) {
            m_interactionController->SetRoot(this);
        }
    }
    return *this;
}

void ProjectileDriver::Initialize() {
    auto& driverMgr = Widget::DriverUpdateManager::GetSingleton();
    m_subsystem = driverMgr.GetProjectileSubsystem();

    if (!m_subsystem) {
        spdlog::error("ProjectileDriver::Initialize - DriverUpdateManager has no subsystem");
        return;
    }

    // Initialize all children (drivers, projectiles, lights, etc.)
    for (auto& child : m_children) {
        child->Initialize();
    }

    // Auto-register root drivers (those without parents) with DriverUpdateManager
    // Child drivers are updated by their parent, not directly by the manager
    if (!m_parent) {
        driverMgr.Register(this);
        spdlog::trace("[Driver '{}'] Initialized and registered (root driver, {} children)",
            GetID(), m_children.size());
    } else {
        spdlog::trace("[Driver '{}'] Initialized (child driver, {} children)",
            GetID(), m_children.size());
    }
}

void ProjectileDriver::Update(float deltaTime) {
    if (!m_localVisible) {
        return;
    }

    // Compute and set local rotation from facing strategy (if configured)
    // This is the scene graph approach: facing sets OUR rotation,
    // and children automatically inherit it via GetWorldRotation()
    // NOTE: Use GetWorldPosition() (not GetCenterPosition()) so facing is computed
    // from the menu's actual rendered position toward the anchor
    if (m_facingStrategy && m_facingAnchor) {
        RE::NiMatrix3 facingRotation = m_facingStrategy->ComputeRotation(
            GetWorldPosition(),
            m_facingAnchor->world.translate);
        SetLocalRotation(facingRotation);
    }

    // Update layout (positions children in LOCAL space only - no rotation needed)
    // Children's world positions will include our rotation via scene graph
    UpdateLayout(deltaTime);

    // Compensate for rotation-induced drift during grab
    // When the driver rotates to face the HMD, the grabbed handle's world position changes
    // even though the driver center stays at hand+offset. We adjust the offset to keep
    // the grabbed handle locked to the hand position.
    if (m_isGrabbing) {
        // Resolve hand node fresh each frame - survives VR controller reconnects/tracking loss
        RE::NiAVObject* handNode = m_grabbingIsLeftHand
            ? VRNodes::GetLeftHand()
            : VRNodes::GetRightHand();

        // Safety check: if hand node unavailable (controller off, tracking lost, etc.)
        // restore previous anchor and end grab gracefully
        if (!handNode) {
            spdlog::warn("ProjectileDriver::Update - Hand node unavailable during grab, restoring previous anchor");
            m_anchor = m_previousAnchor;
            m_isGrabbing = false;
            m_grabbedProjectile.reset();
        } else {
            // Update anchor to track hand position (fresh each frame)
            m_anchor.SetDirect(handNode);

            if (auto grabbedProj = m_grabbedProjectile.lock()) {
                // Grabbed projectile still exists - compensate for drift
                RE::NiPoint3 handleWorldPos = grabbedProj->GetWorldPosition();
                RE::NiPoint3 handPos = handNode->world.translate;
                RE::NiPoint3 drift = handleWorldPos - handPos;

                // Adjust offset to compensate for rotation-induced drift
                RE::NiPoint3 currentOffset = m_anchor.GetOffset();
                m_anchor.SetOffset(currentOffset - drift);
            }
            // If grabbedProjectile expired but hand is valid, just skip drift compensation
        }
    }

    // Update children recursively
    // THREAD SAFETY: Copy before iterating - main thread may call AddChild/Clear while VR thread updates
    auto childrenCopy = m_children;
    for (auto& child : childrenCopy) {
        child->Update(deltaTime);
    }
}

void ProjectileDriver::AddChild(IPositionablePtr child) {
    if (!child) {
        spdlog::warn("ProjectileDriver::AddChild - null child");
        return;
    }

    // Set this driver as the parent BEFORE Initialize() so it knows it's a child
    child->SetParent(this);

    // Add to children FIRST so UpdateLayout can position it
    m_children.push_back(child);

    // If we're already initialized, position and then initialize the child
    // so the projectile spawns at the correct position
    if (m_subsystem) {
        UpdateLayout(0.0f);  // Position child before spawning
        child->Initialize();
    }

    spdlog::trace("[Driver '{}'] AddChild: child='{}' (total: {})",
        GetID(), child->GetID(), m_children.size());
}

void ProjectileDriver::Clear() {
    spdlog::trace("[Driver '{}'] Clear: removing {} children", GetID(), m_children.size());
    // Destroy all projectile children
    for (auto& child : m_children) {
        if (auto* proj = dynamic_cast<ControlledProjectile*>(child.get())) {
            if (proj->IsValid()) {
                spdlog::trace("[Driver '{}'] Clear: destroying projectile '{}'", GetID(), proj->GetID());
                proj->Destroy();
            }
        } else if (auto* childDriver = dynamic_cast<ProjectileDriver*>(child.get())) {
            childDriver->Clear();
        }
    }
    m_children.clear();
}

void ProjectileDriver::UpdateLayout(float deltaTime) {

}

void ProjectileDriver::SetVisible(bool visible) {
    bool wasVisible = m_localVisible;
    if (visible == wasVisible) {
        return;  // No change
    }

    spdlog::trace("[Driver '{}'] SetVisible: {} -> {} (children={})",
        GetID(), wasVisible, visible, m_children.size());

    m_localVisible = visible;

    // Auto-clear interaction state when hiding
    if (wasVisible && !visible && m_interactionController) {
        m_interactionController->Clear();
    }

    // When hiding: notify children via OnParentHide() so they can release resources
    // This does NOT change children's m_localVisible (preserves user intent)
    if (!visible) {
        for (auto& child : m_children) {
            child->OnParentHide();
        }
    }
    // When showing: children's Update() will be called again (since we're visible)
    // and they'll rebind based on their own m_localVisible state
}

void ProjectileDriver::OnParentHide() {
    // Propagate to our children so they can release resources
    spdlog::trace("[Driver '{}'] OnParentHide: propagating to {} children", GetID(), m_children.size());
    for (auto& child : m_children) {
        child->OnParentHide();
    }
}

void ProjectileDriver::SetCenter(const RE::NiPoint3& worldPos) {
    m_anchor.SetWorldPosition(worldPos);
}

void ProjectileDriver::SetAnchor(RE::NiAVObject* node, bool useRotation, bool useScale) {
    m_anchor.SetDirect(node);
    m_anchor.SetUseRotation(useRotation);
    m_anchor.SetUseScale(useScale);
    if (node) {
        spdlog::trace("ProjectileDriver: Set anchor to {:p}, useRot={}, useScale={}",
            (void*)node, useRotation, useScale);
    }
}

void ProjectileDriver::SetTransitionMode(TransitionMode mode) {
    m_transitionMode = mode;
    for (auto& child : m_children) {
        if (auto* proj = dynamic_cast<ControlledProjectile*>(child.get())) {
            proj->SetTransitionMode(mode);
        } else if (auto* childDriver = dynamic_cast<ProjectileDriver*>(child.get())) {
            childDriver->SetTransitionMode(mode);
        }
    }
}

void ProjectileDriver::SetSmoothingSpeed(float speed) {
    m_smoothingSpeed = speed;
    for (auto& child : m_children) {
        if (auto* proj = dynamic_cast<ControlledProjectile*>(child.get())) {
            proj->SetSmoothingSpeed(speed);
        } else if (auto* childDriver = dynamic_cast<ProjectileDriver*>(child.get())) {
            childDriver->SetSmoothingSpeed(speed);
        }
    }
}

bool ProjectileDriver::OnEvent(InputEvent& event) {
    // Base driver implementation:
    // - Handle GrabStart on anchor handles by forwarding to StartDriverPositioning on ROOT driver
    // - Handle GrabEnd if we're currently grabbing
    // - Let other events bubble up

    // Check haptic feedback flag - if disabled and we have a parent (not root),
    // suppress haptic pulse for this event as it bubbles up
    if (!m_useHapticFeedback && m_parent) {
        event.sendHapticPulse = false;
    }

    if (event.type == InputEventType::GrabStart) {
        // Check if the source is a ControlledProjectile with isAnchorHandle
        if (auto* proj = dynamic_cast<ControlledProjectile*>(event.source)) {
            if (proj->IsAnchorHandle()) {
                // Find root driver and start positioning on it (not on this driver)
                ProjectileDriver* root = this;
                while (root->m_parent) {
                    if (auto* parentDriver = dynamic_cast<ProjectileDriver*>(root->m_parent)) {
                        root = parentDriver;
                    } else {
                        break;
                    }
                }
                spdlog::trace("[Driver '{}'] GrabStart: anchor='{}', positioning root='{}'",
                    GetID(), proj->GetID(), root->GetID());
                // Pass which hand and the anchor handle projectile
                root->StartDriverPositioning(event.isLeftHand, proj);
                return true;  // Consumed
            }
        }
        // Non-anchor grabs: let derived class or parent handle
        return false;
    }

    if (event.type == InputEventType::GrabEnd) {
        // Find root driver and check if it's grabbing
        ProjectileDriver* root = this;
        while (root->m_parent) {
            if (auto* parentDriver = dynamic_cast<ProjectileDriver*>(root->m_parent)) {
                root = parentDriver;
            } else {
                break;
            }
        }
        if (root->m_isGrabbing) {
            spdlog::trace("[Driver '{}'] GrabEnd: ending positioning on root='{}'", GetID(), root->GetID());
            root->EndDriverPositioning();
            return true;  // Consumed
        }
        return false;
    }

    // Other events (Hover, Activate) - don't handle at driver level, let them bubble
    return false;
}

// Helper to find first anchor handle projectile in hierarchy
static ControlledProjectile* FindFirstAnchorHandle(const std::vector<IPositionablePtr>& children) {
    for (const auto& child : children) {
        if (auto* proj = dynamic_cast<ControlledProjectile*>(child.get())) {
            if (proj->IsAnchorHandle()) {
                return proj;
            }
        } else if (auto* driver = dynamic_cast<ProjectileDriver*>(child.get())) {
            // Recurse into sub-drivers
            if (auto* found = FindFirstAnchorHandle(driver->GetChildren())) {
                return found;
            }
        }
    }
    return nullptr;
}

// Compute local offset from a node up to (but not including) the root driver
// This is anchor-independent and avoids timing issues with world position computation
static RE::NiPoint3 ComputeLocalOffsetToRoot(IPositionable* node, ProjectileDriver* root) {
    RE::NiPoint3 offset{0, 0, 0};
    IPositionable* current = node;
    while (current && current != root) {
        offset = offset + current->GetLocalPosition();
        current = current->GetParent();
    }
    return offset;
}

void ProjectileDriver::StartDriverPositioning(bool isLeftHand,
                                               ControlledProjectile* grabbedProjectile) {
    // Resolve hand node - will be refreshed each frame in Update()
    RE::NiAVObject* handNode = isLeftHand
        ? VRNodes::GetLeftHand()
        : VRNodes::GetRightHand();

    if (!handNode) {
        spdlog::warn("ProjectileDriver::StartDriverPositioning - hand node unavailable");
        return;
    }

    if (m_isGrabbing) {
        spdlog::trace("ProjectileDriver::StartDriverPositioning - Already grabbing, ignoring");
        return;
    }

    // Store current anchor to restore later
    m_previousAnchor = m_anchor;

    // Store which hand is grabbing - node will be resolved fresh each frame
    m_grabbingIsLeftHand = isLeftHand;

    // Create new anchor for the grab node
    m_anchor.Clear();
    m_anchor.SetDirect(handNode);

    // If no projectile specified, try to find an anchor handle for drift compensation
    if (!grabbedProjectile) {
        grabbedProjectile = FindFirstAnchorHandle(m_children);
    }

    if (grabbedProjectile) {
        // Grabbing/tracking an anchor handle - preserve relative position so handle stays at hand
        // IMPORTANT: Use GetCenterPosition() (NOT GetWorldPosition()) because:
        // GetWorldPosition() = m_localPosition + GetCenterPosition()
        // If we use GetWorldPosition(), m_localPosition gets counted twice
        RE::NiPoint3 handWorldPos = handNode->world.translate;
        RE::NiPoint3 driverCenterWorld = GetCenterPosition();
        RE::NiPoint3 grabOffset = driverCenterWorld - handWorldPos;
        m_anchor.SetOffset(grabOffset);

        spdlog::trace("[Driver '{}'] StartDriverPositioning: hand={} anchor='{}' offset=({:.1f},{:.1f},{:.1f})",
            GetID(), isLeftHand ? "left" : "right", grabbedProjectile->GetID(),
            grabOffset.x, grabOffset.y, grabOffset.z);

        // Store weak reference for drift compensation during facing rotation
        m_grabbedProjectile = grabbedProjectile->shared_from_this();
    } else {
        // No anchor handle found - put driver center directly at hand (zero offset)
        m_anchor.SetOffset({0, 0, 0});
        m_grabbedProjectile.reset();

        spdlog::trace("[Driver '{}'] StartDriverPositioning: hand={} (no anchor handle, spawn at hand center)",
            GetID(), isLeftHand ? "left" : "right");
    }

    m_isGrabbing = true;
}

void ProjectileDriver::UpdateGrabbedProjectile(ControlledProjectile* proj) {
    if (!m_isGrabbing) {
        return;  // Only relevant during grab
    }

    // If no projectile specified, search for first anchor handle
    if (!proj) {
        proj = FindFirstAnchorHandle(m_children);
    }

    if (proj) {
        m_grabbedProjectile = proj->shared_from_this();
        spdlog::trace("ProjectileDriver::UpdateGrabbedProjectile - Now tracking anchor handle");
    } else {
        m_grabbedProjectile.reset();
    }
}

void ProjectileDriver::EndDriverPositioning() {
    if (!m_isGrabbing) {
        spdlog::trace("ProjectileDriver::EndDriverPositioning - Not grabbing, ignoring");
        return;
    }

    // Clear grabbed projectile reference
    m_grabbedProjectile.reset();

    // Get current center position (where the driver is now after being dragged)
    RE::NiPoint3 currentCenter = GetCenterPosition();

    // If we had a previous anchor node, calculate new offset
    auto* prevAnchorNode = m_previousAnchor.ResolveNode();
    if (prevAnchorNode) {
        RE::NiPoint3 anchorPos = prevAnchorNode->world.translate;
        RE::NiPoint3 newOffset = currentCenter - anchorPos;

        // Update the previous anchor with the new offset and restore it
        m_previousAnchor.SetOffset(newOffset);
        m_anchor = m_previousAnchor;

        spdlog::trace("[Driver '{}'] EndDriverPositioning: restored anchor with offset=({:.1f},{:.1f},{:.1f})",
            GetID(), newOffset.x, newOffset.y, newOffset.z);
    } else {
        // No previous anchor node - just set world position
        m_anchor.SetWorldPosition(currentCenter);

        spdlog::trace("[Driver '{}'] EndDriverPositioning: set world pos=({:.1f},{:.1f},{:.1f})",
            GetID(), currentCenter.x, currentCenter.y, currentCenter.z);
    }

    m_isGrabbing = false;
}

void ProjectileDriver::SetInteractionController(std::unique_ptr<Widget::InteractionController> controller) {
    m_interactionController = std::move(controller);
    if (m_interactionController) {
        m_interactionController->SetRoot(this);
        spdlog::trace("ProjectileDriver: Set interaction controller");
    }
}

Widget::InteractionController* ProjectileDriver::GetInteractionController() {
    // If we have a controller, return it
    if (m_interactionController) {
        return m_interactionController.get();
    }

    // Otherwise traverse up to parent driver
    if (m_parent) {
        if (auto* parentDriver = dynamic_cast<ProjectileDriver*>(m_parent)) {
            return parentDriver->GetInteractionController();
        }
    }

    return nullptr;
}

const Widget::InteractionController* ProjectileDriver::GetInteractionController() const {
    // If we have a controller, return it
    if (m_interactionController) {
        return m_interactionController.get();
    }

    // Otherwise traverse up to parent driver
    if (m_parent) {
        if (auto* parentDriver = dynamic_cast<const ProjectileDriver*>(m_parent)) {
            return parentDriver->GetInteractionController();
        }
    }

    return nullptr;
}

} // namespace Projectile
