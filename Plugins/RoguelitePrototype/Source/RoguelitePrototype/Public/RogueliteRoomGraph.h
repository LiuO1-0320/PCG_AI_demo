#pragma once

#include "CoreMinimal.h"

// Plain Core data: no World, Actor, UObject, PCG, socket reservations or spatial coordinates.
enum class ERogueliteGraphNodeRole : uint8 { Start = 0, MainPath = 1, Branch = 2, Final = 3 };
enum class ERogueliteGraphEdgeRole : uint8 { MainPath = 0, OptionalBranch = 1, Loop = 2 }; // Loop intent reserved; V1/V2 reject it.
enum class ERogueliteGraphSeedPurpose : uint32
{
    Topology = 0x70a91d23u,
    RoomType = 0xb01d47a1u,
    TemplateSelection = 0x6c8e9cf5u,
    Encounter = 0x51d7348bu,
    RoomContent = 0x9e56d8abu
};
enum class ERogueliteGraphError : uint8
{
    None, InvalidSettings, UnsupportedAlgorithm, UnsatisfiableConstraints, AttemptExhausted, InvalidGraph
};

struct ROGUELITEPROTOTYPE_API FRogueliteGraphGenerationSettings
{
    int32 MinMainPathNodes = 5; // Includes Start and Final.
    int32 MaxMainPathNodes = 8;
    int32 MinBranches = 2;
    int32 MaxBranches = 4;
    int32 MinBranchDepth = 1; // Each branch is a chain attached to a non-Final main node.
    int32 MaxBranchDepth = 3; // Depth equals the number of new nodes in that chain.
    int32 MaxNodes = 24;
    int32 MaxDegree = 3; // Logical capacity only; template socket capacity is checked in B2.
    int32 MaxAttempts = 8;
    int32 AlgorithmVersion = 1; // V1 compatibility default; V2 is explicit opt-in.
    int32 ConfigVersion = 1;
    bool bEnableFallback = true;
    // V2 only; excluded from every V1 random draw, validation and summary.
    int32 MinNodes = 2;
    int32 MinDecisionNodes = 0;
    int32 MaxDecisionNodes = 24;
    int32 MinNestedDecisionNodes = 0;
    int32 MaxBranchGenerationDepth = 3;
    int32 RecursiveBranchWeight = 3;
};

struct ROGUELITEPROTOTYPE_API FRogueliteRoomGraphNode
{
    int32 NodeId = INDEX_NONE;
    ERogueliteGraphNodeRole Role = ERogueliteGraphNodeRole::MainPath;
    int32 MainPathOrder = INDEX_NONE;
    int32 BranchId = INDEX_NONE;
    int32 BranchDepth = 0;
    int32 ParentNodeId = INDEX_NONE;
    int32 Degree = 0;
    int32 MaxAllowedDegree = 0;
    int32 BranchGenerationDepth = 0; // Main=0; a chain attached to generation D has generation D+1.
    int32 ChildCount = 0; // V2: rooted at Start, independently validated against Edges.
    bool bDecisionNode = false; // ChildCount >= 2, not total degree or branch count.
};

struct ROGUELITEPROTOTYPE_API FRogueliteRoomGraphEdge
{
    int32 EdgeId = INDEX_NONE;
    int32 NodeA = INDEX_NONE;
    int32 NodeB = INDEX_NONE;
    ERogueliteGraphEdgeRole Role = ERogueliteGraphEdgeRole::MainPath;
    // Intent only. B2 maps this to the existing ERogueliteConnectionMode enum.
    // Keeping the label here avoids including RoomAssembly's Actor-dependent header.
    FString ConnectionMode = TEXT("StraightCorridor");
};

struct ROGUELITEPROTOTYPE_API FRogueliteRoomGraphBranch
{
    int32 BranchId = INDEX_NONE;
    int32 AttachmentNodeId = INDEX_NONE;
    TArray<int32> NodeIds;
    int32 GenerationDepth = 0; // V2 only. BranchDepth remains the position along this chain.
};

struct ROGUELITEPROTOTYPE_API FRogueliteRoomGraph
{
    int32 Seed = 0;
    int32 AlgorithmVersion = 0;
    int32 ConfigVersion = 0;
    int32 StartNodeId = INDEX_NONE;
    int32 FinalNodeId = INDEX_NONE;
    TArray<FRogueliteRoomGraphNode> Nodes;
    TArray<FRogueliteRoomGraphEdge> Edges;
    TArray<int32> MainPath;
    TArray<FRogueliteRoomGraphBranch> Branches;
    FString SettingsDigest;
    FString TopologyDigest; // Shape/roles/order, excluding Seed and configuration.
    FString StableDigest;   // Includes Seed, versions and full settings digest.

    const FRogueliteRoomGraphNode* FindNode(int32 NodeId) const;
    TArray<int32> GetNeighborIds(int32 NodeId) const;
    bool IsReachable(int32 FromNodeId, int32 ToNodeId) const;
};

struct ROGUELITEPROTOTYPE_API FRogueliteGraphAttempt
{
    int32 AttemptIndex = INDEX_NONE; // Zero-based.
    int32 TopologySeed = 0;
    int32 MainPathNodes = 0;
    int32 BranchCount = 0;
    bool bAccepted = false;
    ERogueliteGraphError Error = ERogueliteGraphError::None;
    FString Reason;
};

struct ROGUELITEPROTOTYPE_API FRogueliteGraphGenerationResult
{
    bool bSucceeded = false;
    bool bUsedFallback = false;
    int32 Seed = 0;
    int32 AttemptsUsed = 0;
    int32 AcceptedAttemptIndex = INDEX_NONE; // INDEX_NONE when fallback/failure.
    ERogueliteGraphError Error = ERogueliteGraphError::None;
    FString Reason;
    FString FallbackReason;
    TArray<FRogueliteGraphAttempt> Attempts; // Includes every rejected candidate.
    FRogueliteRoomGraph Graph; // Empty on failure; never a partial candidate.
};

class ROGUELITEPROTOTYPE_API FRogueliteRoomGraphGenerator
{
public:
    static FRogueliteGraphGenerationResult Generate(int32 Seed, const FRogueliteGraphGenerationSettings& Settings);
    static bool ValidateSettings(const FRogueliteGraphGenerationSettings& Settings, ERogueliteGraphError& Error, FString& Reason);
    static bool ValidateGraph(const FRogueliteRoomGraph& Graph, const FRogueliteGraphGenerationSettings& Settings, FString& Reason);
    static int32 DeriveSeed(int32 Seed, ERogueliteGraphSeedPurpose Purpose, int32 AlgorithmVersion = 1,
        int32 ConfigVersion = 1, int32 StableId = 0, int32 AttemptIndex = 0);
    static FString DescribeSettings(const FRogueliteGraphGenerationSettings& Settings);
    static FString DescribeTopology(const FRogueliteRoomGraph& Graph);
    static FString DescribeGraph(const FRogueliteRoomGraph& Graph);
    static FString StableTextDigest(const FString& Text);
};
