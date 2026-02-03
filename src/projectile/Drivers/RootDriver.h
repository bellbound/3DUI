#pragma once

#include "../ProjectileDriver.h"
#include <functional>

namespace Projectile {

// A simple container driver that does not reposition its children.
// Children retain their manually set local positions.
// Use this as a root container when you want explicit control over child positioning.
//
// Usage (Create → Configure → Show):
//   auto root = std::make_unique<RootDriver>();
//
//   // Configure hierarchy
//   root->AddChild(childDriver);
//   childDriver->SetLocalPosition({0, 0, 0});
//   root->AddChild(anotherChild);
//   anotherChild->SetLocalPosition({0, 0, -10});
//
//   // Set up centralized event handling
//   root->SetOnEvent([](const InputEvent& event) {
//       auto* proj = dynamic_cast<ControlledProjectile*>(event.source);
//       if (event.type == InputEventType::Activate) {
//           // Handle activation
//       }
//   });
//
//   root->SetVisible(true);  // Lazily initializes and shows
//
class RootDriver : public ProjectileDriver {
public:
    // Callback type for centralized event handling
    // Receives ALL events that bubble to root (after internal handling)
    using EventCallback = std::function<void(const InputEvent&)>;

    RootDriver() {
        // Default to hidden - consumer calls SetVisible(true) to initialize and show
        m_localVisible = false;
    }
    ~RootDriver() override = default;

    // === Visibility ===
    // SetVisible(true) lazily initializes the hierarchy on first call.
    // This enables the Create → Configure → SetVisible(true) pattern.
    void SetVisible(bool visible) override {
        if (visible && !IsInitialized()) {
            Initialize();
        }
        ProjectileDriver::SetVisible(visible);
    }

    // Set callback for centralized event handling
    // The callback receives all events that bubble up to this root driver
    // Events are delivered AFTER internal handling (grab, etc.)
    void SetOnEvent(EventCallback callback) { m_onEventCallback = std::move(callback); }

    // Check if an event callback is set
    bool HasEventCallback() const { return m_onEventCallback != nullptr; }

protected:
    // Override to do nothing - children keep their manually set positions
    void UpdateLayout(float /*deltaTime*/) override {
        // No-op: children retain their local positions as set by caller
        // The base Update() still recursively calls child->Update()
    }

    // Override to call event callback after base handling
    bool OnEvent(InputEvent& event) override {
        // Let base class handle first (e.g., anchor handle grab)
        bool consumed = ProjectileDriver::OnEvent(event);

        // Always notify consumer callback (even if consumed internally)
        // This allows consumer to observe all events
        if (m_onEventCallback) {
            m_onEventCallback(event);
        }

        return consumed;
    }

private:
    EventCallback m_onEventCallback;
};

} // namespace Projectile
