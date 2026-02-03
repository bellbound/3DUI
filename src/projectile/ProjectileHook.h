#pragma once

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <atomic>

namespace Projectile {

// Vtable hook for Projectile::Unk_AC (physics update function)
// Matches SpellWheelVR's approach: hook ArrowProjectile vtable at index 0xAC
//
// SpellWheelVR reference (SpellWheelVR.cpp):
//   ArrowProjectileVtbl_Offset = 0x016F93A8
//   RelocPtr<...> GetVelocityOriginalFunctionProjectile_vtbl(ArrowProjectileVtbl_Offset + 0xAC * 8);
//   g_originalGetVelocityFunctionProjectile = *GetVelocityOriginalFunctionProjectile_vtbl;
//   SafeWrite64(GetVelocityOriginalFunctionProjectile_vtbl.GetUIntPtr(), GetFnAddr(&GetVelocity_HookProjectile));

class ProjectileHook {
public:
    // Install the vtable hook - call once during plugin load
    // Returns true on success
    static bool Install();

    // Our hook function - called every frame for each projectile
    static void Hook_GetVelocity(RE::Projectile* proj, float delta);

    // Counter management for fast early-out when no controlled projectiles exist
    // Called by ProjectileSubsystem when projectiles are registered/unregistered
    static void IncrementControlledCount() { s_controlledCount.fetch_add(1, std::memory_order_relaxed); }
    static void DecrementControlledCount() { s_controlledCount.fetch_sub(1, std::memory_order_relaxed); }
    static void ResetControlledCount() { s_controlledCount.store(0, std::memory_order_relaxed); }

private:
    // Original function pointer type matching Skyrim's signature
    using GetVelocityFunc = void(*)(RE::Projectile*, float);

    // Storage for original function - must call it first
    static inline GetVelocityFunc s_originalFunc = nullptr;

    // Fast counter for early-out check in Hook_GetVelocity
    // Avoids subsystem lookup when no controlled projectiles exist
    static inline std::atomic<size_t> s_controlledCount{0};
};

} // namespace Projectile
