/*=============================================================================
	PathBuilder.cpp
	Author: Fernando Vel�zquez

	Unreal Editor addon for path network generation.

	The goal is to be able to build paths with as little flaws or technical
	limitations as possible.
	In order to achieve this, it'll be necessary to evaluate the same nodes
	multiple times, which means saving and loading states on demand.

	First, we must process paths by proximity, it is necessary that paths that
	are the nearest to each other are processed first so that pruning isn't
	needed.

	In order to prevent constant relocations we'll need to build a relation
	list where nodes are paired, sorted by distance.
=============================================================================*/

#include "XC_Core.h"
#include "Engine.h"
#include "XC_CoreGlobals.h"
#include "API_FunctionLoader.h"

#define MAX_DISTANCE 1000
#define MAX_WEIGHT 10000000

enum EReachSpecFlags
{
	R_WALK = 1,	//walking required
	R_FLY = 2,   //flying required 
	R_SWIM = 4,  //swimming required
	R_JUMP = 8,   // jumping required
	R_DOOR = 16,
	R_SPECIAL = 32,
	R_PLAYERONLY = 64
};

#define MAX_SCOUT_HEIGHT 200
#define MAX_SCOUT_RADIUS 150


//============== Partial actor list compactor
//
static void CompactActors( TTransArray<AActor*>& Actors, int32 StartFrom)
{
	Actors.ModifyAllItems();
	FTransactionBase* Undo = GUndo;
	GUndo = nullptr;

	int32 i=StartFrom;
	for ( int32 j=StartFrom ; j<Actors.Num() ; j++ )
	{
		GWarn->StatusUpdatef( j-StartFrom, Actors.Num()-StartFrom, TEXT("Compacting actor list (%i > %i)"), j, i);
		if ( (i != j) && Actors(j) )
			Actors(i++) = Actors(j);
	}
	if( i != Actors.Num() )
		Actors.Remove( i, Actors.Num()-i );

	GUndo = Undo;
	Actors.ModifyAllItems();
}

//============== Cleanup a NavigationPoint
//
static void Cleanup( ANavigationPoint* N)
{
	N->nextNavigationPoint = nullptr;
	N->nextOrdered         = nullptr;
	N->prevOrdered         = nullptr;
	N->startPath           = nullptr;
	N->previousPath        = nullptr;
	N->OtherTag            = 0;
	N->visitedWeight       = 0;
	N->bestPathWeight      = 0;
	for ( int32 i=0; i<16; i++)
	{
		N->Paths[i]           = -1;
		N->upstreamPaths[i]   = -1;
		N->PrunedPaths[i]     = -1;
		N->VisNoReachPaths[i] = nullptr;
	}
}

//============== Counts amount of paths in array
//
static int CountPaths( int32* PathArray)
{
	int i;
	for ( i=0 ; i<16 && PathArray[i]>=0 ; i++ );
	return i;
}

//============== Gets a free slot in a Paths array (Paths, PrunedPaths, upstreamPaths)
//
static int FreePath( int32* PathArray)
{
	for ( int i=0 ; i<16 ; i++ )
		if ( PathArray[i] < 0 )
			return i;
	return INDEX_NONE;
}



//============== Sorted linked list element
//
template < class T > struct TUpwardsSortableLinkedRef
{
	T* Ref;
	TUpwardsSortableLinkedRef<T>* Next;

	TUpwardsSortableLinkedRef<T>* SortThis( int (*CompareFunc)(const T&, const T&) )
	{
		TUpwardsSortableLinkedRef<T>* Last = this;
		for ( TUpwardsSortableLinkedRef<T>* CompareTo=Next ; CompareTo ; Last=CompareTo, CompareTo=CompareTo->Next )
			if ( (*CompareFunc)(*this->Ref,*CompareTo->Ref) == 1 ) //Stop here
				break;

		if ( Last != this )
		{
			TUpwardsSortableLinkedRef<T>* Result = Next;
			Next = Last->Next;
			Last->Next = this;
			return Result;
		}
		return this;
	}
};


class FPathBuilderInfo
{
	struct Candidate
	{
		ANavigationPoint* Path;
		float DistSq;
	};
public:
	ANavigationPoint* Owner;
	TArray<Candidate> Candidates;

	//[+]=A stays, [-]=A goes forward in link
	static int Compare( const FPathBuilderInfo& A, const FPathBuilderInfo& B)
	{
		if ( !B.Candidates.Num() )                             return  1; //Whether A has candidates or not is irrelevant
		if ( !A.Candidates.Num() )                             return -1;
		if ( A.Candidates(0).DistSq > B.Candidates(0).DistSq ) return -1;
		return 1;
	}
};
static int32 InfoListRaw[3] = {0,0,0};
static TArray<FPathBuilderInfo>& InfoList = *(TArray<FPathBuilderInfo>*)InfoListRaw;
static void RegisterInfo( ANavigationPoint* N);
typedef TUpwardsSortableLinkedRef<FPathBuilderInfo> TPathInfoLink;



//============== Engine.dll manual imports
//
class FPathBuilder;
static int32 (FPathBuilder::*findScoutStart)( FVector) = 0;
static void  (FPathBuilder::*getScout)(void) = 0;

class ENGINE_API FPathBuilder
{
public:
	ULevel*                  Level;
	APawn*                   Scout;
//	int32                    Pad[16];
};


class FPathBuilderMaster : public FPathBuilder
{
public:
	float                    GoodDistance; //Up to 2x during lookup
	float                    GoodHeight;
	float                    GoodRadius;
	float                    GoodJumpZ;
	float                    GoodGroundSpeed;
	int32                    Aerial;
	UClass*                  InventorySpotClass;
	UClass*                  WarpZoneMarkerClass;
	int32                    TotalCandidates;
	FString                  BuildResult;

	FPathBuilderMaster();
	void RebuildPaths();
private:
	void Setup();
	void DefinePaths();
	void UndefinePaths();

	void AddMarkers();
	void DefineSpecials();
	void BuildCandidatesLists();
	void ProcessCandidatesLists();

	void HandleInventory( AInventory* Inv);
	void HandleWarpZone( AWarpZoneInfo* Info);

	void DefineFor( ANavigationPoint* A, ANavigationPoint* B);
	FReachSpec CreateSpec( ANavigationPoint* Start, ANavigationPoint* End);
	int AttachReachSpec( const FReachSpec& Spec, int32 bPrune=0);

	void GetScout();
	int FindStart( FVector V);
};

FString PathsRebuild( ULevel* Level, APawn* ScoutReference, UBOOL bBuildAir)
{
	FPathBuilderMaster Builder;
	if ( ScoutReference )
	{
		Builder.GoodRadius      = ScoutReference->CollisionRadius;
		Builder.GoodHeight      = ScoutReference->CollisionHeight;
		Builder.GoodJumpZ       = ScoutReference->JumpZ;
		Builder.GoodGroundSpeed = ScoutReference->GroundSpeed;
	}
	Builder.Level = Level;
	Builder.Aerial = bBuildAir;
	Builder.RebuildPaths();
	return Builder.BuildResult;
}

//============== FPathBuilderMaster main funcs
//
inline FPathBuilderMaster::FPathBuilderMaster()
{
	appMemzero( this, sizeof(*this));
}


inline void FPathBuilderMaster::RebuildPaths()
{
	guard(FPathBuilderMaster::RebuildPaths)
	GWarn->BeginSlowTask( TEXT("Paths Rebuild [XC]"), true, false);
	Setup();
	GetScout();
	UndefinePaths();
	DefinePaths();
	if ( InfoList.Num() > 0 )
		InfoList.Empty();
	if ( Scout )
		Level->DestroyActor( Scout);
	GWarn->EndSlowTask();
	unguard
}



//============== FPathBuilderMaster internals
//============== Build steps
//
inline void FPathBuilderMaster::Setup()
{
	guard(FPathBuilderMaster::Setup)
	debugf( NAME_DevPath, TEXT("Setting up builder..."));
	if ( GoodDistance < 200 )	GoodDistance = 1000;
	if ( GoodHeight <= 5 )		GoodHeight = 39;
	if ( GoodRadius <= 5 )		GoodRadius = 17;
	if ( GoodJumpZ <= 5 )       GoodJumpZ = 325;
	if ( GoodGroundSpeed <= 5 ) GoodGroundSpeed = 400;
	if ( !InventorySpotClass )		InventorySpotClass  = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("InventorySpot") );
	if ( !WarpZoneMarkerClass )		WarpZoneMarkerClass = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("WarpZoneMarker") );
	if ( InfoList.Num() > 0 )	InfoList.Empty();

#if _WINDOWS
	HMODULE hEngine = GetModuleHandleA( "Engine.dll");
	Get( findScoutStart, hEngine, "?findScoutStart@FPathBuilder@@AAEHVFVector@@@Z");
	Get( getScout      , hEngine, "?getScout@FPathBuilder@@AAEXXZ");
#else
	void* hGlobal = dlopen( NULL, RTLD_NOW | RTLD_GLOBAL);
	Get( findScoutStart, hGlobal, "findScoutStart__12FPathBuilderG7FVector");
	Get( getScout      , hGlobal, "getScout__12FPathBuilder");
#endif
	check( findScoutStart );
	check( getScout );
	TotalCandidates = 0;
	unguard
}



inline void FPathBuilderMaster::DefinePaths()
{
	debugf( NAME_DevPath, TEXT("Defining paths..."));

	// Setup initial list
	for ( int32 i=0 ; i<Level->Actors.Num() ; i++ )
		if ( Level->Actors(i) && Level->Actors(i)->IsA(ANavigationPoint::StaticClass()) )
			RegisterInfo( (ANavigationPoint*)Level->Actors(i));

	AddMarkers();
	DefineSpecials();
	BuildCandidatesLists();
	ProcessCandidatesLists();
	BuildResult += FString::Printf( TEXT("Created %i reachSpecs."), Level->ReachSpecs.Num() );
}

inline void FPathBuilderMaster::UndefinePaths()
{
	debugf( NAME_DevPath, TEXT("Undefining paths..."));
	GWarn->StatusUpdatef( 1, 1, TEXT("Undefining paths..."));
	Level->ReachSpecs.Empty();
	Level->GetLevelInfo()->NavigationPointList = nullptr;

	int32 FirstDeleted = Level->Actors.Num();
	for ( int32 i=0; i<Level->Actors.Num(); i++ )
	{
		GWarn->StatusUpdatef( i, Level->Actors.Num(), TEXT("Undefining paths (Actor %i/%i)..."), i, Level->Actors.Num() );
		ANavigationPoint* Actor = Cast<ANavigationPoint>( Level->Actors(i));
		if ( Actor )
		{
			if ( Actor->IsA(AInventorySpot::StaticClass()) && Actor->bHiddenEd )
			{
				FirstDeleted = Min( FirstDeleted, i);
				if ( ((AInventorySpot*)Actor)->markedItem )
					((AInventorySpot*)Actor)->markedItem->myMarker = nullptr;
				Level->DestroyActor(Actor);
			}
			else if ( Actor->IsA(AWarpZoneMarker::StaticClass()) || Actor->IsA(ATriggerMarker::StaticClass()) || Actor->IsA(AButtonMarker::StaticClass()) )
			{
				FirstDeleted = Min( FirstDeleted, i);
				Level->DestroyActor(Actor);
			}
			else
				Cleanup( Actor);
		}
	}
	CompactActors( Level->Actors, FirstDeleted);
	AInventorySpot::StaticClass()->ClassUnique = 0;
}


//============== Creates special markers for items, warp zones
//
inline void FPathBuilderMaster::AddMarkers()
{
	int32 i;
	int32 BaseListSize = InfoList.Num();

	// Add InventorySpots
	for ( i=0 ; i<Level->Actors.Num() ; i++ )
	{
		AInventory* Actor = Cast<AInventory>( Level->Actors(i));
		if ( Actor )
			HandleInventory( Actor);
	}

	// Add WarpZoneMarkers
	for ( i=0 ; i<Level->Actors.Num() ; i++ )
	{
		AWarpZoneInfo* Actor = Cast<AWarpZoneInfo>( Level->Actors(i));
		if ( Actor )
			HandleWarpZone( Actor);
	}
	// TODO: Add custom markers

	BuildResult += FString::Printf( TEXT("Processed %i NavigationPoints (%i markers).\r\n"), InfoList.Num(), InfoList.Num()-BaseListSize);
}

//============== Special Reachspecs code
//
inline void FPathBuilderMaster::DefineSpecials()
{
	guard(FPathBuilderMaster::DefineSpecials)
	debugf( NAME_DevPath, TEXT("Defining special paths..."));
	FReachSpec SpecialSpec;
	SpecialSpec.distance = 500;
	SpecialSpec.CollisionRadius = 60;
	SpecialSpec.CollisionHeight = 60;
	SpecialSpec.reachFlags = R_SPECIAL;
	SpecialSpec.bPruned = 0;

	for ( int32 i=0 ; i<InfoList.Num() ; i++ )
	{
		//Tag->Event first
		if ( InfoList(i).Owner->Event != NAME_None )
		{
			for ( int32 j=0 ; j<InfoList.Num() ; j++ )
				if ( InfoList(j).Owner->Tag == InfoList(i).Owner->Event )
				{
					SpecialSpec.Start = InfoList(i).Owner;
					SpecialSpec.End = InfoList(j).Owner;
					AttachReachSpec( SpecialSpec);
				}
		}

		if ( InfoList(i).Owner->IsA(ALiftCenter::StaticClass()) )
		{
			ALiftCenter* LC = (ALiftCenter*)InfoList(i).Owner;
			for ( int32 j=0 ; j<InfoList.Num() ; j++ )
			{
				if ( InfoList(j).Owner->IsA(ALiftExit::StaticClass())
					&& (LC->LiftTag == ((ALiftExit*)InfoList(j).Owner)->LiftTag) )
				{
					SpecialSpec.Start = LC;
					SpecialSpec.End = InfoList(j).Owner;
					AttachReachSpec( SpecialSpec);
					Exchange( SpecialSpec.Start, SpecialSpec.End);
					AttachReachSpec( SpecialSpec);
				}
			}
		}
		else if ( InfoList(i).Owner->IsA(ATeleporter::StaticClass()) )
		{
			ATeleporter* Teleporter = (ATeleporter*)InfoList(i).Owner;
			for ( int32 j=0 ; j<InfoList.Num() ; j++ )
			{
				if ( (InfoList(j).Owner->IsA(ATeleporter::StaticClass()))
					&& ((ATeleporter*)InfoList(j).Owner)->URL == *Teleporter->Tag )
				{
					SpecialSpec.Start = Teleporter;
					SpecialSpec.End = InfoList(j).Owner;
					AttachReachSpec( SpecialSpec);
				}
			}
		}
		else if ( InfoList(i).Owner->IsA(AWarpZoneMarker::StaticClass()) )
		{
			AWarpZoneMarker* Warp = (AWarpZoneMarker*)InfoList(i).Owner;
			for ( int32 j=0 ; j<InfoList.Num() ; j++ )
			{
				if ( (InfoList(j).Owner->IsA(AWarpZoneMarker::StaticClass()))
					&& ((AWarpZoneMarker*)InfoList(j).Owner)->markedWarpZone->OtherSideURL == *Warp->markedWarpZone->ThisTag )
				{
					SpecialSpec.Start = Warp;
					SpecialSpec.End = InfoList(j).Owner;
					AttachReachSpec( SpecialSpec);
				}
			}
		}
		//TODO: Custom event (?)

	}
	unguard
}



//============== Candidates are possible connections
//
// Instead of connecting right away, candidates will be selected and sorted by distance
//
inline void FPathBuilderMaster::BuildCandidatesLists()
{
	debugf( NAME_DevPath, TEXT("Building candidates lists..."));
	float MaxDistSq = GoodDistance * GoodDistance * 2 * 2;

	uint32 MaxPossiblePairs = InfoList.Num() * InfoList.Num() / 2;

	for ( int32 i=0 ; i<InfoList.Num() ; i++ )
	{
		if ( InfoList(i).Owner->IsA( ALiftCenter::StaticClass()) ) 
			continue; //No LiftCenter

		for ( int32 j=i+1 ; j<InfoList.Num() ; j++ )
		{
			if ( InfoList(j).Owner->IsA( ALiftCenter::StaticClass()) ) 
				continue; //No LiftCenter

			float DistSq = (InfoList(i).Owner->Location - InfoList(j).Owner->Location).SizeSquared();
			if ( DistSq > MaxDistSq ) 
				continue; //Too far

			if ( !Level->Model->FastLineCheck( InfoList(i).Owner->Location, InfoList(j).Owner->Location) ) 
				continue; //Not visible

			uint32 CurrentPair = (InfoList.Num() * 2 - i) * i / 2 + j - i;
			GWarn->StatusUpdatef( CurrentPair, MaxPossiblePairs, TEXT("Building candidates lists (%i/%i)"), CurrentPair, MaxPossiblePairs);
				
			int32 k = 0;
			while ( (k < InfoList(i).Candidates.Num()) && (InfoList(i).Candidates(k).DistSq < DistSq) )
				k++;
			InfoList(i).Candidates.Insert( k);
			InfoList(i).Candidates(k).Path = InfoList(j).Owner;
			InfoList(i).Candidates(k).DistSq = DistSq;
			TotalCandidates++;
		}
	}
}

//============== Connect candidates to each other
//
// Connection must pass reachability checks
// Actors on full path lists
//
inline void FPathBuilderMaster::ProcessCandidatesLists()
{
	guard(FPathBuilderMaster::ProcessCandidatesLists)
	debugf( NAME_DevPath, TEXT("Processing candidates lists..."));
	FMemMark Mark(GMem);

	// Build initial sorted list
	int32 i;
	TPathInfoLink* InfoData = new(GMem,MEM_Zeroed,InfoList.Num()) TPathInfoLink();
	TPathInfoLink* InfoLink = nullptr;
	for ( i=0 ; i<InfoList.Num() ; i++ )
		if ( InfoList(i).Candidates.Num() > 0 )
		{
			GWarn->StatusUpdatef( i, InfoList.Num(), TEXT("Sorting candidates lists (%i)"), i);
			InfoData[i].Ref = &InfoList(i);
			InfoData[i].Next = InfoLink;
			InfoLink = InfoData[i].SortThis( &FPathBuilderInfo::Compare );
		}

	// Process and re-sort list until done
	i = 0;
	while ( InfoLink )
	{
		i++;
		GWarn->StatusUpdatef( i, TotalCandidates, TEXT("Processing candidates  (%i/%i)"), i, TotalCandidates);
		DefineFor( InfoLink->Ref->Owner, InfoLink->Ref->Candidates(0).Path);
		InfoLink->Ref->Candidates.Remove(0);
		if ( !InfoLink->Ref->Candidates.Num() )
			InfoLink = InfoLink->Next;
		else
			InfoLink = InfoLink->SortThis( &FPathBuilderInfo::Compare );
	}

	Mark.Pop();
	unguard
}
/*
static int ConnectedIdx( ANavigationPoint* Start, ANavigationPoint* End)
{
	for ( int i=0 ; (i<16) && (Start->Paths[i]>=0) ; i++ )
	{
		FReachSpec& Spec = Start->GetLevel()->ReachSpecs(Start->Paths[i]);
		if ( Spec.Start == Start && Spec.End == End )
			return i;
	}
	return INDEX_NONE;
}
*/

//============== Connect two candidates with physics checks
//
// Both nodes are visible to each other
//
inline void FPathBuilderMaster::DefineFor( ANavigationPoint* A, ANavigationPoint* B)
{
	guard(FPathBuilderMaster::DefineFor)
	int32 i, k;
	float Distance;
	FVector X;
	FMemMark Mark(GMem);

	//The higher the value, the more likely to be pruned
	//Formula is 32 + Str + Dist * (1.1+Str/100)
	float PruneStrength = (float)(CountPaths(A->Paths) + CountPaths(A->upstreamPaths) + CountPaths(B->Paths) + CountPaths(B->upstreamPaths));
	
	//Get normalized direction and distance (A -> B)
	X = (B->Location - A->Location);
	if ( (Distance=X.Size()) < 1 ) //Do not define ridiculously near paths
		return;
	X /= Distance; //Normalize

	//Construct a list of nodes contained within prunable field (check formula)
	float MaxPrunableDistance = (Distance * 1.1 + 32) * (PruneStrength / 100 + 1);
	int32 MiddleCount = 0;
	ANavigationPoint** Paths = new(GMem,InfoList.Num()) ANavigationPoint*;
	for ( i=0 ; i<InfoList.Num() ; i++ )
	{
		if ( (InfoList(i).Owner == A) || (InfoList(i).Owner == B) )
			continue; //Discard origins

		FVector ADelta = InfoList(i).Owner->Location - A->Location; //Dir . ADelta > 0 (req)
		FVector BDelta = InfoList(i).Owner->Location - B->Location; //Dir . BDelta < 0 (req)
		if ( (ADelta | X) * (BDelta | X) >= 0 )
			continue; //Fast: Only consider nodes in the band between A and B (parallel planes)

		float ExistingDistance = ADelta.Size() + BDelta.Size();
		if ( ExistingDistance > MaxPrunableDistance )
			continue; //Slow: Only consider nodes in a 3d ellipsis around the points

		Paths[MiddleCount++] = InfoList(i).Owner;
		InfoList(i).Owner->bestPathWeight = 1; //FLAG MIDDLE POINTS!
	}

	//Move middle points to their own list, release general path list
	ANavigationPoint** MiddlePoints = nullptr;
	if ( MiddleCount > 0 )
	{
		MiddlePoints = new(GMem,MiddleCount) ANavigationPoint*;
		appMemcpy( MiddlePoints, Paths, MiddleCount * sizeof(ANavigationPoint*));
	}

	//A=Start, B=End
	for ( int32 RoundTrip=0 ; RoundTrip<2 ; RoundTrip++, Exchange(A,B) )
	{
		// Do not consider one way
		if ( A->bOneWayPath && (((B->Location - A->Location) | A->Rotation.Vector()) <= 0) )
			continue;

		//Evaluate pruning first
		int32 Prune = 0;
		if ( MiddleCount )
		{
			for ( i=0 ; i<MiddleCount ; i++ ) 
				MiddlePoints[i]->visitedWeight = MAX_WEIGHT; //PREPARE MIDDLE POINTS
			#define FINISHED_QUERY 1
			#define PENDING_QUERY 2
			i = k = 0;
			Paths[k++] = A;
			while ( i < k )
			{
				ANavigationPoint* Start = Paths[i++];
				for ( int32 j=0 ; j<16 && Start->Paths[j]>=0 ; j++ )
				{
					const FReachSpec& Spec = Level->ReachSpecs(Start->Paths[j]);
					ANavigationPoint* End = (ANavigationPoint*)Spec.End;
					//CUT HERE!!
					if ( End == B ) //Connection exists!
					{
						i = k;
						Prune = 1;
						break;
					}
					int32 CurWeight = Max( 1, Spec.distance) + Start->visitedWeight;
					if ( End->bestPathWeight && (End->visitedWeight > CurWeight) )
					{
						End->visitedWeight = CurWeight;
						if ( (End->OtherTag == FINISHED_QUERY) && (i > 0)  ) //Already queried during this route
							Paths[--i] = End; //Check ASAP
						else if ( End->OtherTag != PENDING_QUERY ) //Not queried during this route
							Paths[k++] = End; //Check last
						End->OtherTag = PENDING_QUERY;
					}
				}
				Start->OtherTag = FINISHED_QUERY; //FLAG AS QUERIED
			}
			for ( i=0 ; i<MiddleCount ; i++ ) 
			{
				MiddlePoints[i]->OtherTag = 0;
				MiddlePoints[i]->visitedWeight = 0; //RESET MIDDLE POINTS
			}
			A->OtherTag = B->OtherTag = 0;
		}

		if ( !Prune )
		{
			FReachSpec Spec = CreateSpec( A, B);
			if ( Spec.Start && Spec.End )
				AttachReachSpec( Spec);
			else
			{
				for ( i=0 ; i<16 ; i++ )
					if ( !A->VisNoReachPaths[i] )
					{
						A->VisNoReachPaths[i] = B;
						break;
					}
			}
		}
	}

	for ( i=0 ; i<MiddleCount ; i++ )
		MiddlePoints[i]->bestPathWeight = 0; //UNFLAG MIDDLE POINTS!

	Mark.Pop();
	unguard
}

inline FReachSpec FPathBuilderMaster::CreateSpec( ANavigationPoint* Start, ANavigationPoint* End)
{
	FReachSpec Spec;
	Spec.Init();
	Spec.Start = Start;
	Spec.End = End;
	Scout->JumpZ = GoodJumpZ;
	Scout->GroundSpeed = GoodGroundSpeed;
	Scout->Physics = PHYS_Walking;
	Scout->bCanWalk = 1;
	Scout->bCanSwim = 1;
	Scout->bCanJump = 1;
	Scout->bCanFly = 0;
	Scout->MaxStepHeight = 25;

	//IMPORTANT: SCOUT NEEDS pointReachable() REPLACEMENT TO ALLOW BETTER JUMPING
	//This also sets reachflags
	int Reachable = Spec.findBestReachable( Start->Location, End->Location, Scout);

	//Try with MaxStepHeight big enough to simulate PickWallAdjust() jumps
	if ( !Reachable )
	{
		//Free fall
		// v_end^2 = v_start^2 + 2.gravity.h = 0
		// h = -(v_start^2) / (2.gravity)
		Scout->MaxStepHeight = -(GoodJumpZ*GoodJumpZ) / (Start->Region.Zone->ZoneGravity.Z * 2);
		Reachable = Spec.findBestReachable( Start->Location, End->Location, Scout);
		if ( Reachable )
			Spec.reachFlags |= R_JUMP;
	}

	if ( Aerial && !Reachable )
	{
		Scout->MaxStepHeight = 25;
		Scout->bCanFly = 1;
		Scout->Physics = PHYS_Flying;
		Reachable = Spec.findBestReachable( Start->Location, End->Location, Scout);
	}

	if ( !Reachable )
		Spec.Init();
	return Spec;
}

inline int FPathBuilderMaster::AttachReachSpec( const FReachSpec& Spec, int32 bPrune)
{
	ANavigationPoint* Start = (ANavigationPoint*)Spec.Start;
	ANavigationPoint* End   = (ANavigationPoint*)Spec.End;

	int32 SpecIdx = Level->ReachSpecs.AddItem( Spec);

	int32 i = INDEX_NONE;
	if ( bPrune )
	{
		if ( (i=FreePath(Start->PrunedPaths)) >= 0 )
			Start->PrunedPaths[i] = SpecIdx;
	}
	else
	{
		//Upstream required
		int j;
		if ( (i=FreePath(Start->Paths)) >= 0 && (j=FreePath(End->upstreamPaths)) >= 0 )
		{
			Start->Paths[i] = SpecIdx;
			End->upstreamPaths[j] = SpecIdx;
		}
	}

	if ( i == INDEX_NONE )
	{
		Level->ReachSpecs.Remove( SpecIdx);
		return 0;
	}
	return 1;
}

//============== Physics utils
//
static int FlyTo( APawn* Scout, AActor* Other)
{
	float NetRadius = Scout->CollisionRadius;
	float NetHeight = Scout->CollisionHeight;
	if ( Other->bCollideActors )
	{
		NetRadius += Other->CollisionRadius;
		NetHeight += Other->CollisionHeight;
	}
	for ( int32 Loops=8 ; Loops>=0 ; Loops++ )
	{
		//Up and down 4 times each
		FVector Offset( 0, 0, Scout->MaxStepHeight * ((Loops&1) ? 1.0 : -1.0 ) );
		Scout->moveSmooth( Offset);
		Scout->moveSmooth( Other->Location - Scout->Location);
		FVector Delta = Other->Location - Scout->Location;
		if ( Square(Delta.X + Delta.Y) <= Square(NetRadius)
			&& Square(Delta.Z) <= Square(NetHeight) )
			return 1;
	}
	return 0;
}



//============== Data utils
//
static void RegisterInfo( ANavigationPoint* N)
{
	FPathBuilderInfo& Info = InfoList( InfoList.AddZeroed());

	Info.Owner = N;
	N->nextNavigationPoint = N->Level->NavigationPointList;
	N->Level->NavigationPointList = N;
}

struct FQueryResult
{
	ANavigationPoint* Owner;
	float DistSq;
	FQueryResult* Next;

	FQueryResult( FQueryResult** Chain, ANavigationPoint* InOwner, float InDistSq )
		: Owner(InOwner) , DistSq( InDistSq), Next(nullptr)
	{
		while( *Chain && ((*Chain)->DistSq < DistSq) )
			Chain = &(*Chain)->Next;
		Next = *Chain;
		*Chain = this;
	}
};

static int TraverseTo( APawn* Scout, AActor* To, float MaxDistance, int Visible)
{
	debugf( NAME_DevPath, TEXT("Traversing to %s"), To->GetName() );
	FQueryResult* Results = nullptr;
	MaxDistance = MaxDistance * MaxDistance;

	FMemMark Mark(GMem);
	for ( int32 i=0 ; i<InfoList.Num() ; i++ )
	{
		ANavigationPoint* N = InfoList(i).Owner;
		float DistSq = (N->Location - To->Location).SizeSquared();
		if ( (DistSq <= MaxDistance) && (!Visible || To->XLevel->Model->FastLineCheck(To->Location, N->Location))  )
		{
			//Do not traverse from warp zones
			if ( !N->Region.Zone->IsA(AWarpZoneInfo::StaticClass()) )
				new(GMem) FQueryResult( &Results, N, DistSq);
		}
	}

	int Found;
	for ( Found=0 ; !Found && Results ; Results=Results->Next ) //Auto-sorted by distance
		if ( To->GetLevel()->FarMoveActor( Scout, Results->Owner->Location) && FlyTo(Scout,To) )
			Found = 1;
	Mark.Pop();
	return Found;
}


//============== Adds inventory marker
//
inline void FPathBuilderMaster::HandleInventory( AInventory* Inv)
{
	guard(FPathBuilderMaster::HandleInventory)
	if ( Inv->bHiddenEd || Inv->myMarker || Inv->bDeleteMe )
		return;

	//Adjust Scout using player dims
	Scout->SetCollisionSize( GoodRadius, GoodHeight);

	//Attempt to stand at item
	if ( !FindStart(Inv->Location) 
		|| (Abs(Scout->Location.Z - Inv->Location.Z) > Scout->CollisionHeight) 
		|| (Scout->Location-Inv->Location).SizeSquared2D() > Square(Scout->CollisionRadius+Inv->CollisionRadius) )
	{
		//Failed, attempt to move towards item from elsewhere
		if ( !TraverseTo( Scout, Inv, GoodDistance * 0.5, 0) || !TraverseTo( Scout, Inv, GoodDistance * 1.5, 1) )
		{
			//Failed, just place above item
			Level->FarMoveActor(Scout, Inv->Location + FVector(0,0,GoodHeight-Inv->CollisionHeight), 1, 1);
		}
	}

	AActor* Default = InventorySpotClass->GetDefaultActor();
	int bOldCol = Default->bCollideWhenPlacing;
	Default->bCollideWhenPlacing = 0;
	Inv->myMarker = (AInventorySpot*)Level->SpawnActor( InventorySpotClass, NAME_None, NULL, NULL, Scout->Location);
	Default->bCollideWhenPlacing = bOldCol;
	if ( Inv->myMarker )
	{
		Inv->myMarker->markedItem = Inv;
		Inv->myMarker->bAutoBuilt = 1;
		RegisterInfo( Inv->myMarker);
	}
	unguard
}

//============== Adds warp zone marker
//
inline void FPathBuilderMaster::HandleWarpZone( AWarpZoneInfo* Info)
{
	guard(FPathBuilderMaster::HandleWarpZone)
	//Adjust Scout using player dims
	Scout->SetCollisionSize( GoodRadius, GoodHeight);
	if ( !FindStart(Info->Location) || (Scout->Region.Zone != Info) )
	{
		//Failed, attempte to traverse from nearest pathnode
		if ( !TraverseTo( Scout, Info, GoodDistance, 1) )
		{
			//Failed, just place on the warp zone
			Level->FarMoveActor( Scout, Info->Location, 1, 1);
		}
	}

	AWarpZoneMarker *Marker = (AWarpZoneMarker*)Level->SpawnActor( WarpZoneMarkerClass, NAME_None, NULL, NULL, Scout->Location);
	Marker->markedWarpZone = Info;
	Marker->bAutoBuilt = 1;
	RegisterInfo( Marker);
	unguard
}

//============== FPathBuilder forwards
//
inline void FPathBuilderMaster::GetScout()
{
	(this->*getScout)();
	check( Scout );

	Scout->GroundSpeed = GoodGroundSpeed;
	Scout->JumpZ = GoodJumpZ;
	Scout->SetCollisionSize( GoodRadius, GoodHeight);
}

inline int FPathBuilderMaster::FindStart( FVector V)
{
	return (this->*findScoutStart)(V); 
}