#include "ControlledProjectile.h"
#include "ProjectileSubsystem.h"
#include "Drivers/TextDriver.h"
#include "../log.h"

#include <chrono>
#include <codecvt>
#include <locale>

namespace Projectile {

ControlledProjectile::~ControlledProjectile() {
    if (m_valid) {
        Destroy();
    }
}

ControlledProjectile::ControlledProjectile(ControlledProjectile&& other) noexcept
    : IPositionable(std::move(other))
    , m_subsystem(other.m_subsystem)
    , m_uuid(other.m_uuid)
    , m_modelPath(std::move(other.m_modelPath))
    , m_texturePath(std::move(other.m_texturePath))
    , m_borderColor(std::move(other.m_borderColor))
    , m_text(std::move(other.m_text))
    , m_baseScale(other.m_baseScale)
    , m_scaleCorrection(other.m_scaleCorrection)
    , m_rotationCorrection(other.m_rotationCorrection)
    , m_isAnchorHandle(other.m_isAnchorHandle)
    , m_closeOnActivate(other.m_closeOnActivate)
    , m_useHapticFeedback(other.m_useHapticFeedback)
    , m_activateable(other.m_activateable)
    , m_onActivateDown(std::move(other.m_onActivateDown))
    , m_onActivateUp(std::move(other.m_onActivateUp))
    , m_onHoverEnter(std::move(other.m_onHoverEnter))
    , m_onHoverExit(std::move(other.m_onHoverExit))
    , m_onGrabStart(std::move(other.m_onGrabStart))
    , m_onGrabEnd(std::move(other.m_onGrabEnd))
    , m_formIndex(other.m_formIndex)
    , m_valid(other.m_valid)
    , m_billboardMode(other.m_billboardMode)
    , m_smoother(std::move(other.m_smoother))
    , m_gameProjectile(std::move(other.m_gameProjectile))
    // Use exchange() for atomics to atomically transfer ownership and invalidate source
    , m_bindState(other.m_bindState.exchange(BindState::Unbound))
    , m_hoverScale(other.m_hoverScale)
    , m_lastHeading(other.m_lastHeading)
    , m_fireGeneration(other.m_fireGeneration.exchange(0))
    , m_background(std::move(other.m_background))
    , m_labelTextDriver(std::move(other.m_labelTextDriver))
    , m_labelText(std::move(other.m_labelText))
    , m_labelTextScale(other.m_labelTextScale)
    , m_labelTextVisible(other.m_labelTextVisible)
    , m_labelOffset(other.m_labelOffset)
{
    other.m_subsystem = nullptr;
    other.m_uuid = UUID::Invalid();
    other.m_formIndex = -1;
    other.m_valid = false;
    other.m_smoother.Reset();
    // Note: atomics already invalidated via exchange() above
    other.m_hoverScale = 1.0f;
    other.m_lastHeading = 0.0f;
    // Label text members already moved, reset to defaults
    other.m_labelTextScale = 1.0f;
    other.m_labelTextVisible = true;
    other.m_labelOffset = {0, 0, -10.0f};
}

ControlledProjectile& ControlledProjectile::operator=(ControlledProjectile&& other) noexcept {
    if (this != &other) {
        if (m_valid) {
            Destroy();
        }

        IPositionable::operator=(std::move(other));
        m_subsystem = other.m_subsystem;
        m_uuid = other.m_uuid;
        m_modelPath = std::move(other.m_modelPath);
        m_texturePath = std::move(other.m_texturePath);
        m_borderColor = std::move(other.m_borderColor);
        m_text = std::move(other.m_text);
        m_baseScale = other.m_baseScale;
        m_scaleCorrection = other.m_scaleCorrection;
        m_rotationCorrection = other.m_rotationCorrection;
        m_isAnchorHandle = other.m_isAnchorHandle;
        m_closeOnActivate = other.m_closeOnActivate;
        m_useHapticFeedback = other.m_useHapticFeedback;
        m_activateable = other.m_activateable;
        m_onActivateDown = std::move(other.m_onActivateDown);
        m_onActivateUp = std::move(other.m_onActivateUp);
        m_onHoverEnter = std::move(other.m_onHoverEnter);
        m_onHoverExit = std::move(other.m_onHoverExit);
        m_onGrabStart = std::move(other.m_onGrabStart);
        m_onGrabEnd = std::move(other.m_onGrabEnd);
        m_formIndex = other.m_formIndex;
        m_valid = other.m_valid;
        m_billboardMode = other.m_billboardMode;
        m_smoother = std::move(other.m_smoother);
        m_gameProjectile = std::move(other.m_gameProjectile);
        // Use exchange() for atomics to atomically transfer ownership and invalidate source
        m_bindState.store(other.m_bindState.exchange(BindState::Unbound));
        m_hoverScale = other.m_hoverScale;
        m_lastHeading = other.m_lastHeading;
        m_fireGeneration.store(other.m_fireGeneration.exchange(0));
        m_background = std::move(other.m_background);
        m_labelTextDriver = std::move(other.m_labelTextDriver);
        m_labelText = std::move(other.m_labelText);
        m_labelTextScale = other.m_labelTextScale;
        m_labelTextVisible = other.m_labelTextVisible;
        m_labelOffset = other.m_labelOffset;

        other.m_subsystem = nullptr;
        other.m_uuid = UUID::Invalid();
        other.m_formIndex = -1;
        other.m_valid = false;
        other.m_smoother.Reset();
        // Note: atomics already invalidated via exchange() above
        other.m_hoverScale = 1.0f;
        other.m_lastHeading = 0.0f;
        // Label text members already moved, reset to defaults
        other.m_labelTextScale = 1.0f;
        other.m_labelTextVisible = true;
        other.m_labelOffset = {0, 0, -10.0f};
    }
    return *this;
}

bool ControlledProjectile::IsValid() const {
    return m_valid && m_subsystem != nullptr;
}

RE::NiPoint3 ControlledProjectile::GetPosition() const {
    return m_smoother.GetCurrent().position;
}

RE::NiPoint3 ControlledProjectile::GetWorldPosition() const {
    // Scene graph: WorldPos = Parent.WorldPos + Parent.WorldRot x LocalPos
    // This ensures parent rotation affects our position in world space
    RE::NiPoint3 localPos = GetLocalPosition();
    if (m_parent) {
        RE::NiMatrix3 parentWorldRot = m_parent->GetWorldRotation();
        RE::NiPoint3 rotatedLocalPos = RotatePoint(parentWorldRot, localPos);
        return m_parent->GetWorldPosition() + rotatedLocalPos;
    }
    return localPos;
}

float ControlledProjectile::GetWorldScale() const {
    // Multiply local scale by parent's world scale if we have a parent
    // Then apply baseScale (user-defined), scaleCorrection (from bounds), and hover scale as final multipliers
    // Final scale = parentWorldScale * localScale * baseScale * scaleCorrection * hoverScale
    float parentScale = m_parent ? m_parent->GetWorldScale() * m_localScale : m_localScale;
    return parentScale * m_baseScale * m_scaleCorrection * m_hoverScale;
}

RE::NiMatrix3 ControlledProjectile::GetWorldRotation() const {
    // Get base world rotation from parent chain
    RE::NiMatrix3 worldRot = IPositionable::GetWorldRotation();

    // Apply rotation correction if any component is non-zero
    // Correction is applied in model space (rightmost in multiplication chain)
    // FinalWorldRot = ParentWorldRot x LocalRot x CorrectionRot
    // Note: rotationCorrection is specified in DEGREES for user convenience
    const auto& corr = m_rotationCorrection;
    if (corr.x != 0.0f || corr.y != 0.0f || corr.z != 0.0f) {
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        RE::NiPoint3 corrRadians(corr.x * DEG_TO_RAD, corr.y * DEG_TO_RAD, corr.z * DEG_TO_RAD);
        RE::NiMatrix3 correctionMatrix = EulerToMatrix(corrRadians);
        worldRot = MultiplyMatrices(worldRot, correctionMatrix);
    }

    return worldRot;
}

void ControlledProjectile::SetRotation(const RE::NiPoint3& rot) {
    if (!IsValid()) {
        return;
    }

    // Convert Euler angles (radians) to rotation matrix
    // The public API accepts (pitch, roll, yaw) as NiPoint3
    RE::NiMatrix3 rotMatrix = EulerToMatrix(rot);

    // Use target to preserve lerp destination
    ProjectileTransform transform = m_smoother.GetTarget();
    transform.rotation = rotMatrix;
    SetTransform(transform);
}

void ControlledProjectile::SetRotation(float pitch, float roll, float yaw) {
    SetRotation(RE::NiPoint3(pitch, roll, yaw));
}

RE::NiPoint3 ControlledProjectile::GetRotation() const {
    // Convert rotation matrix back to Euler angles for API compatibility
    return MatrixToEuler(m_smoother.GetCurrent().rotation);
}

void ControlledProjectile::SetLocalScale(float scale) {
    m_localScale = scale;
    // Actual application happens in Update() via GetWorldScale()
}

void ControlledProjectile::SetHoverScale(float scale) {
    m_hoverScale = scale;
    // Actual application happens in Update() via GetWorldScale()
}

void ControlledProjectile::SetTransform(const ProjectileTransform& transform) {
    if (!IsValid()) {
        return;
    }

    // If starting a new transition, initialize from current game position
    if (m_smoother.GetMode() == TransitionMode::Lerp && !m_smoother.IsTransitioning()) {
        m_smoother.SetCurrent(m_gameProjectile.GetTargetTransform());
    }

    // Set target (smoother handles instant vs lerp mode)
    m_smoother.SetTarget(transform);

    // In instant mode, apply immediately
    if (m_smoother.GetMode() == TransitionMode::Instant) {
        m_gameProjectile.SetTransform(m_smoother.GetCurrent());
    }
}

ProjectileTransform ControlledProjectile::GetTransform() const {
    return m_smoother.GetCurrent();
}

bool ControlledProjectile::OnEvent(InputEvent& event) {
    // Check haptic feedback flags - if disabled, suppress haptic pulse for this event
    // Either flag being false disables haptics for events on this element
    if (!m_useHapticFeedback || !m_activateable) {
        event.sendHapticPulse = false;
    }

    // Dispatch to the appropriate callback based on event type
    // If callback is set and returns true, event is consumed (stops bubbling)
    switch (event.type) {
        case InputEventType::ActivateDown:
            if (m_onActivateDown && m_onActivateDown()) {
                return true;
            }
            break;
        case InputEventType::ActivateUp:
            if (m_onActivateUp && m_onActivateUp()) {
                return true;
            }
            break;
        case InputEventType::HoverEnter:
            if (m_onHoverEnter && m_onHoverEnter()) {
                return true;
            }
            break;
        case InputEventType::HoverExit:
            if (m_onHoverExit && m_onHoverExit()) {
                return true;
            }
            break;
        case InputEventType::GrabStart:
            if (m_onGrabStart && m_onGrabStart()) {
                return true;
            }
            break;
        case InputEventType::GrabEnd:
            if (m_onGrabEnd && m_onGrabEnd()) {
                return true;
            }
            break;
    }
    // No callback set or callback returned false - let event bubble up
    return false;
}

void ControlledProjectile::SetVisible(bool visible) {
    // Before initialization - just pre-configure desired state
    // Initialize() will respect m_localVisible when it runs
    if (!IsValid()) {
        spdlog::trace("[Visibility] {} SetVisible({}) SKIP: not valid (pre-init)",
            m_uuid.ToString(), visible);
        m_localVisible = visible;
        return;
    }

    // Early out if already in desired state
    if (visible == m_localVisible) {
        return;
    }

    BindState state = m_bindState.load();
    spdlog::trace("[Visibility] {} SetVisible({}) m_localVisible={}->{} state={}",
        m_uuid.ToString(), visible, m_localVisible, visible, static_cast<int>(state));

    // Update user intent
    m_localVisible = visible;

    // Handle show request
    if (visible) {
        // Only actually bind if effectively visible (parent chain is visible too)
        // and currently unbound (not firing or bound)
        if (IsEffectivelyVisible() && state == BindState::Unbound) {
            RebindProjectile();
        } else {
            spdlog::trace("[Visibility] {} show deferred: effectivelyVisible={} state={}",
                m_uuid.ToString(), IsEffectivelyVisible(), static_cast<int>(state));
        }

        // Propagate to background - initialize if needed
        // Background may not have been initialized if parent was hidden during primary's Initialize()
        if (m_background) {
            if (!m_background->IsValid() && IsEffectivelyVisible()) {
                m_background->Initialize();
            }
            m_background->SetVisible(true);
        }

        // Propagate to label text driver - initialize if needed
        // Label may not have been initialized if parent was hidden during primary's Initialize()
        if (m_labelTextDriver && m_labelTextVisible) {
            if (!m_labelTextDriver->IsInitialized() && IsEffectivelyVisible()) {
                m_labelTextDriver->Initialize();
            }
            m_labelTextDriver->SetVisible(true);
        }
        return;
    }

    // Handle hide request - unbind to release resources
    UnbindProjectile();

    // Propagate to background
    if (m_background) {
        m_background->SetVisible(visible);
    }

    // Propagate to label text driver
    if (m_labelTextDriver) {
        m_labelTextDriver->SetVisible(visible);
    }
}

void ControlledProjectile::OnParentHide() {
    // Parent is hiding - release resources but preserve m_localVisible (user intent)
    // When parent shows again, Update() will rebind if m_localVisible is true
    // UnbindProjectile handles: Bound, Firing, and Unbound cases
    BindState state = m_bindState.load();
    spdlog::trace("[Visibility] {} OnParentHide m_localVisible={} state={} valid={}",
        m_uuid.ToString(), m_localVisible, static_cast<int>(state), IsValid());
    UnbindProjectile();

    // Propagate to background
    if (m_background) {
        m_background->OnParentHide();
    }

    // Propagate to label text driver
    if (m_labelTextDriver) {
        m_labelTextDriver->OnParentHide();
    }
}

bool ControlledProjectile::IsVisible() const {
    // Return local visibility (user intent)
    // Use IsEffectivelyVisible() to check if actually rendered
    return m_localVisible;
}

void ControlledProjectile::UnbindProjectile() {
    if (!IsValid()) {
        spdlog::trace("[Visibility] {} UnbindProjectile: SKIP not valid", m_uuid.ToString());
        return;
    }

    // Atomically transition to Unbound state
    // Try Firing -> Unbound first (cancel pending bind)
    BindState expected = BindState::Firing;
    if (m_bindState.compare_exchange_strong(expected, BindState::Unbound)) {
        // Successfully cancelled pending bind
        spdlog::trace("[BindState] {} Firing -> Unbound (cancel pending)", m_uuid.ToString());
        ++m_fireGeneration;

        if (m_subsystem && m_formIndex >= 0) {
            m_subsystem->ReleaseForm(m_formIndex);
        }
        m_formIndex = -1;

        spdlog::trace("[Visibility] {} UnbindProjectile: cancelled pending bind (gen={})",
            m_uuid.ToString(), m_fireGeneration.load());
        return;
    }

    // Try Bound -> Unbound (normal unbind)
    expected = BindState::Bound;
    if (m_bindState.compare_exchange_strong(expected, BindState::Unbound)) {
        // Successfully transitioned from Bound to Unbound
        spdlog::trace("[BindState] {} Bound -> Unbound", m_uuid.ToString());
        ++m_fireGeneration;

        // Cache current transform before releasing
        if (m_gameProjectile.IsBound()) {
            m_smoother.SetCurrent(m_gameProjectile.GetTargetTransform());
        }

        // Unbind and mark for deletion
        m_gameProjectile.MarkForDeletion();
        m_gameProjectile.Unbind();

        // Release the form
        if (m_subsystem && m_formIndex >= 0) {
            m_subsystem->ReleaseForm(m_formIndex);
        }
        m_formIndex = -1;

        spdlog::trace("[Visibility] {} UnbindProjectile: unbound (gen={})",
            m_uuid.ToString(), m_fireGeneration.load());
        return;
    }

    // Already Unbound - nothing to do
    spdlog::trace("[Visibility] {} UnbindProjectile: SKIP already unbound", m_uuid.ToString());
}

void ControlledProjectile::RebindProjectile() {
    // [DIAG] Track rebind timing
    auto rebindStart = std::chrono::high_resolution_clock::now();
    spdlog::trace("[REBIND] {} RebindProjectile: ENTER", m_uuid.ToString());

    if (!IsValid()) {
        return;
    }

    // Atomically transition Unbound -> Firing
    // This prevents duplicate fires and ensures thread safety
    BindState expected = BindState::Unbound;
    if (!m_bindState.compare_exchange_strong(expected, BindState::Firing)) {
        // Already Firing or Bound - nothing to do
        spdlog::trace("[Visibility] {} RebindProjectile: SKIP state={} (not Unbound)",
            m_uuid.ToString(), static_cast<int>(expected));
        return;
    }

    // We now own the Firing state - proceed with fire
    spdlog::trace("[BindState] {} Unbound -> Firing", m_uuid.ToString());
    if (!m_subsystem) {
        spdlog::warn("[Visibility] {} RebindProjectile: no subsystem", m_uuid.ToString());
        spdlog::trace("[BindState] {} Firing -> Unbound (no subsystem)", m_uuid.ToString());
        m_bindState.store(BindState::Unbound);  // Revert state
        return;
    }

    // Re-acquire a form for our model
    std::string modelPath = !m_texturePath.empty()
        ? "meshes\\3DUI\\icon_template.nif"
        : m_modelPath;
    auto acquireStart = std::chrono::high_resolution_clock::now();
    int newFormIndex = m_subsystem->AcquireForm(modelPath);
    auto acquireEnd = std::chrono::high_resolution_clock::now();
    auto acquireTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(acquireEnd - acquireStart).count();
    if (acquireTimeUs > 200) {
        spdlog::trace("[REBIND] {} AcquireForm took {}us (model='{}')",
            m_uuid.ToString(), acquireTimeUs, modelPath);
    }
    if (newFormIndex < 0) {
        spdlog::warn("[Visibility] {} RebindProjectile: no form for '{}'",
            m_uuid.ToString(), modelPath);
        spdlog::trace("[BindState] {} Firing -> Unbound (no form)", m_uuid.ToString());
        m_bindState.store(BindState::Unbound);  // Revert state
        return;
    }

    m_formIndex = newFormIndex;

    // Increment generation BEFORE firing - any pending tasks from previous fires become stale
    ++m_fireGeneration;

    // Fire the projectile through subsystem (uses current transform from smoother)
    auto fireStart = std::chrono::high_resolution_clock::now();
    bool fireOk = m_subsystem->FireProjectileFor(this);
    auto fireEnd = std::chrono::high_resolution_clock::now();
    auto fireTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(fireEnd - fireStart).count();
    if (fireTimeUs > 200) {
        spdlog::trace("[REBIND] {} FireProjectileFor took {}us (form={} gen={})",
            m_uuid.ToString(), fireTimeUs, newFormIndex, m_fireGeneration.load());
    }
    if (!fireOk) {
        spdlog::warn("[Visibility] {} RebindProjectile: fire failed", m_uuid.ToString());
        m_subsystem->ReleaseForm(newFormIndex);
        m_formIndex = -1;
        spdlog::trace("[BindState] {} Firing -> Unbound (fire failed)", m_uuid.ToString());
        m_bindState.store(BindState::Unbound);  // Revert state
        return;
    }

    // [DIAG] Log rebind duration
    auto rebindEnd = std::chrono::high_resolution_clock::now();
    auto rebindTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(rebindEnd - rebindStart).count();
    spdlog::trace("[REBIND] {} RebindProjectile: fire started (form={} gen={}) took {}us",
        m_uuid.ToString(), newFormIndex, m_fireGeneration.load(), rebindTimeUs);
    if (rebindTimeUs > 1000) {
        spdlog::warn("[PERF] RebindProjectile took {}us UUID={}", rebindTimeUs, m_uuid.ToString());
    }
}

void ControlledProjectile::SetBillboardMode(BillboardMode mode) {
    m_billboardMode = mode;
}

void ControlledProjectile::Update(float deltaTime) {
    if (!IsValid()) {
        return;
    }

    BindState state = m_bindState.load();

    // Check if we should rebind (parent became visible again)
    // This handles the case where parent hid us via OnParentHide(), then showed again
    // Only try if Unbound (RebindProjectile will atomically check and transition)
    if (m_localVisible && state == BindState::Unbound && IsEffectivelyVisible()) {
        spdlog::trace("[Visibility] {} Update: parent visible, triggering rebind", m_uuid.ToString());
        RebindProjectile();
        state = m_bindState.load();  // Refresh state after potential transition
    }

    // Update billboard first - this sets our local rotation
    UpdateBillboard();

    // Compute world transform from scene graph hierarchy
    ProjectileTransform transform = m_smoother.GetTarget();
    transform.position = GetWorldPosition();
    transform.scale = GetWorldScale();

    // Get world rotation matrix from scene graph
    // This composes parent rotation with our local (billboard) rotation
    transform.rotation = GetWorldRotation();

    // If starting a new transition and we have a bound game projectile, initialize from current game position
    if (state == BindState::Bound && m_smoother.GetMode() == TransitionMode::Lerp && !m_smoother.IsTransitioning()) {
        m_smoother.SetCurrent(m_gameProjectile.GetTargetTransform());
    }
    m_smoother.SetTarget(transform);

    // Apply transition smoothing and update game projectile (if bound)
    UpdateTransition(deltaTime);

    // Apply pending texture from main thread (texture loading is not thread-safe)
    // This was previously called from ApplyTransform() on hook threads, causing crashes
    if (m_bindState.load() == BindState::Bound) {
        m_gameProjectile.ApplyPendingTexture();
    }

    // Update background if present
    if (m_background) {
        // Initialize background if needed (handles deferred initialization when parent was hidden
        // during primary's Initialize(), then later parent became visible via Update() path)
        if (!m_background->IsValid() && m_localVisible && IsEffectivelyVisible()) {
            spdlog::trace("[Visibility] {} Update: initializing deferred background", m_uuid.ToString());
            m_background->Initialize();
        }
        m_background->Update(deltaTime);
    }

    // Update label text driver if present and visible
    if (m_labelTextDriver && m_labelTextVisible) {
        m_labelTextDriver->Update(deltaTime);
    }
}

void ControlledProjectile::UpdateTransition(float deltaTime) {
    if (!IsValid()) {
        return;
    }

    // Update smoother (may be no-op in Instant mode)
    // Always update even when not bound so position is ready when shown
    m_smoother.Update(deltaTime);

    // Only apply to game projectile if Bound
    // (Firing state means async bind is still pending)
    if (m_bindState.load() == BindState::Bound && m_gameProjectile.IsBound()) {
        // Store the desired transform - actual application happens on main thread
        // via ProjectileSubsystem::OnProjectileUpdate() which is called from the game hook.
        // This ensures we never modify game data from the VR compositor thread.
        m_gameProjectile.SetTransform(m_smoother.GetCurrent());
    }
}

void ControlledProjectile::Destroy() {
    if (!m_valid || !m_subsystem) {
        return;
    }

    spdlog::trace("ControlledProjectile::Destroy() UUID={}", m_uuid.ToString());

    // Destroy background first
    if (m_background) {
        m_background->Destroy();
        m_background.reset();
    }

    // Destroy label text driver
    if (m_labelTextDriver) {
        m_labelTextDriver->Clear();
        m_labelTextDriver.reset();
    }

    // Mark for deletion and unbind
    m_gameProjectile.MarkForDeletion();
    m_gameProjectile.Unbind();

    // Release our form
    if (m_formIndex >= 0) {
        m_subsystem->ReleaseForm(m_formIndex);
    }

    // Notify subsystem
    m_subsystem->UnregisterProjectile(m_uuid);

    m_valid = false;
    m_formIndex = -1;
}

void ControlledProjectile::Initialize() {
    // [DIAG] Track initialization timing
    auto initStart = std::chrono::high_resolution_clock::now();

    // Already initialized?
    if (m_valid) {
        return;
    }

    // Get subsystem singleton
    m_subsystem = ProjectileSubsystem::GetSingleton();
    if (!m_subsystem || !m_subsystem->IsInitialized()) {
        spdlog::error("ControlledProjectile::Initialize - Subsystem not available");
        return;
    }

    // Generate UUID
    m_uuid = UUID::Generate();
    m_valid = true;

    // Set up texture info if using texture mode
    if (!m_texturePath.empty()) {
        m_gameProjectile.SetTexturePath(m_texturePath);
        m_gameProjectile.SetBorderColor(m_borderColor);
    }

    // Register with subsystem (stores weak reference via shared_from_this)
    m_subsystem->RegisterProjectile(m_uuid, shared_from_this());

    // Check if we should skip firing at initialization:
    // 1. User pre-configured as hidden (via SetVisible(false) before Initialize)
    // 2. Parent hierarchy is hidden (IsEffectivelyVisible() returns false)
    // m_localVisible tracks user intent, m_bindState tracks resource state
    // IMPORTANT: Must check IsEffectivelyVisible() to prevent spawning visible projectiles
    // for elements whose parents are hidden (e.g., inactive sub-menus)
    if (!m_localVisible || !IsEffectivelyVisible()) {
        // State already defaults to Unbound - nothing to do
        spdlog::info("ControlledProjectile::Initialize() UUID={} - starting hidden (local={} effective={}), skipping fire",
            m_uuid.ToString(), m_localVisible, IsEffectivelyVisible());
        return;
    }

    // Determine effective model path
    std::string modelPath = !m_texturePath.empty()
        ? "meshes\\3DUI\\icon_template.nif"
        : m_modelPath;

    // Acquire a form for this model
    m_formIndex = m_subsystem->AcquireForm(modelPath);
    if (m_formIndex < 0) {
        spdlog::error("ControlledProjectile::Initialize - Failed to acquire form for '{}'", modelPath);
        return;
    }

    // Transition to Firing state - BindToProjectile will complete to Bound
    spdlog::trace("[BindState] {} Unbound -> Firing (init)", m_uuid.ToString());
    m_bindState.store(BindState::Firing);

    // Increment generation BEFORE firing - any pending tasks from previous fires become stale
    ++m_fireGeneration;

    // Fire the game projectile
    if (!m_subsystem->FireProjectileFor(this)) {
        spdlog::error("ControlledProjectile::Initialize - Failed to fire projectile");
        spdlog::trace("[BindState] {} Firing -> Unbound (init fire failed)", m_uuid.ToString());
        m_bindState.store(BindState::Unbound);  // Revert state on failure
        // Keep valid anyway - user can still manipulate transform
    }

    spdlog::trace("ControlledProjectile::Initialize() UUID={}, form={}",
        m_uuid.ToString(), m_formIndex);

    // Initialize background if present and visible
    if (m_background && m_localVisible && IsEffectivelyVisible()) {
        m_background->Initialize();
    }

    // Initialize label text driver if present and visible
    if (m_labelTextDriver && m_labelTextVisible && m_localVisible && IsEffectivelyVisible()) {
        m_labelTextDriver->Initialize();
    }

    // [DIAG] Log initialization duration
    auto initEnd = std::chrono::high_resolution_clock::now();
    auto initTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(initEnd - initStart).count();
    if (initTimeUs > 1000) {
        spdlog::warn("[PERF] ControlledProjectile::Initialize took {}us UUID={}",
            initTimeUs, m_uuid.ToString());
    }
}

void ControlledProjectile::BindToProjectile(RE::Projectile* proj, uint64_t generation) {
    // Atomically transition Firing -> Bound
    // This ensures we don't bind if UnbindProjectile already cancelled us
    BindState expected = BindState::Firing;
    if (!m_bindState.compare_exchange_strong(expected, BindState::Bound)) {
        spdlog::trace("[BindState] {} Firing -> Bound FAILED (state was {})",
            m_uuid.ToString(), static_cast<int>(expected));
        // State changed (likely to Unbound by UnbindProjectile) - let projectile die
        return;
    }

    // CRITICAL: Re-check generation AFTER winning the CAS to prevent TOCTOU race
    // Scenario: generation check passes, then rapid off/on toggles increment generation,
    // CAS succeeds (state is Firing again), but we'd bind the WRONG projectile.
    // By checking generation after CAS, we ensure atomicity of (state + generation) validation.
    if (generation != m_fireGeneration.load()) {
        spdlog::trace("[Visibility] {} BindToProjectile: stale gen={} (current={}), reverting to Unbound",
            m_uuid.ToString(), generation, m_fireGeneration.load());
        // Revert to Unbound (not Firing) - the correct callback may have already failed,
        // and we don't want to leave the system stuck in Firing waiting for a callback
        // that will never arrive. Update() will trigger a fresh RebindProjectile() if needed.
        m_bindState.store(BindState::Unbound);
        // Let the game projectile die naturally - we don't own it
        return;
    }

    spdlog::trace("[BindState] {} Firing -> Bound (gen={})", m_uuid.ToString(), generation);

    // We now own the Bound state - complete the bind
    m_gameProjectile.BindToProjectile(proj);

    // Ensure visibility is set (may have been false from previous hide)
    m_gameProjectile.SetVisible(true);

    // Immediately apply correct transform to prevent first-frame flash
    // Set current = target so projectile appears at intended position with no lerp
    m_smoother.SetCurrent(m_smoother.GetTarget());
    m_gameProjectile.SetTransform(m_smoother.GetCurrent());
    m_gameProjectile.ApplyTransform();

    spdlog::trace("[Visibility] {} BindToProjectile: bound successfully (gen={})",
        m_uuid.ToString(), generation);
}

// === Background Projectile Implementation ===

void ControlledProjectile::EnsureBackground() {
    if (m_background) return;

    m_background = std::make_shared<ControlledProjectile>();
    m_background->SetParent(this);
    m_background->SetActivateable(false);
    m_background->SetUseHapticFeedback(false);
    m_background->SetLocalPosition({0, 0, 0});  // Same position as primary
}

void ControlledProjectile::SetBackgroundModelPath(const std::string& path) {
    if (path.empty()) {
        ClearBackground();
        return;
    }
    EnsureBackground();
    m_background->SetModelPath(path);

    // If we're already initialized and visible, initialize the background too
    if (m_valid && m_localVisible && IsEffectivelyVisible()) {
        m_background->Initialize();
    }
}

void ControlledProjectile::SetBackgroundScale(float scale) {
    if (m_background) {
        m_background->SetBaseScale(scale);
    }
}

void ControlledProjectile::ClearBackground() {
    if (m_background) {
        m_background->SetVisible(false);
        m_background->Destroy();
        m_background.reset();
    }
}

void ControlledProjectile::UpdateBillboard() {
    if (!IsValid() || m_billboardMode == BillboardMode::None) {
        return;
    }

    RE::NiPoint3 targetPos;
    switch (m_billboardMode) {
        case BillboardMode::FacePlayer:
            targetPos = GameProjectileUtils::GetPlayerPosition();
            break;
        case BillboardMode::FaceHMD:
        case BillboardMode::YawOnly:
            targetPos = GameProjectileUtils::GetHMDPosition();
            break;
        default:
            return;
    }

    // Get current world position (from scene graph)
    RE::NiPoint3 currentPos = GetWorldPosition();

    // Compute desired WORLD rotation (what we want the final orientation to be)
    RE::NiMatrix3 desiredWorldRotation;

    if (m_billboardMode == BillboardMode::YawOnly) {
        // Yaw-only: project onto horizontal plane
        RE::NiPoint3 flatTarget = targetPos;
        flatTarget.z = currentPos.z;  // Same height
        desiredWorldRotation = BuildFacingRotation(currentPos, flatTarget);
    } else {
        // Full 3D facing
        desiredWorldRotation = BuildFacingRotation(currentPos, targetPos);
    }

    // Convert world rotation to local rotation
    // WorldRot = ParentWorldRot x LocalRot
    // Therefore: LocalRot = Inverse(ParentWorldRot) x DesiredWorldRot
    if (m_parent) {
        RE::NiMatrix3 parentWorldRot = m_parent->GetWorldRotation();
        RE::NiMatrix3 parentInverse = InverseRotationMatrix(parentWorldRot);
        SetLocalRotation(MultiplyMatrices(parentInverse, desiredWorldRotation));
    } else {
        // No parent - local rotation IS world rotation
        SetLocalRotation(desiredWorldRotation);
    }
}

// === Label Text Implementation ===

void ControlledProjectile::EnsureLabelTextDriver() {
    if (m_labelTextDriver) return;

    m_labelTextDriver = std::make_shared<TextDriver>();
    m_labelTextDriver->SetParent(this);
    m_labelTextDriver->SetLocalPosition(m_labelOffset);
    m_labelTextDriver->SetTextScale(m_labelTextScale);

    // Apply any pre-configured text
    if (!m_labelText.empty()) {
        m_labelTextDriver->SetText(m_labelText);
    }

    spdlog::info("ControlledProjectile::EnsureLabelTextDriver - Created for {}",
        m_uuid.ToString());
}

void ControlledProjectile::SetLabelText(const std::wstring& text) {
    m_labelText = text;

    if (text.empty()) {
        ClearLabelText();
        return;
    }

    EnsureLabelTextDriver();
    m_labelTextDriver->SetText(text);

    // If we're already initialized and visible, initialize the driver too
    if (m_valid && m_localVisible && m_labelTextVisible && IsEffectivelyVisible()) {
        m_labelTextDriver->Initialize();
        m_labelTextDriver->SetVisible(true);
    }
}

void ControlledProjectile::SetLabelText(const std::string& text) {
    // Convert narrow string to wide string
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    SetLabelText(converter.from_bytes(text));
}

void ControlledProjectile::SetLabelTextScale(float scale) {
    m_labelTextScale = scale;
    if (m_labelTextDriver) {
        m_labelTextDriver->SetTextScale(scale);
    }
}

void ControlledProjectile::SetLabelTextVisible(bool visible) {
    if (m_labelTextVisible == visible) {
        return;
    }

    m_labelTextVisible = visible;

    if (m_labelTextDriver) {
        // Only actually show if we (the parent) are also effectively visible
        if (visible && m_localVisible && IsEffectivelyVisible()) {
            m_labelTextDriver->SetVisible(true);
        } else if (!visible) {
            m_labelTextDriver->SetVisible(false);
        }
    }
}

void ControlledProjectile::SetLabelOffset(const RE::NiPoint3& offset) {
    m_labelOffset = offset;
    if (m_labelTextDriver) {
        m_labelTextDriver->SetLocalPosition(offset);
    }
}

void ControlledProjectile::ClearLabelText() {
    m_labelText.clear();
    if (m_labelTextDriver) {
        m_labelTextDriver->SetVisible(false);
        m_labelTextDriver->Clear();
        m_labelTextDriver.reset();
    }
}

} // namespace Projectile
