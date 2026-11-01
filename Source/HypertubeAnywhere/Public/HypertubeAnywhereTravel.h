#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "Module/GameInstanceModule.h"
#include "HypertubeAnywhereTravel.generated.h"

class AFGCharacterPlayer;

namespace HypertubeAnywhere
{
	/** How close (cm) the player must be to a hypertube to jump into it. */
	constexpr float EnterRange = 600.0f;

	/**
	 * Exits the hypertube if the player is travelling in one, otherwise enters the closest hypertube
	 * in range, heading the way the player is looking. Server only; clients go through the RCO.
	 */
	void ToggleHypertube(AFGCharacterPlayer* Player);
}

/** Lets clients ask the server to toggle them in or out of a hypertube. */
UCLASS(NotBlueprintable)
class HYPERTUBEANYWHERE_API UHypertubeAnywhereRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable)
	void Server_ToggleHypertube();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** RCOs need at least one replicated property or their RPCs never reach the server. */
	UPROPERTY(Replicated)
	bool bForceNetField = false;
};

/** Root module whose only job is registering the remote call object with SML. */
UCLASS()
class HYPERTUBEANYWHERE_API UHypertubeAnywhereGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UHypertubeAnywhereGameInstanceModule();
};
