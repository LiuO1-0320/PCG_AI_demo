#include "RogueliteRoomGraph.h"
#include "RogueliteRoomGraphV2.h"
#include "Containers/StringConv.h"
#include "Math/RandomStream.h"

namespace
{
    constexpr int32 MaxSupportedNodes = 1024;
    constexpr int32 MaxSupportedAttempts = 128;
    uint32 Mix32(uint32 X)
    {
        X ^= X >> 16; X *= 0x7feb352du; X ^= X >> 15; X *= 0x846ca68bu; return X ^ (X >> 16);
    }
    uint64 EdgeKey(int32 A, int32 B)
    {
        return (uint64(uint32(FMath::Min(A, B))) << 32) | uint32(FMath::Max(A, B));
    }
    int32 BranchSlots(int32 MainNodes, int32 Degree)
    {
        return FMath::Max(0, Degree - 1) + (MainNodes - 2) * FMath::Max(0, Degree - 2);
    }
    bool FitsMinimum(int32 MainNodes, const FRogueliteGraphGenerationSettings& S)
    {
        return (MainNodes == 2 || S.MaxDegree >= 2) && BranchSlots(MainNodes, S.MaxDegree) >= S.MinBranches
            && int64(MainNodes) + int64(S.MinBranches) * S.MinBranchDepth <= S.MaxNodes;
    }
    void AddNode(FRogueliteRoomGraph& G, const FRogueliteGraphGenerationSettings& S, ERogueliteGraphNodeRole Role,
        int32 MainOrder, int32 Branch, int32 Depth, int32 Parent)
    {
        auto& N = G.Nodes.AddDefaulted_GetRef();
        N.NodeId = G.Nodes.Num() - 1; N.Role = Role; N.MainPathOrder = MainOrder;
        N.BranchId = Branch; N.BranchDepth = Depth; N.ParentNodeId = Parent; N.MaxAllowedDegree = S.MaxDegree;
    }
    void AddEdge(FRogueliteRoomGraph& G, int32 A, int32 B, ERogueliteGraphEdgeRole Role)
    {
        auto& E = G.Edges.AddDefaulted_GetRef(); E.EdgeId = G.Edges.Num() - 1;
        E.NodeA = A; E.NodeB = B; E.Role = Role; ++G.Nodes[A].Degree; ++G.Nodes[B].Degree;
    }
    FRogueliteRoomGraph MakeMain(int32 Seed, int32 Count, const FRogueliteGraphGenerationSettings& S)
    {
        FRogueliteRoomGraph G;
        G.Seed = Seed; G.AlgorithmVersion = S.AlgorithmVersion; G.ConfigVersion = S.ConfigVersion;
        G.SettingsDigest = FRogueliteRoomGraphGenerator::StableTextDigest(FRogueliteRoomGraphGenerator::DescribeSettings(S));
        G.StartNodeId = 0; G.FinalNodeId = Count - 1;
        for (int32 I = 0; I < Count; ++I)
        {
            AddNode(G, S, I == 0 ? ERogueliteGraphNodeRole::Start : I == Count - 1 ? ERogueliteGraphNodeRole::Final
                : ERogueliteGraphNodeRole::MainPath, I, INDEX_NONE, 0, I - 1);
            G.MainPath.Add(I);
            if (I > 0) AddEdge(G, I - 1, I, ERogueliteGraphEdgeRole::MainPath);
        }
        return G;
    }
    void AppendBranch(FRogueliteRoomGraph& G, const FRogueliteGraphGenerationSettings& S, int32 Anchor, int32 Depth)
    {
        const int32 Id = G.Branches.Num();
        auto& B = G.Branches.AddDefaulted_GetRef(); B.BranchId = Id; B.AttachmentNodeId = Anchor;
        int32 Parent = Anchor;
        for (int32 D = 1; D <= Depth; ++D)
        {
            const int32 NodeId = G.Nodes.Num();
            AddNode(G, S, ERogueliteGraphNodeRole::Branch, INDEX_NONE, Id, D, Parent);
            AddEdge(G, Parent, NodeId, ERogueliteGraphEdgeRole::OptionalBranch);
            B.NodeIds.Add(NodeId); Parent = NodeId;
        }
    }
    void Finalize(FRogueliteRoomGraph& G)
    {
        G.TopologyDigest = FRogueliteRoomGraphGenerator::StableTextDigest(FRogueliteRoomGraphGenerator::DescribeTopology(G));
        G.StableDigest = FRogueliteRoomGraphGenerator::StableTextDigest(FRogueliteRoomGraphGenerator::DescribeGraph(G));
    }
}

const FRogueliteRoomGraphNode* FRogueliteRoomGraph::FindNode(int32 Id) const
{
    return Nodes.IsValidIndex(Id) && Nodes[Id].NodeId == Id ? &Nodes[Id] : nullptr;
}
TArray<int32> FRogueliteRoomGraph::GetNeighborIds(int32 Id) const
{
    TArray<int32> Result;
    if (!FindNode(Id)) return Result;
    for (const auto& E : Edges)
        if (E.NodeA == Id && FindNode(E.NodeB)) Result.Add(E.NodeB);
        else if (E.NodeB == Id && FindNode(E.NodeA)) Result.Add(E.NodeA);
    return Result;
}
bool FRogueliteRoomGraph::IsReachable(int32 From, int32 To) const
{
    if (!FindNode(From) || !FindNode(To)) return false;
    TArray<bool> Seen; Seen.Init(false, Nodes.Num()); TArray<int32> Queue{From}; Seen[From] = true;
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        if (Queue[Head] == To) return true;
        for (int32 N : GetNeighborIds(Queue[Head])) if (!Seen[N]) { Seen[N] = true; Queue.Add(N); }
    }
    return false;
}

int32 FRogueliteRoomGraphGenerator::DeriveSeed(int32 Seed, ERogueliteGraphSeedPurpose Purpose,
    int32 AlgorithmVersion, int32 ConfigVersion, int32 StableId, int32 AttemptIndex)
{
    uint32 X = uint32(Seed) ^ uint32(Purpose) ^ 0x9e3779b9u;
    for (int32 V : {AlgorithmVersion, ConfigVersion, StableId, AttemptIndex}) X = Mix32(X ^ uint32(V));
    int32 Result; FMemory::Memcpy(&Result, &X, sizeof(Result)); return Result;
}
FString FRogueliteRoomGraphGenerator::StableTextDigest(const FString& Text)
{
    const FTCHARToUTF8 Bytes(*Text);
    uint64 Hash = 14695981039346656037ull;
    for (int32 I = 0; I < Bytes.Length(); ++I) { Hash ^= uint8(Bytes.Get()[I]); Hash *= 1099511628211ull; }
    return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Hash));
}
FString FRogueliteRoomGraphGenerator::DescribeSettings(const FRogueliteGraphGenerationSettings& S)
{
    if (S.AlgorithmVersion == 2) return RogueliteGraphV2::DescribeSettings(S);
    return FString::Printf(TEXT("B1SettingsV1|main=%d,%d|branches=%d,%d|depth=%d,%d|nodes=%d|degree=%d|attempts=%d|algorithm=%d|config=%d|fallback=%d"),
        S.MinMainPathNodes, S.MaxMainPathNodes, S.MinBranches, S.MaxBranches, S.MinBranchDepth, S.MaxBranchDepth,
        S.MaxNodes, S.MaxDegree, S.MaxAttempts, S.AlgorithmVersion, S.ConfigVersion, int32(S.bEnableFallback));
}
FString FRogueliteRoomGraphGenerator::DescribeTopology(const FRogueliteRoomGraph& G)
{
    if (G.AlgorithmVersion == 2) return RogueliteGraphV2::DescribeTopology(G);
    FString Text = FString::Printf(TEXT("B1TopologyV1|start=%d|final=%d|main="), G.StartNodeId, G.FinalNodeId);
    for (int32 Id : G.MainPath) Text += FString::Printf(TEXT("%d,"), Id);
    for (const auto& N : G.Nodes) Text += FString::Printf(TEXT("|n=%d,%d,%d,%d,%d,%d,%d,%d"), N.NodeId, int32(N.Role),
        N.MainPathOrder, N.BranchId, N.BranchDepth, N.ParentNodeId, N.Degree, N.MaxAllowedDegree);
    for (const auto& E : G.Edges) Text += FString::Printf(TEXT("|e=%d,%d,%d,%d,%s"), E.EdgeId, E.NodeA, E.NodeB, int32(E.Role), *E.ConnectionMode);
    for (const auto& B : G.Branches)
    {
        Text += FString::Printf(TEXT("|b=%d,%d:"), B.BranchId, B.AttachmentNodeId);
        for (int32 Id : B.NodeIds) Text += FString::Printf(TEXT("%d,"), Id);
    }
    return Text;
}
FString FRogueliteRoomGraphGenerator::DescribeGraph(const FRogueliteRoomGraph& G)
{
    if(G.AlgorithmVersion==2)return FString::Printf(TEXT("B4GraphV2|seed=%d|algorithm=%d|config=%d|settings=%s|%s"),G.Seed,G.AlgorithmVersion,
        G.ConfigVersion,*G.SettingsDigest,*DescribeTopology(G));
    return FString::Printf(TEXT("B1GraphV1|seed=%d|algorithm=%d|config=%d|settings=%s|%s"), G.Seed, G.AlgorithmVersion,
        G.ConfigVersion, *G.SettingsDigest, *DescribeTopology(G));
}

bool FRogueliteRoomGraphGenerator::ValidateSettings(const FRogueliteGraphGenerationSettings& S, ERogueliteGraphError& Error, FString& Reason)
{
    if (S.AlgorithmVersion == 2) return RogueliteGraphV2::ValidateSettings(S, Error, Reason);
    Error = ERogueliteGraphError::InvalidSettings;
    if (S.AlgorithmVersion != 1) { Error = ERogueliteGraphError::UnsupportedAlgorithm; Reason = TEXT("Supported AlgorithmVersion values are 1 and 2."); return false; }
    if (S.ConfigVersion < 1 || S.MinMainPathNodes < 2 || S.MaxMainPathNodes < S.MinMainPathNodes || S.MaxMainPathNodes > MaxSupportedNodes
        || S.MinBranches < 0 || S.MaxBranches < S.MinBranches || S.MaxBranches > MaxSupportedNodes
        || S.MinBranchDepth < 1 || S.MaxBranchDepth < S.MinBranchDepth || S.MaxBranchDepth > MaxSupportedNodes
        || S.MaxNodes < 2 || S.MaxNodes > MaxSupportedNodes || S.MaxDegree < 1 || S.MaxDegree > 4
        || S.MaxAttempts < 1 || S.MaxAttempts > MaxSupportedAttempts)
    { Reason = TEXT("Invalid ranges: main>=2; ordered branch/depth ranges; nodes/count/depth<=1024; degree=1..4; attempts=1..128; config>=1."); return false; }
    for (int32 M = S.MinMainPathNodes; M <= S.MaxMainPathNodes; ++M)
        if (FitsMinimum(M, S)) { Error = ERogueliteGraphError::None; Reason.Reset(); return true; }
    Error = ERogueliteGraphError::UnsatisfiableConstraints;
    Reason = FString::Printf(TEXT("Cannot satisfy minimum main/branches/depth: main=%d..%d, branches>=%d, depth>=%d, MaxNodes=%d, MaxDegree=%d; Final cannot host branches."),
        S.MinMainPathNodes, S.MaxMainPathNodes, S.MinBranches, S.MinBranchDepth, S.MaxNodes, S.MaxDegree);
    return false;
}

bool FRogueliteRoomGraphGenerator::ValidateGraph(const FRogueliteRoomGraph& G, const FRogueliteGraphGenerationSettings& S, FString& Reason)
{
    if (S.AlgorithmVersion == 2) return RogueliteGraphV2::ValidateGraph(G, S, Reason);
    ERogueliteGraphError Error;
    if (!ValidateSettings(S, Error, Reason)) return false;
    const auto Fail = [&Reason](const TCHAR* Message) { Reason = Message; return false; };
    const int32 N = G.Nodes.Num(), M = G.MainPath.Num();
    if (N < 2 || N > S.MaxNodes || G.Edges.Num() != N - 1 || M < S.MinMainPathNodes || M > S.MaxMainPathNodes
        || G.Branches.Num() < S.MinBranches || G.Branches.Num() > S.MaxBranches) return Fail(TEXT("Node/edge/main/branch counts violate settings or E=N-1."));
    if (G.AlgorithmVersion != S.AlgorithmVersion || G.ConfigVersion != S.ConfigVersion
        || G.SettingsDigest != StableTextDigest(DescribeSettings(S))) return Fail(TEXT("Graph versions/settings digest mismatch."));
    if (G.StartNodeId != G.MainPath[0] || G.FinalNodeId != G.MainPath.Last() || G.StartNodeId == G.FinalNodeId)
        return Fail(TEXT("Start/Final do not bound the main path."));
    TArray<int32> Degree, Parent; Degree.Init(0, N); Parent.SetNum(N);
    TArray<bool> Members; Members.Init(false, N);
    for (int32 I = 0; I < N; ++I)
    {
        Parent[I] = I;
        if (G.Nodes[I].NodeId != I || G.Nodes[I].MaxAllowedDegree != S.MaxDegree) return Fail(TEXT("Node IDs/order or degree capacity invalid."));
    }
    if (!G.FindNode(G.StartNodeId) || !G.FindNode(G.FinalNodeId)) return Fail(TEXT("Invalid Start/Final node reference."));
    const auto Root = [&Parent](int32 I) { while (Parent[I] != I) I = Parent[I]; return I; };
    TSet<uint64> Edges, ExpectedMain, ExpectedBranch;
    for (int32 I = 0; I < G.Edges.Num(); ++I)
    {
        const auto& E = G.Edges[I];
        if (E.EdgeId != I || !G.FindNode(E.NodeA) || !G.FindNode(E.NodeB) || E.NodeA == E.NodeB
            || E.ConnectionMode != TEXT("StraightCorridor")) return Fail(TEXT("Illegal edge ID/order/reference/self-loop/connection intent."));
        const uint64 Key = EdgeKey(E.NodeA, E.NodeB);
        if (Edges.Contains(Key)) return Fail(TEXT("Duplicate undirected edge."));
        Edges.Add(Key); ++Degree[E.NodeA]; ++Degree[E.NodeB];
        const int32 RA = Root(E.NodeA), RB = Root(E.NodeB);
        if (RA == RB) return Fail(TEXT("Cycle detected."));
        Parent[RB] = RA;
    }
    for (int32 I = 0; I < N; ++I)
        if (Root(I) != Root(G.StartNodeId)) return Fail(TEXT("Disconnected node."));
        else if (Degree[I] != G.Nodes[I].Degree || Degree[I] > S.MaxDegree) return Fail(TEXT("Degree mismatch or capacity exceeded."));
    for (int32 I = 0; I < M; ++I)
    {
        const int32 Id = G.MainPath[I];
        if (!G.FindNode(Id) || Members[Id]) return Fail(TEXT("Invalid/repeated main path node."));
        Members[Id] = true;
        const auto& Node = G.Nodes[Id];
        const auto Role = I == 0 ? ERogueliteGraphNodeRole::Start : I == M - 1 ? ERogueliteGraphNodeRole::Final : ERogueliteGraphNodeRole::MainPath;
        if (Node.Role != Role || Node.MainPathOrder != I || Node.BranchId != INDEX_NONE || Node.BranchDepth != 0
            || Node.ParentNodeId != (I == 0 ? INDEX_NONE : G.MainPath[I - 1])) return Fail(TEXT("Invalid Start/MainPath/Final role or parent metadata."));
        if (I > 0) ExpectedMain.Add(EdgeKey(G.MainPath[I - 1], Id));
    }
    for (int32 I = 0; I < G.Branches.Num(); ++I)
    {
        const auto& B = G.Branches[I];
        const auto* Anchor = G.FindNode(B.AttachmentNodeId);
        if (B.BranchId != I || !Anchor || Anchor->Role == ERogueliteGraphNodeRole::Final
            || (Anchor->Role != ERogueliteGraphNodeRole::Start && Anchor->Role != ERogueliteGraphNodeRole::MainPath)
            || B.NodeIds.Num() < S.MinBranchDepth || B.NodeIds.Num() > S.MaxBranchDepth) return Fail(TEXT("Invalid branch ID/anchor/depth; Final cannot host a branch."));
        int32 Previous = B.AttachmentNodeId;
        for (int32 D = 0; D < B.NodeIds.Num(); ++D)
        {
            const int32 Id = B.NodeIds[D];
            if (!G.FindNode(Id) || Members[Id]) return Fail(TEXT("Invalid/reused branch member."));
            Members[Id] = true;
            const auto& Node = G.Nodes[Id];
            if (Node.Role != ERogueliteGraphNodeRole::Branch || Node.BranchId != I || Node.BranchDepth != D + 1
                || Node.MainPathOrder != INDEX_NONE || Node.ParentNodeId != Previous) return Fail(TEXT("Branch member metadata mismatch."));
            ExpectedBranch.Add(EdgeKey(Previous, Id)); Previous = Id;
        }
    }
    for (bool Member : Members) if (!Member) return Fail(TEXT("Node missing from main/branch ownership."));
    for (const auto& E : G.Edges)
        if (!((E.Role == ERogueliteGraphEdgeRole::MainPath && ExpectedMain.Contains(EdgeKey(E.NodeA, E.NodeB)))
            || (E.Role == ERogueliteGraphEdgeRole::OptionalBranch && ExpectedBranch.Contains(EdgeKey(E.NodeA, E.NodeB)))))
            return Fail(TEXT("Edge role/path relationship mismatch."));
    if (G.TopologyDigest != StableTextDigest(DescribeTopology(G)) || G.StableDigest != StableTextDigest(DescribeGraph(G)))
        return Fail(TEXT("Stable summary is stale or invalid."));
    Reason = TEXT("Connected tree, required main path, branch ranges, IDs, degrees and summaries passed."); return true;
}

FRogueliteGraphGenerationResult FRogueliteRoomGraphGenerator::Generate(int32 Seed, const FRogueliteGraphGenerationSettings& S)
{
    if (S.AlgorithmVersion == 2) return RogueliteGraphV2::Generate(Seed, S);
    FRogueliteGraphGenerationResult R; R.Seed = Seed;
    if (!ValidateSettings(S, R.Error, R.Reason)) return R;
    for (int32 Attempt = 0; Attempt < S.MaxAttempts; ++Attempt)
    {
        auto& A = R.Attempts.AddDefaulted_GetRef(); A.AttemptIndex = Attempt;
        A.TopologySeed = DeriveSeed(Seed, ERogueliteGraphSeedPurpose::Topology, S.AlgorithmVersion, S.ConfigVersion, 0, Attempt);
        FRandomStream Random(A.TopologySeed);
        A.MainPathNodes = Random.RandRange(S.MinMainPathNodes, S.MaxMainPathNodes);
        A.BranchCount = Random.RandRange(S.MinBranches, S.MaxBranches);
        ++R.AttemptsUsed;
        const auto Reject = [&A](const TCHAR* Why) { A.Error = ERogueliteGraphError::UnsatisfiableConstraints; A.Reason = Why; };
        if ((A.MainPathNodes > 2 && S.MaxDegree < 2) || A.BranchCount > BranchSlots(A.MainPathNodes, S.MaxDegree))
        { Reject(TEXT("Sample exceeds degree/branch attachment capacity.")); continue; }
        TArray<int32> Depths;
        int64 Total = A.MainPathNodes;
        for (int32 B = 0; B < A.BranchCount; ++B) { const int32 D = Random.RandRange(S.MinBranchDepth, S.MaxBranchDepth); Depths.Add(D); Total += D; }
        if (Total > S.MaxNodes) { Reject(TEXT("Sample exceeds MaxNodes; requested branch count/depth is not reduced.")); continue; }
        FRogueliteRoomGraph G = MakeMain(Seed, A.MainPathNodes, S);
        for (int32 Depth : Depths)
        {
            TArray<int32> Anchors;
            for (int32 Id : G.MainPath) if (Id != G.FinalNodeId && G.Nodes[Id].Degree < S.MaxDegree) Anchors.Add(Id);
            AppendBranch(G, S, Anchors[Random.RandRange(0, Anchors.Num() - 1)], Depth);
        }
        Finalize(G);
        if (!ValidateGraph(G, S, A.Reason)) { A.Error = ERogueliteGraphError::InvalidGraph; continue; }
        A.bAccepted = true; R.bSucceeded = true; R.AcceptedAttemptIndex = Attempt;
        R.Graph = MoveTemp(G); R.Reason = TEXT("Seeded candidate accepted."); return R;
    }
    R.Error = ERogueliteGraphError::AttemptExhausted;
    R.Reason = TEXT("All bounded seeded attempts rejected; see Attempts for original reasons.");
    if (!S.bEnableFallback) return R;
    R.FallbackReason = R.Reason;
    for (int32 M = S.MinMainPathNodes; M <= S.MaxMainPathNodes; ++M)
    {
        if (!FitsMinimum(M, S)) continue;
        FRogueliteRoomGraph G = MakeMain(Seed, M, S);
        for (int32 B = 0; B < S.MinBranches; ++B)
        {
            for (int32 Id : G.MainPath)
                if (Id != G.FinalNodeId && G.Nodes[Id].Degree < S.MaxDegree) { AppendBranch(G, S, Id, S.MinBranchDepth); break; }
        }
        Finalize(G);
        if (!ValidateGraph(G, S, R.Reason)) { R.Error = ERogueliteGraphError::InvalidGraph; return R; }
        R.Graph = MoveTemp(G); R.bSucceeded = true; R.bUsedFallback = true; R.Error = ERogueliteGraphError::None;
        R.Reason = TEXT("Deterministic fallback accepted; all configured minimums and hard constraints retained."); return R;
    }
    return R;
}
