#include "FormManager.h"
#include "../log.h"

namespace Projectile {

void FormManager::Initialize(std::vector<RE::BGSProjectile*> projForms,
                              std::vector<RE::TESAmmo*> ammoForms) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_initialized) {
        spdlog::warn("FormManager already initialized");
        return;
    }

    // Create form slots - use the larger of the two arrays
    // Use parentheses to avoid Windows max macro
    size_t numForms = (std::max)(projForms.size(), ammoForms.size());
    m_forms.resize(numForms);

    for (size_t i = 0; i < numForms; ++i) {
        m_forms[i].projForm = i < projForms.size() ? projForms[i] : nullptr;
        m_forms[i].ammoForm = i < ammoForms.size() ? ammoForms[i] : nullptr;
        m_forms[i].assignedModel.clear();
        m_forms[i].refCount = 0;
    }

    m_modelToForm.clear();
    m_initialized = true;

    spdlog::info("FormManager initialized with {} form slots", numForms);
}

void FormManager::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    m_forms.clear();
    m_modelToForm.clear();
    m_initialized = false;

    spdlog::info("FormManager shut down");
}

int FormManager::AcquireForm(const std::string& modelPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!m_initialized) {
        spdlog::error("FormManager::AcquireForm called before initialization");
        return -1;
    }

    // [DIAG] Log form pool state on every acquire
    spdlog::trace("[FORM] AcquireForm('{}') - used={}/{} free={}",
        modelPath, GetUsedForms(), GetTotalForms(), GetFreeForms());

    // First, check if this model is already assigned to a form
    int existingForm = FindFormByModel(modelPath);
    if (existingForm >= 0) {
        m_forms[existingForm].refCount++;
        spdlog::trace("FormManager::AcquireForm reusing form {} for '{}', refCount={}",
            existingForm, modelPath, m_forms[existingForm].refCount);
        return existingForm;
    }

    // Need a new form - find a free one
    int freeForm = FindFreeForm();
    if (freeForm < 0) {
        spdlog::error("[FORM] EXHAUSTED - No free forms for '{}' (used={}/{})",
            modelPath, GetUsedForms(), GetTotalForms());
        // [DIAG] Dump all form slots for debugging
        for (size_t i = 0; i < m_forms.size(); ++i) {
            spdlog::error("[FORM]   Slot {}: refCount={} model='{}'",
                i, m_forms[i].refCount, m_forms[i].assignedModel);
        }
        return -1;
    }

    // Assign the model to this form
    SetFormModel(freeForm, modelPath);
    m_forms[freeForm].assignedModel = modelPath;
    m_forms[freeForm].refCount = 1;
    m_modelToForm[modelPath] = freeForm;

    spdlog::trace("FormManager::AcquireForm assigned form {} to '{}'", freeForm, modelPath);
    return freeForm;
}

void FormManager::ReleaseForm(int formIndex) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (formIndex < 0 || formIndex >= static_cast<int>(m_forms.size())) {
        return;
    }

    FormSlot& slot = m_forms[formIndex];
    if (slot.refCount <= 0) {
        spdlog::warn("FormManager::ReleaseForm form {} already has refCount 0", formIndex);
        return;
    }

    slot.refCount--;
    spdlog::trace("FormManager::ReleaseForm form {} refCount now {}", formIndex, slot.refCount);

    // If refCount hits 0, the form is now free for reassignment
    // We keep the model assigned until someone else claims it (lazy cleanup)
    // This way if the same model is requested again soon, we don't need to re-set it
    if (slot.refCount == 0) {
        // Remove from fast lookup - form is now fully free
        m_modelToForm.erase(slot.assignedModel);
        spdlog::trace("FormManager::ReleaseForm form {} now free (was '{}')",
            formIndex, slot.assignedModel);
        // Clear the model string to prevent memory fragmentation from retained strings
        slot.assignedModel.clear();
    }
}

RE::BGSProjectile* FormManager::GetProjectileForm(int formIndex) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (formIndex < 0 || formIndex >= static_cast<int>(m_forms.size())) {
        return nullptr;
    }
    return m_forms[formIndex].projForm;
}

RE::TESAmmo* FormManager::GetAmmoForm(int formIndex) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (formIndex < 0 || formIndex >= static_cast<int>(m_forms.size())) {
        return nullptr;
    }
    return m_forms[formIndex].ammoForm;
}

const FormManager::FormSlot* FormManager::GetFormSlot(int formIndex) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (formIndex < 0 || formIndex >= static_cast<int>(m_forms.size())) {
        return nullptr;
    }
    return &m_forms[formIndex];
}

size_t FormManager::GetUsedForms() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& slot : m_forms) {
        if (slot.refCount > 0) {
            ++count;
        }
    }
    return count;
}

size_t FormManager::GetFreeForms() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& slot : m_forms) {
        if (slot.refCount == 0) {
            ++count;
        }
    }
    return count;
}

int FormManager::FindFormByModel(const std::string& modelPath) const {
    auto it = m_modelToForm.find(modelPath);
    if (it != m_modelToForm.end()) {
        return it->second;
    }
    return -1;
}

int FormManager::FindFreeForm() const {
    // First pass: prefer forms with refCount == 0 that already have our model
    // (This is handled by FindFormByModel, so here we just find any free form)

    // Second pass: any form with refCount == 0
    for (size_t i = 0; i < m_forms.size(); ++i) {
        if (m_forms[i].refCount == 0) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

void FormManager::SetFormModel(int formIndex, const std::string& modelPath) {
    if (formIndex < 0 || formIndex >= static_cast<int>(m_forms.size())) {
        return;
    }

    FormSlot& slot = m_forms[formIndex];

    // Set model on BGSProjectile (inherits from TESModel)
    if (slot.projForm) {
        slot.projForm->SetModel(modelPath.c_str());
        spdlog::info("[BOUNDS] SetModel called for '{}'", modelPath);
    }

    // Set model on TESAmmo (also inherits from TESModel)
    if (slot.ammoForm) {
        slot.ammoForm->SetModel(modelPath.c_str());
    }

    spdlog::trace("FormManager::SetFormModel form {} model set to '{}'", formIndex, modelPath);
}

} // namespace Projectile
