#include "Menu/CampusMenuPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
	TArray<CampusMenuPresentation::FAttachmentLabel> AttachmentLabelFixture(const FVector2D& Center, bool bCrowded)
	{
		const FVector2D Spread[] = {
			FVector2D(-280.f, -12.f), FVector2D(-160.f, -15.f), FVector2D(-110.f, -40.f), FVector2D(10.f, -60.f),
			FVector2D(260.f, 0.f), FVector2D(-80.f, 50.f), FVector2D(40.f, 65.f), FVector2D(135.f, 50.f)
		};
		const FVector2D Crowded[] = {
			FVector2D(-36.f, -4.f), FVector2D(-20.f, -7.f), FVector2D(-12.f, -14.f), FVector2D(0.f, -18.f),
			FVector2D(36.f, 0.f), FVector2D(-12.f, 12.f), FVector2D(4.f, 18.f), FVector2D(18.f, 15.f)
		};
		TArray<CampusMenuPresentation::FAttachmentLabel> Labels;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Spread); ++Index)
		{
			CampusMenuPresentation::FAttachmentLabel Label;
			Label.Slot = static_cast<ECampusAttachmentSlot>(Index);
			Label.Anchor = Center + (bCrowded ? Crowded[Index] : Spread[Index]);
			Labels.Add(Label);
		}
		return Labels;
	}

	void CheckAttachmentLabelBounds(FAutomationTestBase& Test, const FString& Context, const FBox2D& SafeArea,
		const TArray<CampusMenuPresentation::FAttachmentLabel>& Labels)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s keeps all eight attachment choices"), *Context), Labels.Num(), 8);
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			const FBox2D& Bounds = Labels[Index].Bounds;
			const FString Prefix = FString::Printf(TEXT("%s slot %d"), *Context, Index);
			const bool bFinite = FMath::IsFinite(Bounds.Min.X) && FMath::IsFinite(Bounds.Min.Y)
				&& FMath::IsFinite(Bounds.Max.X) && FMath::IsFinite(Bounds.Max.Y);
			if (!Test.TestTrue(*(Prefix + TEXT(" has a real readable hit rectangle")), Bounds.bIsValid && bFinite && Bounds.GetSize().X >= 120.f && Bounds.GetSize().Y >= 32.f))
			{
				continue;
			}
			Test.TestTrue(*(Prefix + TEXT(" clears the menu, navigation and viewport edges")),
				SafeArea.IsInsideOrOn(Bounds.Min) && SafeArea.IsInsideOrOn(Bounds.Max));
			for (int32 Other = Index + 1; Other < Labels.Num(); ++Other)
			{
				const FBox2D& OtherBounds = Labels[Other].Bounds;
				const FVector2D Overlap = Bounds.Max.ComponentMin(OtherBounds.Max) - Bounds.Min.ComponentMax(OtherBounds.Min);
				Test.TestFalse(*FString::Printf(TEXT("%s cannot cover attachment choice %d"), *Prefix, Other), Overlap.X > 0.01f && Overlap.Y > 0.01f);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusAttachmentLabelsFollowPartsTest, "CampusOps.Experience.UIPresentation.AttachmentLabelsFollowParts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusAttachmentLabelsFollowPartsTest::RunTest(const FString&)
{
	for (const FVector2D ViewSize : { FVector2D(1920.f, 1080.f), FVector2D(2560.f, 1080.f) })
	{
		for (const bool bAttachmentPanel : { false, true })
		{
			const FBox2D SafeArea(FVector2D(bAttachmentPanel ? 746.f : 432.f, 150.f), FVector2D(ViewSize.X - 20.f, ViewSize.Y - 120.f));
			const FVector2D Center = SafeArea.GetCenter();
			const FString Context = FString::Printf(TEXT("%.0fx%.0f %s"), ViewSize.X, ViewSize.Y, bAttachmentPanel ? TEXT("attachment page") : TEXT("whole weapon"));
			TArray<CampusMenuPresentation::FAttachmentLabel> Before = AttachmentLabelFixture(Center, false);
			CampusMenuPresentation::PlaceAttachmentLabels(ViewSize, bAttachmentPanel, Center, Before);
			CheckAttachmentLabelBounds(*this, Context, SafeArea, Before);

			const FVector2D Shift(72.f, -40.f);
			TArray<CampusMenuPresentation::FAttachmentLabel> After = AttachmentLabelFixture(Center + Shift, false);
			CampusMenuPresentation::PlaceAttachmentLabels(ViewSize, bAttachmentPanel, Center + Shift, After);
			CheckAttachmentLabelBounds(*this, Context + TEXT(" after the weapon moves"), SafeArea, After);
			for (int32 Index = 0; Index < Before.Num() && Index < After.Num(); ++Index)
			{
				const auto& Initial = Before[Index];
				const auto& Moved = After[Index];
				const FString Prefix = FString::Printf(TEXT("%s slot %d"), *Context, Index);
				TestTrue(*(Prefix + TEXT(" follows its moving part instead of staying in a fixed column")),
					(Moved.Bounds.GetCenter() - Initial.Bounds.GetCenter() - Shift).Size() <= 32.f);
				for (const auto* Label : { &Initial, &Moved })
				{
					TestTrue(*(Prefix + TEXT(" remains beside its part with a short leader")),
						(Label->Bounds.GetClosestPointTo(Label->Anchor) - Label->Anchor).Size() <= 160.f);
					const FVector2D Icon = Label->Bounds.Min + FVector2D(13.f, 20.f);
					TestTrue(*(Prefix + TEXT(" does not stretch its icon line across the weapon display")), (Icon - Label->Anchor).Size() <= 300.f);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusAttachmentLabelsCrowdedViewportTest, "CampusOps.Experience.UIPresentation.AttachmentLabelsCrowdedViewport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusAttachmentLabelsCrowdedViewportTest::RunTest(const FString&)
{
	for (const FVector2D ViewSize : { FVector2D(1024.f, 768.f), FVector2D(1280.f, 720.f) })
	{
		for (const bool bAttachmentPanel : { false, true })
		{
			const FBox2D SafeArea(FVector2D(bAttachmentPanel ? 746.f : 432.f, 150.f), FVector2D(ViewSize.X - 20.f, ViewSize.Y - 120.f));
			const FVector2D Center = SafeArea.GetCenter();
			TArray<CampusMenuPresentation::FAttachmentLabel> Labels = AttachmentLabelFixture(Center, true);
			CampusMenuPresentation::PlaceAttachmentLabels(ViewSize, bAttachmentPanel, Center, Labels);
			CheckAttachmentLabelBounds(*this,
				FString::Printf(TEXT("crowded %.0fx%.0f %s"), ViewSize.X, ViewSize.Y, bAttachmentPanel ? TEXT("attachment page") : TEXT("whole weapon")), SafeArea, Labels);
		}
	}
	return true;
}
#endif
