#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace Projectile {

// Per-hand haptic event for ramped/timed pulses
struct HapticEvent {
    float startStrength;
    float endStrength;
    double duration;       // seconds
    double startTime;      // timestamp when event started
    bool easeIn;
    bool easeOut;
    bool isNew;
};

// Singleton haptics manager for VR controller feedback
// Inspired by HIGGS haptics system
// Uses background threads with 5ms rate-limiting (OpenVR hardware constraint)
class Haptics {
public:
    static Haptics& GetSingleton();

    // Initialize the haptics system (spawns background threads)
    // Must be called after VR system is available
    void Initialize();

    // Shutdown (stops background threads)
    void Shutdown();

    // Check if system is initialized
    bool IsInitialized() const { return m_initialized.load(); }

    // Queue a ramped haptic event (interpolates from start to end strength over duration)
    // isLeftHand: which controller to vibrate
    // startStrength/endStrength: 0.0-1.0 intensity
    // duration: seconds
    // easeIn/easeOut: apply smoothstep easing at start/end
    void QueueHapticEvent(bool isLeftHand, float startStrength, float endStrength,
                          float duration, bool easeIn = false, bool easeOut = false);

    // Queue a simple pulse (fixed strength for a short duration)
    // Convenience wrapper for QueueHapticEvent with same start/end
    void QueueHapticPulse(bool isLeftHand, float strength, float duration = 0.02f);

private:
    Haptics() = default;
    ~Haptics();

    Haptics(const Haptics&) = delete;
    Haptics& operator=(const Haptics&) = delete;

    // Background thread loop (processes events for one hand)
    void ProcessLoop(bool isLeft);

    // Trigger actual VR haptic pulse via OpenVR
    void TriggerHapticPulse(bool isLeftHand, float strength);

    // Get current time in seconds
    double GetTime() const;

    // Easing function: smoothstep for natural feel
    static float Smoothstep(float t);

    // Per-hand state
    std::vector<HapticEvent> m_leftEvents;
    std::vector<HapticEvent> m_rightEvents;
    std::mutex m_leftLock;
    std::mutex m_rightLock;

    std::thread m_leftThread;
    std::thread m_rightThread;

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shutdown{false};

    // Timing
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace Projectile
