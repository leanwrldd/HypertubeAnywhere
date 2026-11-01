#include "HypertubeAnywhereTravel.h"

#include "Buildables/FGBuildablePipeHyper.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "FGCharacterMovementComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPipeConnectionComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogHypertubeAnywhere, Log, All);

namespace HypertubeAnywhere
{
	// Matches AFGPipeHyperStart::mInitialMinSpeedFactor, so entering mid-tube feels like using an entrance.
	constexpr float InitialMinSpeedFactor = 1.4f;

	// The substep EnterPipeHyperDirect passes to EnterPipeHyperInternal.
	constexpr float InitialAccumulatedDeltaTime = 0.016f;

	struct FTubeEntry
	{
		AFGBuildablePipeHyper* Pipe = nullptr;
		UFGPipeConnectionComponentBase* Connection = nullptr;
		float Progress = 0.0f;
		FVector TravelDirection = FVector::ZeroVector;
		float DistanceToPlayer = TNumericLimits<float>::Max();
	};

	bool FindClosestTube(const AFGCharacterPlayer* Player, FTubeEntry& OutEntry)
	{
		const FVector PlayerLocation = Player->GetActorLocation();
		const FVector LookDirection = Player->GetControlRotation().Vector();

		for (TActorIterator<AFGBuildablePipeHyper> It(Player->GetWorld()); It; ++It)
		{
			AFGBuildablePipeHyper* Pipe = *It;
			const USplineComponent* Spline = Pipe->GetSplineComponent();
			UFGPipeConnectionComponentBase* Connection0 = Pipe->GetConnection0();
			UFGPipeConnectionComponentBase* Connection1 = Pipe->GetConnection1();
			if (Spline == nullptr || Connection0 == nullptr || Connection1 == nullptr)
			{
				continue;
			}

			// Cheap reject before the closest-point search, which walks the whole spline.
			const FSphere Bounds = Spline->Bounds.GetSphere();
			if (FVector::Dist(Bounds.Center, PlayerLocation) > Bounds.W + EnterRange)
			{
				continue;
			}

			const float InputKey = Spline->FindInputKeyClosestToWorldLocation(PlayerLocation);
			const FVector ClosestPoint = Spline->GetLocationAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
			const float Distance = FVector::Dist(ClosestPoint, PlayerLocation);
			if (Distance > EnterRange || Distance >= OutEntry.DistanceToPlayer)
			{
				continue;
			}

			const float SplineLength = Spline->GetSplineLength();
			const float DistanceAlongSpline = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
			const FVector SplineDirection = Spline->GetDirectionAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);

			// Don't assume connection 0 sits at the spline start; pick whichever is closer to it.
			const FVector SplineStart = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
			const bool bConnection0AtStart = FVector::DistSquared(Connection0->GetComponentLocation(), SplineStart)
				<= FVector::DistSquared(Connection1->GetComponentLocation(), SplineStart);
			UFGPipeConnectionComponentBase* StartConnection = bConnection0AtStart ? Connection0 : Connection1;
			UFGPipeConnectionComponentBase* EndConnection = bConnection0AtStart ? Connection1 : Connection0;

			// Pipe progress counts from the connection entered through (EnterPipeHyperDirect always
			// passes 0 for either end), so heading backwards means entering through the far end.
			const bool bForward = FVector::DotProduct(LookDirection, SplineDirection) >= 0.0f;

			OutEntry.Pipe = Pipe;
			OutEntry.Connection = bForward ? StartConnection : EndConnection;
			OutEntry.Progress = bForward ? DistanceAlongSpline : SplineLength - DistanceAlongSpline;
			OutEntry.TravelDirection = bForward ? SplineDirection : -SplineDirection;
			OutEntry.DistanceToPlayer = Distance;
		}

		return OutEntry.Pipe != nullptr;
	}

	void ExitHypertube(AFGCharacterPlayer* Player, UFGCharacterMovementComponent* Movement)
	{
		Movement->PipeHyperForceExit(false);

		// The player is left inside the tube's collision; teleporting in place lets the engine
		// find the nearest spot the capsule fits.
		Player->TeleportTo(Player->GetActorLocation(), Player->GetActorRotation());
		UE_LOG(LogHypertubeAnywhere, Display, TEXT("%s exited hypertube"), *Player->GetName());
	}

	void EnterHypertube(AFGCharacterPlayer* Player, UFGCharacterMovementComponent* Movement)
	{
		FTubeEntry Entry;
		if (!FindClosestTube(Player, Entry))
		{
			UE_LOG(LogHypertubeAnywhere, Display, TEXT("%s: no hypertube within %.0f cm"), *Player->GetName(), EnterRange);
			return;
		}

		const float MinSpeed = Movement->GetPipeMinSpeed() * InitialMinSpeedFactor;
		const float Velocity = FMath::Max(FVector::DotProduct(Player->GetVelocity(), Entry.TravelDirection), MinSpeed);

		// Same sequence as UFGCharacterMovementComponent::EnterPipeHyperDirect, but starting part
		// way along the tube instead of at its end.
		if (!Movement->EnterPipeHyperInternal(Entry.Connection, Velocity, Entry.Progress, InitialAccumulatedDeltaTime))
		{
			UE_LOG(LogHypertubeAnywhere, Warning, TEXT("%s: game refused hypertube entry into %s"), *Player->GetName(), *Entry.Pipe->GetName());
			return;
		}

		const AGameStateBase* GameState = Player->GetWorld()->GetGameState();
		const float ServerTime = GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : Player->GetWorld()->GetTimeSeconds();
		Player->Client_HyperTubeStart(Entry.Connection, ServerTime, Velocity, Entry.Progress, Movement->GetPipeHyperDataRef().mTimeInterpreter);

		UE_LOG(LogHypertubeAnywhere, Display, TEXT("%s entered %s through %s at %.0f cm, speed %.0f"),
			*Player->GetName(), *Entry.Pipe->GetName(), *Entry.Connection->GetName(), Entry.Progress, Velocity);
	}

	// Walking, falling, swimming, hovering and parachuting can jump in; ladders, ziplines and
	// cutscenes own the character's movement, so leave those alone.
	bool CanEnterFromMovementMode(const UFGCharacterMovementComponent* Movement)
	{
		if (Movement->MovementMode != MOVE_Custom)
		{
			return true;
		}

		switch (static_cast<ECustomMovementMode>(Movement->CustomMovementMode))
		{
		case ECustomMovementMode::CMM_Hover:
		case ECustomMovementMode::CMM_HoverSlowFall:
		case ECustomMovementMode::CMM_Parachute:
			return true;
		default:
			return false;
		}
	}

	void ToggleHypertube(AFGCharacterPlayer* Player)
	{
		if (Player == nullptr || !Player->HasAuthority())
		{
			return;
		}

		UFGCharacterMovementComponent* Movement = Player->GetFGMovementComponent();
		if (Movement == nullptr)
		{
			return;
		}

		if (Movement->IsInHyperPipe())
		{
			ExitHypertube(Player, Movement);
		}
		else if (CanEnterFromMovementMode(Movement) && !Player->IsDrivingVehicle())
		{
			EnterHypertube(Player, Movement);
		}
	}
}

void UHypertubeAnywhereRemoteCallObject::Server_ToggleHypertube_Implementation()
{
	HypertubeAnywhere::ToggleHypertube(GetOwnerPlayerCharacter());
}

void UHypertubeAnywhereRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHypertubeAnywhereRemoteCallObject, bForceNetField);
}

UHypertubeAnywhereGameInstanceModule::UHypertubeAnywhereGameInstanceModule()
{
	// Native root modules are silently skipped by SML unless this is set.
	bRootModule = true;
	RemoteCallObjects.Add(UHypertubeAnywhereRemoteCallObject::StaticClass());
}
