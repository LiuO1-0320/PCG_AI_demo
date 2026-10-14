#if WITH_DEV_AUTOMATION_TESTS

#include "RogueliteRoomGraph.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

namespace
{
    using Generator = FRogueliteRoomGraphGenerator;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FString DescribeResult(const FRogueliteGraphGenerationResult& R)
    {
        FString Text = FString::Printf(TEXT("Seed=%d success=%d fallback=%d used=%d accepted=%d error=%d reason=%s fallbackReason=%s|%s"),
            R.Seed, R.bSucceeded, R.bUsedFallback, R.AttemptsUsed, R.AcceptedAttemptIndex, int32(R.Error), *R.Reason, *R.FallbackReason,
            *Generator::DescribeGraph(R.Graph));
        for (const auto& A : R.Attempts) Text += FString::Printf(TEXT("|attempt=%d,%d,%d,%d,%d,%d,%s"), A.AttemptIndex,
            A.TopologySeed, A.MainPathNodes, A.BranchCount, A.bAccepted, int32(A.Error), *A.Reason);
        return Text;
    }
    // Independent BFS/count/degree assertions, in addition to the production validator.
    bool CheckTree(FAutomationTestBase& Test, const FRogueliteRoomGraph& G, const FRogueliteGraphGenerationSettings& S)
    {
        bool OK = Test.TestEqual(TEXT("Tree edge count"), G.Edges.Num(), G.Nodes.Num() - 1);
        TArray<int32> Degree; Degree.Init(0, G.Nodes.Num());
        TSet<uint64> Unique;
        for (const auto& E : G.Edges)
        {
            if (!G.Nodes.IsValidIndex(E.NodeA) || !G.Nodes.IsValidIndex(E.NodeB)) { Test.AddError(TEXT("Illegal endpoint")); return false; }
            OK &= Test.TestTrue(TEXT("No self edge"), E.NodeA != E.NodeB);
            const uint64 Key = (uint64(uint32(FMath::Min(E.NodeA, E.NodeB))) << 32) | uint32(FMath::Max(E.NodeA, E.NodeB));
            OK &= Test.TestFalse(TEXT("No duplicate undirected edge"), Unique.Contains(Key)); Unique.Add(Key);
            ++Degree[E.NodeA]; ++Degree[E.NodeB];
        }
        TSet<int32> Seen{G.StartNodeId}; TArray<int32> Queue{G.StartNodeId};
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
            for (const auto& E : G.Edges)
            {
                const int32 Next = E.NodeA == Queue[Head] ? E.NodeB : E.NodeB == Queue[Head] ? E.NodeA : INDEX_NONE;
                if (Next != INDEX_NONE && !Seen.Contains(Next)) { Seen.Add(Next); Queue.Add(Next); }
            }
        OK &= Test.TestEqual(TEXT("All nodes reachable by independent BFS"), Seen.Num(), G.Nodes.Num());
        OK &= Test.TestTrue(TEXT("Final reachable"), Seen.Contains(G.FinalNodeId));
        int32 Starts = 0, Finals = 0;
        for (const auto& N : G.Nodes)
        {
            Starts += N.Role == ERogueliteGraphNodeRole::Start;
            Finals += N.Role == ERogueliteGraphNodeRole::Final;
            OK &= Test.TestEqual(TEXT("Stored degree"), N.Degree, Degree[N.NodeId]);
            OK &= Test.TestTrue(TEXT("Degree budget"), Degree[N.NodeId] <= S.MaxDegree);
        }
        OK &= Test.TestEqual(TEXT("Unique Start"), Starts, 1); OK &= Test.TestEqual(TEXT("Unique Final"), Finals, 1);
        OK &= Test.TestTrue(TEXT("Main path range"), G.MainPath.Num() >= S.MinMainPathNodes && G.MainPath.Num() <= S.MaxMainPathNodes);
        OK &= Test.TestTrue(TEXT("Branch count range"), G.Branches.Num() >= S.MinBranches && G.Branches.Num() <= S.MaxBranches);
        OK &= Test.TestTrue(TEXT("Node budget"), G.Nodes.Num() <= S.MaxNodes);
        for (const auto& B : G.Branches)
        {
            OK &= Test.TestTrue(TEXT("Branch depth range"), B.NodeIds.Num() >= S.MinBranchDepth && B.NodeIds.Num() <= S.MaxBranchDepth);
            OK &= Test.TestTrue(TEXT("No Final branch"), B.AttachmentNodeId != G.FinalNodeId);
        }
        FString Reason;
        OK &= Test.TestTrue(TEXT("Production validation"), Generator::ValidateGraph(G, S, Reason));
        if (!OK) Test.AddError(Reason);
        return OK;
    }
    FRogueliteGraphGenerationSettings TightSettings()
    {
        FRogueliteGraphGenerationSettings S;
        S.MinMainPathNodes = S.MaxMainPathNodes = 2; S.MinBranches = 0; S.MaxBranches = 4;
        S.MaxDegree = 2; S.MaxNodes = 5; S.MaxAttempts = 1;
        return S;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphHundredSeedsTest, "Roguelite.RoomGraph.HundredSeeds", Flags)
bool FRogueliteGraphHundredSeedsTest::RunTest(const FString&)
{
    const FRogueliteGraphGenerationSettings S;
    TSet<FString> Shapes; int32 Fallbacks = 0, Rejections = 0, MinNodes = MAX_int32, MaxNodes = 0, DegreeThree = 0;
    for (int32 Seed = 0; Seed < 100; ++Seed)
    {
        const auto A = Generator::Generate(Seed, S), B = Generator::Generate(Seed, S);
        if (!TestTrue(TEXT("Generation success"), A.bSucceeded)) { AddError(DescribeResult(A)); return false; }
        TestEqual(TEXT("Complete deterministic output and diagnostics"), DescribeResult(A), DescribeResult(B));
        TestEqual(TEXT("Stable digest"), A.Graph.StableDigest, B.Graph.StableDigest);
        if (!CheckTree(*this, A.Graph, S)) return false;
        Shapes.Add(A.Graph.TopologyDigest); Fallbacks += A.bUsedFallback;
        MinNodes = FMath::Min(MinNodes, A.Graph.Nodes.Num()); MaxNodes = FMath::Max(MaxNodes, A.Graph.Nodes.Num());
        for (const auto& N : A.Graph.Nodes) DegreeThree += N.Degree == 3;
        for (const auto& Attempt : A.Attempts) Rejections += !Attempt.bAccepted;
        AddInfo(FString::Printf(TEXT("B1_SEED Seed=%d Nodes=%d Edges=%d Main=%d Branches=%d Attempts=%d Fallback=%d Shape=%s Digest=%s"),
            Seed, A.Graph.Nodes.Num(), A.Graph.Edges.Num(), A.Graph.MainPath.Num(), A.Graph.Branches.Num(), A.AttemptsUsed,
            A.bUsedFallback, *A.Graph.TopologyDigest, *A.Graph.StableDigest));
    }
    TestTrue(TEXT("Different seeds produce multiple shapes, not just different seed hashes"), Shapes.Num() > 1);
    AddInfo(FString::Printf(TEXT("B1_STATS Seeds=100 Success=100 UniqueShapes=%d Fallbacks=%d RejectedCandidates=%d Nodes=%d..%d Degree3Nodes=%d"),
        Shapes.Num(), Fallbacks, Rejections, MinNodes, MaxNodes, DegreeThree));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphSeedIsolationTest, "Roguelite.RoomGraph.SeedIsolation", Flags)
bool FRogueliteGraphSeedIsolationTest::RunTest(const FString&)
{
    const ERogueliteGraphSeedPurpose Purposes[] = {ERogueliteGraphSeedPurpose::Topology, ERogueliteGraphSeedPurpose::RoomType,
        ERogueliteGraphSeedPurpose::TemplateSelection, ERogueliteGraphSeedPurpose::Encounter, ERogueliteGraphSeedPurpose::RoomContent};
    const int32 Golden[] = {-165691117, 1268324568, -495885804, -808553067, -4827729};
    const FRogueliteGraphGenerationSettings S; const auto Before = Generator::Generate(1337, S);
    TSet<int32> Seeds;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Purposes); ++I)
    {
        const int32 Derived = Generator::DeriveSeed(1337, Purposes[I]); Seeds.Add(Derived);
        TestEqual(TEXT("Version 1 seed golden vector"), Derived, Golden[I]);
        if (I > 0) { FRandomStream Other(Derived); for (int32 Draw = 0; Draw < 10000; ++Draw) Other.GetUnsignedInt(); }
    }
    TestEqual(TEXT("Purpose separation in fixture"), Seeds.Num(), 5);
    TestEqual(TEXT("40,000 unrelated draws do not affect topology"), DescribeResult(Before), DescribeResult(Generator::Generate(1337, S)));
    TestTrue(TEXT("Stable entity ID changes content seed"), Generator::DeriveSeed(1337, ERogueliteGraphSeedPurpose::RoomContent, 1, 1, 1)
        != Generator::DeriveSeed(1337, ERogueliteGraphSeedPurpose::RoomContent, 1, 1, 2));
    TestTrue(TEXT("Attempt namespace differs"), Generator::DeriveSeed(1337, Purposes[0], 1, 1, 0, 0) != Generator::DeriveSeed(1337, Purposes[0], 1, 1, 0, 1));
    TestTrue(TEXT("Config version participates"), Generator::DeriveSeed(1337, Purposes[0], 1, 1) != Generator::DeriveSeed(1337, Purposes[0], 1, 2));
    AddInfo(TEXT("B1_SEED_ISOLATION Golden=5 UnrelatedDraws=40000 Passed")); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphInvalidSettingsTest, "Roguelite.RoomGraph.InvalidSettings", Flags)
bool FRogueliteGraphInvalidSettingsTest::RunTest(const FString&)
{
    const auto Reject = [this](const FRogueliteGraphGenerationSettings& S, ERogueliteGraphError Expected)
    {
        const auto R = Generator::Generate(7, S);
        TestFalse(TEXT("Invalid/unsatisfiable input fails"), R.bSucceeded);
        TestTrue(TEXT("Classified failure"), R.Error == Expected);
        TestEqual(TEXT("Preflight makes zero attempts"), R.AttemptsUsed, 0);
        TestEqual(TEXT("No partial graph"), R.Graph.Nodes.Num(), 0);
        TestFalse(TEXT("Reason present"), R.Reason.IsEmpty());
        AddInfo(FString::Printf(TEXT("B1_EXPECTED_REJECTION Error=%d Settings=%s Reason=%s"), int32(R.Error), *Generator::DescribeSettings(S), *R.Reason));
    };
    FRogueliteGraphGenerationSettings S;
    S.MinMainPathNodes=1; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxMainPathNodes=4; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MinBranches=-1; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxBranches=1; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MinBranchDepth=0; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxBranchDepth=0; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxNodes=1025; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxDegree=0; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxDegree=5; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxAttempts=0; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.MaxAttempts=129; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.ConfigVersion=0; Reject(S, ERogueliteGraphError::InvalidSettings);
    S={}; S.AlgorithmVersion=3; Reject(S, ERogueliteGraphError::UnsupportedAlgorithm); // V2 is now supported; no Golden changes.
    S={}; S.MaxDegree=2; Reject(S, ERogueliteGraphError::UnsatisfiableConstraints); // Required two branches, one slot.
    S={}; S.MaxNodes=6; Reject(S, ERogueliteGraphError::UnsatisfiableConstraints);
    S={}; S.MaxDegree=1; Reject(S, ERogueliteGraphError::UnsatisfiableConstraints);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphFallbackTest, "Roguelite.RoomGraph.RetryFallback", Flags)
bool FRogueliteGraphFallbackTest::RunTest(const FString&)
{
    auto S = TightSettings(); int32 Fixture = INDEX_NONE;
    for (int32 Seed=0; Seed<256; ++Seed) if (Generator::Generate(Seed,S).bUsedFallback) { Fixture=Seed; break; }
    if (!TestTrue(TEXT("Find a deterministically rejected seeded candidate"), Fixture != INDEX_NONE)) return false;
    const auto A=Generator::Generate(Fixture,S), B=Generator::Generate(Fixture,S);
    TestTrue(TEXT("Explicit fallback"), A.bSucceeded && A.bUsedFallback && !A.FallbackReason.IsEmpty());
    TestEqual(TEXT("Fallback is reproducible including failure reasons"), DescribeResult(A), DescribeResult(B));
    TestEqual(TEXT("Exactly one bounded attempt"), A.AttemptsUsed, 1);
    TestEqual(TEXT("Fallback has no accepted random attempt"), A.AcceptedAttemptIndex, INDEX_NONE);
    CheckTree(*this,A.Graph,S); AddInfo(TEXT("B1_FORCED_FALLBACK ")+DescribeResult(A));
    S.bEnableFallback=false;
    const auto Failed=Generator::Generate(Fixture,S);
    TestTrue(TEXT("Exhaustion fails with fallback disabled"), !Failed.bSucceeded && Failed.Error==ERogueliteGraphError::AttemptExhausted);
    TestEqual(TEXT("No leaked candidate"), Failed.Graph.Nodes.Num(),0);
    TestEqual(TEXT("Failure repeatable"),DescribeResult(Failed),DescribeResult(Generator::Generate(Fixture,S)));
    AddInfo(TEXT("B1_FORCED_FAILURE ")+DescribeResult(Failed));
    S.bEnableFallback=true; S.MaxAttempts=8; Fixture=INDEX_NONE;
    for (int32 Seed=0; Seed<256; ++Seed)
    {
        const auto R=Generator::Generate(Seed,S);
        if (R.bSucceeded && !R.bUsedFallback && R.AcceptedAttemptIndex>0) { Fixture=Seed; break; }
    }
    if (!TestTrue(TEXT("Retry success fixture exists"), Fixture!=INDEX_NONE)) return false;
    const auto Retry=Generator::Generate(Fixture,S);
    TestTrue(TEXT("Retry remains bounded"), Retry.AttemptsUsed<=S.MaxAttempts);
    TestEqual(TEXT("Retry output and original rejected candidates repeat"),DescribeResult(Retry),DescribeResult(Generator::Generate(Fixture,S)));
    AddInfo(TEXT("B1_RETRY_SUCCESS ")+DescribeResult(Retry));
    // A tight required-minimum fixture: fallback must retain the branch, not drop it.
    S=TightSettings(); S.MinBranches=1; S.MinBranchDepth=S.MaxBranchDepth=2; S.MaxNodes=4;
    for (int32 Seed=0; Seed<256; ++Seed)
    {
        const auto R=Generator::Generate(Seed,S);
        if (R.bUsedFallback)
        {
            TestEqual(TEXT("Required minimum branch retained"),R.Graph.Branches.Num(),1);
            TestEqual(TEXT("Required depth retained"),R.Graph.Branches[0].NodeIds.Num(),2);
            CheckTree(*this,R.Graph,S); AddInfo(TEXT("B1_REQUIRED_FALLBACK ")+DescribeResult(R)); return true;
        }
    }
    AddError(TEXT("Required-minimum fallback fixture not found")); return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphBudgetsTest, "Roguelite.RoomGraph.DegreeBudgets", Flags)
bool FRogueliteGraphBudgetsTest::RunTest(const FString&)
{
    for (int32 D=1; D<=4; ++D)
    {
        FRogueliteGraphGenerationSettings S;
        S.MaxDegree=D; S.MinMainPathNodes=D==1?2:5; S.MaxMainPathNodes=D==1?2:8;
        S.MinBranches=D==1?0:D==2?1:2; S.MaxBranches=D==1?0:D==2?1:4;
        for (int32 Seed=100; Seed<125; ++Seed)
        {
            const auto R=Generator::Generate(Seed,S);
            if (!TestTrue(TEXT("Budget fixture succeeds"),R.bSucceeded) || !CheckTree(*this,R.Graph,S)) return false;
        }
    }
    for (int32 Seed : {MIN_int32, -1, MAX_int32})
    {
        const FRogueliteGraphGenerationSettings S; const auto R=Generator::Generate(Seed,S);
        TestTrue(TEXT("Full signed Seed range"),R.bSucceeded); CheckTree(*this,R.Graph,S);
    }
    AddInfo(TEXT("B1_BUDGETS Degree=1..4 Seeds=100 SignedSeedExtremes=3 Passed")); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphMutationTest, "Roguelite.RoomGraph.RejectMalformedGraphs", Flags)
bool FRogueliteGraphMutationTest::RunTest(const FString&)
{
    const FRogueliteGraphGenerationSettings S; const auto Good=Generator::Generate(1337,S).Graph;
    const auto Reject=[this,&S](const FRogueliteRoomGraph& G,const TCHAR* Name)
    { FString Reason; TestFalse(Name,Generator::ValidateGraph(G,S,Reason)); AddInfo(FString(Name)+TEXT(": ")+Reason); };
    auto G=Good; G.Edges[0].NodeB=G.Edges[0].NodeA; Reject(G,TEXT("Self loop"));
    G=Good; G.Edges[1].NodeA=G.Edges[0].NodeB; G.Edges[1].NodeB=G.Edges[0].NodeA; Reject(G,TEXT("Reverse duplicate"));
    G=Good; G.Edges[0].NodeB=9999; Reject(G,TEXT("Illegal reference"));
    G=Good; G.Nodes[0].Degree=99; Reject(G,TEXT("Degree metadata"));
    G=Good; G.Nodes[0].MaxAllowedDegree=4; Reject(G,TEXT("Degree authority"));
    G=Good; G.MainPath[0]=9999; G.StartNodeId=9999; Reject(G,TEXT("Invalid Start reference cannot crash"));
    G=Good; G.Branches[0].AttachmentNodeId=G.FinalNodeId; Reject(G,TEXT("Final branch forbidden"));
    G=Good; const int32 DuplicateMember=G.Branches[0].NodeIds[0]; G.Branches[0].NodeIds.Add(DuplicateMember); Reject(G,TEXT("Repeated branch member"));
    G=Good; G.Edges.RemoveAt(0); Reject(G,TEXT("Missing edge"));
    G=Good; G.Edges[0].NodeA=1; G.Edges[0].NodeB=3; Reject(G,TEXT("Cycle with unchanged edge count"));
    G=Good; G.Edges[0].ConnectionMode=TEXT("Direct"); Reject(G,TEXT("Formal graph requires corridor"));
    G=Good; G.StableDigest=TEXT("stale"); Reject(G,TEXT("Stale summary"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphGoldenSummaryTest, "Roguelite.RoomGraph.GoldenSummaryAndQueries", Flags)
bool FRogueliteGraphGoldenSummaryTest::RunTest(const FString&)
{
    FRogueliteGraphGenerationSettings S;
    S.MinMainPathNodes=S.MaxMainPathNodes=2; S.MinBranches=S.MaxBranches=0; S.MaxDegree=1;
    const auto R=Generator::Generate(1337,S);
    TestTrue(TEXT("Two-node fixture succeeds"),R.bSucceeded);
    TestEqual(TEXT("Settings golden digest"),R.Graph.SettingsDigest,FString(TEXT("fcbe6ac57b97760d")));
    TestEqual(TEXT("Topology golden digest"),R.Graph.TopologyDigest,FString(TEXT("0b88e440b27b2fa2")));
    TestEqual(TEXT("Full golden digest"),R.Graph.StableDigest,FString(TEXT("a78edc8bfc4f1456")));
    TestEqual(TEXT("FNV-1a empty golden"),Generator::StableTextDigest(TEXT("")),FString(TEXT("cbf29ce484222325")));
    TestEqual(TEXT("FNV-1a ASCII golden"),Generator::StableTextDigest(TEXT("hello")),FString(TEXT("a430d84680aabd0b")));
    TestTrue(TEXT("Query Start to Final"),R.Graph.IsReachable(0,1));
    TestTrue(TEXT("Undirected reverse query"),R.Graph.IsReachable(1,0));
    TestTrue(TEXT("Self reachability"),R.Graph.IsReachable(0,0));
    TestFalse(TEXT("Invalid query endpoint"),R.Graph.IsReachable(0,9999));
    TestEqual(TEXT("Ordered neighbor query"),R.Graph.GetNeighborIds(0)[0],1);
    TestEqual(TEXT("Invalid query is empty"),R.Graph.GetNeighborIds(-1).Num(),0);
    AddInfo(TEXT("B1_GOLDEN StableDigest=a78edc8bfc4f1456 Shape=0b88e440b27b2fa2")); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteGraphWorldIsolationTest, "Roguelite.RoomGraph.NoActorMutation", Flags)
bool FRogueliteGraphWorldIsolationTest::RunTest(const FString&)
{
    TMap<AActor*,FTransform> Before;
    for (TObjectIterator<AActor> It; It; ++It) if (!It->HasAnyFlags(RF_ClassDefaultObject)) Before.Add(*It,It->GetActorTransform());
    const FRogueliteGraphGenerationSettings S;
    for (int32 I=0; I<100; ++I) TestTrue(TEXT("Pure generation"),Generator::Generate(I,S).bSucceeded);
    TMap<AActor*,FTransform> After;
    for (TObjectIterator<AActor> It; It; ++It) if (!It->HasAnyFlags(RF_ClassDefaultObject)) After.Add(*It,It->GetActorTransform());
    TestEqual(TEXT("Actor count unchanged"),After.Num(),Before.Num());
    for (const auto& Pair : Before)
    { const auto* Transform=After.Find(Pair.Key); TestTrue(TEXT("Actor identity and transform unchanged"),Transform && Transform->Equals(Pair.Value,0)); }
    AddInfo(FString::Printf(TEXT("B1_WORLD_ISOLATION ActorsBefore=%d ActorsAfter=%d Calls=100"),Before.Num(),After.Num())); return true;
}

#endif
