#pragma once

// Core types
#include "../util/UUID.h"
#include "GameProjectile.h"
#include "ControlledProjectile.h"
#include "ProjectileSubsystem.h"
#include "FormIDs.h"

namespace Projectile {

// Convenience type aliases
using ProjectilePtr = ControlledProjectilePtr;
using ProjectileWeakPtr = ControlledProjectileWeakPtr;

// Quick access to the singleton
inline ProjectileSubsystem* GetSubsystem() {
    return ProjectileSubsystem::GetSingleton();
}

} // namespace Projectile
