#include "DriverUpdateManager.h"
#include "TooltipTextDisplayManager.h"
#include "ProjectileHook.h"
#include "../MenuChecker.h"
#include "../projectile/ProjectileSubsystem.h"
#include "../projectile/AsyncTextureLoader.h"
#include "../log.h"
#include <algorithm>
#include <thread>
#include <sstream>

namespace Widget {

DriverUpdateManager& DriverUpdateManager::GetSingleton() {
    static DriverUpdateManager instance;
    return instance;
}

bool DriverUpdateManager::InstallHook() {
    // Hook into main game loop - REL::RelocationID(35565, 36564) is the main update function
    // This runs every frame on the main thread, making it safe for game object modifications.
    // Offset 0x7ee (VR) / 0x748 (SE) / 0xc26 (AE) points to a call site within the function.

    SKSE::AllocTrampoline(1 << 4);  // 16 bytes
    auto& trampoline = SKSE::GetTrampoline();

    REL::Relocation<std::uintptr_t> mainLoopFunc{REL::RelocationID(35565, 36564)};

    // The offset varies by game version - use REL::Relocate for cross-version compatibility
    // SE: 0x748, AE: 0xc26, VR: 0x7ee
    auto hookOffset = REL::Relocate(0x748, 0xc26, 0x7ee);

    s_originalFunc = trampoline.write_call<5>(
        mainLoopFunc.address() + hookOffset,
        &DriverUpdateManager::OnMainThreadUpdate
    );

    spdlog::info("DriverUpdateManager: Installed main thread hook at {:x} + 0x{:x}",
        mainLoopFunc.address(), hookOffset);

    return true;
}

void DriverUpdateManager::Initialize(Projectile::ProjectileSubsystem* projectileSubsystem) {
    if (!projectileSubsystem) {
        spdlog::error("DriverUpdateManager::Initialize called with null projectileSubsystem");
        return;
    }

    m_projectileSubsystem = projectileSubsystem;
    m_hasLastUpdateTime = false;

    // Start async texture loader worker thread
    Projectile::AsyncTextureLoader::GetInstance().Start();

    spdlog::info("DriverUpdateManager initialized (main thread hook mode)");
}

void DriverUpdateManager::Shutdown() {
    // Shutdown tooltip system first
    TooltipTextDisplayManager::GetSingleton()->Shutdown();

    // Shutdown async texture loader (stops worker thread)
    Projectile::AsyncTextureLoader::GetInstance().Shutdown();

    // Note: Hook cannot be uninstalled - it remains for the lifetime of the game.
    // This is fine because the hook checks if manager is initialized.

    UnregisterAll();
    m_projectileSubsystem = nullptr;
    spdlog::info("DriverUpdateManager shutdown");
}

void DriverUpdateManager::OnMainThreadUpdate() {
    // Call original function first
    s_originalFunc();

    // FAST PATH: Skip all work if no drivers are registered
    // This check happens before GetSingleton() to minimize overhead when idle
    if (s_registeredCount.load(std::memory_order_relaxed) == 0) {
        return;
    }

    auto& instance = GetSingleton();

    // Skip if not initialized
    if (!instance.m_projectileSubsystem) {
        return;
    }

    // THREAD DEBUG: Log thread ID once to verify we're on main thread
    static bool s_loggedThreadId = false;
    if (!s_loggedThreadId) {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        spdlog::warn("[THREAD DEBUG] OnMainThreadUpdate running on thread ID: {}", oss.str());
        s_loggedThreadId = true;
    }

    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    float deltaTime = 0.016f; // Default ~60fps

    if (instance.m_hasLastUpdateTime) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - instance.m_lastUpdateTime);
        deltaTime = elapsed.count() / 1000000.0f;
    }

    instance.m_lastUpdateTime = now;
    instance.m_hasLastUpdateTime = true;

    instance.Update(deltaTime);
}

void DriverUpdateManager::Update(float deltaTime) {
    // [DIAG] Watchdog for detecting long update cycles
    static auto s_lastUpdateEnd = std::chrono::steady_clock::now();
    auto updateStart = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(updateStart - s_lastUpdateEnd);
    if (sinceLast > std::chrono::seconds(2)) {
        spdlog::error("[WATCHDOG] Gap between updates: {}ms - possible previous freeze!",
            sinceLast.count());
    }

    // Process completed async texture loads first (fires callbacks on main thread)
    // This must happen before driver updates so textures are swapped in promptly
    Projectile::AsyncTextureLoader::GetInstance().ProcessCompletedLoads();

    // Skip all interaction updates when a game menu is open
    bool menuOpen = MenuChecker::IsGameStopped();

    // THREAD SAFETY: Since we're now on the main thread, we don't strictly need
    // to copy the vector before iterating. However, keeping the copy is defensive
    // in case callbacks indirectly cause Register/Unregister during iteration.
    auto registeredCopy = m_registered;

    for (auto* driver : registeredCopy) {
        if (!driver) continue;

        // Update driver first (positioning) - always runs for visual consistency
        driver->Update(deltaTime);

        // Then interaction (hover detection) - skip when menu is open
        // Interaction controller is owned by the driver
        if (!menuOpen) {
            if (auto* interaction = driver->GetInteractionController()) {
                interaction->Update(deltaTime);
            }
        }
    }

    // Update tooltip system (must be after interaction updates which set tooltip state)
    TooltipTextDisplayManager::GetSingleton()->Update(deltaTime);

    // [DIAG] Watchdog - detect slow updates
    auto updateEnd = std::chrono::steady_clock::now();
    auto updateDuration = std::chrono::duration_cast<std::chrono::milliseconds>(updateEnd - updateStart);
    if (updateDuration > std::chrono::milliseconds(100)) {
        spdlog::warn("[WATCHDOG] Update took {}ms (drivers={})",
            updateDuration.count(), m_registered.size());
    }
    s_lastUpdateEnd = updateEnd;
}

void DriverUpdateManager::Register(Projectile::ProjectileDriver* driver) {
    if (!driver) {
        spdlog::warn("DriverUpdateManager::Register called with null driver");
        return;
    }

    // Lazy hook installation - install on first driver registration
    // This ensures zero per-frame cost when no 3D UI is being used
    if (!s_hooksInstalled) {
        spdlog::info("DriverUpdateManager: First driver registered - installing hooks");

        // Install main thread hook for frame updates
        if (!InstallHook()) {
            spdlog::error("DriverUpdateManager: Failed to install main thread hook");
        }

        // Install projectile vtable hook for physics updates
        if (!Projectile::ProjectileHook::Install()) {
            spdlog::error("DriverUpdateManager: Failed to install projectile hook");
        }

        s_hooksInstalled = true;
    }

    // Check if already registered
    auto it = std::find(m_registered.begin(), m_registered.end(), driver);
    if (it != m_registered.end()) {
        spdlog::warn("DriverUpdateManager::Register - driver '{}' already registered", driver->GetID());
        return;
    }

    m_registered.push_back(driver);
    s_registeredCount.fetch_add(1, std::memory_order_relaxed);
    spdlog::trace("[DriverMgr] Register: driver='{}' total={}", driver->GetID(), m_registered.size());
}

void DriverUpdateManager::Unregister(Projectile::ProjectileDriver* driver) {
    auto it = std::find(m_registered.begin(), m_registered.end(), driver);
    if (it != m_registered.end()) {
        std::string driverId = driver ? driver->GetID() : "(null)";
        m_registered.erase(it);
        s_registeredCount.fetch_sub(1, std::memory_order_relaxed);
        spdlog::trace("[DriverMgr] Unregister: driver='{}' remaining={}", driverId, m_registered.size());
    }
}

void DriverUpdateManager::UnregisterAll() {
    m_registered.clear();
    s_registeredCount.store(0, std::memory_order_relaxed);
    spdlog::info("DriverUpdateManager: Unregistered all");
}

void DriverUpdateManager::HideAllDrivers() {
    spdlog::trace("[DriverMgr] HideAllDrivers: {} registered drivers", m_registered.size());

    // Hide all tooltips first - they have independent visibility from drivers
    // and would otherwise stay visible during cell transitions/load screens
    TooltipTextDisplayManager::GetSingleton()->HideAll();

    // Store which drivers were visible before hiding, so we can restore them later
    m_hiddenVisibleDrivers.clear();
    for (auto* driver : m_registered) {
        if (driver && driver->IsVisible()) {
            m_hiddenVisibleDrivers.push_back(driver);
            spdlog::trace("[DriverMgr] HideAllDrivers: hiding driver '{}'", driver->GetID());
            driver->SetVisible(false);
        }
    }

    spdlog::trace("[DriverMgr] HideAllDrivers: stored {} visible drivers for restoration",
        m_hiddenVisibleDrivers.size());
}

void DriverUpdateManager::RestoreVisibleDrivers() {
    if (m_hiddenVisibleDrivers.empty()) {
        spdlog::trace("[DriverMgr] RestoreVisibleDrivers: no drivers to restore");
        return;
    }

    spdlog::trace("[DriverMgr] RestoreVisibleDrivers: restoring {} drivers",
        m_hiddenVisibleDrivers.size());

    for (auto* driver : m_hiddenVisibleDrivers) {
        // Verify driver is still registered (wasn't destroyed while hidden)
        auto it = std::find(m_registered.begin(), m_registered.end(), driver);
        if (it != m_registered.end() && driver) {
            spdlog::trace("[DriverMgr] RestoreVisibleDrivers: restoring driver '{}'", driver->GetID());
            driver->SetVisible(true);
        }
    }

    m_hiddenVisibleDrivers.clear();
}

} // namespace Widget
