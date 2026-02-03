#pragma once

#include "../projectile/IPositionable.h"

namespace Projectile {

// Discriminated union for haptic pulse configurations
// Supports both simple fixed-strength pulses and ramped pulses with easing
struct HapticPulseConfig {
    enum class Type {
        Simple,  // Fixed strength for duration
        Ramped   // Interpolates from start to end strength
    };

    Type type = Type::Simple;

    // Simple pulse fields
    float strength = 0.0f;
    float duration = 0.0f;  // seconds

    // Ramped pulse fields (used when type == Ramped)
    float startStrength = 0.0f;
    float endStrength = 0.0f;
    bool easeIn = false;
    bool easeOut = false;

    // Factory for simple pulse
    static HapticPulseConfig Simple(float strength, float duration) {
        HapticPulseConfig config;
        config.type = Type::Simple;
        config.strength = strength;
        config.duration = duration;
        return config;
    }

    // Factory for ramped pulse
    static HapticPulseConfig Ramped(float start, float end, float duration,
                                     bool easeIn = false, bool easeOut = false) {
        HapticPulseConfig config;
        config.type = Type::Ramped;
        config.startStrength = start;
        config.endStrength = end;
        config.duration = duration;
        config.easeIn = easeIn;
        config.easeOut = easeOut;
        return config;
    }

    // No haptic pulse
    static HapticPulseConfig None() {
        return HapticPulseConfig{};
    }

    bool IsValid() const {
        if (type == Type::Simple) {
            return strength > 0.0f && duration > 0.0f;
        } else {
            return (startStrength > 0.0f || endStrength > 0.0f) && duration > 0.0f;
        }
    }
};

// Get the appropriate haptic pulse configuration for an input event type
// Returns a config with strength, duration, and optional ramping parameters
HapticPulseConfig GetPulseForEvent(InputEventType type);

// Trigger a haptic pulse for the given event
// Checks event.sendHapticPulse flag before triggering
// Automatically routes to the correct hand based on event.isLeftHand
void TriggerPulseForEvent(const InputEvent& event);

} // namespace Projectile
