#include "Player/CampusCombatPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

using CampusCombatPresentation::CleanPlayerName;
using CampusCombatPresentation::FMedalQueue;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalFilteringTest, "CampusOps.Experience.CombatPresentation.MedalFiltering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalFilteringTest::RunTest(const FString&)
{
	FMedalQueue Queue;
	for (const ECampusScoreEvent Event : { ECampusScoreEvent::Kill, ECampusScoreEvent::Assist, ECampusScoreEvent::RoundWin, ECampusScoreEvent::RoundLoss })
	{
		Queue.Push(Event, 0.f);
	}
	TestEqual(TEXT("ordinary score and round results do not queue medals"), Queue.NumPending(), 0);
	TestFalse(TEXT("ordinary score and round results do not start a medal"), Queue.Tick(0.2f));
	TestNull(TEXT("ordinary score leaves the medal display empty"), Queue.GetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalPriorityTest, "CampusOps.Experience.CombatPresentation.MedalPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalPriorityTest::RunTest(const FString&)
{
	const ECampusScoreEvent PriorityOrder[] = {
		ECampusScoreEvent::Headshot, ECampusScoreEvent::Melee, ECampusScoreEvent::Longshot, ECampusScoreEvent::Revenge,
		ECampusScoreEvent::DoubleKill, ECampusScoreEvent::FirstBlood, ECampusScoreEvent::TripleKill, ECampusScoreEvent::Streak5
	};
	for (int32 Index = 1; Index < static_cast<int32>(UE_ARRAY_COUNT(PriorityOrder)); ++Index)
	{
		FMedalQueue Queue;
		Queue.Push(PriorityOrder[Index - 1], 0.f);
		Queue.Push(PriorityOrder[Index], 0.f);
		TestFalse(TEXT("same-kill events collect for the first 100ms"), Queue.Tick(0.099f));
		TestTrue(TEXT("a medal starts at the end of the collection window"), Queue.Tick(0.1f));
		TestTrue(TEXT("the higher priority same-kill medal leads"), Queue.GetActive() && Queue.GetActive()->Event == PriorityOrder[Index]);
		TestEqual(TEXT("the other same-kill medal remains queued"), Queue.NumPending(), 1);
		TestTrue(TEXT("the lower priority medal follows after the leading display"), Queue.Tick(1.76f));
		TestTrue(TEXT("the following medal retains its event"), Queue.GetActive() && Queue.GetActive()->Event == PriorityOrder[Index - 1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalCapacityTest, "CampusOps.Experience.CombatPresentation.MedalCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalCapacityTest::RunTest(const FString&)
{
	FMedalQueue Queue;
	Queue.Push(ECampusScoreEvent::Melee, 0.f);
	Queue.Tick(0.2f);
	const ECampusScoreEvent Events[] = {
		ECampusScoreEvent::Streak5, ECampusScoreEvent::TripleKill, ECampusScoreEvent::FirstBlood,
		ECampusScoreEvent::DoubleKill, ECampusScoreEvent::Revenge, ECampusScoreEvent::Longshot, ECampusScoreEvent::Headshot
	};
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Events)); ++Index)
	{
		Queue.Push(Events[Index], 0.3f + Index * 0.2f);
		TestTrue(TEXT("continuous events keep at most four pending medals"), Queue.NumPending() <= 4);
	}
	TestEqual(TEXT("the pending capacity excludes the current medal"), Queue.NumPending(), 4);
	TestTrue(TEXT("new events do not interrupt the current medal"), Queue.GetActive() && Queue.GetActive()->Event == ECampusScoreEvent::Melee);
	TestTrue(TEXT("a pending medal starts when the current display ends"), Queue.Tick(1.9f));
	TestTrue(TEXT("overflow preserves the highest priority pending medal"), Queue.GetActive() && Queue.GetActive()->Event == ECampusScoreEvent::Streak5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalCountTest, "CampusOps.Experience.CombatPresentation.MedalCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalCountTest::RunTest(const FString&)
{
	FMedalQueue Queue;
	Queue.Push(ECampusScoreEvent::Headshot, 0.f);
	Queue.Push(ECampusScoreEvent::Headshot, 0.5f);
	TestEqual(TEXT("matching pending medals share one queue entry"), Queue.NumPending(), 1);
	Queue.Tick(0.7f);
	TestTrue(TEXT("matching pending medals retain their earned count"), Queue.GetActive() && Queue.GetActive()->Count == 2);
	Queue.Push(ECampusScoreEvent::Headshot, 0.8f);
	TestEqual(TEXT("a later same-kind event waits behind the active medal"), Queue.NumPending(), 1);
	TestTrue(TEXT("a later event does not mutate the active medal count"), Queue.GetActive() && Queue.GetActive()->Count == 2);
	Queue.Reset();
	for (int32 Index = 0; Index < 110; ++Index)
	{
		Queue.Push(ECampusScoreEvent::Headshot, 1.f);
	}
	Queue.Tick(1.2f);
	TestTrue(TEXT("merged count is capped at two display digits"), Queue.GetActive() && Queue.GetActive()->Count == 99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalExpiryTest, "CampusOps.Experience.CombatPresentation.MedalExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalExpiryTest::RunTest(const FString&)
{
	FMedalQueue Queue;
	Queue.Push(ECampusScoreEvent::Headshot, 0.f);
	TestFalse(TEXT("pending medals older than six seconds do not display"), Queue.Tick(6.01f));
	TestEqual(TEXT("expired medals are removed from the queue"), Queue.NumPending(), 0);
	Queue.Push(ECampusScoreEvent::Headshot, 10.f);
	TestTrue(TEXT("a pending medal remains eligible at six seconds"), Queue.Tick(16.f));
	Queue.Reset();
	Queue.Push(ECampusScoreEvent::Headshot, 0.f);
	Queue.Push(ECampusScoreEvent::Headshot, 5.f);
	TestTrue(TEXT("a repeated pending medal refreshes its expiry"), Queue.Tick(6.01f));
	TestTrue(TEXT("a refreshed pending medal retains both events"), Queue.GetActive() && Queue.GetActive()->Count == 2);
	Queue.Reset();
	Queue.Push(ECampusScoreEvent::Streak5, 0.f);
	Queue.Push(ECampusScoreEvent::Headshot, 7.f);
	TestEqual(TEXT("pushing a fresh event removes stale higher priority medals"), Queue.NumPending(), 1);
	Queue.Tick(7.2f);
	TestTrue(TEXT("a fresh event can display after stale medals are discarded"), Queue.GetActive() && Queue.GetActive()->Event == ECampusScoreEvent::Headshot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusMedalLifecycleTest, "CampusOps.Experience.CombatPresentation.MedalLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusMedalLifecycleTest::RunTest(const FString&)
{
	FMedalQueue Queue;
	Queue.Push(ECampusScoreEvent::Headshot, 0.f);
	Queue.Tick(0.25f);
	TestEqual(TEXT("the display duration is 1.65 seconds"), FMedalQueue::DisplayTime, 1.65f);
	TestEqual(TEXT("display timing starts when the medal becomes active"), Queue.GetStartTime(), 0.25f);
	TestFalse(TEXT("an active medal does not restart before its duration ends"), Queue.Tick(1.89f));
	TestNotNull(TEXT("the medal remains visible for its full display duration"), Queue.GetActive());
	TestFalse(TEXT("ending the final medal does not report a new activation"), Queue.Tick(1.9f));
	TestNull(TEXT("the final medal disappears after 1.65 seconds"), Queue.GetActive());
	Queue.Push(ECampusScoreEvent::Revenge, 2.f);
	Queue.Tick(2.2f);
	Queue.Push(ECampusScoreEvent::Longshot, 2.3f);
	Queue.Reset();
	TestNull(TEXT("round reset immediately clears the active medal"), Queue.GetActive());
	TestEqual(TEXT("round reset discards all pending medals"), Queue.NumPending(), 0);
	TestFalse(TEXT("a reset queue cannot replay a previous round medal"), Queue.Tick(0.f));
	Queue.Push(ECampusScoreEvent::Melee, 0.f);
	TestTrue(TEXT("a new round starts with its own collection window"), Queue.Tick(0.2f));
	TestTrue(TEXT("the new round displays only its newly earned event"), Queue.GetActive() && Queue.GetActive()->Event == ECampusScoreEvent::Melee && Queue.GetActive()->Count == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusPlayerNameTest, "CampusOps.Experience.CombatPresentation.PlayerNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusPlayerNameTest::RunTest(const FString&)
{
	TestEqual(TEXT("Chinese names retain their characters and internal spaces"), CleanPlayerName(TEXT("  光華 玩家  ")), FString(TEXT("光華 玩家")));
	const FString Unknown = NSLOCTEXT("CampusHUD", "UnknownPlayer", "未知玩家").ToString();
	TestEqual(TEXT("empty names use the localized fallback"), CleanPlayerName(FString()), Unknown);
	TestEqual(TEXT("blank names use the localized fallback"), CleanPlayerName(TEXT(" \t\r\n ")), Unknown);
	FString Controlled(TEXT("甲"));
	for (int32 Codepoint = 1; Codepoint <= 31; ++Codepoint)
	{
		Controlled.AppendChar(static_cast<TCHAR>(Codepoint));
	}
	for (int32 Codepoint = 127; Codepoint <= 159; ++Codepoint)
	{
		Controlled.AppendChar(static_cast<TCHAR>(Codepoint));
	}
	for (const int32 Codepoint : { 0x061c, 0x200e, 0x200f, 0x2028, 0x2029, 0x202a, 0x202b, 0x202c, 0x202d, 0x202e, 0x2066, 0x2067, 0x2068, 0x2069 })
	{
		Controlled.AppendChar(static_cast<TCHAR>(Codepoint));
	}
	Controlled += TEXT("乙");
	TestEqual(TEXT("control, line separator and bidi characters cannot alter adjacent HUD text"), CleanPlayerName(Controlled), FString(TEXT("甲乙")));
	TestEqual(TEXT("long names end in an ellipsis within the 64-unit display limit"), CleanPlayerName(FString::ChrN(4096, TEXT('A'))), FString::ChrN(63, TEXT('A')) + TEXT("…"));
	FString Emoji;
	Emoji.AppendChar(static_cast<TCHAR>(0xd83d));
	Emoji.AppendChar(static_cast<TCHAR>(0xde00));
	TestEqual(TEXT("short UTF16 emoji names retain the complete surrogate pair"), CleanPlayerName(TEXT("玩家") + Emoji), TEXT("玩家") + Emoji);
	const FString FittingEmoji = FString::ChrN(61, TEXT('A')) + Emoji;
	TestEqual(TEXT("truncation preserves an emoji that fits before the ellipsis"), CleanPlayerName(FittingEmoji + TEXT("extra")), FittingEmoji + TEXT("…"));
	TestEqual(TEXT("truncation never leaves half an emoji at its UTF16 boundary"), CleanPlayerName(FString::ChrN(62, TEXT('A')) + Emoji + TEXT("extra")), FString::ChrN(62, TEXT('A')) + TEXT("…"));
	return true;
}
#endif
