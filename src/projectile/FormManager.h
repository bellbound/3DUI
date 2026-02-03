#pragma once

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace Projectile {

// Tracks model→FormID assignments with reference counting.
// Allows multiple projectile instances to share the same FormID when they use the same model.
// Forms are released when refCount hits 0, making them available for other models.
class FormManager {
public:
    struct FormSlot {
        RE::BGSProjectile* projForm = nullptr;
        RE::TESAmmo* ammoForm = nullptr;
        std::string assignedModel;  // Empty = unassigned/free
        int refCount = 0;           // How many live instances use this form
    };

    FormManager() = default;
    ~FormManager() = default;

    // Non-copyable
    FormManager(const FormManager&) = delete;
    FormManager& operator=(const FormManager&) = delete;

    // Initialize with forms loaded from plugin
    void Initialize(std::vector<RE::BGSProjectile*> projForms,
                    std::vector<RE::TESAmmo*> ammoForms);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Acquire a form for the given model path.
    // If model already assigned to a form, reuses it (refCount++).
    // Otherwise assigns to a free form and sets the model.
    // Returns formIndex, or -1 if no form available.
    int AcquireForm(const std::string& modelPath);

    // Release a form. Decrements refCount.
    // If refCount hits 0, form becomes available for reassignment.
    void ReleaseForm(int formIndex);

    // Get the forms for firing
    RE::BGSProjectile* GetProjectileForm(int formIndex);
    RE::TESAmmo* GetAmmoForm(int formIndex);

    // Get form slot info (for debugging/stats)
    const FormSlot* GetFormSlot(int formIndex) const;

    // Statistics
    size_t GetTotalForms() const { return m_forms.size(); }
    size_t GetUsedForms() const;   // Forms with refCount > 0
    size_t GetFreeForms() const;   // Forms with refCount == 0

private:
    // Find a form already assigned to this model, or -1 if none
    int FindFormByModel(const std::string& modelPath) const;

    // Find a free form (refCount == 0), or -1 if none
    int FindFreeForm() const;

    // Set the model on a form's BGSProjectile and TESAmmo
    void SetFormModel(int formIndex, const std::string& modelPath);

    mutable std::recursive_mutex m_mutex;
    std::vector<FormSlot> m_forms;
    std::unordered_map<std::string, int> m_modelToForm;  // Fast lookup: model → formIndex
    bool m_initialized = false;
};

} // namespace Projectile
