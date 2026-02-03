#include "log.h"
#include "InputManager.h"
#include "higgsinterface001.h"
#include "MenuChecker.h"
#include "projectile/DriverUpdateManager.h"
#include "projectile/ProjectileCleanupManager.h"
#include "projectile/ProjectileSubsystem.h"
#include "ThreeDUIInterface001.h"
#include "ThreeDUIActorMenu.h"

// =============================================================================
// P3DUI Interface Export - allows other SKSE plugins to request our interface
// =============================================================================
struct P3DUIMessage {
    enum { kMessage_QueryInterface = 0x3D01F001 };  // Unique identifier for 3DUI
    void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
};

static void* GetP3DUIApi(unsigned int revisionNumber) {
    if (revisionNumber == 1) {
        return P3DUI::GetInterface001();
    }
    spdlog::warn("P3DUI: Unknown API revision {} requested", revisionNumber);
    return nullptr;
}

// Direct DLL export - fallback when SKSE messaging doesn't work
// Other plugins can use GetModuleHandle("3DUI.dll") + GetProcAddress("GetP3DUIInterface")
extern "C" __declspec(dllexport) void* GetP3DUIInterface(unsigned int revisionNumber) {
    spdlog::info("P3DUI: GetP3DUIInterface called directly (revision {})", revisionNumber);
    return GetP3DUIApi(revisionNumber);
}

// Direct DLL export for ActorMenu interface
// Other plugins can use GetModuleHandle("3DUI.dll") + GetProcAddress("GetP3DUIActorMenuInterface")
extern "C" __declspec(dllexport) void* GetP3DUIActorMenuInterface(unsigned int revisionNumber) {
    spdlog::info("P3DUI: GetP3DUIActorMenuInterface called directly (revision {})", revisionNumber);
    if (revisionNumber == 1) {
        return P3DUI::GetActorMenuInterface();
    }
    spdlog::warn("P3DUI: Unknown ActorMenu API revision {} requested", revisionNumber);
    return nullptr;
}

void P3DUIInterfaceQueryHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg->type == P3DUIMessage::kMessage_QueryInterface) {
        spdlog::info("P3DUI: Received interface query from plugin '{}'", a_msg->sender ? a_msg->sender : "unknown");
        auto* msg = static_cast<P3DUIMessage*>(a_msg->data);
        if (msg) {
            msg->GetApiFunction = &GetP3DUIApi;
        }
    }
}

// Event sink for cell attach/detach - hides drivers when player's cell is unloaded
class CellEventSink : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
{
public:
	static CellEventSink* GetSingleton()
	{
		static CellEventSink instance;
		return &instance;
	}

	RE::BSEventNotifyControl ProcessEvent(
		const RE::TESCellAttachDetachEvent* a_event,
		RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || a_event->reference.get() != player) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (a_event->attached) {
			// Player attached to new cell - restore previously visible drivers
			spdlog::trace("CellEventSink: Player attached to cell, restoring visible drivers");
			Widget::DriverUpdateManager::GetSingleton().RestoreVisibleDrivers();
		} else {
			// Player detached from cell - hide all drivers (unbind projectiles)
			spdlog::trace("CellEventSink: Player detached from cell, hiding all drivers");
			Widget::DriverUpdateManager::GetSingleton().HideAllDrivers();
		}

		return RE::BSEventNotifyControl::kContinue;
	}

private:
	CellEventSink() = default;
	~CellEventSink() = default;
	CellEventSink(const CellEventSink&) = delete;
	CellEventSink& operator=(const CellEventSink&) = delete;
};

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		spdlog::info("PostLoad");
		break;

	case SKSE::MessagingInterface::kPostPostLoad:
		spdlog::info("PostPostLoad - Getting HIGGS interface");
		{
			auto* messaging = SKSE::GetMessagingInterface();
			HiggsPluginAPI::GetHiggsInterface001(messaging);

			if (g_higgsInterface) {
				spdlog::info("Got HIGGS interface! Build number: {}", g_higgsInterface->GetBuildNumber());
			} else {
				spdlog::info("Failed to get HIGGS interface");
			}
		}
		break;

	case SKSE::MessagingInterface::kDataLoaded:
		spdlog::info("DataLoaded - Initializing managers");

		// Register menu event handler for input blocking during menus
		MenuChecker::RegisterEventSink();

		// Initialize ProjectileSubsystem FIRST - required by DriverUpdateManager
		// This must happen before any code that might call HideAllDrivers or use drivers
		{
			auto* projSubsystem = Projectile::ProjectileSubsystem::GetSingleton();
			if (!projSubsystem->Initialize()) {
				spdlog::error("Failed to initialize ProjectileSubsystem - 3D UI features will be unavailable");
			} else {
				spdlog::info("ProjectileSubsystem initialized successfully");

				// Initialize DriverUpdateManager with subsystem reference
				Widget::DriverUpdateManager::GetSingleton().Initialize(projSubsystem);
			}
		}

		// Register cell event sink for cell unload handling
		if (auto* sourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
			sourceHolder->AddEventSink<RE::TESCellAttachDetachEvent>(CellEventSink::GetSingleton());
			spdlog::info("Registered cell attach/detach event sink");
		}

		// Initialize InputManager (needs OpenVR hook API for VR button callbacks)
		InputManager::GetSingleton()->Initialize();

		break;

	case SKSE::MessagingInterface::kPreLoadGame:
		spdlog::info("PreLoadGame - Hiding all drivers to unbind projectiles");
		Widget::DriverUpdateManager::GetSingleton().HideAllDrivers();
		break;

	case SKSE::MessagingInterface::kPostLoadGame:
		// Clean up orphaned projectiles from previous session
		ProjectileCleanupManager::GetSingleton()->CleanupOrphanedProjectiles();

		// Notify user if VR interactivity is unavailable due to missing dependency
		if (InputManager::GetSingleton()->IsSkyrimVRToolsMissing()) {
			RE::DebugNotification("3DUI: SkyrimVRTools not found - VR interactions disabled");
			spdlog::warn("Displayed user notification: SkyrimVRTools missing");
		}
		break;

	case SKSE::MessagingInterface::kNewGame:
		// Notify user if VR interactivity is unavailable due to missing dependency
		if (InputManager::GetSingleton()->IsSkyrimVRToolsMissing()) {
			RE::DebugNotification("3DUI: SkyrimVRTools not found - VR interactions disabled");
			spdlog::warn("Displayed user notification: SkyrimVRTools missing");
		}
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
	SKSE::Init(skse);
	SetupLog();

	spdlog::info("3DUI loading...");

	// Note: Hooks are now installed lazily by DriverUpdateManager::Register()
	// on first driver registration. This ensures zero per-frame cost when no
	// mod is actively using 3D UI features.

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		spdlog::error("Failed to register SKSE message listener");
		return false;
	}

	// Register handler for P3DUI interface queries from other plugins
	// nullptr = receive from any sender plugin
	if (!messaging->RegisterListener(nullptr, P3DUIInterfaceQueryHandler)) {
		spdlog::warn("Failed to register P3DUI interface query handler");
	} else {
		spdlog::info("Registered P3DUI interface export handler");
	}

	spdlog::info("3DUI loaded successfully");
	return true;
}
