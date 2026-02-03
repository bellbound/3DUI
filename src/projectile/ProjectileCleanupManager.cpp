#include "ProjectileCleanupManager.h"
#include "FormIDs.h"
#include <spdlog/spdlog.h>

ProjectileCleanupManager::ProjectileCleanupManager()
{
}

void ProjectileCleanupManager::ResolveProjectileForms()
{
    if (m_formsResolved) {
        return;
    }

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("ProjectileCleanupManager::ResolveProjectileForms - Failed to get TESDataHandler");
        return;
    }

    m_projectileFormIDs.clear();

    for (uint32_t baseFormID : Projectile::FormIDs::ProjectileFormIDs) {
        auto* form = dataHandler->LookupForm(baseFormID, Projectile::FormIDs::PluginName);
        if (form) {
            m_projectileFormIDs.insert(form->GetFormID());
            spdlog::trace("ProjectileCleanupManager - Resolved projectile form {:08X}", form->GetFormID());
        }
    }

    spdlog::info("ProjectileCleanupManager::ResolveProjectileForms - Resolved {} projectile forms",
        m_projectileFormIDs.size());
    m_formsResolved = true;
}

bool ProjectileCleanupManager::IsOurProjectile(RE::FormID formID) const
{
    return m_projectileFormIDs.contains(formID);
}

void ProjectileCleanupManager::CleanupOrphanedProjectiles()
{
    // Ensure forms are resolved
    ResolveProjectileForms();

    if (m_projectileFormIDs.empty()) {
        spdlog::warn("ProjectileCleanupManager::CleanupOrphanedProjectiles - No projectile forms resolved, skipping cleanup");
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("ProjectileCleanupManager::CleanupOrphanedProjectiles - No player, skipping cleanup");
        return;
    }

    auto* playerCell = player->GetParentCell();
    if (!playerCell) {
        spdlog::warn("ProjectileCleanupManager::CleanupOrphanedProjectiles - No player cell, skipping cleanup");
        return;
    }

    spdlog::info("ProjectileCleanupManager::CleanupOrphanedProjectiles - Scanning player cell for orphaned projectiles");

    std::vector<RE::TESObjectREFR*> projectilesToDelete;

    // Scan all references in the player's cell
    playerCell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
        if (!ref) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check if it's a projectile
        auto* projectile = ref->As<RE::Projectile>();
        if (!projectile) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check if it's one of our projectiles by base form
        auto* baseObj = projectile->GetBaseObject();
        if (!baseObj) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        if (IsOurProjectile(baseObj->GetFormID())) {
            spdlog::info("ProjectileCleanupManager - Found orphaned projectile {:08X} (base: {:08X})",
                projectile->GetFormID(), baseObj->GetFormID());
            projectilesToDelete.push_back(projectile);
        }

        return RE::BSContainer::ForEachResult::kContinue;
    });

    // Delete the orphaned projectiles
    for (auto* proj : projectilesToDelete) {
        if (proj) {
            spdlog::info("ProjectileCleanupManager - Deleting orphaned projectile {:08X}", proj->GetFormID());
            proj->Disable();
            proj->SetDelete(true);
        }
    }

    spdlog::info("ProjectileCleanupManager::CleanupOrphanedProjectiles - Cleaned up {} orphaned projectiles",
        projectilesToDelete.size());
}
