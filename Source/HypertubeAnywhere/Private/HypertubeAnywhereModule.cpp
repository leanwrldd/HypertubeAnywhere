#include "HypertubeAnywhereModule.h"

#include "HypertubeAnywhereTravel.h"
#include "Engine/AssetManager.h"
#include "EnhancedPlayerInput.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "InputAction.h"
#include "Input/FGChildInputMappingContext.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHypertubeAnywhereInput, Log, All);

namespace HypertubeAnywhere
{
	const TCHAR* const InputContentPath = TEXT("/HypertubeAnywhere/Input");
	const TCHAR* const ToggleActionPath = TEXT("/HypertubeAnywhere/Input/IA_HTA_ToggleHypertube.IA_HTA_ToggleHypertube");

	UInputAction* GetToggleAction()
	{
		static UInputAction* Action = []
		{
			UInputAction* Loaded = LoadObject<UInputAction>(nullptr, ToggleActionPath);
			if (Loaded == nullptr)
			{
				UE_LOG(LogHypertubeAnywhereInput, Error, TEXT("Failed to load input action %s"), ToggleActionPath);
				return Loaded;
			}
			// Only native code references it, so keep it from being garbage collected.
			Loaded->AddToRoot();
			return Loaded;
		}();
		return Action;
	}

	// The rebindable key lives in a child context of the player controller's context. The game
	// finds child contexts through the Asset Manager, which only scans game feature plugins for
	// them, so register this mod's input folder explicitly.
	void RegisterChildInputContexts()
	{
		UAssetManager::CallOrRegister_OnAssetManagerCreated(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
		{
			const int32 NumFound = UAssetManager::Get().ScanPathForPrimaryAssets(
				FPrimaryAssetType(UFGChildInputMappingContext::StaticClass()->GetFName()),
				InputContentPath, UFGChildInputMappingContext::StaticClass(), false);
			UE_LOG(LogHypertubeAnywhereInput, Display, TEXT("Registered %d child input context(s) from %s"), NumFound, InputContentPath);
		}));
	}

	void HandleInput(AFGCharacterPlayer* Player)
	{
		if (Player == nullptr || !Player->IsLocallyControlled())
		{
			return;
		}

		AFGPlayerController* PlayerController = Cast<AFGPlayerController>(Player->GetController());
		const UEnhancedPlayerInput* PlayerInput = PlayerController != nullptr ? Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) : nullptr;
		const UInputAction* ToggleAction = GetToggleAction();
		if (PlayerInput == nullptr || ToggleAction == nullptr)
		{
			return;
		}

		const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(ToggleAction);
		if (Instance == nullptr || Instance->GetTriggerEvent() != ETriggerEvent::Triggered)
		{
			return;
		}

		if (Player->HasAuthority())
		{
			ToggleHypertube(Player);
		}
		else if (UHypertubeAnywhereRemoteCallObject* RemoteCallObject = PlayerController->GetRemoteCallObjectOfClass<UHypertubeAnywhereRemoteCallObject>())
		{
			RemoteCallObject->Server_ToggleHypertube();
		}
		else
		{
			UE_LOG(LogHypertubeAnywhereInput, Warning, TEXT("No remote call object; is the mod installed on the server?"));
		}
	}
}

void FHypertubeAnywhereModule::StartupModule()
{
#if !WITH_EDITOR
	// FactoryGame's editor build only has link stubs, too short for funchook to patch,
	// so hooking there crashes the editor (and Alpakit's cook). Hooks only run in game.
	HypertubeAnywhere::RegisterChildInputContexts();

	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGCharacterPlayer, Tick,
		[](AFGCharacterPlayer* Self, float DeltaSeconds)
		{
			HypertubeAnywhere::HandleInput(Self);
		});

	UE_LOG(LogHypertubeAnywhereInput, Display, TEXT("Hypertube Anywhere hooks installed"));
#endif
}

void FHypertubeAnywhereModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FHypertubeAnywhereModule, HypertubeAnywhere)
