#include "GridProjectileDriver.h"
#include "../InteractionController.h"  // Required for unique_ptr destructor
#include "../../log.h"

namespace Projectile {

// Note: SetFacingAnchor override removed - scene graph handles rotation inheritance
// Child drivers automatically inherit world rotation via GetWorldRotation()

void GridProjectileDriver::UpdateLayout(float deltaTime) {
    // THREAD SAFETY: Copy children before iterating - main thread may modify while VR thread updates
    auto children = GetChildrenMutable();  // Copy, not reference

    for (size_t i = 0; i < children.size(); ++i) {
        auto& child = children[i];
        if (!child) continue;

        // Position each child at its vertical offset in local Z (negative = downward)
        // Scene graph applies parent rotation automatically via GetWorldPosition()
        RE::NiPoint3 localPos(0, 0, -static_cast<float>(i) * m_rowSpacing);
        child->SetLocalPosition(localPos);
    }
}

} // namespace Projectile
