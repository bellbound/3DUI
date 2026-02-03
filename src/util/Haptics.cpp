#include "Haptics.h"
#include "../InputManager.h"
#include "../Config.h"
#include <algorithm>
#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Projectile {

// =============================================================================
// BSVRInterface haptic call (same approach as VR Climbing)
// Uses SkyrimVR's internal BSVRInterface::TriggerHapticPulse via the g_openVR pointer,
// avoiding any dependency on linking openvr_api.lib.
// The deprecated IVRSystem::TriggerHapticPulse doesn't work reliably with modern SteamVR.
// =============================================================================

// SkyrimVR.exe global: BSOpenVR* g_openVR
// Offset sourced from SKSEVR GameVR.cpp
static REL::Relocation<void**> g_openVR{ REL::Offset(0x02FEB9B0) };

// Cached pointer - looked up once, reused thereafter
static void* s_cachedOpenVR = nullptr;
static bool s_openVRLookupDone = false;

static bool IsReadableAddress(const void* a_ptr, std::size_t a_bytes = sizeof(void*))
{
#if defined(_WIN32)
    if (!a_ptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(a_ptr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
    if (!(mbi.Protect & kReadable)) {
        return false;
    }
    // Check if entire range is within the region
    auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    auto regionEnd = base + mbi.RegionSize;
    auto ptrAddr = reinterpret_cast<std::uintptr_t>(a_ptr);
    return (ptrAddr + a_bytes) <= regionEnd;
#else
    return a_ptr != nullptr;
#endif
}

static void* GetOpenVRInterface()
{
    if (s_openVRLookupDone) {
        return s_cachedOpenVR;
    }
    s_openVRLookupDone = true;

#if defined(_WIN32)
    void** ppOpenVR = g_openVR.get();
    if (!IsReadableAddress(ppOpenVR)) {
        spdlog::warn("Haptics: g_openVR address not readable");
        return nullptr;
    }

    void* openVR = *ppOpenVR;
    if (!IsReadableAddress(openVR)) {
        spdlog::warn("Haptics: BSOpenVR instance not readable");
        return nullptr;
    }

    s_cachedOpenVR = openVR;
    spdlog::info("Haptics: Successfully cached BSOpenVR interface at {:p}", openVR);
    return openVR;
#else
    return nullptr;
#endif
}

static void CallBSVRTriggerHapticPulse(void* a_openVR, int a_hand, float a_durationUnits)
{
    if (!a_openVR) {
        return;
    }

    // Get vtable pointer and validate it's readable
    void** vtblPtr = *reinterpret_cast<void***>(a_openVR);
    if (!IsReadableAddress(vtblPtr, sizeof(void*) * 15)) {
        spdlog::warn("Haptics: vtable not readable");
        return;
    }

    // BSVRInterface vtbl index for TriggerHapticPulse is 14 (see SKSEVR GameVR.h).
    constexpr std::size_t kVtblIndex_TriggerHapticPulse = 14;
    using Fn = void (*)(void*, int, float);
    auto fn = reinterpret_cast<Fn>(vtblPtr[kVtblIndex_TriggerHapticPulse]);
    if (!fn) {
        spdlog::warn("Haptics: TriggerHapticPulse function pointer is null");
        return;
    }

    fn(a_openVR, a_hand, a_durationUnits);
}

Haptics& Haptics::GetSingleton() {
    static Haptics instance;
    return instance;
}

Haptics::~Haptics() {
    Shutdown();
}

void Haptics::Initialize() {
    if (m_initialized.load()) {
        spdlog::warn("Haptics: Already initialized");
        return;
    }

    // Check if InputManager has VR system available
    auto* inputMgr = InputManager::GetSingleton();
    if (!inputMgr->IsInitialized()) {
        spdlog::error("Haptics: InputManager not initialized - cannot access VR system");
        return;
    }

    m_startTime = std::chrono::steady_clock::now();
    m_shutdown.store(false);

    // Spawn background threads for each hand
    m_leftThread = std::thread(&Haptics::ProcessLoop, this, true);
    m_rightThread = std::thread(&Haptics::ProcessLoop, this, false);

    m_initialized.store(true);
    spdlog::info("Haptics: Initialized with background threads");
}

void Haptics::Shutdown() {
    if (!m_initialized.load()) {
        return;
    }

    m_shutdown.store(true);

    // Wait for threads to finish
    if (m_leftThread.joinable()) {
        m_leftThread.join();
    }
    if (m_rightThread.joinable()) {
        m_rightThread.join();
    }

    // Clear events
    {
        std::scoped_lock lock(m_leftLock);
        m_leftEvents.clear();
    }
    {
        std::scoped_lock lock(m_rightLock);
        m_rightEvents.clear();
    }

    m_initialized.store(false);
    spdlog::info("Haptics: Shut down");
}

void Haptics::QueueHapticEvent(bool isLeftHand, float startStrength, float endStrength,
                                float duration, bool easeIn, bool easeOut) {
    if (!m_initialized.load()) {
        return;
    }

    // Check global disable flag from config
    if (Config::options.globallyDisableHapticFeedback) {
        return;
    }

    HapticEvent event;
    event.startStrength = std::clamp(startStrength, 0.0f, 1.0f);
    event.endStrength = std::clamp(endStrength, 0.0f, 1.0f);
    event.duration = static_cast<double>(duration);
    event.startTime = 0.0;  // Will be set when processed
    event.easeIn = easeIn;
    event.easeOut = easeOut;
    event.isNew = true;

    if (isLeftHand) {
        std::scoped_lock lock(m_leftLock);
        m_leftEvents.push_back(std::move(event));
    } else {
        std::scoped_lock lock(m_rightLock);
        m_rightEvents.push_back(std::move(event));
    }
}

void Haptics::QueueHapticPulse(bool isLeftHand, float strength, float duration) {
    QueueHapticEvent(isLeftHand, strength, strength, duration, false, false);
}

void Haptics::ProcessLoop(bool isLeft) {
    auto& events = isLeft ? m_leftEvents : m_rightEvents;
    auto& lock = isLeft ? m_leftLock : m_rightLock;

    while (!m_shutdown.load()) {
        {
            std::scoped_lock scopedLock(lock);

            size_t numEvents = events.size();
            if (numEvents > 0) {
                double currentTime = GetTime();

                // Process only the last (most recent) event - matches HIGGS behavior
                // This ensures rapid input doesn't queue up stale pulses
                HapticEvent& lastEvent = events[numEvents - 1];

                if (lastEvent.isNew) {
                    lastEvent.isNew = false;
                    lastEvent.startTime = currentTime;
                }

                // Calculate normalized time [0, 1]
                float t;
                if (lastEvent.duration <= 0.0) {
                    t = 1.0f;  // Instant pulse
                } else {
                    double elapsed = currentTime - lastEvent.startTime;
                    t = static_cast<float>((std::min)(1.0, elapsed / lastEvent.duration));
                }

                // Apply easing
                float easedT = t;
                if (lastEvent.easeIn && lastEvent.easeOut) {
                    easedT = Smoothstep(t);
                } else if (lastEvent.easeIn) {
                    // Ease in only: slow start, fast end
                    easedT = t * t;
                } else if (lastEvent.easeOut) {
                    // Ease out only: fast start, slow end
                    easedT = 1.0f - (1.0f - t) * (1.0f - t);
                }

                // Interpolate strength
                float strength = lastEvent.startStrength +
                    (lastEvent.endStrength - lastEvent.startStrength) * easedT;

                TriggerHapticPulse(isLeft, strength);

                // Cleanup events that are past their duration
                auto end = std::remove_if(events.begin(), events.end(),
                    [currentTime](const HapticEvent& evt) {
                        return !evt.isNew && (currentTime - evt.startTime >= evt.duration);
                    });
                events.erase(end, events.end());
            }
        }

        // Rate limit: OpenVR TriggerHapticPulse can only be called once every 5ms
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Haptics::TriggerHapticPulse(bool isLeftHand, float strength) {
    if (strength <= 0.0f) {
        return;
    }

    // Use Skyrim's internal BSVRInterface::TriggerHapticPulse
    // This is the same approach VR Climbing uses and works reliably,
    // unlike the deprecated IVRSystem::TriggerHapticPulse from OpenVR.
    void* openVR = GetOpenVRInterface();
    if (!openVR) {
        return;
    }

    // Hand enum in SKSEVR: Left=0, Right=1
    constexpr int kLeftHand = 0;
    constexpr int kRightHand = 1;

    // BSVRInterface takes duration as arbitrary "units" (not microseconds)
    // Typical values: 0.1-1.0 for noticeable pulses
    // We pass strength directly as duration - higher strength = longer pulse = stronger feel
    CallBSVRTriggerHapticPulse(openVR, isLeftHand ? kLeftHand : kRightHand, strength);
}

double Haptics::GetTime() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - m_startTime);
    return elapsed.count();
}

float Haptics::Smoothstep(float t) {
    // Classic smoothstep: 3t² - 2t³
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace Projectile
