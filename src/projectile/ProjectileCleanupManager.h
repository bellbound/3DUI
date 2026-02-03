#pragma once

#include <RE/Skyrim.h>
#include <set>

class ProjectileCleanupManager
{
public:
    static ProjectileCleanupManager* GetSingleton()
    {
        static ProjectileCleanupManager instance;
        return &instance;
    }

    // Called on game load to clean up orphaned projectiles from previous session
    void CleanupOrphanedProjectiles();

private:
    ProjectileCleanupManager();
    ~ProjectileCleanupManager() = default;
    ProjectileCleanupManager(const ProjectileCleanupManager&) = delete;
    ProjectileCleanupManager& operator=(const ProjectileCleanupManager&) = delete;

    // Cache of resolved projectile form IDs (with load order applied)
    std::set<RE::FormID> m_projectileFormIDs;
    bool m_formsResolved = false;

    // Resolve base form IDs to full form IDs with load order
    void ResolveProjectileForms();

    // Check if a form ID belongs to our projectiles
    bool IsOurProjectile(RE::FormID formID) const;
};
