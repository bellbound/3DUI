#pragma once

#include "../util/UUID.h"
#include "GameProjectile.h"
#include "TransformSmoother.h"
#include "IPositionable.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Projectile {

// Forward declarations
class ProjectileSubsystem;
class TextDriver;

// Billboarding mode for automatic rotation
enum class BillboardMode {
    None,           // No automatic rotation
    FacePlayer,     // Face player position
    FaceHMD,        // Face HMD (VR head) position
    YawOnly         // Only rotate on Y axis to face player
};

// Binding state for lock-free thread safety between main thread and SKSE task thread
// State transitions use atomic compare_exchange to prevent race conditions
enum class BindState : uint8_t {
    Unbound,    // No form acquired, no projectile (initial state, or after unbind)
    Firing,     // Form acquired, async fire in progress, waiting for BindToProjectile callback
    Bound       // Projectile active and bound
};

// User-facing handle to a controlled projectile
// This class provides a clean API for manipulating projectiles
// Owns its GameProjectile directly - no pool indirection
// Implements IPositionable for composable hierarchy support
// Uses enable_shared_from_this to support self-registration with subsystem
class ControlledProjectile : public IPositionable, public std::enable_shared_from_this<ControlledProjectile> {
    friend class ProjectileSubsystem;  // Allow subsystem to access m_formIndex for spawning

public:
    ControlledProjectile() = default;
    ~ControlledProjectile();

    // Move-only (handles are unique)
    ControlledProjectile(const ControlledProjectile&) = delete;
    ControlledProjectile& operator=(const ControlledProjectile&) = delete;
    ControlledProjectile(ControlledProjectile&& other) noexcept;
    ControlledProjectile& operator=(ControlledProjectile&& other) noexcept;

    // Check if this projectile is valid and active
    bool IsValid() const;
    explicit operator bool() const { return IsValid(); }

    // Get the unique identifier
    const UUID& GetUUID() const { return m_uuid; }

    // === Visual Configuration (formerly WidgetItem) ===
    void SetModelPath(const std::string& path) { m_modelPath = path; }
    const std::string& GetModelPath() const { return m_modelPath; }

    void SetTexturePath(const std::string& path) { m_texturePath = path; }
    const std::string& GetTexturePath() const { return m_texturePath; }

    void SetBorderColor(const std::string& color) { m_borderColor = color; }
    const std::string& GetBorderColor() const { return m_borderColor; }

    void SetText(const std::wstring& text) { m_text = text; }
    const std::wstring& GetText() const { return m_text; }

    void SetBaseScale(float scale) { m_baseScale = scale; }
    float GetBaseScale() const { return m_baseScale; }

    void SetScaleCorrection(float correction) { m_scaleCorrection = correction; }
    float GetScaleCorrection() const { return m_scaleCorrection; }

    // Rotation correction in DEGREES (pitch, roll, yaw) applied in model space
    void SetRotationCorrection(const RE::NiPoint3& euler) { m_rotationCorrection = euler; }
    const RE::NiPoint3& GetRotationCorrection() const { return m_rotationCorrection; }

    // === Behavior Flags ===
    void SetIsAnchorHandle(bool isAnchor) { m_isAnchorHandle = isAnchor; }
    bool IsAnchorHandle() const { return m_isAnchorHandle; }

    void SetCloseOnActivate(bool shouldClose) { m_closeOnActivate = shouldClose; }
    bool ShouldCloseOnActivate() const { return m_closeOnActivate; }

    // Haptic feedback: when false, no controller vibration on interactions with this element
    void SetUseHapticFeedback(bool enabled) { m_useHapticFeedback = enabled; }
    bool GetUseHapticFeedback() const { return m_useHapticFeedback; }

    // Activateable: when false, no haptic pulses AND no hover scale animation
    // Still tracks hover state and sends events (for non-interactive display elements)
    void SetActivateable(bool activateable) { m_activateable = activateable; }
    bool IsActivateable() const { return m_activateable; }

    // Per-element hover threshold override (<= 0 means use controller's default)
    void SetHoverThresholdOverride(float threshold) { m_hoverThresholdOverride = threshold; }
    float GetHoverThresholdOverride() const { return m_hoverThresholdOverride; }
    bool HasHoverThresholdOverride() const { return m_hoverThresholdOverride > 0.0f; }

    // === Background Projectile ===
    // Optional secondary projectile rendered at same position (e.g., for glow effects, panels)
    // Background is non-interactive and follows primary's visibility lifecycle
    void SetBackgroundModelPath(const std::string& path);
    void SetBackgroundScale(float scale);
    void ClearBackground();
    ControlledProjectile* GetBackground() const { return m_background.get(); }

    // === Label Text ===
    // Optional text rendered below the projectile using TextDriver
    // Label follows primary's visibility lifecycle and is positioned relative to center
    void SetLabelText(const std::wstring& text);
    void SetLabelText(const std::string& text);  // Convenience overload for narrow strings
    const std::wstring& GetLabelText() const { return m_labelText; }

    void SetLabelTextScale(float scale);
    float GetLabelTextScale() const { return m_labelTextScale; }

    void SetLabelTextVisible(bool visible);
    bool IsLabelTextVisible() const { return m_labelTextVisible; }

    // Offset from projectile center (default: below center)
    void SetLabelOffset(const RE::NiPoint3& offset);
    const RE::NiPoint3& GetLabelOffset() const { return m_labelOffset; }

    void ClearLabelText();
    TextDriver* GetLabelTextDriver() const { return m_labelTextDriver.get(); }

    // === Event Callbacks ===
    // Set callbacks for specific events. If a callback is set and returns true,
    // the event is consumed and won't bubble up to the parent.
    using EventCallback = std::function<bool()>;  // Return true to consume event

    void SetOnActivateDown(EventCallback cb) { m_onActivateDown = std::move(cb); }
    void SetOnActivateUp(EventCallback cb) { m_onActivateUp = std::move(cb); }
    void SetOnHoverEnter(EventCallback cb) { m_onHoverEnter = std::move(cb); }
    void SetOnHoverExit(EventCallback cb) { m_onHoverExit = std::move(cb); }
    void SetOnGrabStart(EventCallback cb) { m_onGrabStart = std::move(cb); }
    void SetOnGrabEnd(EventCallback cb) { m_onGrabEnd = std::move(cb); }

    // === Position Control ===
    // IPositionable override - set local position (relative to parent)
    // Drivers should use this method
    void SetLocalPosition(const RE::NiPoint3& pos) override { m_localPosition = pos; }

    // IPositionable overrides for world transform computation
    RE::NiPoint3 GetWorldPosition() const override;
    RE::NiMatrix3 GetWorldRotation() const override;  // Applies rotationCorrection
    float GetWorldScale() const override;

    // Event handling - returns false to let events bubble up
    bool OnEvent(InputEvent& event) override;

    // === Rotation Control ===
    void SetRotation(const RE::NiPoint3& rot);  // Euler angles (pitch, roll, yaw)
    void SetRotation(float pitch, float roll, float yaw);
    RE::NiPoint3 GetRotation() const;

    // === Scale Control ===
    // Local scale - set by driver via SetLocalScale() from IPositionable
    // Use SetLocalScale() for driver-controlled scale
    void SetLocalScale(float scale) override;
    float GetLocalScale() const override { return m_localScale; }

    // Hover scale multiplier - set by InteractionController (1.0 = normal, >1 = enlarged)
    // Final rendered scale = worldScale * hoverScale
    void SetHoverScale(float scale);

    // === Compound Transform ===
    void SetTransform(const ProjectileTransform& transform);
    ProjectileTransform GetTransform() const;

    // === Visibility (IPositionable overrides) ===
    void SetVisible(bool visible) override;
    bool IsVisible() const override;
    void OnParentHide() override;

    // === Billboarding ===
    void SetBillboardMode(BillboardMode mode);
    BillboardMode GetBillboardMode() const { return m_billboardMode; }

    // === Transition Mode ===
    void SetTransitionMode(TransitionMode mode) { m_smoother.SetMode(mode); }

    // Smoothing speed for Lerp mode (higher = more responsive, 10 = snappy, 2 = floaty)
    void SetSmoothingSpeed(float speed) { m_smoother.SetSpeed(speed); }

    // Seed the smoother with a transform (sets both current and target)
    // Call this before firing to ensure projectile spawns at correct position
    void SeedTransform(const ProjectileTransform& transform) {
        m_smoother.SetCurrent(transform);
        m_smoother.SetTarget(transform);
    }

    // Main update function (IPositionable override)
    // Computes world transform from hierarchy, handles billboard rotation and transition smoothing
    void Update(float deltaTime) override;

    // === Lifecycle ===
    // Destroy this projectile and release resources
    void Destroy();

    // === Initialization ===
    // IPositionable override - acquires form and spawns game projectile
    // Called automatically by driver when hierarchy is spawned
    void Initialize() override;

    // Bind to a fired game projectile (with generation check to handle rapid visibility toggles)
    void BindToProjectile(RE::Projectile* proj, uint64_t generation);

    // Get current fire generation (for async task validation)
    uint64_t GetFireGeneration() const { return m_fireGeneration.load(); }

    // Get the underlying game projectile (for hook identification)
    GameProjectile& GetGameProjectile() { return m_gameProjectile; }
    const GameProjectile& GetGameProjectile() const { return m_gameProjectile; }

private:
    // Internal update helpers
    void UpdateBillboard();
    void UpdateTransition(float deltaTime);
    RE::NiPoint3 GetPosition() const;  // Returns smoother's current position

    // Resource management helpers
    void UnbindProjectile();   // Release game projectile without changing m_localVisible
    void RebindProjectile();   // Re-acquire and fire game projectile

    // Subsystem reference
    ProjectileSubsystem* m_subsystem = nullptr;

    // Identity
    UUID m_uuid;

    // Visual configuration (formerly WidgetItem)
    std::string m_modelPath;
    std::string m_texturePath;
    std::string m_borderColor;
    std::wstring m_text;
    float m_baseScale = 1.0f;
    float m_scaleCorrection = 1.0f;
    RE::NiPoint3 m_rotationCorrection{0, 0, 0};  // Degrees (pitch, roll, yaw)

    // Behavior flags
    bool m_isAnchorHandle = false;
    bool m_closeOnActivate = false;
    bool m_useHapticFeedback = true;   // When false, suppresses haptic pulses for this element
    bool m_activateable = true;        // When false, no haptics AND no hover scale animation
    float m_hoverThresholdOverride = -1.0f;  // Per-element override (<= 0 means use controller's default)

    // Event callbacks
    EventCallback m_onActivateDown;
    EventCallback m_onActivateUp;
    EventCallback m_onHoverEnter;
    EventCallback m_onHoverExit;
    EventCallback m_onGrabStart;
    EventCallback m_onGrabEnd;

    // State
    int m_formIndex = -1;           // Which FormManager form this projectile uses
    bool m_valid = false;
    BillboardMode m_billboardMode = BillboardMode::YawOnly;  // Default to YawOnly
    TransformSmoother m_smoother;
    GameProjectile m_gameProjectile;  // Directly owned, no pool

    // Atomic binding state for lock-free thread safety
    // Transitions: Unbound->Firing (main), Firing->Bound (callback), Firing/Bound->Unbound (main)
    std::atomic<BindState> m_bindState{BindState::Unbound};

    // Hover scale multiplier (final = worldScale * hoverScale)
    // Note: base scale uses m_localScale from IPositionable
    float m_hoverScale = 1.0f;   // Set via SetHoverScale()

    // Track last heading for billboard continuity (prevents 180-degree flips)
    float m_lastHeading = 0.0f;

    // Generation counter for async fire requests - prevents stale bindings on rapid visibility toggles
    std::atomic<uint64_t> m_fireGeneration{0};

    // Background projectile (optional, owned)
    std::shared_ptr<ControlledProjectile> m_background;

    // Helper to lazy-create background
    void EnsureBackground();

    // Label text (optional, owned)
    std::shared_ptr<TextDriver> m_labelTextDriver;
    std::wstring m_labelText;
    float m_labelTextScale = 1.0f;
    bool m_labelTextVisible = true;
    RE::NiPoint3 m_labelOffset{0, 0, -10.0f};  // Default: below center

    // Helper to lazy-create label text driver
    void EnsureLabelTextDriver();
};

// Shared handle for cases where multiple owners need reference
using ControlledProjectilePtr = std::shared_ptr<ControlledProjectile>;
using ControlledProjectileWeakPtr = std::weak_ptr<ControlledProjectile>;

} // namespace Projectile
