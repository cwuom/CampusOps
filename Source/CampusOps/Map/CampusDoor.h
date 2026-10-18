#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusDoor.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;

/**
 * Hinged classroom / office door: the leaf swings away from whoever opens it (interact key), blocks movement and
 * opens by itself for bots walking into it. Rounds stop in (or punch through) the steel leaf and hole the glass of
 * its vision panel, which breaks away piece by piece. Server authoritative, replicated.
 * The hinge is the actor origin; the leaf runs along local +X (its width), Z up, thickness across local Y.
 */
UCLASS()
class CAMPUSOPS_API ACampusDoor : public AActor
{
	GENERATED_BODY()

public:
	ACampusDoor();

	/** Server, before FinishSpawning. */
	void Setup(float InWidth, float InHeight);
	/**
	 * Server, once the building geometry exists: how far the leaf can swing each way before it would hit a wall, stair
	 * or prop, or one of the non-colliding Decor boxes (unit-cube transforms: notice boards, dados, trim). False if it
	 * can't open usefully either way (the opening is then left without a door).
	 */
	bool MeasureClearance(TConstArrayView<FTransform> Decor = {});
	/** Server: opens away from User, or closes. */
	void Toggle(const AActor* User);
	bool IsOpen() const { return OpenSide != 0; }
	/** Middle of the closed leaf at handle height (prompts and reach checks). */
	FVector GetHandleLocation() const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void BuildLeaf();
	void OpenForBots();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Swing;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Blocker;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Paint;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Steel;
	UPROPERTY(Transient) TObjectPtr<class UCampusBreakableGlass> Glass;

	UPROPERTY(Replicated) float Width = 100.f;
	UPROPERTY(Replicated) float Height = 214.f;
	/** 0 closed, +1 / -1 swung toward local +Y / -Y. */
	UPROPERTY(Replicated) int8 OpenSide = 0;
	/** Free swing toward local +Y / -Y, degrees. */
	UPROPERTY(Replicated) float MaxOpenPos = 95.f;
	UPROPERTY(Replicated) float MaxOpenNeg = 95.f;

	float TargetAngle() const { return OpenSide > 0 ? MaxOpenPos : (OpenSide < 0 ? -MaxOpenNeg : 0.f); }

	float Angle = 0.f;
	float AngularVelocity = 0.f;
	float BotScanTime = 0.f;
	bool bBuilt = false;
};
