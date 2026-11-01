#include "HypertubeAnywhereModule.h"

#include "HypertubeAnywhereTravel.h"
#include "Buildables/FGPipeHyperStart.h"
#include "Engine/AssetManager.h"
#include "EnhancedPlayerInput.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "InputAction.h"
#include "Input/FGChildInputMappingContext.h"
#include "Patching/NativeHookManager.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <dlfcn.h>
#endif

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

	// Entrances only run (and draw full power) while a player is this close (cm). Far enough
	// that the entrance has spun up before anyone reaches it.
	constexpr float EntranceActiveRange = 2000.0f;

	bool IsAnyPlayerNear(const AActor* Entrance)
	{
		const UWorld* World = Entrance->GetWorld();
		if (World == nullptr)
		{
			return false;
		}

		const FVector EntranceLocation = Entrance->GetActorLocation();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* Pawn = It->IsValid() ? (*It)->GetPawn() : nullptr;
			if (Pawn != nullptr && FVector::DistSquared(Pawn->GetActorLocation(), EntranceLocation) <= FMath::Square(EntranceActiveRange))
			{
				return true;
			}
		}
		return false;
	}

	using FCanProduceFn = bool (*)(const AFGPipeHyperStart*);
	FCanProduceFn OriginalCanProduce = nullptr;

	bool CanProduceHook(const AFGPipeHyperStart* Self)
	{
		return IsAnyPlayerNear(Self) && OriginalCanProduce(Self);
	}

	// SUBSCRIBE_UOBJECT_METHOD resolves virtuals through the vtable slot our headers compile to.
	// For CanProduce_Implementation that slot is wrong in the shipping game: it hooked
	// AFGBuildableFactory::GetIdlePowerConsumption instead. Look up the exported function itself,
	// in whichever module holds AFGPipeHyperStart's vtable (Steam, EGS or server build).
	void* FindCanProduceImplementation()
	{
		const void* VTable = *reinterpret_cast<void* const*>(GetDefault<AFGPipeHyperStart>());
#if PLATFORM_WINDOWS
		HMODULE GameModule = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(VTable), &GameModule))
		{
			return nullptr;
		}
		return reinterpret_cast<void*>(GetProcAddress(GameModule, "?CanProduce_Implementation@AFGPipeHyperStart@@UEBA_NXZ"));
#else
		Dl_info Info;
		void* GameModule = dladdr(VTable, &Info) != 0 ? dlopen(Info.dli_fname, RTLD_NOW | RTLD_NOLOAD) : nullptr;
		return GameModule != nullptr ? dlsym(GameModule, "_ZNK17AFGPipeHyperStart25CanProduce_ImplementationEv") : nullptr;
#endif
	}

	// Vanilla entrances "produce", and so draw full power, whenever they're attached to a tube.
	// Not producing drops them to the factory idle draw; HasPower comes from the circuit, so
	// they still work once a player walks up.
	void HookEntranceCanProduce()
	{
		void* Target = FindCanProduceImplementation();
		if (Target == nullptr)
		{
			UE_LOG(LogHypertubeAnywhereInput, Error, TEXT("AFGPipeHyperStart::CanProduce_Implementation not found; entrances keep vanilla power use"));
			return;
		}

		FNativeHookManagerInternal::RegisterHookFunction(TEXT("AFGPipeHyperStart::CanProduce_Implementation"),
			Target, nullptr, 0, reinterpret_cast<void*>(&CanProduceHook), reinterpret_cast<void**>(&OriginalCanProduce));
		UE_LOG(LogHypertubeAnywhereInput, Display, TEXT("Hooked AFGPipeHyperStart::CanProduce_Implementation at %p"), Target);
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

	HypertubeAnywhere::HookEntranceCanProduce();

	UE_LOG(LogHypertubeAnywhereInput, Display, TEXT("Hypertube Anywhere hooks installed"));
#endif
}

void FHypertubeAnywhereModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FHypertubeAnywhereModule, HypertubeAnywhere)
