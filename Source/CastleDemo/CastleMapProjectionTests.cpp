#include "CastleMapProjection.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMapProjectionTest,"Castle.Map.OverheadOrientation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCastleMapProjectionTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("World +X moves up"),CastleMapOffset(FVector(2400,0,0),43.f/2400.f).Equals(FVector2D(0,-43),.001));
    TestTrue(TEXT("World +Y moves right"),CastleMapOffset(FVector(0,2400,0),43.f/2400.f).Equals(FVector2D(43,0),.001));
    // The cave is 2 cells in X and 3 in Y. Its upper-right room must be
    // two steps right and one step up, with markers at the same center.
    const FVector CaveOrigin(0,10000,0),RoomCenter=CaveOrigin+FVector(2100,4200,0);
    const auto CavePoint=CastleMapOffset(RoomCenter-CaveOrigin,55.f/2100.f);
    TestTrue(TEXT("Rectangular cave keeps all rooms within display bounds"),CavePoint.Equals(FVector2D(110,-55),.001));
    const auto Marker=CastleMapOffset(RoomCenter+FVector(525,0,110)-CaveOrigin,55.f/2100.f);
    TestTrue(TEXT("Moving marker follows room orientation and ignores height"),(Marker-CavePoint).Equals(FVector2D(0,-13.75),.001));
    return true;
}
#endif
