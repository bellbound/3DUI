#include "HapticPulses.h"
#include "Haptics.h"
#include "../log.h"

namespace Projectile {

HapticPulseConfig GetPulseForEvent(InputEventType type) {
    // All pulses: 25% less intense (strength * 0.75), 15% shorter (duration * 0.85)
    switch (type) {
        case InputEventType::HoverEnter:
            // Light tick when hand enters hover zone
            return HapticPulseConfig::Simple(0.11f, 0.017f);

        case InputEventType::HoverExit:
            // Subtle feedback when leaving hover zone
            return HapticPulseConfig::Simple(0.075f, 0.017f);

        case InputEventType::GrabStart:
            // Medium pulse with fade-out for satisfying grab feel
            return HapticPulseConfig::Ramped(0.375f, 0.15f, 0.068f, false, true);

        case InputEventType::GrabEnd:
            // Lighter pulse on release
            return HapticPulseConfig::Simple(0.225f, 0.025f);

        case InputEventType::ActivateDown:
            // Strong click feedback
            return HapticPulseConfig::Simple(0.375f, 0.068f);

        case InputEventType::ActivateUp:
            // Light release feedback
            return HapticPulseConfig::Simple(0.15f, 0.017f);

        default:
            return HapticPulseConfig::None();
    }
}

void TriggerPulseForEvent(const InputEvent& event) {
    // Check if haptic feedback should be sent for this event
    if (!event.sendHapticPulse) {
        spdlog::trace("TriggerPulseForEvent: sendHapticPulse=false, skipping (type={})",
            static_cast<int>(event.type));
        return;
    }

    // Get the pulse configuration for this event type
    HapticPulseConfig config = GetPulseForEvent(event.type);
    if (!config.IsValid()) {
        spdlog::trace("TriggerPulseForEvent: Invalid config for event type {}",
            static_cast<int>(event.type));
        return;
    }

    // Get haptics singleton
    auto& haptics = Haptics::GetSingleton();
    if (!haptics.IsInitialized()) {
        spdlog::trace("TriggerPulseForEvent: Haptics not initialized");
        return;
    }

    // Trigger the pulse on the appropriate hand
    if (config.type == HapticPulseConfig::Type::Simple) {
        spdlog::trace("TriggerPulseForEvent: Queuing simple pulse (hand={}, strength={:.2f}, duration={:.3f})",
            event.isLeftHand ? "left" : "right", config.strength, config.duration);
        haptics.QueueHapticPulse(event.isLeftHand, config.strength, config.duration);
    } else {
        // Ramped pulse
        spdlog::trace("TriggerPulseForEvent: Queuing ramped pulse (hand={}, start={:.2f}, end={:.2f})",
            event.isLeftHand ? "left" : "right", config.startStrength, config.endStrength);
        haptics.QueueHapticEvent(
            event.isLeftHand,
            config.startStrength,
            config.endStrength,
            config.duration,
            config.easeIn,
            config.easeOut
        );
    }
}

} // namespace Projectile
