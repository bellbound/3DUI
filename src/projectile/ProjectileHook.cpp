#include "ProjectileHook.h"
#include "ProjectileSubsystem.h"
#include "../log.h"
#include <thread>
#include <sstream>

namespace Projectile {

// Vtable constants from SpellWheelVR (Skyrim VR 1.4.15)
// ArrowProjectile inherits from MissileProjectile -> Projectile -> TESObjectREFR
// The function we hook is at vtable index 0xAC
namespace {
    // ArrowProjectile vtable offset in Skyrim VR
    constexpr REL::Offset ArrowProjectileVtbl_Offset{0x016F93A8};

    // Vtable index for the physics update function
    constexpr size_t kVtableIndex = 0xAC;

    // Frame counter for throttled logging
    static uint64_t s_hookCallCount = 0;
    static constexpr uint64_t kLogEveryNFrames = 1000; 
}

bool ProjectileHook::Install() {
    // Get pointer to the vtable entry we want to hook
    // vtable[0xAC] contains the function pointer for physics updates
    REL::Relocation<GetVelocityFunc*> vtblEntry{
        ArrowProjectileVtbl_Offset.address() + kVtableIndex * sizeof(void*)
    };

    // Save original function pointer
    s_originalFunc = *vtblEntry;

    if (!s_originalFunc) {
        spdlog::error("ProjectileHook: Failed to read original function pointer");
        return false;
    }

    // Overwrite vtable entry with our hook
    // REL::safe_write handles memory protection internally
    REL::safe_write(vtblEntry.address(), reinterpret_cast<uintptr_t>(&Hook_GetVelocity));

    spdlog::info("ProjectileHook: Installed at vtable offset 0x{:X}, index 0x{:X}",
        ArrowProjectileVtbl_Offset.offset(), kVtableIndex);

    return true;
}

void ProjectileHook::Hook_GetVelocity(RE::Projectile* proj, float delta) {
    // Always call original first - let game do its physics
    if (s_originalFunc) {
        s_originalFunc(proj, delta);
    }

    // FAST PATH: Skip subsystem lookup if no controlled projectiles exist
    // This is critical for performance - the hook runs for EVERY arrow projectile
    if (s_controlledCount.load(std::memory_order_relaxed) == 0) {
        return;
    }

    // THREAD DEBUG: Log thread ID once to verify which thread game hook runs on
    static bool s_loggedThreadId = false;
    if (!s_loggedThreadId) {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        spdlog::warn("[THREAD DEBUG] Hook_GetVelocity (game hook) running on thread ID: {}", oss.str());
        s_loggedThreadId = true;
    }

    ++s_hookCallCount;

    // Then let our subsystem override if this is a controlled projectile
    auto* subsystem = ProjectileSubsystem::GetSingleton();
    if (subsystem && subsystem->IsInitialized()) {
        subsystem->OnProjectileUpdate(proj, delta);
    }
}

} // namespace Projectile
