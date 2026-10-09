#include "CastleDemo.h"

static int32 NodeStreamSeed(int32 RunSeed, int32 Floor, uint32 Purpose)
{
    uint32 X=uint32(RunSeed) ^ (uint32(Floor)*0x9e3779b9u) ^ Purpose;
    X^=X>>16; X*=0x7feb352du; X^=X>>15; X*=0x846ca68bu; X^=X>>16;
    return int32(X);
}

void CastleGenerateFloorNodes(int32 RunSeed,int32 Floor,TArray<FCastleRoom>& Rooms,TArray<int32>& Route)
{
    check(Rooms.Num()==16);
    FRandomStream LayoutR(NodeStreamSeed(RunSeed,Floor,0x70a91d23u));
    FRandomStream TypeR(NodeStreamSeed(RunSeed,Floor,0xb01d47a1u));
    const int32 DX[]={1,0,-1,0},DY[]={0,1,0,-1};
    auto Neighbor=[&](int32 I,int32 Side)
    {
        const int32 X=I%4+DX[Side],Y=I/4+DY[Side];
        return X>=0 && X<4 && Y>=0 && Y<4 ? Y*4+X:-1;
    };
    auto Connect=[&](int32 A,int32 B)
    {
        for(int32 Side=0;Side<4;++Side)if(Neighbor(A,Side)==B)
        {
            Rooms[A].Doors|=1<<Side; Rooms[B].Doors|=1<<((Side+2)%4);return;
        }
        checkNoEntry();
    };
    auto HasAlternative=[&]()
    {
        // An alternative entrance-to-exit route exists when removing a route edge
        // still leaves the exit reachable.
        for(int32 Edge=1;Edge<Route.Num();++Edge)
        {
            const int32 A=Route[Edge-1],B=Route[Edge];
            TSet<int32> Seen{0};TArray<int32> Queue{0};
            for(int32 Head=0;Head<Queue.Num();++Head)
            {
                const int32 I=Queue[Head];
                for(int32 Side=0;Side<4;++Side)if(Rooms[I].Doors&(1<<Side))
                {
                    const int32 N=Neighbor(I,Side);
                    if(N<0 || !Rooms[N].bActive || (I==A&&N==B) || (I==B&&N==A) || Seen.Contains(N))continue;
                    Seen.Add(N);Queue.Add(N);
                }
            }
            if(Seen.Contains(15))return true;
        }
        return false;
    };
    int32 ActiveCount=0;
    bool bValid=false;
    for(int32 Attempt=0;Attempt<64 && !bValid;++Attempt)
    {
        Route.Reset();
        for(auto& Room:Rooms){Room.Doors=0;Room.RouteOrder=-1;Room.bActive=false;}
        int32 X=0,Y=0,Previous=0;
        Rooms[0].bActive=true;Route.Add(0);
        while(X<3 || Y<3)
        {
            const bool bEast=Y==3 || (X<3 && LayoutR.RandRange(0,1)==0);
            if(bEast)++X;else ++Y;
            const int32 Next=Y*4+X;
            Rooms[Next].bActive=true;Connect(Previous,Next);Route.Add(Next);Previous=Next;
        }
        ActiveCount=7;
        const int32 Target=10+LayoutR.RandRange(0,2);
        while(ActiveCount<Target)
        {
            TArray<int32> Frontier;
            for(int32 I=0;I<16;++I)if(!Rooms[I].bActive)
                for(int32 Side=0;Side<4;++Side){const int32 N=Neighbor(I,Side);if(N>=0 && Rooms[N].bActive){Frontier.Add(I);break;}}
            if(Frontier.IsEmpty())break;
            const int32 Pick=Frontier[LayoutR.RandRange(0,Frontier.Num()-1)];
            TArray<int32> Parents;
            for(int32 Side=0;Side<4;++Side){const int32 N=Neighbor(Pick,Side);if(N>=0 && Rooms[N].bActive)Parents.Add(N);}
            Rooms[Pick].bActive=true;++ActiveCount;
            Connect(Pick,Parents[LayoutR.RandRange(0,Parents.Num()-1)]);
        }
        TArray<FIntPoint> Extra;
        for(int32 I=0;I<16;++I)if(Rooms[I].bActive)
            for(int32 Side=0;Side<2;++Side){const int32 N=Neighbor(I,Side);if(N>=0 && Rooms[N].bActive && !(Rooms[I].Doors&(1<<Side)))Extra.Add(FIntPoint(I,N));}
        for(int32 Added=0;Added<2 && !Extra.IsEmpty();++Added)
        {
            const int32 Pick=LayoutR.RandRange(0,Extra.Num()-1);
            const FIntPoint Edge=Extra[Pick];Extra.RemoveAtSwap(Pick);
            Connect(Edge.X,Edge.Y);
            if(HasAlternative())break;
        }
        bValid=ActiveCount==Target && HasAlternative();
    }
    if(!bValid)
    {
        // Fixed, verified two-route map if a seeded candidate cannot meet constraints.
        Route={0,1,2,3,7,11,15};
        for(auto& Room:Rooms){Room.Doors=0;Room.RouteOrder=-1;Room.bActive=false;}
        for(int32 I:Route)Rooms[I].bActive=true;
        for(int32 I:{4,5,6,10,14})Rooms[I].bActive=true;
        for(int32 I=1;I<Route.Num();++I)Connect(Route[I-1],Route[I]);
        for(const FIntPoint Edge:{FIntPoint(0,4),FIntPoint(4,5),FIntPoint(5,1),FIntPoint(2,6),FIntPoint(6,10),FIntPoint(10,14),FIntPoint(14,15)})Connect(Edge.X,Edge.Y);
        ActiveCount=12;
        UE_LOG(LogTemp,Warning,TEXT("CASTLE_LAYOUT_FALLBACK Seed=%d"),RunSeed);
    }
    static const ECastleRoom Main[]={ECastleRoom::Entrance,ECastleRoom::Combat,ECastleRoom::Combat,
        ECastleRoom::Shop,ECastleRoom::Combat,ECastleRoom::Rest,ECastleRoom::Exit};
    for(int32 Order=0;Order<7;++Order)
    {
        const int32 I=Route[Order];Rooms[I].Type=Main[Order];Rooms[I].RouteOrder=Order;
    }
    TArray<int32> Sides;
    for(int32 I=0;I<16;++I)if(Rooms[I].bActive && Rooms[I].RouteOrder<0)Sides.Add(I);
    for(int32 I=Sides.Num()-1;I>0;--I)Sides.Swap(I,TypeR.RandRange(0,I));
    ECastleRoom Specials[]={ECastleRoom::Event,ECastleRoom::Elite,ECastleRoom::Trap};
    for(int32 I=2;I>0;--I)Swap(Specials[I],Specials[TypeR.RandRange(0,I)]);
    Rooms[Sides[0]].Type=ECastleRoom::Chest;
    Rooms[Sides[1]].Type=Specials[0];
    Rooms[Sides[2]].Type=ECastleRoom::Combat;
    if(Sides.Num()>3)Rooms[Sides[3]].Type=Specials[1];
    if(Sides.Num()>4)Rooms[Sides[4]].Type=ECastleRoom::Combat;
    uint32 Hash=2166136261u;
    int32 Links=0;
    for(const auto& Room:Rooms)if(Room.bActive)
    {
        Hash=(Hash^uint32(Room.Doors+16*int32(Room.Type)))*16777619u;
        for(int32 Side=0;Side<4;++Side)if(Room.Doors&(1<<Side))++Links;
    }
    UE_LOG(LogTemp,Display,TEXT("CASTLE_FLOOR_ROUTE Floor=%d Nodes=%d Links=%d Exit=15 Hash=%u"),Floor,ActiveCount,Links/2,Hash);
}
