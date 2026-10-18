#include "Game/CampusGameInstance.h"
#include "Game/CampusMaps.h"
#include "Game/CampusEnvironment.h"
#include "CampusArmory.h"
#include "CampusAssets.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AudioDevice.h"
#include "Menu/CampusMenuGameMode.h"
#include "Game/CampusGameModeBase.h"
#include "Game/CampusPlayerState.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusInputConfig.h"
#include "Player/CampusViewmodelComponent.h"
#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusFX.h"
#include "Map/CampusWindowGlass.h"
#include "Map/CampusDoor.h"
#include "Map/CampusHQScreens.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "AI/CampusBotController.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInstance.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "CampusTypes.h"

static const TCHAR* CampusSlot = TEXT("CampusProfile");

void UCampusGameInstance::Init()
{
	Super::Init();
	if (UCampusSaveGame* Save = Cast<UCampusSaveGame>(UGameplayStatics::LoadGameFromSlot(CampusSlot, 0)))
	{
		Profile = Save->Profile;
	}
	EnsureLoadouts();
	if (Profile.Graphics.Revision != FCampusGraphicsSettings::CurrentRevision && !IsRunningCommandlet() && !IsRunningDedicatedServer())
	{
		CampusGraphics::ApplyPreset(Profile.Graphics, CampusGraphics::DetectPreset());
		Profile.Graphics.Revision = FCampusGraphicsSettings::CurrentRevision;
		SaveProfile();
	}
	ApplyGraphicsQuality();
	ApplyKeyBindings();
	ApplyAudioMix();
	// Engine diagnostics (Lumen budgets, shader compile notices, ...) are printed straight onto the game view.
	if (!FParse::Param(FCommandLine::Get(), TEXT("ScreenMessages")))
	{
		GAreScreenMessagesEnabled = false;
	}

	FString ScriptFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("CampusScriptFile="), ScriptFile))
	{
		TArray<FString> Lines;
		if (FFileHelper::LoadFileToStringArray(Lines, *ScriptFile))
		{
			for (const FString& Raw : Lines)
			{
				FString Line = Raw.TrimStartAndEnd();
				FString TimeStr;
				FString Command;
				if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || !Line.Split(TEXT(" "), &TimeStr, &Command))
				{
					continue;
				}
				Script.Add({ FCString::Atof(*TimeStr), Command.TrimStartAndEnd() });
			}
			Script.Sort([](const FScriptStep& A, const FScriptStep& B) { return A.Time < B.Time; });
			bScriptWaitingForShaders = true;
			ScriptTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusGameInstance::TickScript));
			UE_LOG(LogTemp, Display, TEXT("[CampusScript] loaded %d steps from %s"), Script.Num(), *ScriptFile);
		}
	}
}

void UCampusGameInstance::Shutdown()
{
	if (ScriptTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ScriptTicker);
	}
	Super::Shutdown();
}

bool UCampusGameInstance::TickScript(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || ScriptIndex >= Script.Num())
	{
		return true;
	}
	if (bScriptWaitingForShaders)
	{
		const bool bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
		if (bCompiling || World->GetTimeSeconds() < 1.f)
		{
			return true;
		}
		bScriptWaitingForShaders = false;
		// Resume where the script paused (times are absolute from the first shader wait).
		ScriptClock = ScriptIndex > 0 ? Script[ScriptIndex - 1].Time : 0.f;
	}
	ScriptClock += DeltaTime;
	if (ShotSeqRemaining > 0 && ShotSeqFrame++ % FMath::Max(ShotSeqEvery, 1) == 0)
	{
		CampusShot(FString::Printf(TEXT("%s_%03d"), *ShotSeqName, ShotSeqIndex++));
		--ShotSeqRemaining;
	}
	while (ScriptIndex < Script.Num() && Script[ScriptIndex].Time <= ScriptClock)
	{
		const FString Command = Script[ScriptIndex++].Command;
		UE_LOG(LogTemp, Display, TEXT("[CampusScript] %.1fs: %s"), ScriptClock, *Command);
		if (Command == TEXT("waitshaders"))
		{
			bScriptWaitingForShaders = true;
			break;
		}
		if (APlayerController* PC = GetFirstLocalPlayerController())
		{
			PC->ConsoleCommand(Command, true);
		}
		else if (GEngine)
		{
			GEngine->Exec(World, *Command);
		}
	}
	return true;
}

void UCampusGameInstance::CampusShot(const FString& Name)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoShots") / (Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogTemp, Display, TEXT("[CampusScript] screenshot -> %s"), *Path);
}

void UCampusGameInstance::CampusShotSeq(const FString& Name, int32 Count, int32 Every)
{
	ShotSeqName = Name;
	ShotSeqRemaining = Count;
	ShotSeqEvery = Every;
	ShotSeqFrame = 0;
	ShotSeqIndex = 0;
}

void UCampusGameInstance::CampusMenu(const FString& Page, int32 Slot)
{
	if (ACampusMenuPlayerController* PC = Cast<ACampusMenuPlayerController>(GetFirstLocalPlayerController()))
	{
		PC->DebugMenu(Page, Slot);
	}
}

void UCampusGameInstance::CampusAim(int32 bOn)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->BotSetAim(bOn != 0);
	}
}

void UCampusGameInstance::CampusFire(int32 bOn)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->BotSetFire(bOn != 0);
	}
}

void UCampusGameInstance::CampusView(float X, float Y, float Z, float Yaw, float Pitch)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC)
	{
		return;
	}
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
	}
	PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
}

void UCampusGameInstance::CampusDebugVM()
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusVM] %s"), *C->Viewmodel->GetDebugString());
	}
}

void UCampusGameInstance::CampusAimTrace()
{
	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector End = Cam + PC->PlayerCameraManager->GetCameraRotation().Vector() * 50000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusAimTrace), true, PC->GetPawn());
	Params.bReturnFaceIndex = true;
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Cam, End, ECC_Visibility, Params))
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusAimTrace] nothing"));
		return;
	}
	UPrimitiveComponent* Prim = Hit.GetComponent();
	const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Prim);
	int32 Section = 0;
	UMaterialInterface* Mat = Prim ? Prim->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) : nullptr;
	if (!Mat && Prim)
	{
		Mat = Prim->GetMaterial(0);
	}
	const UMaterialInstance* MI = Cast<UMaterialInstance>(Mat);
	UE_LOG(LogTemp, Display, TEXT("[CampusAimTrace] %.0f cm at %s: %s / %s (%s) mesh=%s material=%s parent=%s"), Hit.Distance, *Hit.ImpactPoint.ToString(),
		*GetNameSafe(Hit.GetActor()), *GetNameSafe(Prim), Prim ? *Prim->GetClass()->GetName() : TEXT("-"),
		SM && SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : TEXT("-"), *GetNameSafe(Mat), MI ? *GetNameSafe(MI->Parent) : TEXT("-"));
}

void UCampusGameInstance::CampusTreeCheck()
{
	for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
	{
		UInstancedStaticMeshComponent* ISM = *It;
		const UStaticMesh* Mesh = ISM->GetStaticMesh();
		if (ISM->GetWorld() != GetWorld() || !Mesh || !Mesh->GetName().Contains(TEXT("tree")))
		{
			continue;
		}
		const UBodySetup* Body = Mesh->GetBodySetup();
		UE_LOG(LogTemp, Display, TEXT("[CampusTreeCheck] %s: %d instances collision=%d profile=%s capsules=%d boxes=%d bounds=%s"), *Mesh->GetName(), ISM->GetInstanceCount(),
			(int32)ISM->GetCollisionEnabled(), *ISM->GetCollisionProfileName().ToString(), Body ? Body->AggGeom.SphylElems.Num() : -1, Body ? Body->AggGeom.BoxElems.Num() : -1,
			*Mesh->GetBoundingBox().ToString());
		if (Body)
		{
			for (const FKSphylElem& S : Body->AggGeom.SphylElems)
			{
				UE_LOG(LogTemp, Display, TEXT("[CampusTreeCheck]   capsule centre %s radius %.0f length %.0f"), *S.Center.ToString(), S.Radius, S.Length);
			}
		}
		for (int32 i = 0; i < FMath::Min(ISM->GetInstanceCount(), 4); ++i)
		{
			FTransform T;
			ISM->GetInstanceTransform(i, T, true);
			const FVector Trunk = T.GetLocation() + FVector(0.f, 0.f, 100.f);
			const FVector From = Trunk + FVector(200.f, 0.f, 0.f);
			// Parallel bullet lines across the trunk at three heights: the band of Y that stops them is the trunk.
			FString BulletHit;
			for (const float Up : { 0.f, 150.f, 300.f })
			{
				float Lo = TNumericLimits<float>::Max(), Hi = -TNumericLimits<float>::Max();
				for (float Side = -150.f; Side <= 150.f; Side += 5.f)
				{
					FHitResult Hit;
					const FVector Shift(0.f, Side, Up);
					if (GetWorld()->LineTraceSingleByChannel(Hit, From + Shift, Trunk + FVector(-200.f, 0.f, 0.f) + Shift, CAMPUS_TRACE_WEAPON)
						&& Cast<UStaticMeshComponent>(Hit.GetComponent()) && Cast<UStaticMeshComponent>(Hit.GetComponent())->GetStaticMesh() == Mesh)
					{
						Lo = FMath::Min(Lo, Side);
						Hi = FMath::Max(Hi, Side);
					}
				}
				BulletHit += Lo <= Hi ? FString::Printf(TEXT(" +%.0f: y %.0f..%.0f"), Up, Lo, Hi) : FString::Printf(TEXT(" +%.0f: none"), Up);
			}
			FHitResult Sweep;
			const bool bPawn = GetWorld()->SweepSingleByChannel(Sweep, From, Trunk - FVector(200.f, 0.f, 0.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f));
			const FString PawnHit = bPawn ? FString::Printf(TEXT("%s at %.0f cm"), *GetNameSafe(Sweep.GetComponent()), Sweep.Distance) : FString(TEXT("none"));
			UE_LOG(LogTemp, Display, TEXT("[CampusTreeCheck]   #%d at %s scale %.2f: bullet %s, player %s"), i, *T.GetLocation().ToCompactString(), T.GetScale3D().Z, *BulletHit, *PawnHit);
		}
	}
}

void UCampusGameInstance::CampusProbe(float X, float Y, float Z, float Radius)
{
	FVector P(X, Y, Z);
	if (Radius < 0.f)
	{
		// Negative radius: probe |Radius| around the point X cm ahead of the camera.
		if (APlayerController* PC = GetFirstLocalPlayerController(); PC && PC->PlayerCameraManager)
		{
			const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
			P = Cam + PC->PlayerCameraManager->GetCameraRotation().Vector() * X;
			UE_LOG(LogTemp, Display, TEXT("[CampusProbe] camera %s rot %s probe %s"), *Cam.ToString(), *PC->PlayerCameraManager->GetCameraRotation().ToString(), *P.ToString());
		}
		Radius = -Radius;
	}
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Prim = *It;
		if (Prim->IsA<UInstancedStaticMeshComponent>() || Prim->GetWorld() != GetWorld() || !Prim->IsRegistered())
		{
			continue;
		}
		if (FVector::Dist(Prim->Bounds.Origin, P) < Radius)
		{
			const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Prim);
			UE_LOG(LogTemp, Display, TEXT("[CampusProbe] %s / %s (%s) mesh=%s centre=%s visible=%d"), *GetNameSafe(Prim->GetOwner()), *Prim->GetName(), *Prim->GetClass()->GetName(),
				SM && SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : TEXT("-"), *Prim->Bounds.Origin.ToString(), Prim->IsVisible() ? 1 : 0);
		}
	}
	for (TObjectIterator<ULightComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsRegistered() && FVector::Dist(It->GetComponentLocation(), P) < Radius)
		{
			UE_LOG(LogTemp, Display, TEXT("[CampusProbe] light %s at %s intensity=%.0f visible=%d"), *It->GetClass()->GetName(), *It->GetComponentLocation().ToString(), It->Intensity, It->IsVisible() ? 1 : 0);
		}
	}
	for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
	{
		UInstancedStaticMeshComponent* ISM = *It;
		if (!ISM->GetWorld() || ISM->GetWorld() != GetWorld() || !ISM->GetStaticMesh())
		{
			continue;
		}
		const FBox Local = ISM->GetStaticMesh()->GetBoundingBox();
		for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
		{
			FTransform T;
			ISM->GetInstanceTransform(i, T, true);
			const FVector C = T.TransformPosition(Local.GetCenter());
			if (FVector::Dist(C, P) < Radius)
			{
				UE_LOG(LogTemp, Display, TEXT("[CampusProbe] %s / %s mesh=%s inst=%d centre=%s"), *GetNameSafe(ISM->GetOwner()), *ISM->GetName(), *ISM->GetStaticMesh()->GetName(), i, *C.ToString());
			}
		}
	}
}

void UCampusGameInstance::CampusReloadPose(float Progress, int32 bEmpty)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->Viewmodel->SetDebugReload(Progress, bEmpty != 0);
	}
}

void UCampusGameInstance::CampusGod()
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->bInvulnerable = true;
	}
}

void UCampusGameInstance::CampusEquip(const FString& WeaponId, const FString& Attachments)
{
	ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr;
	const FCampusWeaponDef* Def = UCampusArmory::Get().FindWeapon(FName(*WeaponId));
	if (!C || !Def)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusEquip] no pawn or no weapon %s"), *WeaponId);
		return;
	}
	FCampusLoadout L = GetActiveLoadout();
	FCampusWeaponLoadout& Slot = Def->IsSecondary() ? L.Secondary : L.Primary;
	Slot.WeaponId = Def->Id;
	Slot.Attachments.Reset();
	TArray<FString> Ids;
	Attachments.ParseIntoArray(Ids, TEXT(","));
	for (const FString& Id : Ids)
	{
		Slot.Attachments.Add(FName(*Id));
	}
	C->GiveLoadout(L);
}

void UCampusGameInstance::CampusFreeCam(int32 bOn)
{
	ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr;
	if (!C)
	{
		return;
	}
	const bool bFree = bOn != 0;
	C->bInvulnerable = C->bInvulnerable || bFree;
	C->SetActorEnableCollision(!bFree);
	if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(bFree ? MOVE_Flying : MOVE_Walking);
	}
	C->ArmsMesh->SetHiddenInGame(bFree, true);
	if (ACampusWeapon* W = C->GetCurrentWeapon())
	{
		W->SetActorHiddenInGame(bFree);
	}
}

void UCampusGameInstance::CampusAct(const FString& Action)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->DebugAction(Action);
	}
}

void UCampusGameInstance::CampusMove(float X, float Y)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		C->ScriptedMove = FVector2D(X, Y);
	}
}

void UCampusGameInstance::CampusSunScatter(float Intensity)
{
	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SetVolumetricScatteringIntensity(Intensity);
			UE_LOG(LogTemp, Display, TEXT("[CampusSunScatter] %s -> %.2f"), *It->GetOwner()->GetName(), Intensity);
		}
	}
}

void UCampusGameInstance::CampusWhere(const FString& Tag)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusWhere] %s pawn %s vel %s"), *Tag, *C->GetActorLocation().ToString(), *C->GetVelocity().ToString());
	}
}

void UCampusGameInstance::CampusPose(const FString& Pose, float X, float Y, float Z, float Pitch, float Yaw, float Roll, float ViewmodelFOV)
{
	if (ACampusCharacter* C = GetFirstLocalPlayerController() ? GetFirstLocalPlayerController()->GetPawn<ACampusCharacter>() : nullptr)
	{
		if (Pose != TEXT("fov"))
		{
			C->Viewmodel->SetPoseOverride(FName(*Pose), FVector(X, Y, Z), FRotator(Pitch, Yaw, Roll));
		}
		if (ViewmodelFOV > 1.f)
		{
			C->BaseViewmodelFOV = ViewmodelFOV;
		}
	}
}

void UCampusGameInstance::CampusLookAtBot()
{
	APlayerController* PC = GetFirstLocalPlayerController();
	APawn* Self = PC ? PC->GetPawn() : nullptr;
	if (!Self)
	{
		return;
	}
	// Nearest enemy if there is one (hit feedback only shows on enemies), otherwise the nearest bot.
	const ACampusCharacter* SelfChar = Cast<ACampusCharacter>(Self);
	ACampusCharacter* Best = nullptr;
	bool bBestEnemy = false;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Self || !It->IsAlive())
		{
			continue;
		}
		const bool bEnemy = SelfChar && SelfChar->IsEnemyOf(*It);
		const bool bCloser = !Best || FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Self->GetActorLocation());
		if ((bEnemy && !bBestEnemy) || (bEnemy == bBestEnemy && bCloser))
		{
			Best = *It;
			bBestEnemy = bEnemy;
		}
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] no bot found"));
		return;
	}
	const FVector Forward = Best->GetActorForwardVector();
	Self->SetActorLocation(Best->GetActorLocation() + Forward * 380.f + FVector(0.f, 0.f, 10.f), false, nullptr, ETeleportType::TeleportPhysics);
	// Aim from the eyes at the chest, so a burst actually connects.
	const ACharacter* SelfCharacter = Cast<ACharacter>(Self);
	const FVector Eye = Self->GetActorLocation() + FVector(0.f, 0.f, SelfCharacter ? SelfCharacter->BaseEyeHeight : 64.f);
	PC->SetControlRotation((Best->GetActorLocation() + FVector(0.f, 0.f, 30.f) - Eye).Rotation());
	const USkeletalMeshComponent* Body = Best->GetMesh();
	UE_LOG(LogTemp, Display, TEXT("[CampusBot] %s at %s visible=%d hidden=%d ownerNoSee=%d fpType=%d mesh=%s anim=%s owner=%s"),
		*Best->GetName(), *Best->GetActorLocation().ToCompactString(), Body->IsVisible() ? 1 : 0, Body->bHiddenInGame ? 1 : 0, Body->bOwnerNoSee ? 1 : 0,
		(int32)Body->FirstPersonPrimitiveType, *GetNameSafe(Body->GetSkeletalMeshAsset()), *GetNameSafe(Body->GetAnimInstance()), *GetNameSafe(Best->GetOwner()));
}

void UCampusGameInstance::CampusLookAtWindow(float Distance)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusCharacter* Self = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Self)
	{
		return;
	}
	TOptional<FBox> Best;
	for (TActorIterator<ACampusWindowGlass> It(GetWorld()); It; ++It)
	{
		for (const FBox& Pane : It->GetPanes())
		{
			if (!Best || FVector::DistSquared(Pane.GetCenter(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetCenter(), Self->GetActorLocation()))
			{
				Best = Pane;
			}
		}
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusGlass] no window pane"));
		return;
	}
	const FVector Size = Best->GetSize();
	const FVector N = Size.X < Size.Y ? FVector::XAxisVector : FVector::YAxisVector;
	const FVector Center = Best->GetCenter();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusLookAtWindow), false, Self);
	for (const float Side : { 1.f, -1.f })
	{
		const FVector Spot = Center + N * Side * Distance;
		FHitResult Floor;
		if (GetWorld()->LineTraceSingleByChannel(Floor, Spot, Spot - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params) && Center.Z - Floor.ImpactPoint.Z < 220.f)
		{
			Self->SetActorLocation(Floor.ImpactPoint + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Eye = Self->GetActorLocation() + FVector(0.f, 0.f, Self->BaseEyeHeight);
			PC->SetControlRotation((Center - Eye).Rotation());
			UE_LOG(LogTemp, Display, TEXT("[CampusGlass] facing pane at %s from %s"), *Center.ToCompactString(), *Self->GetActorLocation().ToCompactString());
			return;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusGlass] pane at %s has no floor in front"), *Center.ToCompactString());
}

void UCampusGameInstance::CampusLookAtDoor(float Distance)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusCharacter* Self = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Self)
	{
		return;
	}
	ACampusDoor* Best = nullptr;
	for (TActorIterator<ACampusDoor> It(GetWorld()); It; ++It)
	{
		if (!It->IsOpen() && (!Best || FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Self->GetActorLocation())))
		{
			Best = *It;
		}
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusGlass] no closed door"));
		return;
	}
	// The vision panel sits in the middle of the leaf, about 1.5 m up.
	const FVector Panel = Best->GetHandleLocation() + FVector(0.f, 0.f, 50.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusLookAtDoor), false, Self);
	for (const float Side : { 1.f, -1.f })
	{
		const FVector Spot = Panel + Best->GetActorRightVector() * Side * Distance;
		FHitResult Floor;
		if (GetWorld()->LineTraceSingleByChannel(Floor, Spot, Spot - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params) && Panel.Z - Floor.ImpactPoint.Z < 220.f)
		{
			Self->SetActorLocation(Floor.ImpactPoint + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Eye = Self->GetActorLocation() + FVector(0.f, 0.f, Self->BaseEyeHeight);
			PC->SetControlRotation((Panel - Eye).Rotation());
			UE_LOG(LogTemp, Display, TEXT("[CampusGlass] facing door panel at %s from %s"), *Panel.ToCompactString(), *Self->GetActorLocation().ToCompactString());
			return;
		}
	}
}

void UCampusGameInstance::CampusLookAtBotFrom(float Distance)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusCharacter* Self = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Self)
	{
		return;
	}
	ACampusCharacter* Best = nullptr;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self && It->IsAlive() && Self->IsEnemyOf(*It) && (!Best || FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Self->GetActorLocation())))
		{
			Best = *It;
		}
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] no enemy found"));
		return;
	}
	const FVector Target = Best->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusLookAtBot), false, Self);
	Params.AddIgnoredActor(Best);
	for (int32 i = 0; i < 16; ++i)
	{
		const FVector Dir = FRotator(0.f, Best->GetActorRotation().Yaw + i * 22.5f, 0.f).Vector();
		const FVector Spot = Best->GetActorLocation() + Dir * Distance + FVector(0.f, 0.f, 10.f);
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 60.f), Target, ECC_Visibility, Params);
		const bool bFloor = GetWorld()->LineTraceSingleByChannel(Hit, Spot, Spot - FVector(0.f, 0.f, 200.f), ECC_Visibility, Params);
		if (!bBlocked && bFloor)
		{
			Self->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Eye = Self->GetActorLocation() + FVector(0.f, 0.f, Self->BaseEyeHeight);
			PC->SetControlRotation((Target - Eye).Rotation());
			UE_LOG(LogTemp, Display, TEXT("[CampusBot] watching %s from %s"), *Best->GetName(), *Spot.ToCompactString());
			return;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusBot] no clear line to %s"), *Best->GetName());
}

void UCampusGameInstance::CampusBotThrow(float Yaw, int32 bThrow)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusCharacter* Self = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Self)
	{
		return;
	}
	ACampusCharacter* Best = nullptr;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self && It->IsAlive() && Self->IsEnemyOf(*It) && (!Best || FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Self->GetActorLocation())))
		{
			Best = *It;
		}
	}
	AAIController* AI = Best ? Cast<AAIController>(Best->GetController()) : nullptr;
	if (!AI)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] no enemy bot found"));
		return;
	}
	AI->SetActorTickEnabled(false);
	AI->StopMovement();
	const float Facing = (Self->GetActorLocation() - Best->GetActorLocation()).Rotation().Yaw + Yaw;
	AI->SetControlRotation(FRotator(0.f, Facing, 0.f));
	Best->SetActorRotation(FRotator(0.f, Facing, 0.f));
	if (bThrow)
	{
		const bool bOk = Best->BotThrow(ECampusThrowable::Frag, Best->GetActorLocation() + FRotator(0.f, Facing, 0.f).Vector() * 1400.f);
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] %s throw %s"), *Best->GetName(), bOk ? TEXT("started") : TEXT("refused"));
	}
}

void UCampusGameInstance::CampusKillBot(int32 bHead)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusCharacter* Self = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Self || !Self->HasAuthority())
	{
		return;
	}
	ACampusCharacter* Best = nullptr;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self && It->IsAlive() && Self->IsEnemyOf(*It) && (!Best || FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Self->GetActorLocation())))
		{
			Best = *It;
		}
	}
	if (!Best)
	{
		return;
	}
	const FName Bone = bHead ? FName(TEXT("head")) : FName(TEXT("spine_03"));
	const FVector HitAt = Best->GetMesh()->GetBoneIndex(Bone) != INDEX_NONE ? Best->GetMesh()->GetBoneLocation(Bone) : Best->GetActorLocation();
	const FVector Dir = (HitAt - Self->GetPawnViewLocation()).GetSafeNormal();
	CampusFX::SpawnBloodHit(GetWorld(), HitAt, Dir, bHead != 0);
	Best->ApplyCampusDamage(500.f, bHead ? ECampusHitZone::Head : ECampusHitZone::UpperTorso, PC, Self, NAME_None, Dir, Bone);
	UE_LOG(LogTemp, Display, TEXT("[CampusBot] killed %s (head %d)"), *Best->GetName(), bHead);
}

void UCampusGameInstance::CampusEndMatch(int32 Result)
{
	APlayerController* PC = GetFirstLocalPlayerController();
	ACampusGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ACampusGameModeBase>() : nullptr;
	const ACampusPlayerState* PS = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!GM || !PS)
	{
		return;
	}
	const ECampusTeam Mine = PS->Team == ECampusTeam::None ? ECampusTeam::Alpha : PS->Team;
	const ECampusTeam Theirs = Mine == ECampusTeam::Alpha ? ECampusTeam::Bravo : ECampusTeam::Alpha;
	GM->DebugEndMatch(Result == 2 ? ECampusTeam::None : (Result ? Mine : Theirs));
}

void UCampusGameInstance::CampusBots()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] navmesh %s tiles=%d building=%d"), *GetNameSafe(Recast), Recast ? Recast->GetNavMeshTilesCount() : -1, Nav->IsNavigationBuildInProgress() ? 1 : 0);
	}
	for (TActorIterator<ACampusBotController> It(World); It; ++It)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusBot] %s"), *It->GetDebugString());
	}
}

void UCampusGameInstance::CampusFog(float Density, float Falloff, float Start, float MaxOpacity, float Mie, float Aerial)
{
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SetFogDensity(Density);
			It->SetFogHeightFalloff(Falloff);
			It->SetStartDistance(Start);
			It->SetFogMaxOpacity(MaxOpacity);
		}
	}
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SetMieScatteringScale(Mie);
			It->SetAerialPespectiveViewDistanceScale(Aerial);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusFog] density %.4f falloff %.3f start %.0f max %.2f mie %.4f aerial %.2f"), Density, Falloff, Start, MaxOpacity, Mie, Aerial);
}

void UCampusGameInstance::CampusFogSky(float Contribution, float AmbientScale)
{
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SetHeightFogContribution(Contribution);
		}
	}
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SkyAtmosphereAmbientContributionColorScale = FLinearColor(AmbientScale, AmbientScale, AmbientScale);
			It->MarkRenderStateDirty();
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusFogSky] contribution %.2f ambient %.2f"), Contribution, AmbientScale);
}

void UCampusGameInstance::CampusWeather(int32 Weather, int32 Time)
{
	if (ACampusEnvironment* Env = ACampusEnvironment::Find(GetWorld()))
	{
		Env->ForceConditions((ECampusWeather)FMath::Clamp(Weather, 0, (int32)ECampusWeather::Count - 1), (ECampusTimeOfDay)FMath::Clamp(Time, 0, (int32)ECampusTimeOfDay::Count - 1));
	}
}

void UCampusGameInstance::CampusHost(const FString& Mode)
{
	if (Mode == TEXT("Custom"))
	{
		HostMatch(Profile.CustomMode == 1 ? ECampusMatchMode::Gunfight : ECampusMatchMode::TeamDeathmatch, Profile.CustomTeamSize, Profile.bCustomBots, &Profile.CustomRules);
		return;
	}
	HostMatch(Mode == TEXT("Gunfight") ? ECampusMatchMode::Gunfight : ECampusMatchMode::TeamDeathmatch, Mode == TEXT("Gunfight") ? 1 : 5, true);
}

void UCampusGameInstance::EnsureLoadouts()
{
	const UCampusArmory& Armory = UCampusArmory::Get();
	while (Profile.Loadouts.Num() < 3)
	{
		Profile.Loadouts.Add(Armory.MakeDefaultLoadout(Profile.Loadouts.Num()));
	}
	if (Profile.LoadoutRevision < 1)
	{
		// The old "近战突击" preset came with a laser fitted: aim-activated emitters are now only what the player fits.
		for (FCampusLoadout& L : Profile.Loadouts)
		{
			L.Primary.Attachments.Remove(TEXT("laser_tac"));
			L.Secondary.Attachments.Remove(TEXT("laser_tac"));
		}
		Profile.LoadoutRevision = 1;
	}
	for (FCampusLoadout& L : Profile.Loadouts)
	{
		Armory.SanitizeLoadout(L);
	}
	Profile.ActiveLoadout = FMath::Clamp(Profile.ActiveLoadout, 0, Profile.Loadouts.Num() - 1);
}

void UCampusGameInstance::SaveProfile()
{
	EnsureLoadouts();
	UCampusSaveGame* Save = Cast<UCampusSaveGame>(UGameplayStatics::CreateSaveGameObject(UCampusSaveGame::StaticClass()));
	Save->Profile = Profile;
	UGameplayStatics::SaveGameToSlot(Save, CampusSlot, 0);
}

void UCampusGameInstance::ApplyKeyBindings() const
{
	UCampusInputConfig::Get()->ApplyBindings(Profile.KeyBindings);
}

void UCampusGameInstance::SetKeyBinding(FName Id, const FKey& Key)
{
	UCampusInputConfig* Input = UCampusInputConfig::Get();
	if (!UCampusInputConfig::IsBindableKey(Key))
	{
		return;
	}
	const FKey Previous = Input->GetKey(Id);
	auto Store = [this](const FCampusBindable& B, const FKey& K)
	{
		if (K == B.Default)
		{
			Profile.KeyBindings.Remove(B.Id);
		}
		else
		{
			Profile.KeyBindings.Add(B.Id, K);
		}
	};
	for (const FCampusBindable& B : UCampusInputConfig::Bindables())
	{
		if (B.Id == Id)
		{
			Store(B, Key);
		}
		else if (Input->GetKey(B.Id) == Key)
		{
			Store(B, Previous);
		}
	}
	ApplyKeyBindings();
}

void UCampusGameInstance::ResetKeyBindings()
{
	Profile.KeyBindings.Reset();
	ApplyKeyBindings();
}

const FCampusLoadout& UCampusGameInstance::GetActiveLoadout() const
{
	return Profile.Loadouts[FMath::Clamp(Profile.ActiveLoadout, 0, Profile.Loadouts.Num() - 1)];
}

FCampusLoadout& UCampusGameInstance::EditLoadout(int32 Index)
{
	EnsureLoadouts();
	return Profile.Loadouts[FMath::Clamp(Index, 0, Profile.Loadouts.Num() - 1)];
}

void UCampusGameInstance::ApplyGraphicsQuality() const
{
	if (GEngine && GEngine->GetGameUserSettings() && !IsRunningCommandlet() && !IsRunningDedicatedServer())
	{
		CampusGraphics::Apply(Profile.Graphics);
	}
}

void UCampusGameInstance::CampusPreset(int32 Preset)
{
	CampusGraphics::ApplyPreset(Profile.Graphics, Preset);
	ApplyGraphicsQuality();
}

void UCampusGameInstance::CampusUpscale(int32 AntiAliasing, int32 DLSSMode, int32 FrameGeneration)
{
	Profile.Graphics.AntiAliasing = FMath::Clamp(AntiAliasing, 0, 4);
	Profile.Graphics.DLSSMode = FMath::Clamp(DLSSMode, 0, 4);
	Profile.Graphics.FrameGeneration = FMath::Clamp(FrameGeneration, 0, 4);
	Profile.Graphics.Preset = 4;
	ApplyGraphicsQuality();
}

void UCampusGameInstance::CampusGfx(const FString& Setting, float Value)
{
	FCampusGraphicsSettings& G = Profile.Graphics;
	const int32 Level = FMath::RoundToInt(Value);
	const TPair<const TCHAR*, int32*> Levels[] = {
		{ TEXT("AA"), &G.AntiAliasing }, { TEXT("AAQ"), &G.AntiAliasingQuality }, { TEXT("GI"), &G.GlobalIllumination },
		{ TEXT("Shadows"), &G.Shadows }, { TEXT("Refl"), &G.Reflections }, { TEXT("Tex"), &G.Textures }, { TEXT("FX"), &G.Effects },
		{ TEXT("View"), &G.ViewDistance }, { TEXT("Post"), &G.PostProcess }, { TEXT("DLSS"), &G.DLSSMode } };
	bool bFound = false;
	for (const TPair<const TCHAR*, int32*>& L : Levels)
	{
		if (Setting.Equals(L.Key, ESearchCase::IgnoreCase))
		{
			*L.Value = Level;
			bFound = true;
		}
	}
	if (Setting.Equals(TEXT("Scale"), ESearchCase::IgnoreCase))
	{
		G.RenderScale = Value;
		bFound = true;
	}
	else if (Setting.Equals(TEXT("Sharp"), ESearchCase::IgnoreCase))
	{
		G.Sharpness = Value;
		bFound = true;
	}
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CampusScript] CampusGfx: unknown setting %s"), *Setting);
		return;
	}
	G.Preset = 4;
	ApplyGraphicsQuality();
}

void UCampusGameInstance::CampusDumpScreens()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACampusHQScreens> It(World); It; ++It)
		{
			It->DumpToDisk();
		}
	}
}

void UCampusGameInstance::ApplyAudioMix() const
{
	CampusAudio::SetCategoryVolume(CampusAudio::ECategory::Effects, Profile.EffectsVolume);
	CampusAudio::SetCategoryVolume(CampusAudio::ECategory::Ambience, Profile.AmbienceVolume);
	CampusAudio::SetCategoryVolume(CampusAudio::ECategory::Interface, Profile.InterfaceVolume);
	FApp::SetUnfocusedVolumeMultiplier(Profile.bMuteUnfocused ? 0.f : 1.f);
}

void UCampusGameInstance::ApplyAudioVolume(const UObject* WorldContext, float Volume)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (FAudioDeviceHandle Device = World->GetAudioDevice())
	{
		Device->SetTransientPrimaryVolume(FMath::Clamp(Volume, 0.f, 1.f));
	}
}

void UCampusGameInstance::HostMatch(ECampusMatchMode Mode, int32 TeamSize, bool bBots, const FCampusMatchRules* Rules)
{
	SaveProfile();
	const ECampusMap Map = (ECampusMap)FMath::Clamp(Profile.SelectedMap, 0, (int32)ECampusMap::Count - 1);
	const CampusMaps::FMapInfo& Info = CampusMaps::Get(CampusMaps::Get(Map).bPlayable ? Map : ECampusMap::Campus);
	FString Options = FString::Printf(TEXT("listen?game=%s?TeamSize=%d?Bots=%d?Skill=%d?Weather=%d?Time=%d"),
		Mode == ECampusMatchMode::Gunfight ? TEXT("Gunfight") : TEXT("TDM"), TeamSize, bBots ? 1 : 0, Profile.BotSkill, Profile.SelectedWeather, Profile.SelectedTime);
	if (Rules)
	{
		Options += FString::Printf(TEXT("?Custom=1?God=%d?HP=%d?BotHP=%d?NoReload=%d?InfAmmo=%d?InfGear=%d?Regen=%d"),
			Rules->bGodMode ? 1 : 0, Rules->PlayerHealth, Rules->BotHealth, Rules->bNoReload ? 1 : 0, Rules->bInfiniteAmmo ? 1 : 0,
			Rules->bInfiniteEquipment ? 1 : 0, Rules->bHealthRegen ? 1 : 0);
	}
	UGameplayStatics::OpenLevel(GetWorld(), FName(Info.Package), true, Options);
}

void UCampusGameInstance::JoinMatch(const FString& Address)
{
	Profile.LastJoinAddress = Address;
	SaveProfile();
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(Address, TRAVEL_Absolute);
	}
}
