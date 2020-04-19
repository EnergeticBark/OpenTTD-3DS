/* $Id$ */

/** @file yapf_rail.cpp The rail pathfinding. */

#include "../stdafx.h"

#include "yapf.hpp"
#include "yapf_node_rail.hpp"
#include "yapf_costrail.hpp"
#include "yapf_destrail.hpp"
#include "../vehicle_func.h"
#include "../functions.h"

#define DEBUG_YAPF_CACHE 0

int _total_pf_time_us = 0;

template <class Types>
class CYapfReserveTrack
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type

protected:
	/** to access inherited pathfinder */
	FORCEINLINE Tpf& Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

private:
	TileIndex m_res_dest;         ///< The reservation target tile
	Trackdir  m_res_dest_td;      ///< The reservation target trackdir
	Node      *m_res_node;        ///< The reservation target node
	TileIndex m_res_fail_tile;    ///< The tile where the reservation failed
	Trackdir  m_res_fail_td;      ///< The trackdir where the reservation failed

	bool FindSafePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())) {
			m_res_dest = tile;
			m_res_dest_td = td;
			return false;   // Stop iterating segment
		}
		return true;
	}

	/** Reserve a railway platform. Tile contains the failed tile on abort. */
	bool ReserveRailwayStationPlatform(TileIndex &tile, DiagDirection dir)
	{
		TileIndex     start = tile;
		TileIndexDiff diff = TileOffsByDiagDir(dir);

		do {
			if (GetRailwayStationReservation(tile)) return false;
			SetRailwayStationReservation(tile, true);
			MarkTileDirtyByTile(tile);
			tile = TILE_ADD(tile, diff);
		} while (IsCompatibleTrainStationTile(tile, start));

		return true;
	}

	/** Try to reserve a single track/platform. */
	bool ReserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailwayStationTile(tile)) {
			if (!ReserveRailwayStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))) {
				/* Platform could not be reserved, undo. */
				m_res_fail_tile = tile;
				m_res_fail_td = td;
			}
		} else {
			if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) {
				/* Tile couldn't be reserved, undo. */
				m_res_fail_tile = tile;
				m_res_fail_td = td;
				return false;
			}
		}

		return tile != m_res_dest || td != m_res_dest_td;
	}

	/** Unreserve a single track/platform. Stops when the previous failer is reached. */
	bool UnreserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailwayStationTile(tile)) {
			TileIndex     start = tile;
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
			while ((tile != m_res_fail_tile || td != m_res_fail_td) && IsCompatibleTrainStationTile(tile, start)) {
				SetRailwayStationReservation(tile, false);
				tile = TILE_ADD(tile, diff);
			}
		} else if (tile != m_res_fail_tile || td != m_res_fail_td) {
			UnreserveRailTrack(tile, TrackdirToTrack(td));
		}
		return (tile != m_res_dest || td != m_res_dest_td) && (tile != m_res_fail_tile || td != m_res_fail_td);
	}

public:
	/** Set the target to where the reservation should be extended. */
	inline void SetReservationTarget(Node *node, TileIndex tile, Trackdir td)
	{
		m_res_node = node;
		m_res_dest = tile;
		m_res_dest_td = td;
	}

	/** Check the node for a possible reservation target. */
	inline void FindSafePositionOnNode(Node *node)
	{
		assert(node->m_parent != NULL);

		/* We will never pass more than two signals, no need to check for a safe tile. */
		if (node->m_parent->m_num_signals_passed >= 2) return;

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafePositionProc)) {
			m_res_node = node;
		}
	}

	/** Try to reserve the path till the reservation target. */
	bool TryReservePath(PBSTileInfo *target)
	{
		m_res_fail_tile = INVALID_TILE;

		if (target != NULL) {
			target->tile = m_res_dest;
			target->trackdir = m_res_dest_td;
			target->okay = false;
		}

		/* Don't bother if the target is reserved. */
		if (!IsWaitingPositionFree(Yapf().GetVehicle(), m_res_dest, m_res_dest_td)) return false;

		for (Node *node = m_res_node; node->m_parent != NULL; node = node->m_parent) {
			node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::ReserveSingleTrack);
			if (m_res_fail_tile != INVALID_TILE) {
				/* Reservation failed, undo. */
				Node *fail_node = m_res_node;
				TileIndex stop_tile = m_res_fail_tile;
				do {
					/* If this is the node that failed, stop at the failed tile. */
					m_res_fail_tile = fail_node == node ? stop_tile : INVALID_TILE;
					fail_node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
				} while (fail_node != node && (fail_node = fail_node->m_parent) != NULL);

				return false;
			}
		}

		if (target != NULL) target->okay = true;

		if (Yapf().CanUseGlobalCache(*m_res_node))
			YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);

		return true;
	}
};

template <class Types>
class CYapfFollowAnyDepotRailT
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** to access inherited path finder */
	FORCEINLINE Tpf& Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

public:
	/** Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n) */
	inline void PfFollowNode(Node& old_node)
	{
		TrackFollower F(Yapf().GetVehicle());
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** return debug report character to identify the transportation type */
	FORCEINLINE char TransportTypeChar() const
	{
		return 't';
	}

	static bool stFindNearestDepotTwoWay(const Vehicle *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_distance, int reverse_penalty, TileIndex *depot_tile, bool *reversed)
	{
		Tpf pf1;
		bool result1 = pf1.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_distance, reverse_penalty, depot_tile, reversed);

#if DEBUG_YAPF_CACHE
		Tpf pf2;
		TileIndex depot_tile2 = INVALID_TILE;
		bool reversed2 = false;
		pf2.DisableCache(true);
		bool result2 = pf2.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_distance, reverse_penalty, &depot_tile2, &reversed2);
		if (result1 != result2 || (result1 && (*depot_tile != depot_tile2 || *reversed != reversed2))) {
			DEBUG(yapf, 0, "CACHE ERROR: FindNearestDepotTwoWay() = [%s, %s]", result1 ? "T" : "F", result2 ? "T" : "F");
		}
#endif

		return result1;
	}

	FORCEINLINE bool FindNearestDepotTwoWay(const Vehicle *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_distance, int reverse_penalty, TileIndex *depot_tile, bool *reversed)
	{
		/* set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty, true);
		Yapf().SetDestination(v);
		Yapf().SetMaxCost(YAPF_TILE_LENGTH * max_distance);

		/* find the best path */
		bool bFound = Yapf().FindPath(v);
		if (!bFound) return false;

		/* some path found
		 * get found depot tile */
		Node *n = Yapf().GetBestNode();
		*depot_tile = n->GetLastTile();

		/* walk through the path back to the origin */
		Node *pNode = n;
		while (pNode->m_parent != NULL) {
			pNode = pNode->m_parent;
		}

		/* if the origin node is our front vehicle tile/Trackdir then we didn't reverse
		 * but we can also look at the cost (== 0 -> not reversed, == reverse_penalty -> reversed) */
		*reversed = (pNode->m_cost != 0);

		return true;
	}
};

template <class Types>
class CYapfFollowAnySafeTileRailT : public CYapfReserveTrack<Types>
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** to access inherited path finder */
	FORCEINLINE Tpf& Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

public:
	/** Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n) */
	inline void PfFollowNode(Node& old_node)
	{
		TrackFollower F(Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes());
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir()) && F.MaskReservedTracks()) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** Return debug report character to identify the transportation type */
	FORCEINLINE char TransportTypeChar() const
	{
		return 't';
	}

	static bool stFindNearestSafeTile(const Vehicle *v, TileIndex t1, Trackdir td, bool override_railtype)
	{
		/* Create pathfinder instance */
		Tpf pf1;
#if !DEBUG_YAPF_CACHE
		bool result1 = pf1.FindNearestSafeTile(v, t1, td, override_railtype, false);

#else
		bool result2 = pf1.FindNearestSafeTile(v, t1, td, override_railtype, true);
		Tpf pf2;
		pf2.DisableCache(true);
		bool result1 = pf2.FindNearestSafeTile(v, t1, td, override_railtype, false);
		if (result1 != result2) {
			DEBUG(yapf, 0, "CACHE ERROR: FindSafeTile() = [%s, %s]", result2 ? "T" : "F", result1 ? "T" : "F");
			DumpTarget dmp1, dmp2;
			pf1.DumpBase(dmp1);
			pf2.DumpBase(dmp2);
			FILE *f1 = fopen("C:\\yapf1.txt", "wt");
			FILE *f2 = fopen("C:\\yapf2.txt", "wt");
			fwrite(dmp1.m_out.Data(), 1, dmp1.m_out.Size(), f1);
			fwrite(dmp2.m_out.Data(), 1, dmp2.m_out.Size(), f2);
			fclose(f1);
			fclose(f2);
		}
#endif

		return result1;
	}

	bool FindNearestSafeTile(const Vehicle *v, TileIndex t1, Trackdir td, bool override_railtype, bool dont_reserve)
	{
		/* Set origin and destination. */
		Yapf().SetOrigin(t1, td);
		Yapf().SetDestination(v, override_railtype);

		bool bFound = Yapf().FindPath(v);
		if (!bFound) return false;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
		this->SetReservationTarget(pNode, pNode->GetLastTile(), pNode->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		Node *pPrev = NULL;
		while (pNode->m_parent != NULL) {
			pPrev = pNode;
			pNode = pNode->m_parent;

			this->FindSafePositionOnNode(pPrev);
		}

		return dont_reserve || this->TryReservePath(NULL);
	}
};

template <class Types>
class CYapfFollowRailT : public CYapfReserveTrack<Types>
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** to access inherited path finder */
	FORCEINLINE Tpf& Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

public:
	/** Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n) */
	inline void PfFollowNode(Node& old_node)
	{
		TrackFollower F(Yapf().GetVehicle());
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** return debug report character to identify the transportation type */
	FORCEINLINE char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stChooseRailTrack(const Vehicle *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool *path_not_found, bool reserve_track, PBSTileInfo *target)
	{
		/* create pathfinder instance */
		Tpf pf1;
#if !DEBUG_YAPF_CACHE
		Trackdir result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_not_found, reserve_track, target);

#else
		Trackdir result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_not_found, false, NULL);
		Tpf pf2;
		pf2.DisableCache(true);
		Trackdir result2 = pf2.ChooseRailTrack(v, tile, enterdir, tracks, path_not_found, reserve_track, target);
		if (result1 != result2) {
			DEBUG(yapf, 0, "CACHE ERROR: ChooseRailTrack() = [%d, %d]", result1, result2);
			DumpTarget dmp1, dmp2;
			pf1.DumpBase(dmp1);
			pf2.DumpBase(dmp2);
			FILE *f1 = fopen("C:\\yapf1.txt", "wt");
			FILE *f2 = fopen("C:\\yapf2.txt", "wt");
			fwrite(dmp1.m_out.Data(), 1, dmp1.m_out.Size(), f1);
			fwrite(dmp2.m_out.Data(), 1, dmp2.m_out.Size(), f2);
			fclose(f1);
			fclose(f2);
		}
#endif

		return result1;
	}

	FORCEINLINE Trackdir ChooseRailTrack(const Vehicle *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool *path_not_found, bool reserve_track, PBSTileInfo *target)
	{
		if (target != NULL) target->tile = INVALID_TILE;

		/* set origin and destination nodes */
		PBSTileInfo origin = FollowTrainReservation(v);
		Yapf().SetOrigin(origin.tile, origin.trackdir, INVALID_TILE, INVALID_TRACKDIR, 1, true);
		Yapf().SetDestination(v);

		/* find the best path */
		bool path_found = Yapf().FindPath(v);
		if (path_not_found != NULL) {
			/* tell controller that the path was only 'guessed'
			 * treat the path as found if stopped on the first two way signal(s) */
			*path_not_found = !(path_found || Yapf().m_stopped_on_first_two_way_signal);
		}

		/* if path not found - return INVALID_TRACKDIR */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *pNode = Yapf().GetBestNode();
		if (pNode != NULL) {
			/* reserve till end of path */
			this->SetReservationTarget(pNode, pNode->GetLastTile(), pNode->GetLastTrackdir());

			/* path was found or at least suggested
			 * walk through the path back to the origin */
			Node *pPrev = NULL;
			while (pNode->m_parent != NULL) {
				pPrev = pNode;
				pNode = pNode->m_parent;

				this->FindSafePositionOnNode(pPrev);
			}
			/* return trackdir from the best origin node (one of start nodes) */
			Node& best_next_node = *pPrev;
			next_trackdir = best_next_node.GetTrackdir();

			if (reserve_track && path_found) this->TryReservePath(target);
		}
		return next_trackdir;
	}

	static bool stCheckReverseTrain(const Vehicle *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty)
	{
		Tpf pf1;
		bool result1 = pf1.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty);

#if DEBUG_YAPF_CACHE
		Tpf pf2;
		pf2.DisableCache(true);
		bool result2 = pf2.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty);
		if (result1 != result2) {
			DEBUG(yapf, 0, "CACHE ERROR: CheckReverseTrain() = [%s, %s]", result1 ? "T" : "F", result2 ? "T" : "F");
		}
#endif

		return result1;
	}

	FORCEINLINE bool CheckReverseTrain(const Vehicle *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty)
	{
		/* create pathfinder instance
		 * set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty, false);
		Yapf().SetDestination(v);

		/* find the best path */
		bool bFound = Yapf().FindPath(v);

		if (!bFound) return false;

		/* path was found
		 * walk through the path back to the origin */
		Node *pNode = Yapf().GetBestNode();
		while (pNode->m_parent != NULL) {
			pNode = pNode->m_parent;
		}

		/* check if it was reversed origin */
		Node& best_org_node = *pNode;
		bool reversed = (best_org_node.m_cost != 0);
		return reversed;
	}
};

template <class Tpf_, class Ttrack_follower, class Tnode_list, template <class Types> class TdestinationT, template <class Types> class TfollowT>
struct CYapfRail_TypesT
{
	typedef CYapfRail_TypesT<Tpf_, Ttrack_follower, Tnode_list, TdestinationT, TfollowT>  Types;

	typedef Tpf_                                Tpf;
	typedef Ttrack_follower                     TrackFollower;
	typedef Tnode_list                          NodeList;
	typedef CYapfBaseT<Types>                   PfBase;
	typedef TfollowT<Types>                     PfFollow;
	typedef CYapfOriginTileTwoWayT<Types>       PfOrigin;
	typedef TdestinationT<Types>                PfDestination;
	typedef CYapfSegmentCostCacheGlobalT<Types> PfCache;
	typedef CYapfCostRailT<Types>               PfCost;
};

struct CYapfRail1         : CYapfT<CYapfRail_TypesT<CYapfRail1        , CFollowTrackRail    , CRailNodeListTrackDir, CYapfDestinationTileOrStationRailT, CYapfFollowRailT> > {};
struct CYapfRail2         : CYapfT<CYapfRail_TypesT<CYapfRail2        , CFollowTrackRailNo90, CRailNodeListTrackDir, CYapfDestinationTileOrStationRailT, CYapfFollowRailT> > {};

struct CYapfAnyDepotRail1 : CYapfT<CYapfRail_TypesT<CYapfAnyDepotRail1, CFollowTrackRail    , CRailNodeListTrackDir, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT> > {};
struct CYapfAnyDepotRail2 : CYapfT<CYapfRail_TypesT<CYapfAnyDepotRail2, CFollowTrackRailNo90, CRailNodeListTrackDir, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT> > {};

struct CYapfAnySafeTileRail1 : CYapfT<CYapfRail_TypesT<CYapfAnySafeTileRail1, CFollowTrackFreeRail    , CRailNodeListTrackDir, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT> > {};
struct CYapfAnySafeTileRail2 : CYapfT<CYapfRail_TypesT<CYapfAnySafeTileRail2, CFollowTrackFreeRailNo90, CRailNodeListTrackDir, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT> > {};


Trackdir YapfChooseRailTrack(const Vehicle *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool *path_not_found, bool reserve_track, PBSTileInfo *target)
{
	/* default is YAPF type 2 */
	typedef Trackdir (*PfnChooseRailTrack)(const Vehicle*, TileIndex, DiagDirection, TrackBits, bool*, bool, PBSTileInfo*);
	PfnChooseRailTrack pfnChooseRailTrack = &CYapfRail1::stChooseRailTrack;

	/* check if non-default YAPF type needed */
	if (_settings_game.pf.forbid_90_deg) {
		pfnChooseRailTrack = &CYapfRail2::stChooseRailTrack; // Trackdir, forbid 90-deg
	}

	Trackdir td_ret = pfnChooseRailTrack(v, tile, enterdir, tracks, path_not_found, reserve_track, target);

	return td_ret;
}

bool YapfCheckReverseTrain(const Vehicle *v)
{
	/* last wagon */
	const Vehicle *last_veh = GetLastVehicleInChain(v);

	/* get trackdirs of both ends */
	Trackdir td = GetVehicleTrackdir(v);
	Trackdir td_rev = ReverseTrackdir(GetVehicleTrackdir(last_veh));

	/* tiles where front and back are */
	TileIndex tile = v->tile;
	TileIndex tile_rev = last_veh->tile;

	int reverse_penalty = 0;

	if (v->u.rail.track == TRACK_BIT_WORMHOLE) {
		/* front in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile);

		if (TrackdirToExitdir(td) == dir_into_wormhole) tile = GetOtherTunnelBridgeEnd(tile);
		/* Now 'tile' is the tunnel entry/bridge ramp the train will reach when driving forward */

		/* Current position of the train in the wormhole */
		TileIndex cur_tile = TileVirtXY(v->x_pos, v->y_pos);

		/* Add distance to drive in the wormhole as penalty for the forward path, i.e. bonus for the reverse path
		 * Note: Negative penalties are ok for the start tile. */
		reverse_penalty -= DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
	}

	if (last_veh->u.rail.track == TRACK_BIT_WORMHOLE) {
		/* back in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile_rev);

		if (TrackdirToExitdir(td_rev) == dir_into_wormhole) tile_rev = GetOtherTunnelBridgeEnd(tile_rev);
		/* Now 'tile_rev' is the tunnel entry/bridge ramp the train will reach when reversing */

		/* Current position of the last wagon in the wormhole */
		TileIndex cur_tile = TileVirtXY(last_veh->x_pos, last_veh->y_pos);

		/* Add distance to drive in the wormhole as penalty for the revere path. */
		reverse_penalty += DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
	}

	typedef bool (*PfnCheckReverseTrain)(const Vehicle*, TileIndex, Trackdir, TileIndex, Trackdir, int);
	PfnCheckReverseTrain pfnCheckReverseTrain = CYapfRail1::stCheckReverseTrain;

	/* check if non-default YAPF type needed */
	if (_settings_game.pf.forbid_90_deg) {
		pfnCheckReverseTrain = &CYapfRail2::stCheckReverseTrain; // Trackdir, forbid 90-deg
	}

	/* slightly hackish: If the pathfinders finds a path, the cost of the first node is tested to distinguish between forward- and reverse-path. */
	if (reverse_penalty == 0) reverse_penalty = 1;

	bool reverse = pfnCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty);

	return reverse;
}

bool YapfFindNearestRailDepotTwoWay(const Vehicle *v, int max_distance, int reverse_penalty, TileIndex *depot_tile, bool *reversed)
{
	*depot_tile = INVALID_TILE;
	*reversed = false;

	const Vehicle *last_veh = GetLastVehicleInChain(v);

	PBSTileInfo origin = FollowTrainReservation(v);
	TileIndex last_tile = last_veh->tile;
	Trackdir td_rev = ReverseTrackdir(GetVehicleTrackdir(last_veh));

	typedef bool (*PfnFindNearestDepotTwoWay)(const Vehicle*, TileIndex, Trackdir, TileIndex, Trackdir, int, int, TileIndex*, bool*);
	PfnFindNearestDepotTwoWay pfnFindNearestDepotTwoWay = &CYapfAnyDepotRail1::stFindNearestDepotTwoWay;

	/* check if non-default YAPF type needed */
	if (_settings_game.pf.forbid_90_deg) {
		pfnFindNearestDepotTwoWay = &CYapfAnyDepotRail2::stFindNearestDepotTwoWay; // Trackdir, forbid 90-deg
	}

	bool ret = pfnFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_distance, reverse_penalty, depot_tile, reversed);
	return ret;
}

bool YapfRailFindNearestSafeTile(const Vehicle *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	typedef bool (*PfnFindNearestSafeTile)(const Vehicle*, TileIndex, Trackdir, bool);
	PfnFindNearestSafeTile pfnFindNearestSafeTile = CYapfAnySafeTileRail1::stFindNearestSafeTile;

	/* check if non-default YAPF type needed */
	if (_settings_game.pf.forbid_90_deg) {
		pfnFindNearestSafeTile = &CYapfAnySafeTileRail2::stFindNearestSafeTile;
	}

	return pfnFindNearestSafeTile(v, tile, td, override_railtype);
}

/** if any track changes, this counter is incremented - that will invalidate segment cost cache */
int CSegmentCostCacheBase::s_rail_change_counter = 0;

void YapfNotifyTrackLayoutChange(TileIndex tile, Track track)
{
	CSegmentCostCacheBase::NotifyTrackLayoutChange(tile, track);
}
