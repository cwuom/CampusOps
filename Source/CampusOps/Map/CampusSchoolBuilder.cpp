#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusSchoolLayout.h"
#include "Game/CampusEnvironment.h"
#include "CampusAssets.h"
#include "CampusOps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const TCHAR* ShapeName(int32 Shape)
	{
		switch (Shape)
		{
		case 1: return TEXT("Cylinder");
		case 2: return TEXT("Sphere");
		case 3: return TEXT("Cone");
		default: return TEXT("Cube");
		}
	}

	UInstancedStaticMeshComponent* MakeBatch(AActor* Owner, USceneComponent* Root, const FName& Name)
	{
		UInstancedStaticMeshComponent* ISM = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		ISM->SetupAttachment(Root);
		ISM->SetMobility(EComponentMobility::Static);
		ISM->NumCustomDataFloats = ACampusSchoolBuilder::NumCustomFloats;
		ISM->bReceivesDecals = true;
		return ISM;
	}
}

ACampusSchoolBuilder::ACampusSchoolBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	for (int32 Shape = 0; Shape < NumShapes; ++Shape)
	{
		for (int32 Collide = 0; Collide < 2; ++Collide)
		{
			for (int32 Mat = 0; Mat < NumMats; ++Mat)
			{
				const FCampusMatInfo& Info = CampusSurfaces::Info((ECampusMat)Mat);
				const FName Name(*FString::Printf(TEXT("ISM_%s_%s_%s"), ShapeName(Shape), Collide ? TEXT("Solid") : TEXT("Deco"), Info.Name));
				UInstancedStaticMeshComponent* ISM = MakeBatch(this, SceneRoot, Name);
				ISM->ComponentTags.Add(SurfaceTag(Info.Impact));
				const ECampusMat Family = (ECampusMat)Mat;
				if (Family == ECampusMat::Facade || Family == ECampusMat::FacadeFar || Family == ECampusMat::Grille || Family == ECampusMat::Tower || Family == ECampusMat::TowerFar)
				{
					// Hundreds of metres out (or sub-pixel bars): ray tracing only serves the classroom lights' shadows
					// and the ultra GI's short traces, so these would just bloat the scene gathered every frame.
					ISM->bVisibleInRayTracing = false;
				}
				if (Family == ECampusMat::FacadeFar || Family == ECampusMat::TowerFar)
				{
					// A kilometre and more away: their shadows would only cost virtual shadow pages.
					ISM->SetCastShadow(false);
					ISM->bAffectDistanceFieldLighting = false;
					ISM->ComponentTags.Add(TEXT("FarCity"));
				}
				if (Collide)
				{
					ISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				}
				else
				{
					ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					ISM->SetCanEverAffectNavigation(false);
				}
				Batches.Add(ISM);
			}
		}
	}

	Blockers = MakeBatch(this, SceneRoot, TEXT("ISM_Blockers"));
	Blockers->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Blockers->SetCollisionObjectType(ECC_WorldStatic);
	Blockers->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blockers->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blockers->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Blockers->SetHiddenInGame(true);
	Blockers->SetCastShadow(false);

	Ramps = MakeBatch(this, SceneRoot, TEXT("ISM_Ramps"));
	Ramps->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ramps->SetCollisionObjectType(ECC_WorldStatic);
	Ramps->SetCollisionResponseToAllChannels(ECR_Ignore);
	Ramps->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Ramps->SetHiddenInGame(true);
	Ramps->SetCastShadow(false);

	GunfightBlockers = MakeBatch(this, SceneRoot, TEXT("ISM_GunfightBlockers"));
	GunfightBlockers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GunfightBlockers->SetCollisionObjectType(ECC_WorldStatic);
	GunfightBlockers->SetCollisionResponseToAllChannels(ECR_Ignore);
	GunfightBlockers->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GunfightBlockers->SetHiddenInGame(true);
	GunfightBlockers->SetCastShadow(false);
	GunfightBlockers->SetCanEverAffectNavigation(false);

	GunfightVisuals = MakeBatch(this, SceneRoot, TEXT("ISM_GunfightVisuals"));
	GunfightVisuals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GunfightVisuals->SetHiddenInGame(true);
	GunfightVisuals->SetCanEverAffectNavigation(false);
}

ACampusSchoolBuilder* ACampusSchoolBuilder::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACampusSchoolBuilder> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ACampusSchoolBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

FName ACampusSchoolBuilder::SurfaceTag(uint8 Impact)
{
	static const FName Tags[] = { TEXT("Surface_Concrete"), TEXT("Surface_Metal"), TEXT("Surface_Wood"), TEXT("Surface_Dirt") };
	return Tags[FMath::Min<int32>(Impact, UE_ARRAY_COUNT(Tags) - 1)];
}

void ACampusSchoolBuilder::BeginPlay()
{
	Super::BeginPlay();
	// Props are transient, and rebuilding keeps saved maps in sync with the layout code.
	Rebuild();
	// Every machine dresses its own sky: time of day, weather, rain and the ambient beds.
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer && !ACampusEnvironment::Find(World))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ACampusEnvironment>(ACampusEnvironment::StaticClass(), FTransform::Identity, Params);
	}
}

void ACampusSchoolBuilder::SetGunfightMode(bool bEnabled)
{
	GunfightBlockers->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	GunfightVisuals->SetHiddenInGame(!bEnabled);
}

void ACampusSchoolBuilder::Rebuild()
{
	FCampusSchoolBuildContext Ctx(this);
	Ctx.Build();
}
