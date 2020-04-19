/* $Id$ */

/** @file pbs.cpp */
#include "stdafx.h"
#include "pbs.h"
#include "functions.h"
#include "vehicle_func.h"
#include "yapf/follow_track.hpp"

/**
 * Get the reserved trackbits for any tile, regardless of type.
 * @param t the tile
 * @return the reserved trackbits. TRACK_BIT_NONE on nothing reserved or
 *     a tile without rail.
 */
TrackBits GetReservedTrackbits(TileIndex t)
{
	switch (GetTileType(t)) {
		case MP_RAILWAY:
			if (IsRailWaypoint(t) || IsRailDepot(t)) return GetRailWaypointReservation(t);
			if (IsPlainRailTile(t)) return GetTrackReservation(t);
			break;

		case MP_ROAD:
			if (IsLevelCrossing(t)) return GetRailCrossingReservation(t);
			break;

		case MP_STATION:
			if (IsRailwayStation(t)) return GetRailStationReservation(t);
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) return GetRailTunnelBridgeReservation(t);
			break;

		default:
			break;
	}
	return TRACK_BIT_NONE;
}

/**
 * Set the reservation for a complete station platform.
 * @pre IsRailwayStationTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailwayStationPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert(IsRailwayStationTile(start));
	assert(GetRailStationAxis(start) == DiagDirToAxis(dir));

	do {
		SetRailwayStationReservation(tile, b);
		MarkTileDirtyByTile(tile);
		tile = TILE_ADD(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}

/**
 * Try to reserve a specific track on a tile
 * @param tile the tile
 * @param t the track
 * @return true if reservation was successfull, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 */
bool TryReserveRailTrack(TileIndex tile, Track t)
{
	assert((GetTileTrackStatus(tile, TRANSPORT_RAIL, 0) & TrackToTrackBits(t)) != 0);

	if (_settings_client.gui.show_track_reservation) {
		/* show the reserved rail if needed */
		MarkTileDirtyByTile(tile);
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsPlainRailTile(tile)) return TryReserveTrack(tile, t);
			if (IsRailWaypoint(tile) || IsRailDepot(tile)) {
				if (!GetDepotWaypointReservation(tile)) {
					SetDepotWaypointReservation(tile, true);
					MarkTileDirtyByTile(tile); // some GRFs change their appearance when tile is reserved
					return true;
				}
			}
			break;

		case MP_ROAD:
			if (IsLevelCrossing(tile) && !GetCrossingReservation(tile)) {
				SetCrossingReservation(tile, true);
				BarCrossing(tile);
				MarkTileDirtyByTile(tile); // crossing barred, make tile dirty
				return true;
			}
			break;

		case MP_STATION:
			if (IsRailwayStation(tile) && !GetRailwayStationReservation(tile)) {
				SetRailwayStationReservation(tile, true);
				MarkTileDirtyByTile(tile); // some GRFs need redraw after reserving track
				return true;
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && !GetRailTunnelBridgeReservation(tile)) {
				SetTunnelBridgeReservation(tile, true);
				return true;
			}
			break;

		default:
			break;
	}
	return false;
}

/**
 * Lift the reservation of a specific track on a tile
 * @param tile the tile
 * @param t the track
 */
 void UnreserveRailTrack(TileIndex tile, Track t)
{
	assert((GetTileTrackStatus(tile, TRANSPORT_RAIL, 0) & TrackToTrackBits(t)) != 0);

	if (_settings_client.gui.show_track_reservation) {
		MarkTileDirtyByTile(tile);
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsRailWaypoint(tile) || IsRailDepot(tile)) {
				SetDepotWaypointReservation(tile, false);
				MarkTileDirtyByTile(tile);
				break;
			}
			if (IsPlainRailTile(tile)) UnreserveTrack(tile, t);
			break;

		case MP_ROAD:
			if (IsLevelCrossing(tile)) {
				SetCrossingReservation(tile, false);
				UpdateLevelCrossing(tile);
			}
			break;

		case MP_STATION:
			if (IsRailwayStation(tile)) {
				SetRailwayStationReservation(tile, false);
				MarkTileDirtyByTile(tile);
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) SetTunnelBridgeReservation(tile, false);
			break;

		default:
			break;
	}
}


/** Follow a reservation starting from a specific tile to the end. */
static PBSTileInfo FollowReservation(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, bool ignore_oneway = false)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return PBSTileInfo(tile, trackdir, false);

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	while (ft.Follow(tile, trackdir)) {
		TrackdirBits reserved = ft.m_new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.m_new_tile));

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) break;

		/* Can't have more than one reserved trackdir */
		Trackdir new_trackdir = FindFirstTrackdir(reserved);

		/* One-way signal against us. The reservation can't be ours as it is not
		 * a safe position from our direction and we can never pass the signal. */
		if (!ignore_oneway && HasOnewaySignalBlockingTrackdir(ft.m_new_tile, new_trackdir)) break;

		tile = ft.m_new_tile;
		trackdir = new_trackdir;

		if (first_loop) {
			/* Update the start tile after we followed the track the first
			 * time. This is neccessary because the track follower can skip
			 * tiles (in stations for example) which means that we might
			 * never visit our original starting tile again. */
			start_tile = tile;
			start_trackdir = trackdir;
			first_loop = false;
		} else {
			/* Loop encountered? */
			if (tile == start_tile && trackdir == start_trackdir) break;
		}
		/* Depot tile? Can't continue. */
		if (IsRailDepotTile(tile)) break;
		/* Non-pbs signal? Reservation can't continue. */
		if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
	}

	return PBSTileInfo(tile, trackdir, false);
}

/**
 * Helper struct for finding the best matching vehicle on a specific track.
 */
struct FindTrainOnTrackInfo {
	PBSTileInfo res; ///< Information about the track.
	Vehicle *best;   ///< The currently "best" vehicle we have found.

	/** Init the best location to NULL always! */
	FindTrainOnTrackInfo() : best(NULL) {}
};

/** Callback for Has/FindVehicleOnPos to find a train on a specific track. */
static Vehicle *FindTrainOnTrackEnum(Vehicle *v, void *data)
{
	FindTrainOnTrackInfo *info = (FindTrainOnTrackInfo *)data;

	if (v->type == VEH_TRAIN && !(v->vehstatus & VS_CRASHED) && HasBit((TrackBits)v->u.rail.track, TrackdirToTrack(info->res.trackdir))) {
		v = v->First();

		/* ALWAYS return the lowest ID (anti-desync!) */
		if (info->best == NULL || v->index < info->best->index) info->best = v;
		return v;
	}

	return NULL;
}

/**
 * Follow a train reservation to the last tile.
 *
 * @param v the vehicle
 * @param train_on_res Is set to a train we might encounter
 * @returns The last tile of the reservation or the current train tile if no reservation present.
 */
PBSTileInfo FollowTrainReservation(const Vehicle *v, bool *train_on_res)
{
	assert(v->type == VEH_TRAIN);

	TileIndex tile = v->tile;
	Trackdir  trackdir = GetVehicleTrackdir(v);

	if (IsRailDepotTile(tile) && !GetRailDepotReservation(tile)) return PBSTileInfo(tile, trackdir, false);

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(v->owner, GetRailTypeInfo(v->u.rail.railtype)->compatible_railtypes, tile, trackdir);
	ftoti.res.okay = IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg);
	if (train_on_res != NULL) *train_on_res = HasVehicleOnPos(ftoti.res.tile, &ftoti, FindTrainOnTrackEnum);
	return ftoti.res;
}

/**
 * Find the train which has reserved a specific path.
 *
 * @param tile A tile on the path.
 * @param track A reserved track on the tile.
 * @return The vehicle holding the reservation or NULL if the path is stray.
 */
Vehicle *GetTrainForReservation(TileIndex tile, Track track)
{
	assert(HasReservedTracks(tile, TrackToTrackBits(track)));
	Trackdir  trackdir = TrackToTrackdir(track);

	RailTypes rts = GetRailTypeInfo(GetTileRailType(tile))->compatible_railtypes;

	/* Follow the path from tile to both ends, one of the end tiles should
	 * have a train on it. We need FollowReservation to ignore one-way signals
	 * here, as one of the two search directions will be the "wrong" way. */
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		FindTrainOnTrackInfo ftoti;
		ftoti.res = FollowReservation(GetTileOwner(tile), rts, tile, trackdir, true);

		FindVehicleOnPos(ftoti.res.tile, &ftoti, FindTrainOnTrackEnum);
		if (ftoti.best != NULL) return ftoti.best;

		/* Special case for stations: check the whole platform for a vehicle. */
		if (IsRailwayStationTile(ftoti.res.tile)) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				FindVehicleOnPos(st_tile, &ftoti, FindTrainOnTrackEnum);
				if (ftoti.best != NULL) return ftoti.best;
			}
		}

		/* Special case for bridges/tunnels: check the other end as well. */
		if (IsTileType(ftoti.res.tile, MP_TUNNELBRIDGE)) {
			FindVehicleOnPos(GetOtherTunnelBridgeEnd(ftoti.res.tile), &ftoti, FindTrainOnTrackEnum);
			if (ftoti.best != NULL) return ftoti.best;
		}
	}

	return NULL;
}

/**
 * Determine whether a certain track on a tile is a safe position to end a path.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param include_line_end Should end-of-line tiles be considered safe?
 * @param forbid_90def Don't allow trains to make 90 degree turns
 * @return True if it is a safe position
 */
bool IsSafeWaitingPosition(const Vehicle *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg)
{
	if (IsRailDepotTile(tile)) return true;

	if (IsTileType(tile, MP_RAILWAY)) {
		/* For non-pbs signals, stop on the signal tile. */
		if (HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return true;
	}

	/* Check next tile. For perfomance reasons, we check for 90 degree turns ourself. */
	CFollowTrackRail ft(v, GetRailTypeInfo(v->u.rail.railtype)->compatible_railtypes);

	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (forbid_90deg) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);
	if (ft.m_new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;

	if (ft.m_new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
		/* PBS signal on next trackdir? Safe position. */
		if (HasPbsSignalOnTrackdir(ft.m_new_tile, FindFirstTrackdir(ft.m_new_td_bits))) return true;
	}

	return false;
}

/**
 * Check if a safe position is free.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param forbid_90def Don't allow trains to make 90 degree turns
 * @return True if the position is free
 */
bool IsWaitingPositionFree(const Vehicle *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg)
{
	Track     track = TrackdirToTrack(trackdir);
	TrackBits reserved = GetReservedTrackbits(tile);

	/* Tile reserved? Can never be a free waiting position. */
	if (TrackOverlapsTracks(reserved, track)) return false;

	/* Not reserved and depot or not a pbs signal -> free. */
	if (IsRailDepotTile(tile)) return true;
	if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, track))) return true;

	/* Check the next tile, if it's a PBS signal, it has to be free as well. */
	CFollowTrackRail ft(v, GetRailTypeInfo(v->u.rail.railtype)->compatible_railtypes);

	if (!ft.Follow(tile, trackdir)) return true;

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (forbid_90deg) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);

	return !HasReservedTracks(ft.m_new_tile, TrackdirBitsToTrackBits(ft.m_new_td_bits));
}
