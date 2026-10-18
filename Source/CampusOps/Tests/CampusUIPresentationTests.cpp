#include "Player/CampusCombatPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Menu/CampusMenuCameraDirector.h"
#include "UI/SCampusButton.h"
#include "UI/CampusUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/World.h"
#include "Menu/CampusMenuStage.h"
#include "Menu/CampusMenuPresentation.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "SceneView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUIIconAlignmentTest, "CampusOps.Experience.UIPresentation.IconAlignment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUIIconAlignmentTest::RunTest(const FString&)
{
	using CampusCombatPresentation::CenteredIconTop;
	for (const float Scale : { 0.666667f, 1.f, 1.333333f })
	{
		const float TextTop = 180.f * Scale;
		const float InkTop = 8.f * Scale;
		const float InkBottom = 29.f * Scale;
		const float IconSize = 18.f * Scale;
		const float Top = CenteredIconTop(TextTop, InkTop, InkBottom, IconSize, 0.11f, 0.8f);
		const float IconCenter = Top + IconSize * (0.11f + 0.8f) * 0.5f;
		TestTrue(TEXT("the visible skull is centred on the glyph ink, including asymmetric texture padding"), FMath::IsNearlyEqual(IconCenter, TextTop + (InkTop + InkBottom) * 0.5f, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUIMedalMotionTest, "CampusOps.Experience.UIPresentation.MedalMotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUIMedalMotionTest::RunTest(const FString&)
{
	using CampusCombatPresentation::MedalMotion;
	const auto Start = MedalMotion(0.f);
	TestEqual(TEXT("the badge starts transparent"), Start.Alpha, 0.f);
	TestEqual(TEXT("the title waits for the badge entry"), MedalMotion(0.04f).TitleAlpha, 0.f);
	TestTrue(TEXT("the badge briefly settles above its resting size"), MedalMotion(0.12f).BadgeScale > 1.f);
	const auto Hold = MedalMotion(0.3f);
	TestEqual(TEXT("the stable hold is fully opaque"), Hold.Alpha, 1.f);
	TestEqual(TEXT("the title is fully readable during hold"), Hold.TitleAlpha, 1.f);
	TestEqual(TEXT("badge scale settles exactly before the hold"), Hold.BadgeScale, 1.f);
	TestEqual(TEXT("the panel settles exactly before the hold"), Hold.Reveal, 1.f);
	TestEqual(TEXT("the hold does not keep moving"), Hold.OffsetY, 0.f);
	TestEqual(TEXT("the later hold retains the same offset"), MedalMotion(1.2f).OffsetY, Hold.OffsetY);
	TestTrue(TEXT("the exit fades before clearing the queue"), MedalMotion(1.45f).Alpha > 0.f && MedalMotion(1.45f).Alpha < 1.f);
	TestEqual(TEXT("the medal is transparent at the queue deadline"), MedalMotion(CampusCombatPresentation::FMedalQueue::DisplayTime).Alpha, 0.f);
	for (int32 Frame = 0; Frame <= 120; ++Frame)
	{
		const auto Motion = MedalMotion(Frame / 60.f);
		TestTrue(TEXT("opacity remains bounded at every sampled frame"), Motion.Alpha >= 0.f && Motion.Alpha <= 1.f && Motion.TitleAlpha >= 0.f && Motion.TitleAlpha <= 1.f);
		TestTrue(TEXT("badge scale remains bounded without a spring tail"), Motion.BadgeScale >= 0.86f && Motion.BadgeScale <= 1.04f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUIHoverContrastTest, "CampusOps.Experience.UIPresentation.HoverContrast", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUIHoverContrastTest::RunTest(const FString&)
{
	const TSharedRef<SCampusButton> Row = SNew(SCampusButton).Text(FText::FromString(TEXT("Optic candidate")));
	const TSharedRef<SBorder> Fill = StaticCastSharedRef<SBorder>(Row->HoverFill());
	TSharedPtr<STextBlock> Label;
	TArray<TSharedRef<SWidget>> Pending;
	Pending.Add(Row);
	for (int32 Index = 0; Index < Pending.Num() && !Label; ++Index)
	{
		const TSharedRef<SWidget> Widget = Pending[Index];
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			const TSharedRef<STextBlock> Candidate = StaticCastSharedRef<STextBlock>(Widget);
			if (Candidate->GetText().ToString() == TEXT("Optic candidate")) Label = Candidate;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Child = 0; Child < Children->Num(); ++Child) Pending.Add(Children->GetChildAt(Child));
	}
	if (!TestTrue(TEXT("the real attachment row exposes its label"), Label.IsValid())) return false;
	auto Luminance = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	for (const float Progress : { 0.05f, 0.2f, 0.35f, 0.5f, 0.8f, 1.f })
	{
		Row->Highlight = Progress;
		const FLinearColor Ink = Label->GetColorAndOpacity().GetSpecifiedColor();
		const FLinearColor Overlay = Fill->GetBorderBackgroundColor().GetSpecifiedColor();
		const FLinearColor Background = FMath::Lerp(FLinearColor(0.02f, 0.02f, 0.02f), Overlay, Overlay.A);
		const float A = Luminance(Ink);
		const float B = Luminance(Background);
		const float Contrast = (FMath::Max(A, B) + 0.05f) / (FMath::Min(A, B) + 0.05f);
		TestTrue(*FString::Printf(TEXT("attachment text stays readable over its hover fill at progress %.2f"), Progress), Contrast >= 4.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUICameraRetargetTest, "CampusOps.Experience.UIPresentation.CameraRetarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUICameraRetargetTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	ACampusMenuCameraDirector* Director = World->SpawnActor<ACampusMenuCameraDirector>();
	Director->SnapTo(FCampusCameraShot::LookAt(FVector(0.f, -300.f, 100.f), FVector(0.f, 0.f, 100.f), 45.f, 4.f, 0.f));
	Director->MoveTo(FCampusCameraShot::LookAt(FVector(900.f, 300.f, 200.f), FVector(900.f, 0.f, 100.f), 45.f, 4.f, 0.f), 1.55f);
	Director->Tick(0.1f);
	Director->Tick(0.1f);
	const FCampusCameraShot Latest = FCampusCameraShot::LookAt(FVector(-200.f, -350.f, 120.f), FVector(-200.f, 0.f, 100.f), 40.f, 4.f, 0.f);
	Director->MoveTo(Latest, 0.25f);
	for (int32 Frame = 0; Frame < 4; ++Frame) Director->Tick(0.1f);
	TestFalse(TEXT("a low frame rate does not extend the latest page transition beyond its deadline"), Director->IsMoving());
	TestTrue(TEXT("an interrupted transition reaches only the latest page location"), Director->GetCurrentLocation().Equals(Latest.Location, 0.01f));
	TestTrue(TEXT("the rendered camera reaches the latest page without a trailing follow spring"), Director->GetActorLocation().Equals(Latest.Location, 0.01f));
	Director->CutTo(FCampusCameraShot::LookAt(FVector(1500.f, 200.f, 180.f), FVector(1500.f, 0.f, 100.f), 45.f, 4.f, 0.f));
	Director->Tick(0.04f);
	const FCampusCameraShot Final = FCampusCameraShot::LookAt(FVector(-600.f, 350.f, 175.f), FVector(-1350.f, -300.f, 120.f), 50.f, 4.f, 0.f);
	Director->CutTo(Final);
	Director->Tick(0.07f);
	TestTrue(TEXT("rapid cross-area navigation cuts directly to the most recent page under black"), Director->GetActorLocation().Equals(Final.Location, 0.01f) && Director->GetCutOpacity() >= 0.99f);
	Director->Tick(0.2f);
	TestFalse(TEXT("a safe cut finishes its reveal even at a low frame rate"), Director->IsMoving());
	TestTrue(TEXT("an older pending page cannot replay after the latest cut"), Director->GetActorLocation().Equals(Final.Location, 0.01f));
	World->DestroyWorld(false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUIWeaponProjectionTest, "CampusOps.Experience.UIPresentation.WeaponProjection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUIWeaponProjectionTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	ACampusMenuStage* Stage = World->SpawnActor<ACampusMenuStage>();
	ACampusMenuCameraDirector* Director = World->SpawnActor<ACampusMenuCameraDirector>();
	UStaticMeshComponent* Weapon = NewObject<UStaticMeshComponent>(Stage);
	Weapon->SetupAttachment(Stage->WeaponPivot);
	Weapon->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Weapon->SetRelativeScale3D(FVector(1.6f, 0.08f, 0.18f));
	Weapon->RegisterComponent();
	Stage->WeaponComponents.Add(Weapon);
	for (const FIntPoint ViewSize : { FIntPoint(1024, 768), FIntPoint(1280, 720), FIntPoint(2560, 1080) })
	{
		const FIntRect ViewRect(0, 0, ViewSize.X, ViewSize.Y);
		const CampusMenuPresentation::FWeaponLayout Layout = CampusMenuPresentation::WeaponLayout(FVector2D(ViewSize), true);
		for (const FRotator Pose : { FRotator::ZeroRotator, FRotator(0.f, 75.f, 80.f), FRotator(0.f, -90.f, -80.f) })
		{
			Stage->WeaponPivot->SetRelativeRotation(Pose);
			Director->SnapTo(Stage->GetShot(ECampusMenuShot::GunsmithSlot, ECampusAttachmentSlot::Optic, FVector2D(ViewSize)));
			Director->Tick(0.f);
			FMinimalViewInfo View;
			Director->Camera->GetCameraView(0.f, View);
			FSceneViewProjectionData Projection;
			Projection.SetViewRectangle(ViewRect);
			Projection.ViewOrigin = View.Location;
			Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, View.AspectRatioAxisConstraint.Get(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV), ViewRect, Projection);
			const FBox Bounds = Weapon->CalcBounds(Weapon->GetComponentTransform()).GetBox();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
				FVector2D Pixel;
				const bool bProjected = FSceneView::ProjectWorldToScreen(Point, ViewRect, Projection.ComputeViewProjectionMatrix(), Pixel);
				TestTrue(*FString::Printf(TEXT("rotated weapon stays clear of the actual attachment panel and viewport at %dx%d"), ViewSize.X, ViewSize.Y),
					bProjected && Pixel.X >= 746.f && Pixel.X <= ViewSize.X - 20.f && Pixel.Y >= 150.f && Pixel.Y <= ViewSize.Y - 120.f);
				TestTrue(TEXT("the actual camera projects the weapon inside its safe viewport"), bProjected && Layout.Weapon.IsInsideOrOn(Pixel));
			}
		}
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusUIClearWeaponLayoutTest, "CampusOps.Experience.UIPresentation.ClearWeaponLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusUIClearWeaponLayoutTest::RunTest(const FString&)
{
	for (const FVector2D Size : { FVector2D(1024.f, 768.f), FVector2D(1280.f, 720.f), FVector2D(1920.f, 1080.f), FVector2D(2560.f, 1080.f) })
	{
		for (const bool bAttachment : { false, true })
		{
			const CampusMenuPresentation::FWeaponLayout Layout = CampusMenuPresentation::WeaponLayout(Size, bAttachment);
			const float PanelRight = bAttachment ? 722.f : 408.f;
			TestTrue(TEXT("the weapon region has useful area and clears the actual menu panels"), Layout.Weapon.GetSize().X >= 180.f && Layout.Weapon.GetSize().Y >= 180.f && Layout.Weapon.Min.X > PanelRight);
			TArray<CampusMenuPresentation::FAttachmentLabel> Labels;
			for (int32 Index = 0; Index < CampusMenuPresentation::SlotCount; ++Index)
			{
				auto& Label = Labels.AddDefaulted_GetRef();
				Label.Slot = static_cast<ECampusAttachmentSlot>(Index);
				Label.Anchor = Layout.Weapon.GetCenter();
			}
			CampusMenuPresentation::PlaceAttachmentLabels(Size, bAttachment, Layout.Weapon.GetCenter(), Labels);
			for (int32 Index = 0; Index < Labels.Num(); ++Index)
			{
				const FBox2D& Label = Labels[Index].Bounds;
				TestTrue(TEXT("every label clears the panel, navigation and viewport edges"), Label.Min.X > PanelRight && Label.Max.X < Size.X && Label.Min.Y >= 150.f && Label.Max.Y <= Size.Y - 120.f);
				for (int32 Other = Index + 1; Other < Labels.Num(); ++Other)
				{
					TestFalse(TEXT("labels remain disjoint when the attachment panel leaves little width"), Label.Intersect(Labels[Other].Bounds));
				}
			}
		}
	}
	return true;
}
#endif
