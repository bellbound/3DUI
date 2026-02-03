#include "ProjectileSubsystem.h"
#include "ProjectileHook.h"
#include "FormIDs.h"
#include "IPositionable.h"  // For MatrixToEuler
#include "../log.h"

#include <chrono>

namespace Projectile {

ProjectileSubsystem* ProjectileSubsystem::GetSingleton() {
    static ProjectileSubsystem instance;
    return &instance;
}

bool ProjectileSubsystem::Initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_initialized) {
        spdlog::warn("ProjectileSubsystem already initialized");
        return true;
    }

    // Load game forms from FormIDs namespace
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("ProjectileSubsystem: Failed to get TESDataHandler");
        return false;
    }

    const char* pluginName = FormIDs::PluginName;
    spdlog::trace("ProjectileSubsystem loading forms from plugin '{}'", pluginName);

    // Load projectile and ammo forms into temporary vectors
    std::vector<RE::BGSProjectile*> projForms;
    std::vector<RE::TESAmmo*> ammoForms;

    for (uint32_t formID : FormIDs::ProjectileFormIDs) {
        auto* form = dataHandler->LookupForm(formID, pluginName);
        auto* projForm = form ? form->As<RE::BGSProjectile>() : nullptr;
        if (projForm) {
            projForms.push_back(projForm);
            spdlog::trace("Loaded projectile form {:x} from {}", formID, pluginName);
        } else {
            spdlog::warn("Failed to load projectile form {:x} from {}", formID, pluginName);
            projForms.push_back(nullptr);
        }
    }

    for (uint32_t formID : FormIDs::AmmoFormIDs) {
        auto* form = dataHandler->LookupForm(formID, pluginName);
        auto* ammoForm = form ? form->As<RE::TESAmmo>() : nullptr;
        if (ammoForm) {
            ammoForms.push_back(ammoForm);
            spdlog::trace("Loaded ammo form {:x} from {}", formID, pluginName);
        } else {
            spdlog::warn("Failed to load ammo form {:x} from {}", formID, pluginName);
            ammoForms.push_back(nullptr);
        }
    }

    // Initialize FormManager with loaded forms
    m_formManager.Initialize(std::move(projForms), std::move(ammoForms));

    // Load weapon form
    auto* form = dataHandler->LookupForm(FormIDs::WeaponFormID, pluginName);
    m_weaponForm = form ? form->As<RE::TESObjectWEAP>() : nullptr;
    if (!m_weaponForm) {
        spdlog::warn("Failed to load weapon form {:x} from {}", FormIDs::WeaponFormID, pluginName);
    }

    // Note: ProjectileHook is now installed lazily by DriverUpdateManager::Register()
    // on first driver registration. This ensures zero per-frame cost when unused.

    m_initialized = true;
    spdlog::info("ProjectileSubsystem initialized with {} forms", m_formManager.GetTotalForms());

    return true;
}

void ProjectileSubsystem::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!m_initialized) {
        spdlog::warn("ProjectileSubsystem::Shutdown called but not initialized");
        return;
    }

    spdlog::trace("ProjectileSubsystem::Shutdown starting, {} active projectiles", m_projectiles.size());

    ReleaseAllProjectiles();
    m_formManager.Shutdown();

    m_weaponForm = nullptr;
    m_casterRef = nullptr;
    m_projectiles.clear();

    m_initialized = false;
    spdlog::info("ProjectileSubsystem shut down");
}

ControlledProjectilePtr ProjectileSubsystem::GetProjectile(const UUID& uuid) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_projectiles.find(uuid);
    if (it == m_projectiles.end()) {
        spdlog::trace("ProjectileSubsystem::GetProjectile UUID={} not found", uuid.ToString());
        return nullptr;
    }

    auto proj = it->second.lock();
    if (!proj) {
        m_projectiles.erase(it);
        spdlog::warn("ProjectileSubsystem::GetProjectile UUID={} found but expired", uuid.ToString());
    }
    return proj;
}

bool ProjectileSubsystem::HasProjectile(const UUID& uuid) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_projectiles.find(uuid);
    if (it == m_projectiles.end()) {
        return false;
    }

    bool exists = !it->second.expired();
    if (!exists) {
        spdlog::trace("ProjectileSubsystem::HasProjectile UUID={} expired", uuid.ToString());
    }
    return exists;
}

void ProjectileSubsystem::RegisterProjectile(const UUID& uuid, ControlledProjectilePtr proj) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_projectiles[uuid] = proj;
    ProjectileHook::IncrementControlledCount();
    // [DIAG] Track projectile map growth
    spdlog::trace("[TRACK] Registered projectile UUID={}, map size now: {}",
        uuid.ToString(), m_projectiles.size());
}

void ProjectileSubsystem::UnregisterProjectile(const UUID& uuid) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_projectiles.erase(uuid) > 0) {
        ProjectileHook::DecrementControlledCount();
    }
    // [DIAG] Track projectile map shrinkage
    spdlog::trace("[TRACK] Unregistered projectile UUID={}, map size now: {}",
        uuid.ToString(), m_projectiles.size());
}

void ProjectileSubsystem::ReleaseAllProjectiles() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Mark all projectiles for deletion
    for (auto& [uuid, weakPtr] : m_projectiles) {
        if (auto proj = weakPtr.lock()) {
            proj->GetGameProjectile().MarkForDeletion();
            proj->GetGameProjectile().Unbind();
        }
    }
    m_projectiles.clear();
    ProjectileHook::ResetControlledCount();

    spdlog::info("ProjectileSubsystem released all projectiles");
}

void ProjectileSubsystem::OnProjectileUpdate(RE::Projectile* proj, float delta) {
    // [DIAG] Measure mutex acquisition time
    auto lockStart = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto lockEnd = std::chrono::high_resolution_clock::now();
    auto lockTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockStart).count();
    if (lockTimeUs > 100) {
        spdlog::warn("[PERF] OnProjectileUpdate mutex lock took {}us", lockTimeUs);
    }

    if (!m_initialized || !proj) {
        return;
    }

    // [DIAG] Measure FindByGameProjectile time
    auto findStart = std::chrono::high_resolution_clock::now();

    // Find if this projectile belongs to us
    ControlledProjectile* controlledProj = FindByGameProjectile(proj);

    auto findEnd = std::chrono::high_resolution_clock::now();
    auto findTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(findEnd - findStart).count();
    if (findTimeUs > 50) {
        spdlog::warn("[PERF] FindByGameProjectile took {}us (map size={})",
            findTimeUs, m_projectiles.size());
    }

    if (!controlledProj) {
        return;  // Not our projectile
    }

    // Apply our transform overrides
    controlledProj->GetGameProjectile().ApplyTransform();
}

size_t ProjectileSubsystem::GetActiveCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& [uuid, weakPtr] : m_projectiles) {
        if (!weakPtr.expired()) {
            ++count;
        }
    }
    return count;
}

ControlledProjectile* ProjectileSubsystem::FindByGameProjectile(RE::Projectile* proj) {
    for (auto& [uuid, weakPtr] : m_projectiles) {
        if (auto controlledProj = weakPtr.lock()) {
            if (controlledProj->GetGameProjectile().GetProjectile() == proj) {
                return controlledProj.get();
            }
        }
    }
    return nullptr;
}

bool ProjectileSubsystem::FireProjectileFor(ControlledProjectile* controlledProj) {
    if (!controlledProj) {
        spdlog::error("ProjectileSubsystem::FireProjectileFor called with null ControlledProjectile");
        return false;
    }

    // Compute transform directly from scene graph (not smoother, which may be uninitialized)
    // This ensures correct position even when called from Initialize() before first Update()
    ProjectileTransform transform;
    transform.position = controlledProj->GetWorldPosition();
    transform.rotation = controlledProj->GetWorldRotation();
    transform.scale = controlledProj->GetWorldScale();

    // Seed the smoother so subsequent Update() calls don't interpolate from (0,0,0)
    controlledProj->SeedTransform(transform);

    int formIndex = controlledProj->m_formIndex;
    spdlog::trace("ProjectileSubsystem::FireProjectileFor UUID={} form={} pos=({:.1f},{:.1f},{:.1f})",
        controlledProj->GetUUID().ToString(), formIndex,
        transform.position.x, transform.position.y, transform.position.z);

    // Get the forms using formIndex
    auto* projForm = m_formManager.GetProjectileForm(formIndex);
    auto* ammoForm = m_formManager.GetAmmoForm(formIndex);
    auto* weaponForm = GetWeaponForm();
    auto* caster = GetCasterReference();

    if (!ammoForm || !weaponForm || !caster) {
        spdlog::error("FireProjectileFor: Missing required forms (ammo={}, weapon={}, caster={})",
            (void*)ammoForm, (void*)weaponForm, (void*)caster);
        return false;
    }

    // Fire using SKSE task queue for thread safety
    auto task = SKSE::GetTaskInterface();
    if (!task) {
        spdlog::error("FireProjectileFor: SKSE task interface not available");
        return false;
    }

    // Capture what we need for the lambda
    auto weaponPtr = weaponForm;
    auto casterPtr = caster;
    auto ammoPtr = ammoForm;
    // Spawn out of sight so we dont see its initial scale, next transform update will move into position
    RE::NiPoint3 launchPos = RE::NiPoint3{transform.position.x, transform.position.y, transform.position.z - 1000.0f};
    // Extract Euler angles from rotation matrix for LaunchArrow API
    RE::NiPoint3 launchRot = MatrixToEuler(transform.rotation);
    UUID uuid = controlledProj->GetUUID();

    // Capture current fire generation - used to detect stale requests on rapid visibility toggles
    uint64_t fireGeneration = controlledProj->GetFireGeneration();

    // Get weak_ptr with proper synchronization - callers don't hold the mutex
    std::weak_ptr<ControlledProjectile> weakProj;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_projectiles.find(uuid);
        if (it != m_projectiles.end()) {
            weakProj = it->second;
        }
    }

    task->AddTask([weaponPtr, casterPtr, ammoPtr, launchPos, launchRot, weakProj, uuid, fireGeneration]() {
        spdlog::trace("FireProjectileFor task: ENTER UUID={}", uuid.ToString());

        // Check generation BEFORE launching - if stale, don't create the game projectile at all
        auto proj = weakProj.lock();
        if (!proj) {
            spdlog::warn("FireProjectileFor task: ControlledProjectile {} no longer exists", uuid.ToString());
            return;
        }

        if (fireGeneration != proj->GetFireGeneration()) {
            spdlog::trace("FireProjectileFor task: stale generation ({} vs current {}), not launching",
                fireGeneration, proj->GetFireGeneration());
            return;
        }

        if (!casterPtr || !ammoPtr) {
            spdlog::error("FireProjectileFor task: missing caster or ammo");
            return;
        }

        // Get the shooter as an Actor
        auto* shooter = casterPtr->As<RE::Actor>();
        if (!shooter) {
            shooter = RE::PlayerCharacter::GetSingleton();
        }

        if (!shooter) {
            spdlog::error("FireProjectileFor task: no valid shooter");
            return;
        }

        // Set up launch angles from rotation
        RE::Projectile::ProjectileRot angles;
        angles.x = launchRot.x;  // pitch
        angles.z = launchRot.z;  // yaw

        // Launch the projectile
        RE::ProjectileHandle handle;
        RE::Projectile::LaunchArrow(&handle, shooter, ammoPtr, weaponPtr, launchPos, angles);

        spdlog::trace("FireProjectileFor task: LaunchArrow returned, handle valid={}",
            static_cast<bool>(handle));

        if (!handle) {
            spdlog::warn("FireProjectileFor task: LaunchArrow returned null handle");
            return;
        }

        // Get the projectile pointer from handle
        RE::Projectile* gameProj = handle.get().get();
        if (!gameProj) {
            spdlog::error("FireProjectileFor task: handle.get() returned null projectile");
            return;
        }

        // Bind to the ControlledProjectile's GameProjectile (generation already checked above)
        proj->BindToProjectile(gameProj, fireGeneration);
        spdlog::trace("FireProjectileFor task: successfully bound projectile UUID={}", uuid.ToString());
    });

    return true;
}

int ProjectileSubsystem::AcquireForm(const std::string& modelPath) {
    auto lockStart = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto lockEnd = std::chrono::high_resolution_clock::now();
    auto lockTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockStart).count();
    if (lockTimeUs > 200) {
        spdlog::warn("[LOCK] ProjectileSubsystem::AcquireForm waited {}us (model='{}')", lockTimeUs, modelPath);
    }
    return m_formManager.AcquireForm(modelPath);
}

void ProjectileSubsystem::ReleaseForm(int formIndex) {
    auto lockStart = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto lockEnd = std::chrono::high_resolution_clock::now();
    auto lockTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockStart).count();
    if (lockTimeUs > 200) {
        spdlog::warn("[LOCK] ProjectileSubsystem::ReleaseForm waited {}us (form={})", lockTimeUs, formIndex);
    }
    m_formManager.ReleaseForm(formIndex);
}

RE::TESObjectWEAP* ProjectileSubsystem::GetWeaponForm() {
    if (!m_weaponForm) {
        spdlog::warn("ProjectileSubsystem::GetWeaponForm called but no weapon form loaded");
    }
    return m_weaponForm;
}

RE::TESObjectREFR* ProjectileSubsystem::GetCasterReference() {
    if (m_casterRef) {
        return m_casterRef;
    }

    // Use player as caster for now
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("ProjectileSubsystem::GetCasterReference failed - no player singleton");
    }
    return player;
}

} // namespace Projectile
