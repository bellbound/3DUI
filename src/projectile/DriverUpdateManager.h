#pragma once

#include "../projectile/ProjectileDriver.h"
#include "InteractionController.h"
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>

namespace Projectile {
    class ProjectileSubsystem;
}

namespace Widget {

class DriverUpdateManager {
public:
    static DriverUpdateManager& GetSingleton();

    // === Initialization ===
    // Initializes the manager and hooks into main thread frame updates.
    void Initialize(Projectile::ProjectileSubsystem* projectileSubsystem);
    void Shutdown();
    bool IsInitialized() const { return m_projectileSubsystem != nullptr; }

    // Get the projectile subsystem (for driver initialization)
    Projectile::ProjectileSubsystem* GetProjectileSubsystem() const { return m_projectileSubsystem; }

    // === Registration ===
    // Register a driver for automatic frame updates.
    // The driver should have its interaction controller set via SetInteractionController().
    // Caller retains ownership. Call Unregister before destroying it.
    void Register(Projectile::ProjectileDriver* driver);
    void Unregister(Projectile::ProjectileDriver* driver);
    void UnregisterAll();

    // Hide all registered drivers (unbinds projectiles - call before save load/cell change)
    // Stores which drivers were visible for later restoration
    void HideAllDrivers();

    // Restore visibility to drivers that were visible before HideAllDrivers was called
    void RestoreVisibleDrivers();

    size_t GetRegisteredCount() const { return m_registered.size(); }

    // Install main thread hook (called once during plugin init)
    static bool InstallHook();

private:
    DriverUpdateManager() = default;
    ~DriverUpdateManager() = default;

    DriverUpdateManager(const DriverUpdateManager&) = delete;
    DriverUpdateManager& operator=(const DriverUpdateManager&) = delete;

    // Called automatically each frame via main thread hook
    void Update(float deltaTime);

    // Main thread hook callback
    static void OnMainThreadUpdate();

    // Original function pointer (stored by trampoline)
    static inline REL::Relocation<decltype(OnMainThreadUpdate)> s_originalFunc;

    // Lazy hook installation flag - hooks are installed on first Register() call
    static inline bool s_hooksInstalled = false;

    // Fast counter for early-out check in OnMainThreadUpdate
    // Avoids GetSingleton() call and all update work when no drivers registered
    static inline std::atomic<size_t> s_registeredCount{0};

    Projectile::ProjectileSubsystem* m_projectileSubsystem = nullptr;
    std::vector<Projectile::ProjectileDriver*> m_registered;
    std::vector<Projectile::ProjectileDriver*> m_hiddenVisibleDrivers;  // Drivers that were visible before HideAllDrivers
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    bool m_hasLastUpdateTime = false;
};

} // namespace Widget
