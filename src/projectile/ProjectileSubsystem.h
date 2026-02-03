#pragma once

#include "../util/UUID.h"
#include "ControlledProjectile.h"
#include "FormManager.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Projectile {

// Main subsystem for managing controlled projectiles
// Singleton pattern - access via GetSingleton()
class ProjectileSubsystem {
    friend class ControlledProjectile;  // Allow ControlledProjectile to call FireProjectileFor

public:
    static ProjectileSubsystem* GetSingleton();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // === Projectile Management ===

    // Get an existing projectile by UUID
    ControlledProjectilePtr GetProjectile(const UUID& uuid);

    // Check if a projectile exists
    bool HasProjectile(const UUID& uuid) const;

    // Register a projectile (called by ControlledProjectile::Initialize)
    void RegisterProjectile(const UUID& uuid, ControlledProjectilePtr proj);

    // Release all projectiles
    void ReleaseAllProjectiles();

    // === Update (called from ProjectileHook) ===

    // Called by ProjectileHook for each projectile update - applies our transform overrides
    void OnProjectileUpdate(RE::Projectile* proj, float delta);

    // === Form Management (for ControlledProjectile visibility changes) ===
    // Acquire a form for a model. Returns formIndex or -1 if unavailable.
    int AcquireForm(const std::string& modelPath);
    // Release a form when projectile is hidden/destroyed.
    void ReleaseForm(int formIndex);

    // === Statistics ===
    size_t GetActiveCount() const;
    size_t GetUsedForms() const { return m_formManager.GetUsedForms(); }
    size_t GetTotalForms() const { return m_formManager.GetTotalForms(); }

private:
    ProjectileSubsystem() = default;
    ~ProjectileSubsystem() = default;
    ProjectileSubsystem(const ProjectileSubsystem&) = delete;
    ProjectileSubsystem& operator=(const ProjectileSubsystem&) = delete;

    // Fire a projectile for a ControlledProjectile (binds to its GameProjectile)
    // Gets transform from the ControlledProjectile itself
    bool FireProjectileFor(ControlledProjectile* controlledProj);

    // Unregister a projectile from tracking (called by ControlledProjectile::Destroy)
    void UnregisterProjectile(const UUID& uuid);

    // Find a ControlledProjectile by its game projectile pointer (for hook routing)
    ControlledProjectile* FindByGameProjectile(RE::Projectile* proj);

    // Get game forms
    RE::TESObjectWEAP* GetWeaponForm();
    RE::TESObjectREFR* GetCasterReference();

    mutable std::recursive_mutex m_mutex;
    FormManager m_formManager;

    // Map UUID -> ControlledProjectile (weak refs, shared_ptr owned externally)
    std::unordered_map<UUID, std::weak_ptr<ControlledProjectile>, UUID::Hash> m_projectiles;

    bool m_initialized = false;

    // Cached game forms (weapon/caster - projectile/ammo forms moved to FormManager)
    RE::TESObjectWEAP* m_weaponForm = nullptr;
    RE::TESObjectREFR* m_casterRef = nullptr;
};

} // namespace Projectile
