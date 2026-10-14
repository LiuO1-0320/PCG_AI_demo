#include "RogueliteRoomGraphV2.h"
#include "Math/RandomStream.h"

namespace RogueliteGraphV2
{
namespace
{
using Gen=FRogueliteRoomGraphGenerator;
constexpr int32 FallbackStateLimit=16384;
uint64 Key(int32 A,int32 B){return (uint64(uint32(FMath::Min(A,B)))<<32)|uint32(FMath::Max(A,B));}
void Edge(FRogueliteRoomGraph& G,int32 A,int32 B,ERogueliteGraphEdgeRole Role)
{
    auto& E=G.Edges.AddDefaulted_GetRef();E.EdgeId=G.Edges.Num()-1;E.NodeA=A;E.NodeB=B;E.Role=Role;
    ++G.Nodes[A].Degree;++G.Nodes[B].Degree;++G.Nodes[A].ChildCount;G.Nodes[A].bDecisionNode=G.Nodes[A].ChildCount>=2;
}
FRogueliteRoomGraph Main(int32 Seed,int32 Count,const FRogueliteGraphGenerationSettings& S)
{
    FRogueliteRoomGraph G;G.Seed=Seed;G.AlgorithmVersion=2;G.ConfigVersion=S.ConfigVersion;
    G.SettingsDigest=Gen::StableTextDigest(DescribeSettings(S));G.StartNodeId=0;G.FinalNodeId=Count-1;
    for(int32 I=0;I<Count;++I)
    {
        auto& N=G.Nodes.AddDefaulted_GetRef();N.NodeId=I;N.MainPathOrder=I;N.ParentNodeId=I-1;N.MaxAllowedDegree=S.MaxDegree;
        N.Role=I==0?ERogueliteGraphNodeRole::Start:I==Count-1?ERogueliteGraphNodeRole::Final:ERogueliteGraphNodeRole::MainPath;
        G.MainPath.Add(I);if(I)Edge(G,I-1,I,ERogueliteGraphEdgeRole::MainPath);
    }
    return G;
}
TArray<int32> Anchors(const FRogueliteRoomGraph& G,const FRogueliteGraphGenerationSettings& S)
{
    TArray<int32> Out;
    for(const auto& N:G.Nodes)
        if(N.NodeId!=G.FinalNodeId && N.Degree<S.MaxDegree && N.BranchGenerationDepth<S.MaxBranchGenerationDepth)Out.Add(N.NodeId);
    return Out; // Stable NodeId order, never a set/map iteration.
}
void Branch(FRogueliteRoomGraph& G,const FRogueliteGraphGenerationSettings& S,int32 Anchor,int32 Length)
{
    const int32 Id=G.Branches.Num(),Level=G.Nodes[Anchor].BranchGenerationDepth+1;
    auto& B=G.Branches.AddDefaulted_GetRef();B.BranchId=Id;B.AttachmentNodeId=Anchor;B.GenerationDepth=Level;
    int32 Parent=Anchor;
    for(int32 D=1;D<=Length;++D)
    {
        const int32 IdN=G.Nodes.Num();auto& N=G.Nodes.AddDefaulted_GetRef();N.NodeId=IdN;N.Role=ERogueliteGraphNodeRole::Branch;
        N.BranchId=Id;N.BranchDepth=D;N.BranchGenerationDepth=Level;N.ParentNodeId=Parent;N.MaxAllowedDegree=S.MaxDegree;
        B.NodeIds.Add(IdN);Edge(G,Parent,IdN,ERogueliteGraphEdgeRole::OptionalBranch);Parent=IdN;
    }
}
void Finalize(FRogueliteRoomGraph& G)
{
    G.TopologyDigest=Gen::StableTextDigest(DescribeTopology(G));G.StableDigest=Gen::StableTextDigest(Gen::DescribeGraph(G));
}
int32 Decisions(const FRogueliteRoomGraph& G,bool Nested=false)
{
    int32 Count=0;for(const auto& N:G.Nodes)Count+=N.bDecisionNode && (!Nested || N.Role==ERogueliteGraphNodeRole::Branch);return Count;
}
bool Fallback(FRogueliteRoomGraph G,int32 Target,const FRogueliteGraphGenerationSettings& S,int32& States,FRogueliteRoomGraph& Out)
{
    if(++States>FallbackStateLimit || Decisions(G)>S.MaxDecisionNodes || G.Nodes.Num()>S.MaxNodes)return false;
    const int32 Left=Target-G.Branches.Num();
    if(G.Nodes.Num()+Left*S.MinBranchDepth>S.MaxNodes || G.Nodes.Num()+Left*S.MaxBranchDepth<S.MinNodes)return false;
    if(!Left){Finalize(G);FString Reason;if(ValidateGraph(G,S,Reason)){Out=MoveTemp(G);return true;}return false;}
    const auto Free=Anchors(G,S);
    for(int32 Anchor:Free)for(int32 Length=S.MinBranchDepth;Length<=S.MaxBranchDepth;++Length)
    {
        if(States>=FallbackStateLimit)return false;
        if(G.Nodes.Num()+Length>S.MaxNodes)break;
        auto Candidate=G;Branch(Candidate,S,Anchor,Length);
        if(Fallback(MoveTemp(Candidate),Target,S,States,Out))return true;
    }
    return false;
}
}
FString DescribeSettings(const FRogueliteGraphGenerationSettings& S)
{
    return FString::Printf(TEXT("B4SettingsV2|main=%d,%d|branches=%d,%d|chain=%d,%d|nodes=%d,%d|degree=%d|decisions=%d,%d|nested=%d|generation=%d|recursiveWeight=%d|attempts=%d|algorithm=%d|config=%d|fallback=%d|fallbackStates=%d"),
        S.MinMainPathNodes,S.MaxMainPathNodes,S.MinBranches,S.MaxBranches,S.MinBranchDepth,S.MaxBranchDepth,S.MinNodes,S.MaxNodes,S.MaxDegree,
        S.MinDecisionNodes,S.MaxDecisionNodes,S.MinNestedDecisionNodes,S.MaxBranchGenerationDepth,S.RecursiveBranchWeight,S.MaxAttempts,S.AlgorithmVersion,S.ConfigVersion,int32(S.bEnableFallback),FallbackStateLimit);
}
FString DescribeTopology(const FRogueliteRoomGraph& G)
{
    FString T=FString::Printf(TEXT("B4TopologyV2|start=%d|final=%d|main="),G.StartNodeId,G.FinalNodeId);
    for(int32 Id:G.MainPath)T+=FString::Printf(TEXT("%d,"),Id);
    for(const auto& N:G.Nodes)T+=FString::Printf(TEXT("|n=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d"),N.NodeId,int32(N.Role),N.MainPathOrder,N.BranchId,N.BranchDepth,N.ParentNodeId,N.Degree,N.MaxAllowedDegree,N.BranchGenerationDepth,N.ChildCount,N.bDecisionNode);
    for(const auto& E:G.Edges)T+=FString::Printf(TEXT("|e=%d,%d,%d,%d,%s"),E.EdgeId,E.NodeA,E.NodeB,int32(E.Role),*E.ConnectionMode);
    for(const auto& B:G.Branches){T+=FString::Printf(TEXT("|b=%d,%d,%d:"),B.BranchId,B.AttachmentNodeId,B.GenerationDepth);for(int32 Id:B.NodeIds)T+=FString::Printf(TEXT("%d,"),Id);}
    return T;
}
bool ValidateSettings(const FRogueliteGraphGenerationSettings& S,ERogueliteGraphError& Error,FString& Reason)
{
    Error=ERogueliteGraphError::InvalidSettings;
    if(S.AlgorithmVersion!=2 || S.ConfigVersion<1 || S.MinMainPathNodes<2 || S.MaxMainPathNodes<S.MinMainPathNodes || S.MaxMainPathNodes>256
        || S.MinBranches<0 || S.MaxBranches<S.MinBranches || S.MaxBranches>256 || S.MinBranchDepth<1 || S.MaxBranchDepth<S.MinBranchDepth || S.MaxBranchDepth>256
        || S.MinNodes<2 || S.MaxNodes<S.MinNodes || S.MaxNodes>256 || S.MaxDegree<1 || S.MaxDegree>4 || S.MaxAttempts<1 || S.MaxAttempts>128
        || S.MinDecisionNodes<0 || S.MaxDecisionNodes<S.MinDecisionNodes || S.MaxDecisionNodes>256 || S.MinNestedDecisionNodes<0 || S.MinNestedDecisionNodes>S.MaxDecisionNodes
        || S.MaxBranchGenerationDepth<1 || S.MaxBranchGenerationDepth>256 || S.RecursiveBranchWeight<0 || S.RecursiveBranchWeight>10000)
    {Reason=TEXT("V2 invalid ranges: nodes/main/chains/branches<=256, ordered minima/maxima, degree 1..4, attempts 1..128, nested minimum<=decision maximum.");return false;}
    if(S.MinMainPathNodes+int64(S.MinBranches)*S.MinBranchDepth>S.MaxNodes || S.MaxMainPathNodes+int64(S.MaxBranches)*S.MaxBranchDepth<S.MinNodes
        || 2*S.MinDecisionNodes+1>S.MaxNodes || (S.MaxDegree==1 && (S.MinMainPathNodes>2 || S.MinBranches>0 || S.MinDecisionNodes>0))
        || (S.MaxDegree==2 && (S.MinDecisionNodes>1 || S.MinNestedDecisionNodes>0))
        || (S.MinNestedDecisionNodes>0 && (S.MaxBranchGenerationDepth<2 || S.MaxBranches<2)))
    {Error=ERogueliteGraphError::UnsatisfiableConstraints;Reason=TEXT("V2 minimum node/decision/recursive-branch budgets cannot fit degree, chain or generation capacity; minima are not reduced.");return false;}
    Error=ERogueliteGraphError::None;Reason.Reset();return true;
}
bool ValidateGraph(const FRogueliteRoomGraph& G,const FRogueliteGraphGenerationSettings& S,FString& Reason)
{
    ERogueliteGraphError Error;if(!ValidateSettings(S,Error,Reason))return false;
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};const int32 N=G.Nodes.Num();
    if(N<S.MinNodes || N>S.MaxNodes || G.Edges.Num()!=N-1 || G.MainPath.Num()<S.MinMainPathNodes || G.MainPath.Num()>S.MaxMainPathNodes
        || G.Branches.Num()<S.MinBranches || G.Branches.Num()>S.MaxBranches || G.AlgorithmVersion!=2 || G.ConfigVersion!=S.ConfigVersion
        || G.SettingsDigest!=Gen::StableTextDigest(DescribeSettings(S)))return Fail(TEXT("V2 counts/versions/settings or E=N-1 mismatch."));
    if(G.StartNodeId!=0 || !G.FindNode(G.FinalNodeId) || G.MainPath[0]!=G.StartNodeId || G.MainPath.Last()!=G.FinalNodeId)return Fail(TEXT("V2 Start/Final reference invalid."));
    TArray<int32> Degree,Children;Degree.Init(0,N);Children.Init(0,N);TArray<bool> Owned;Owned.Init(false,N);TSet<uint64> Keys;
    for(int32 I=0;I<N;++I)if(G.Nodes[I].NodeId!=I || G.Nodes[I].MaxAllowedDegree!=S.MaxDegree)return Fail(TEXT("V2 stable node order/capacity invalid."));
    for(int32 I=0;I<G.Edges.Num();++I)
    {
        const auto& E=G.Edges[I];if(E.EdgeId!=I || !G.FindNode(E.NodeA) || !G.FindNode(E.NodeB) || E.NodeA>=E.NodeB || G.Nodes[E.NodeB].ParentNodeId!=E.NodeA
            || E.ConnectionMode!=TEXT("StraightCorridor") || (E.Role!=ERogueliteGraphEdgeRole::MainPath && E.Role!=ERogueliteGraphEdgeRole::OptionalBranch))return Fail(TEXT("V2 edge order/parent/reference/mode invalid; Loop not implemented."));
        const uint64 K=Key(E.NodeA,E.NodeB);if(Keys.Contains(K))return Fail(TEXT("V2 duplicate edge."));Keys.Add(K);++Degree[E.NodeA];++Degree[E.NodeB];++Children[E.NodeA];
    }
    for(int32 I=0;I<G.MainPath.Num();++I)
    {
        const int32 Id=G.MainPath[I];if(!G.FindNode(Id) || Owned[Id])return Fail(TEXT("V2 main ownership invalid."));Owned[Id]=true;const auto& Node=G.Nodes[Id];
        const auto Role=I==0?ERogueliteGraphNodeRole::Start:I==G.MainPath.Num()-1?ERogueliteGraphNodeRole::Final:ERogueliteGraphNodeRole::MainPath;
        if(Node.Role!=Role || Node.MainPathOrder!=I || Node.BranchId!=INDEX_NONE || Node.BranchDepth!=0 || Node.BranchGenerationDepth!=0
            || Node.ParentNodeId!=(I?G.MainPath[I-1]:INDEX_NONE))return Fail(TEXT("V2 main metadata invalid."));
    }
    for(int32 I=0;I<G.Branches.Num();++I)
    {
        const auto& B=G.Branches[I];const auto* A=G.FindNode(B.AttachmentNodeId);
        if(B.BranchId!=I || !A || A->NodeId==G.FinalNodeId || B.GenerationDepth!=A->BranchGenerationDepth+1 || B.GenerationDepth>S.MaxBranchGenerationDepth
            || B.NodeIds.Num()<S.MinBranchDepth || B.NodeIds.Num()>S.MaxBranchDepth)return Fail(TEXT("V2 recursive branch ownership/generation/chain length invalid."));
        int32 Previous=B.AttachmentNodeId;
        for(int32 D=0;D<B.NodeIds.Num();++D)
        {
            const int32 Id=B.NodeIds[D];if(!G.FindNode(Id) || Owned[Id])return Fail(TEXT("V2 branch member missing/reused."));Owned[Id]=true;const auto& Node=G.Nodes[Id];
            if(Node.Role!=ERogueliteGraphNodeRole::Branch || Node.BranchId!=I || Node.MainPathOrder!=INDEX_NONE || Node.BranchDepth!=D+1
                || Node.ParentNodeId!=Previous || Node.BranchGenerationDepth!=B.GenerationDepth)return Fail(TEXT("V2 branch member metadata mismatch."));Previous=Id;
        }
    }
    for(const auto& E:G.Edges)if((E.Role==ERogueliteGraphEdgeRole::MainPath)!=(G.Nodes[E.NodeB].Role!=ERogueliteGraphNodeRole::Branch))return Fail(TEXT("V2 edge role mismatch."));
    for(int32 I=0;I<N;++I)if(!Owned[I] || Degree[I]!=G.Nodes[I].Degree || Degree[I]>S.MaxDegree || Children[I]!=G.Nodes[I].ChildCount
        || G.Nodes[I].bDecisionNode!=(Children[I]>=2) || (I!=0 && G.Nodes[I].ParentNodeId<0))return Fail(TEXT("V2 degree/children/DecisionNode/ownership invalid."));
    if(Degree[G.FinalNodeId]!=1 || Children[G.FinalNodeId]!=0 || Decisions(G)<S.MinDecisionNodes || Decisions(G)>S.MaxDecisionNodes
        || Decisions(G,true)<S.MinNestedDecisionNodes)return Fail(TEXT("V2 Final leaf or decision minima/maxima violated."));
    for(const auto& Node:G.Nodes)if(!G.IsReachable(G.StartNodeId,Node.NodeId))return Fail(TEXT("V2 disconnected graph."));
    if(G.TopologyDigest!=Gen::StableTextDigest(DescribeTopology(G)) || G.StableDigest!=Gen::StableTextDigest(Gen::DescribeGraph(G)))return Fail(TEXT("V2 stale topology/result summary."));
    Reason=TEXT("V2 connected Tree, Final leaf, recursive chain/generation and rooted DecisionNode budgets passed; no loop/reconvergence claim.");return true;
}
FRogueliteGraphGenerationResult Generate(int32 Seed,const FRogueliteGraphGenerationSettings& S)
{
    FRogueliteGraphGenerationResult R;R.Seed=Seed;if(!ValidateSettings(S,R.Error,R.Reason))return R;
    for(int32 Attempt=0;Attempt<S.MaxAttempts;++Attempt)
    {
        auto& A=R.Attempts.AddDefaulted_GetRef();A.AttemptIndex=Attempt;A.TopologySeed=Gen::DeriveSeed(Seed,ERogueliteGraphSeedPurpose::Topology,2,S.ConfigVersion,0,Attempt);
        FRandomStream Random(A.TopologySeed);A.MainPathNodes=Random.RandRange(S.MinMainPathNodes,S.MaxMainPathNodes);A.BranchCount=Random.RandRange(S.MinBranches,S.MaxBranches);++R.AttemptsUsed;
        auto G=Main(Seed,A.MainPathNodes,S);bool Built=true;
        for(int32 B=0;B<A.BranchCount;++B)
        {
            const int32 Length=Random.RandRange(S.MinBranchDepth,S.MaxBranchDepth);const auto Pool=Anchors(G,S);
            int32 Sum=0;for(int32 Id:Pool)Sum+=G.Nodes[Id].Role==ERogueliteGraphNodeRole::Branch?S.RecursiveBranchWeight:1;
            if(!Sum || G.Nodes.Num()+Length>S.MaxNodes){Built=false;A.Reason=FString::Printf(TEXT("Branch=%d exhausted weighted anchors or node budget; sampled minima are not trimmed."),B);break;}
            int32 Pick=Random.RandRange(1,Sum),Anchor=INDEX_NONE;
            for(int32 Id:Pool)if((Pick-=G.Nodes[Id].Role==ERogueliteGraphNodeRole::Branch?S.RecursiveBranchWeight:1)<=0){Anchor=Id;break;}
            Branch(G,S,Anchor,Length);
        }
        Finalize(G);if(Built && ValidateGraph(G,S,A.Reason)){A.bAccepted=true;R.bSucceeded=true;R.AcceptedAttemptIndex=Attempt;R.Graph=MoveTemp(G);R.Reason=TEXT("V2 seeded recursive Tree accepted.");return R;}
        A.Error=ERogueliteGraphError::UnsatisfiableConstraints;
    }
    R.Error=ERogueliteGraphError::AttemptExhausted;R.Reason=TEXT("V2 finite seeded candidates exhausted; original attempt diagnostics retained.");
    if(!S.bEnableFallback)return R;R.FallbackReason=R.Reason;int32 States=0;
    for(int32 M=S.MinMainPathNodes;M<=S.MaxMainPathNodes && States<FallbackStateLimit;++M)
        for(int32 B=S.MinBranches;B<=S.MaxBranches && States<FallbackStateLimit;++B)
            if(Fallback(Main(Seed,M,S),B,S,States,R.Graph)){R.bSucceeded=true;R.bUsedFallback=true;R.Error=ERogueliteGraphError::None;
                R.Reason=FString::Printf(TEXT("V2 deterministic bounded fallback accepted after %d states; all configured minimums retained."),States);return R;}
    R.Reason+=FString::Printf(TEXT(" Fallback searched %d/%d states without a valid graph; constraints not relaxed."),States,FallbackStateLimit);return R;
}
}
