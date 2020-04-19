/* $Id$ */

/** @file road_cmd.cpp Commands related to road tiles. */

#include "stdafx.h"
#include "openttd.h"
#include "cmd_helper.h"
#include "road_internal.h"
#include "landscape.h"
#include "town_map.h"
#include "viewport_func.h"
#include "command_func.h"
#include "yapf/yapf.h"
#include "depot_base.h"
#include "newgrf.h"
#include "station_map.h"
#include "variables.h"
#include "autoslope.h"
#include "tunnelbridge_map.h"
#include "window_func.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "cheat_type.h"
#include "functions.h"
#include "effectvehicle_func.h"
#include "elrail_func.h"
#include "roadveh.h"

#include "table/sprites.h"
#include "table/strings.h"

/**
 * Verify whether a road vehicle is available.
 * @return \c true if at least one road vehicle is available, \c false if not
 */
bool RoadVehiclesAreBuilt()
{
	const Vehicle *v;

	FOR_ALL_VEHICLES(v) {
		if (v->type == VEH_ROAD) return true;
	}
	return false;
}

#define M(x) (1 << (x))
/* Level crossings may only be built on these slopes */
static const uint32 VALID_LEVEL_CROSSING_SLOPES = (M(SLOPE_SEN) | M(SLOPE_ENW) | M(SLOPE_NWS) | M(SLOPE_NS) | M(SLOPE_WSE) | M(SLOPE_EW) | M(SLOPE_FLAT));
#undef M

/* Invalid RoadBits on slopes  */
static const RoadBits _invalid_tileh_slopes_road[2][15] = {
	/* The inverse of the mixable RoadBits on a leveled slope */
	{
		ROAD_NONE,         // SLOPE_FLAT
		ROAD_NE | ROAD_SE, // SLOPE_W
		ROAD_NE | ROAD_NW, // SLOPE_S

		ROAD_NE,           // SLOPE_SW
		ROAD_NW | ROAD_SW, // SLOPE_E
		ROAD_NONE,         // SLOPE_EW

		ROAD_NW,           // SLOPE_SE
		ROAD_NONE,         // SLOPE_WSE
		ROAD_SE | ROAD_SW, // SLOPE_N

		ROAD_SE,           // SLOPE_NW
		ROAD_NONE,         // SLOPE_NS
		ROAD_NONE,         // SLOPE_ENW

		ROAD_SW,           // SLOPE_NE
		ROAD_NONE,         // SLOPE_SEN
		ROAD_NONE          // SLOPE_NWS
	},
	/* The inverse of the allowed straight roads on a slope
	 * (with and without a foundation). */
	{
		ROAD_NONE, // SLOPE_FLAT
		ROAD_NONE, // SLOPE_W    Foundation
		ROAD_NONE, // SLOPE_S    Foundation

		ROAD_Y,    // SLOPE_SW
		ROAD_NONE, // SLOPE_E    Foundation
		ROAD_ALL,  // SLOPE_EW

		ROAD_X,    // SLOPE_SE
		ROAD_ALL,  // SLOPE_WSE
		ROAD_NONE, // SLOPE_N    Foundation

		ROAD_X,    // SLOPE_NW
		ROAD_ALL,  // SLOPE_NS
		ROAD_ALL,  // SLOPE_ENW

		ROAD_Y,    // SLOPE_NE
		ROAD_ALL,  // SLOPE_SEN
		ROAD_ALL   // SLOPE_NW
	}
};

Foundation GetRoadFoundation(Slope tileh, RoadBits bits);

/**
 * Is it allowed to remove the given road bits from the given tile?
 * @param tile      the tile to remove the road from
 * @param remove    the roadbits that are going to be removed
 * @param owner     the actual owner of the roadbits of the tile
 * @param rt        the road type to remove the bits from
 * @param flags     command flags
 * @param town_check Shall the town rating checked/affected
 * @return true when it is allowed to remove the road bits
 */
bool CheckAllowRemoveRoad(TileIndex tile, RoadBits remove, Owner owner, RoadType rt, DoCommandFlag flags, bool town_check)
{
	if (_game_mode == GM_EDITOR || remove == ROAD_NONE) return true;

	/* Water can always flood and towns can always remove "normal" road pieces.
	 * Towns are not be allowed to remove non "normal" road pieces, like tram
	 * tracks as that would result in trams that cannot turn. */
	if (_current_company == OWNER_WATER ||
			(rt == ROADTYPE_ROAD && !IsValidCompanyID(_current_company))) return true;

	/* Only do the special processing if the road is owned
	 * by a town */
	if (owner != OWNER_TOWN) return (owner == OWNER_NONE) || CheckOwnership(owner);

	if (!town_check) return true;

	if (_cheats.magic_bulldozer.value) return true;

	Town *t = ClosestTownFromTile(tile, UINT_MAX);
	if (t == NULL) return true;

	/* check if you're allowed to remove the street owned by a town
	 * removal allowance depends on difficulty setting */
	if (!CheckforTownRating(flags, t, ROAD_REMOVE)) return false;

	/* Get a bitmask of which neighbouring roads has a tile */
	RoadBits n = ROAD_NONE;
	RoadBits present = GetAnyRoadBits(tile, rt);
	if (present & ROAD_NE && GetAnyRoadBits(TILE_ADDXY(tile, -1,  0), rt) & ROAD_SW) n |= ROAD_NE;
	if (present & ROAD_SE && GetAnyRoadBits(TILE_ADDXY(tile,  0,  1), rt) & ROAD_NW) n |= ROAD_SE;
	if (present & ROAD_SW && GetAnyRoadBits(TILE_ADDXY(tile,  1,  0), rt) & ROAD_NE) n |= ROAD_SW;
	if (present & ROAD_NW && GetAnyRoadBits(TILE_ADDXY(tile,  0, -1), rt) & ROAD_SE) n |= ROAD_NW;

	int rating_decrease = RATING_ROAD_DOWN_STEP_EDGE;
	/* If 0 or 1 bits are set in n, or if no bits that match the bits to remove,
	 * then allow it */
	if (KillFirstBit(n) != ROAD_NONE && (n & remove) != ROAD_NONE) {
		/* you can remove all kind of roads with extra dynamite */
		if (!_settings_game.construction.extra_dynamite) {
			SetDParam(0, t->index);
			_error_message = STR_2009_LOCAL_AUTHORITY_REFUSES;
			return false;
		}
		rating_decrease = RATING_ROAD_DOWN_STEP_INNER;
	}
	ChangeTownRating(t, rating_decrease, RATING_ROAD_MINIMUM, flags);

	return true;
}


/** Delete a piece of road.
 * @param tile tile where to remove road from
 * @param flags operation to perform
 * @param pieces roadbits to remove
 * @param rt roadtype to remove
 * @param crossing_check should we check if there is a tram track when we are removing road from crossing?
 */
static CommandCost RemoveRoad(TileIndex tile, DoCommandFlag flags, RoadBits pieces, RoadType rt, bool crossing_check, bool town_check = true)
{
	RoadTypes rts = GetRoadTypes(tile);
	/* The tile doesn't have the given road type */
	if (!HasBit(rts, rt)) return CMD_ERROR;

	switch (GetTileType(tile)) {
		case MP_ROAD:
			if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;
			break;

		case MP_STATION:
			if (!IsDriveThroughStopTile(tile)) return CMD_ERROR;
			if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_ROAD) return CMD_ERROR;
			if (HasVehicleOnTunnelBridge(tile, GetOtherTunnelBridgeEnd(tile))) return CMD_ERROR;
			break;

		default:
			return CMD_ERROR;
	}

	if (!CheckAllowRemoveRoad(tile, pieces, GetRoadOwner(tile, rt), rt, flags, town_check)) return CMD_ERROR;

	if (!IsTileType(tile, MP_ROAD)) {
		/* If it's the last roadtype, just clear the whole tile */
		if (rts == RoadTypeToRoadTypes(rt)) return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);

		CommandCost cost(EXPENSES_CONSTRUCTION);
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
			/* Pay for *every* tile of the bridge or tunnel */
			cost.AddCost((GetTunnelBridgeLength(other_end, tile) + 2) * _price.remove_road);
			if (flags & DC_EXEC) {
				SetRoadTypes(other_end, GetRoadTypes(other_end) & ~RoadTypeToRoadTypes(rt));
				SetRoadTypes(tile, GetRoadTypes(tile) & ~RoadTypeToRoadTypes(rt));

				/* If the owner of the bridge sells all it's road, also move the ownership
				 * to the owner of the other roadtype. */
				RoadType other_rt = (rt == ROADTYPE_ROAD) ? ROADTYPE_TRAM : ROADTYPE_ROAD;
				Owner other_owner = GetRoadOwner(tile, other_rt);
				if (other_owner != GetTileOwner(tile)) {
					SetTileOwner(tile, other_owner);
					SetTileOwner(other_end, other_owner);
				}

				/* Mark tiles diry that have been repaved */
				MarkTileDirtyByTile(tile);
				MarkTileDirtyByTile(other_end);
				if (IsBridge(tile)) {
					TileIndexDiff delta = TileOffsByDiagDir(GetTunnelBridgeDirection(tile));

					for (TileIndex t = tile + delta; t != other_end; t += delta) MarkTileDirtyByTile(t);
				}
			}
		} else {
			assert(IsDriveThroughStopTile(tile));
			cost.AddCost(_price.remove_road * 2);
			if (flags & DC_EXEC) {
				SetRoadTypes(tile, GetRoadTypes(tile) & ~RoadTypeToRoadTypes(rt));
				MarkTileDirtyByTile(tile);
			}
		}
		return cost;
	}

	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_NORMAL: {
			const Slope tileh = GetTileSlope(tile, NULL);
			RoadBits present = GetRoadBits(tile, rt);
			const RoadBits other = GetOtherRoadBits(tile, rt);
			const Foundation f = GetRoadFoundation(tileh, present);

			if (HasRoadWorks(tile) && _current_company != OWNER_WATER) return_cmd_error(STR_ROAD_WORKS_IN_PROGRESS);

			/* Autocomplete to a straight road
			 * @li on steep slopes
			 * @li if the bits of the other roadtypes result in another foundation
			 * @li if build on slopes is disabled */
			if (IsSteepSlope(tileh) || (IsStraightRoad(other) &&
					(other & _invalid_tileh_slopes_road[0][tileh & SLOPE_ELEVATED]) != ROAD_NONE) ||
					(tileh != SLOPE_FLAT && !_settings_game.construction.build_on_slopes)) {
				pieces |= MirrorRoadBits(pieces);
			}

			/* limit the bits to delete to the existing bits. */
			pieces &= present;
			if (pieces == ROAD_NONE) return CMD_ERROR;

			/* Now set present what it will be after the remove */
			present ^= pieces;

			/* Check for invalid RoadBit combinations on slopes */
			if (tileh != SLOPE_FLAT && present != ROAD_NONE &&
					(present & _invalid_tileh_slopes_road[0][tileh & SLOPE_ELEVATED]) == present) {
				return CMD_ERROR;
			}

			if (flags & DC_EXEC) {
				if (HasRoadWorks(tile)) {
					/* flooding tile with road works, don't forget to remove the effect vehicle too */
					assert(_current_company == OWNER_WATER);
					Vehicle *v;
					FOR_ALL_VEHICLES(v) {
						if (v->type == VEH_EFFECT && TileVirtXY(v->x_pos, v->y_pos) == tile) {
							delete v;
						}
					}
				}
				if (present == ROAD_NONE) {
					RoadTypes rts = GetRoadTypes(tile) & ComplementRoadTypes(RoadTypeToRoadTypes(rt));
					if (rts == ROADTYPES_NONE) {
						/* Includes MarkTileDirtyByTile() */
						DoClearSquare(tile);
					} else {
						if (rt == ROADTYPE_ROAD && IsRoadOwner(tile, ROADTYPE_ROAD, OWNER_TOWN)) {
							/* Update nearest-town index */
							const Town *town = CalcClosestTownFromTile(tile);
							SetTownIndex(tile, town == NULL ? (TownID)INVALID_TOWN : town->index);
						}
						SetRoadBits(tile, ROAD_NONE, rt);
						SetRoadTypes(tile, rts);
						MarkTileDirtyByTile(tile);
					}
				} else {
					/* When bits are removed, you *always* end up with something that
					 * is not a complete straight road tile. However, trams do not have
					 * onewayness, so they cannot remove it either. */
					if (rt != ROADTYPE_TRAM) SetDisallowedRoadDirections(tile, DRD_NONE);
					SetRoadBits(tile, present, rt);
					MarkTileDirtyByTile(tile);
				}
			}

			/* If we change the foundation we have to pay for it. */
			return CommandCost(EXPENSES_CONSTRUCTION, CountBits(pieces) * _price.remove_road +
					((GetRoadFoundation(tileh, present) != f) ? _price.terraform : (Money)0));
		}

		case ROAD_TILE_CROSSING: {
			if (pieces & ComplementRoadBits(GetCrossingRoadBits(tile))) {
				return CMD_ERROR;
			}

			/* Don't allow road to be removed from the crossing when there is tram;
			 * we can't draw the crossing without roadbits ;) */
			if (rt == ROADTYPE_ROAD && HasTileRoadType(tile, ROADTYPE_TRAM) && (flags & DC_EXEC || crossing_check)) return CMD_ERROR;

			if (flags & DC_EXEC) {
				RoadTypes rts = GetRoadTypes(tile) & ComplementRoadTypes(RoadTypeToRoadTypes(rt));
				if (rts == ROADTYPES_NONE) {
					TrackBits tracks = GetCrossingRailBits(tile);
					bool reserved = GetCrossingReservation(tile);
					MakeRailNormal(tile, GetTileOwner(tile), tracks, GetRailType(tile));
					if (reserved) SetTrackReservation(tile, tracks);
				} else {
					SetRoadTypes(tile, rts);
					/* If we ever get HWAY and it is possible without road then we will need to promote ownership and invalidate town index here, too */
				}
				MarkTileDirtyByTile(tile);
				YapfNotifyTrackLayoutChange(tile, FindFirstTrack(GetTrackBits(tile)));
			}
			return CommandCost(EXPENSES_CONSTRUCTION, _price.remove_road * 2);
		}

		default:
		case ROAD_TILE_DEPOT:
			return CMD_ERROR;
	}
}


/** Delete a piece of road.
 * @param tile tile where to remove road from
 * @param flags operation to perform
 * @param p1 bit 0..3 road pieces to remove (RoadBits)
 *           bit 4..5 road type
 * @param p2 unused
 */
CommandCost CmdRemoveRoad(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	RoadType rt = (RoadType)GB(p1, 4, 2);
	if (!IsValidRoadType(rt)) return CMD_ERROR;

	RoadBits pieces = Extract<RoadBits, 0>(p1);

	return RemoveRoad(tile, flags, pieces, rt, true);
}

/**
 * Calculate the costs for roads on slopes
 *  Aside modify the RoadBits to fit on the slopes
 *
 * @note The RoadBits are modified too!
 * @param tileh The current slope
 * @param pieces The RoadBits we want to add
 * @param existing The existent RoadBits of the current type
 * @param other The other existent RoadBits
 * @return The costs for these RoadBits on this slope
 */
static CommandCost CheckRoadSlope(Slope tileh, RoadBits *pieces, RoadBits existing, RoadBits other)
{
	/* Remove already build pieces */
	CLRBITS(*pieces, existing);

	/* If we can't build anything stop here */
	if (*pieces == ROAD_NONE) return CMD_ERROR;

	/* All RoadBit combos are valid on flat land */
	if (tileh == SLOPE_FLAT) return CommandCost();

	/* Proceed steep Slopes first to reduce lookup table size */
	if (IsSteepSlope(tileh)) {
		/* Force straight roads. */
		*pieces |= MirrorRoadBits(*pieces);

		/* Use existing as all existing since only straight
		 * roads are allowed here. */
		existing |= other;

		if ((existing == ROAD_NONE || existing == *pieces) && IsStraightRoad(*pieces)) {
			return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
		}
		return CMD_ERROR;
	}

	/* Save the merge of all bits of the current type */
	RoadBits type_bits = existing | *pieces;

	/* Roads on slopes */
	if (_settings_game.construction.build_on_slopes && (_invalid_tileh_slopes_road[0][tileh] & (other | type_bits)) == ROAD_NONE) {

		/* If we add leveling we've got to pay for it */
		if ((other | existing) == ROAD_NONE) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);

		return CommandCost();
	}

	/* Autocomplete uphill roads */
	*pieces |= MirrorRoadBits(*pieces);
	type_bits = existing | *pieces;

	/* Uphill roads */
	if (IsStraightRoad(type_bits) && (other == type_bits || other == ROAD_NONE) &&
			(_invalid_tileh_slopes_road[1][tileh] & (other | type_bits)) == ROAD_NONE) {

		/* Slopes with foundation ? */
		if (IsSlopeWithOneCornerRaised(tileh)) {

			/* Prevent build on slopes if it isn't allowed */
			if (_settings_game.construction.build_on_slopes) {

				/* If we add foundation we've got to pay for it */
				if ((other | existing) == ROAD_NONE) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);

				return CommandCost();
			}
		} else {
			if (CountBits(existing) == 1) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
			return CommandCost();
		}
	}
	return CMD_ERROR;
}

/** Build a piece of road.
 * @param tile tile where to build road
 * @param flags operation to perform
 * @param p1 bit 0..3 road pieces to build (RoadBits)
 *           bit 4..5 road type
 *           bit 6..7 disallowed directions to toggle
 * @param p2 the town that is building the road (0 if not applicable)
 */
CommandCost CmdBuildRoad(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);

	RoadBits existing = ROAD_NONE;
	RoadBits other_bits = ROAD_NONE;

	/* Road pieces are max 4 bitset values (NE, NW, SE, SW) and town can only be non-zero
	 * if a non-company is building the road */
	if ((IsValidCompanyID(_current_company) && p2 != 0) || (_current_company == OWNER_TOWN && !IsValidTownID(p2))) return CMD_ERROR;
	if (_current_company != OWNER_TOWN) {
		const Town *town = CalcClosestTownFromTile(tile);
		p2 = (town != NULL) ? town->index : (TownID)INVALID_TOWN;
	}

	RoadBits pieces = Extract<RoadBits, 0>(p1);

	/* do not allow building 'zero' road bits, code wouldn't handle it */
	if (pieces == ROAD_NONE) return CMD_ERROR;

	RoadType rt = (RoadType)GB(p1, 4, 2);
	if (!IsValidRoadType(rt) || !ValParamRoadType(rt)) return CMD_ERROR;

	DisallowedRoadDirections toggle_drd = (DisallowedRoadDirections)GB(p1, 6, 2);

	Slope tileh = GetTileSlope(tile, NULL);

	switch (GetTileType(tile)) {
		case MP_ROAD:
			switch (GetRoadTileType(tile)) {
				case ROAD_TILE_NORMAL: {
					if (HasRoadWorks(tile)) return_cmd_error(STR_ROAD_WORKS_IN_PROGRESS);

					other_bits = GetOtherRoadBits(tile, rt);
					if (!HasTileRoadType(tile, rt)) break;

					existing = GetRoadBits(tile, rt);
					bool crossing = !IsStraightRoad(existing | pieces);
					if (rt != ROADTYPE_TRAM && (GetDisallowedRoadDirections(tile) != DRD_NONE || toggle_drd != DRD_NONE) && crossing) {
						/* Junctions cannot be one-way */
						return_cmd_error(STR_ERR_ONEWAY_ROADS_CAN_T_HAVE_JUNCTION);
					}
					if ((existing & pieces) == pieces) {
						/* We only want to set the (dis)allowed road directions */
						if (toggle_drd != DRD_NONE && rt != ROADTYPE_TRAM && IsRoadOwner(tile, ROADTYPE_ROAD, _current_company)) {
							if (crossing) return_cmd_error(STR_ERR_ONEWAY_ROADS_CAN_T_HAVE_JUNCTION);

							if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

							/* Ignore half built tiles */
							if (flags & DC_EXEC && rt != ROADTYPE_TRAM && IsStraightRoad(existing)) {
								SetDisallowedRoadDirections(tile, GetDisallowedRoadDirections(tile) ^ toggle_drd);
								MarkTileDirtyByTile(tile);
							}
							return CommandCost();
						}
						return_cmd_error(STR_1007_ALREADY_BUILT);
					}
				} break;

				case ROAD_TILE_CROSSING:
					other_bits = GetCrossingRoadBits(tile);
					if (pieces & ComplementRoadBits(other_bits)) goto do_clear;
					pieces = other_bits; // we need to pay for both roadbits

					if (HasTileRoadType(tile, rt)) return_cmd_error(STR_1007_ALREADY_BUILT);
					break;

				default:
				case ROAD_TILE_DEPOT:
					goto do_clear;
			}
			break;

		case MP_RAILWAY: {
			if (IsSteepSlope(tileh)) {
				return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
			}

			/* Level crossings may only be built on these slopes */
			if (!HasBit(VALID_LEVEL_CROSSING_SLOPES, tileh)) {
				return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
			}

			if (GetRailTileType(tile) != RAIL_TILE_NORMAL) goto do_clear;

			Axis roaddir;
			switch (GetTrackBits(tile)) {
				case TRACK_BIT_X:
					if (pieces & ROAD_X) goto do_clear;
					roaddir = AXIS_Y;
					break;

				case TRACK_BIT_Y:
					if (pieces & ROAD_Y) goto do_clear;
					roaddir = AXIS_X;
					break;

				default: goto do_clear;
			}

			if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

			if (flags & DC_EXEC) {
				YapfNotifyTrackLayoutChange(tile, FindFirstTrack(GetTrackBits(tile)));
				/* Always add road to the roadtypes (can't draw without it) */
				bool reserved = HasBit(GetTrackReservation(tile), AxisToTrack(OtherAxis(roaddir)));
				MakeRoadCrossing(tile, _current_company, _current_company, GetTileOwner(tile), roaddir, GetRailType(tile), RoadTypeToRoadTypes(rt) | ROADTYPES_ROAD, p2);
				SetCrossingReservation(tile, reserved);
				UpdateLevelCrossing(tile, false);
				MarkTileDirtyByTile(tile);
			}
			return CommandCost(EXPENSES_CONSTRUCTION, _price.build_road * (rt == ROADTYPE_ROAD ? 2 : 4));
		}

		case MP_STATION: {
			if (!IsDriveThroughStopTile(tile)) goto do_clear;

			RoadBits curbits = AxisToRoadBits(DiagDirToAxis(GetRoadStopDir(tile)));
			if (pieces & ~curbits) goto do_clear;
			pieces = curbits; // we need to pay for both roadbits

			if (HasTileRoadType(tile, rt)) return_cmd_error(STR_1007_ALREADY_BUILT);
		} break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_ROAD) return CMD_ERROR;
			if (MirrorRoadBits(DiagDirToRoadBits(GetTunnelBridgeDirection(tile))) != pieces) return CMD_ERROR;
			if (HasTileRoadType(tile, rt)) return_cmd_error(STR_1007_ALREADY_BUILT);
			/* Don't allow adding roadtype to the bridge/tunnel when vehicles are already driving on it */
			if (HasVehicleOnTunnelBridge(tile, GetOtherTunnelBridgeEnd(tile))) return CMD_ERROR;
			break;

		default: {
do_clear:;
			CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (CmdFailed(ret)) return ret;
			cost.AddCost(ret);
		} break;
	}

	if (other_bits != pieces) {
		/* Check the foundation/slopes when adding road/tram bits */
		CommandCost ret = CheckRoadSlope(tileh, &pieces, existing, other_bits);
		/* Return an error if we need to build a foundation (ret != 0) but the
		 * current setting is turned off */
		if (CmdFailed(ret) || (ret.GetCost() != 0 && !_settings_game.construction.build_on_slopes)) {
			return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
		}
		cost.AddCost(ret);
	}

	if (IsTileType(tile, MP_ROAD)) {
		/* Don't put the pieces that already exist */
		pieces &= ComplementRoadBits(existing);

		/* Check if new road bits will have the same foundation as other existing road types */
		if (IsNormalRoad(tile)) {
			Slope slope = GetTileSlope(tile, NULL);
			Foundation found_new = GetRoadFoundation(slope, pieces | existing);

			/* Test if all other roadtypes can be built at that foundation */
			for (RoadType rtest = ROADTYPE_ROAD; rtest < ROADTYPE_END; rtest++) {
				if (rtest != rt) { // check only other road types
					RoadBits bits = GetRoadBits(tile, rtest);
					/* do not check if there are not road bits of given type */
					if (bits != ROAD_NONE && GetRoadFoundation(slope, bits) != found_new) {
						return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
					}
				}
			}
		}
	}

	if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

	cost.AddCost(CountBits(pieces) * _price.build_road);
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		/* Pay for *every* tile of the bridge or tunnel */
		cost.MultiplyCost(GetTunnelBridgeLength(GetOtherTunnelBridgeEnd(tile), tile) + 2);
	}

	if (flags & DC_EXEC) {
		switch (GetTileType(tile)) {
			case MP_ROAD: {
				RoadTileType rtt = GetRoadTileType(tile);
				if (existing == ROAD_NONE || rtt == ROAD_TILE_CROSSING) {
					SetRoadTypes(tile, GetRoadTypes(tile) | RoadTypeToRoadTypes(rt));
					SetRoadOwner(tile, rt, _current_company);
					if (rt == ROADTYPE_ROAD) SetTownIndex(tile, p2);
				}
				if (rtt != ROAD_TILE_CROSSING) SetRoadBits(tile, existing | pieces, rt);
			} break;

			case MP_TUNNELBRIDGE: {
				TileIndex other_end = GetOtherTunnelBridgeEnd(tile);

				SetRoadTypes(other_end, GetRoadTypes(other_end) | RoadTypeToRoadTypes(rt));
				SetRoadTypes(tile, GetRoadTypes(tile) | RoadTypeToRoadTypes(rt));
				SetRoadOwner(other_end, rt, _current_company);
				SetRoadOwner(tile, rt, _current_company);

				/* Mark tiles diry that have been repaved */
				MarkTileDirtyByTile(other_end);
				MarkTileDirtyByTile(tile);
				if (IsBridge(tile)) {
					TileIndexDiff delta = TileOffsByDiagDir(GetTunnelBridgeDirection(tile));

					for (TileIndex t = tile + delta; t != other_end; t += delta) MarkTileDirtyByTile(t);
				}
			} break;

			case MP_STATION:
				assert(IsDriveThroughStopTile(tile));
				SetRoadTypes(tile, GetRoadTypes(tile) | RoadTypeToRoadTypes(rt));
				SetRoadOwner(tile, rt, _current_company);
				break;

			default:
				MakeRoadNormal(tile, pieces, RoadTypeToRoadTypes(rt), p2, _current_company, _current_company);
				break;
		}

		if (rt != ROADTYPE_TRAM && IsNormalRoadTile(tile)) {
			existing |= pieces;
			SetDisallowedRoadDirections(tile, IsStraightRoad(existing) ?
					GetDisallowedRoadDirections(tile) ^ toggle_drd : DRD_NONE);
		}

		MarkTileDirtyByTile(tile);
	}
	return cost;
}

/** Build a long piece of road.
 * @param end_tile end tile of drag
 * @param flags operation to perform
 * @param p1 start tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0) - start tile starts in the 2nd half of tile (p2 & 1)
 * - p2 = (bit 1) - end tile starts in the 2nd half of tile (p2 & 2)
 * - p2 = (bit 2) - direction: 0 = along x-axis, 1 = along y-axis (p2 & 4)
 * - p2 = (bit 3 + 4) - road type
 * - p2 = (bit 5) - set road direction
 */
CommandCost CmdBuildLongRoad(TileIndex end_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	bool had_bridge = false;
	bool had_tunnel = false;
	bool had_success = false;
	DisallowedRoadDirections drd = DRD_NORTHBOUND;

	_error_message = INVALID_STRING_ID;

	if (p1 >= MapSize()) return CMD_ERROR;

	TileIndex start_tile = p1;
	RoadType rt = (RoadType)GB(p2, 3, 2);
	if (!IsValidRoadType(rt) || !ValParamRoadType(rt)) return CMD_ERROR;

	/* Only drag in X or Y direction dictated by the direction variable */
	if (!HasBit(p2, 2) && TileY(start_tile) != TileY(end_tile)) return CMD_ERROR; // x-axis
	if (HasBit(p2, 2)  && TileX(start_tile) != TileX(end_tile)) return CMD_ERROR; // y-axis

	/* Swap start and ending tile, also the half-tile drag var (bit 0 and 1) */
	if (start_tile > end_tile || (start_tile == end_tile && HasBit(p2, 0))) {
		TileIndex t = start_tile;
		start_tile = end_tile;
		end_tile = t;
		p2 ^= IsInsideMM(p2 & 3, 1, 3) ? 3 : 0;
		drd = DRD_SOUTHBOUND;
	}

	/* On the X-axis, we have to swap the initial bits, so they
	 * will be interpreted correctly in the GTTS. Futhermore
	 * when you just 'click' on one tile to build them. */
	if (HasBit(p2, 2) == (start_tile == end_tile && HasBit(p2, 0) == HasBit(p2, 1))) drd ^= DRD_BOTH;
	/* No disallowed direction bits have to be toggled */
	if (!HasBit(p2, 5)) drd = DRD_NONE;

	TileIndex tile = start_tile;
	/* Start tile is the small number. */
	for (;;) {
		RoadBits bits = HasBit(p2, 2) ? ROAD_Y : ROAD_X;

		if (tile == end_tile && !HasBit(p2, 1)) bits &= ROAD_NW | ROAD_NE;
		if (tile == start_tile && HasBit(p2, 0)) bits &= ROAD_SE | ROAD_SW;

		_error_message = INVALID_STRING_ID;
		CommandCost ret = DoCommand(tile, drd << 6 | rt << 4 | bits, 0, flags, CMD_BUILD_ROAD);
		if (CmdFailed(ret)) {
			if (_error_message != STR_1007_ALREADY_BUILT) return CMD_ERROR;
		} else {
			had_success = true;
			/* Only pay for the upgrade on one side of the bridges and tunnels */
			if (IsTileType(tile, MP_TUNNELBRIDGE)) {
				if (IsBridge(tile)) {
					if ((!had_bridge || GetTunnelBridgeDirection(tile) == DIAGDIR_SE || GetTunnelBridgeDirection(tile) == DIAGDIR_SW)) {
						cost.AddCost(ret);
					}
					had_bridge = true;
				} else { // IsTunnel(tile)
					if ((!had_tunnel || GetTunnelBridgeDirection(tile) == DIAGDIR_SE || GetTunnelBridgeDirection(tile) == DIAGDIR_SW)) {
						cost.AddCost(ret);
					}
					had_tunnel = true;
				}
			} else {
				cost.AddCost(ret);
			}
		}

		if (tile == end_tile) break;

		tile += HasBit(p2, 2) ? TileDiffXY(0, 1) : TileDiffXY(1, 0);
	}

	return !had_success ? CMD_ERROR : cost;
}

/** Remove a long piece of road.
 * @param end_tile end tile of drag
 * @param flags operation to perform
 * @param p1 start tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0) - start tile starts in the 2nd half of tile (p2 & 1)
 * - p2 = (bit 1) - end tile starts in the 2nd half of tile (p2 & 2)
 * - p2 = (bit 2) - direction: 0 = along x-axis, 1 = along y-axis (p2 & 4)
 * - p2 = (bit 3 + 4) - road type
 */
CommandCost CmdRemoveLongRoad(TileIndex end_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (p1 >= MapSize()) return CMD_ERROR;

	TileIndex start_tile = p1;
	RoadType rt = (RoadType)GB(p2, 3, 2);
	if (!IsValidRoadType(rt)) return CMD_ERROR;

	/* Only drag in X or Y direction dictated by the direction variable */
	if (!HasBit(p2, 2) && TileY(start_tile) != TileY(end_tile)) return CMD_ERROR; // x-axis
	if (HasBit(p2, 2)  && TileX(start_tile) != TileX(end_tile)) return CMD_ERROR; // y-axis

	/* Swap start and ending tile, also the half-tile drag var (bit 0 and 1) */
	if (start_tile > end_tile || (start_tile == end_tile && HasBit(p2, 0))) {
		TileIndex t = start_tile;
		start_tile = end_tile;
		end_tile = t;
		p2 ^= IsInsideMM(p2 & 3, 1, 3) ? 3 : 0;
	}

	Money money = GetAvailableMoneyForCommand();
	TileIndex tile = start_tile;
	/* Start tile is the small number. */
	for (;;) {
		RoadBits bits = HasBit(p2, 2) ? ROAD_Y : ROAD_X;

		if (tile == end_tile && !HasBit(p2, 1)) bits &= ROAD_NW | ROAD_NE;
		if (tile == start_tile && HasBit(p2, 0)) bits &= ROAD_SE | ROAD_SW;

		/* try to remove the halves. */
		if (bits != 0) {
			CommandCost ret = RemoveRoad(tile, flags & ~DC_EXEC, bits, rt, true);
			if (CmdSucceeded(ret)) {
				if (flags & DC_EXEC) {
					money -= ret.GetCost();
					if (money < 0) {
						_additional_cash_required = DoCommand(end_tile, start_tile, p2, flags & ~DC_EXEC, CMD_REMOVE_LONG_ROAD).GetCost();
						return cost;
					}
					RemoveRoad(tile, flags, bits, rt, true, false);
				}
				cost.AddCost(ret);
			}
		}

		if (tile == end_tile) break;

		tile += HasBit(p2, 2) ? TileDiffXY(0, 1) : TileDiffXY(1, 0);
	}

	return (cost.GetCost() == 0) ? CMD_ERROR : cost;
}

/** Build a road depot.
 * @param tile tile where to build the depot
 * @param flags operation to perform
 * @param p1 bit 0..1 entrance direction (DiagDirection)
 *           bit 2..3 road type
 * @param p2 unused
 *
 * @todo When checking for the tile slope,
 * distingush between "Flat land required" and "land sloped in wrong direction"
 */
CommandCost CmdBuildRoadDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	DiagDirection dir = Extract<DiagDirection, 0>(p1);
	RoadType rt = (RoadType)GB(p1, 2, 2);

	if (!IsValidRoadType(rt) || !ValParamRoadType(rt)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile, NULL);
	if (tileh != SLOPE_FLAT && (
				!_settings_game.construction.build_on_slopes ||
				IsSteepSlope(tileh) ||
				!CanBuildDepotByTileh(dir, tileh)
			)) {
		return_cmd_error(STR_0007_FLAT_LAND_REQUIRED);
	}

	CommandCost cost = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (CmdFailed(cost)) return CMD_ERROR;

	if (MayHaveBridgeAbove(tile) && IsBridgeAbove(tile)) return_cmd_error(STR_5007_MUST_DEMOLISH_BRIDGE_FIRST);

	if (!Depot::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Depot *dep = new Depot(tile);
		dep->town_index = ClosestTownFromTile(tile, UINT_MAX)->index;

		MakeRoadDepot(tile, _current_company, dir, rt, dep->town_index);
		MarkTileDirtyByTile(tile);
	}
	return cost.AddCost(_price.build_road_depot);
}

static CommandCost RemoveRoadDepot(TileIndex tile, DoCommandFlag flags)
{
	if (!CheckTileOwnership(tile) && _current_company != OWNER_WATER) return CMD_ERROR;

	if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DoClearSquare(tile);
		delete GetDepotByTile(tile);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price.remove_road_depot);
}

static CommandCost ClearTile_Road(TileIndex tile, DoCommandFlag flags)
{
	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_NORMAL: {
			RoadBits b = GetAllRoadBits(tile);

			/* Clear the road if only one piece is on the tile OR we are not using the DC_AUTO flag */
			if ((CountBits(b) == 1 && GetRoadBits(tile, ROADTYPE_TRAM) == ROAD_NONE) || !(flags & DC_AUTO)) {
				RoadTypes rts = GetRoadTypes(tile);
				CommandCost ret(EXPENSES_CONSTRUCTION);
				for (RoadType rt = ROADTYPE_ROAD; rt < ROADTYPE_END; rt++) {
					if (HasBit(rts, rt)) {
						CommandCost tmp_ret = RemoveRoad(tile, flags, GetRoadBits(tile, rt), rt, true);
						if (CmdFailed(tmp_ret)) return tmp_ret;
						ret.AddCost(tmp_ret);
					}
				}
				return ret;
			}
			return_cmd_error(STR_1801_MUST_REMOVE_ROAD_FIRST);
		}

		case ROAD_TILE_CROSSING: {
			RoadTypes rts = GetRoadTypes(tile);
			CommandCost ret(EXPENSES_CONSTRUCTION);

			if (flags & DC_AUTO) return_cmd_error(STR_1801_MUST_REMOVE_ROAD_FIRST);

			/* Must iterate over the roadtypes in a reverse manner because
			 * tram tracks must be removed before the road bits. */
			RoadType rt = ROADTYPE_TRAM;
			do {
				if (HasBit(rts, rt)) {
					CommandCost tmp_ret = RemoveRoad(tile, flags, GetCrossingRoadBits(tile), rt, false);
					if (CmdFailed(tmp_ret)) return tmp_ret;
					ret.AddCost(tmp_ret);
				}
			} while (rt-- != ROADTYPE_ROAD);

			if (flags & DC_EXEC) {
				DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}
			return ret;
		}

		default:
		case ROAD_TILE_DEPOT:
			if (flags & DC_AUTO) {
				return_cmd_error(STR_2004_BUILDING_MUST_BE_DEMOLISHED);
			}
			return RemoveRoadDepot(tile, flags);
	}
}


struct DrawRoadTileStruct {
	uint16 image;
	byte subcoord_x;
	byte subcoord_y;
};

#include "table/road_land.h"

/**
 * Get the foundationtype of a RoadBits Slope combination
 *
 * @param tileh The Slope part
 * @param bits The RoadBits part
 * @return The resulting Foundation
 */
Foundation GetRoadFoundation(Slope tileh, RoadBits bits)
{
	/* Flat land and land without a road doesn't require a foundation */
	if (tileh == SLOPE_FLAT || bits == ROAD_NONE) return FOUNDATION_NONE;

	if (!IsSteepSlope(tileh)) {
		/* Leveled RoadBits on a slope */
		if ((_invalid_tileh_slopes_road[0][tileh] & bits) == ROAD_NONE) return FOUNDATION_LEVELED;

		/* Straight roads without foundation on a slope */
		if (!IsSlopeWithOneCornerRaised(tileh) &&
				(_invalid_tileh_slopes_road[1][tileh] & bits) == ROAD_NONE)
			return FOUNDATION_NONE;
	}

	/* Roads on steep Slopes or on Slopes with one corner raised */
	return (bits == ROAD_X ? FOUNDATION_INCLINED_X : FOUNDATION_INCLINED_Y);
}

const byte _road_sloped_sprites[14] = {
	0,  0,  2,  0,
	0,  1,  0,  0,
	3,  0,  0,  0,
	0,  0
};

/**
 * Whether to draw unpaved roads regardless of the town zone.
 * By default, OpenTTD always draws roads as unpaved if they are on a desert
 * tile or above the snowline. Newgrf files, however, can set a bit that allows
 * paved roads to be built on desert tiles as they would be on grassy tiles.
 *
 * @param tile The tile the road is on
 * @param roadside What sort of road this is
 * @return True if the road should be drawn unpaved regardless of the roadside.
 */
static bool AlwaysDrawUnpavedRoads(TileIndex tile, Roadside roadside)
{
	return (IsOnSnow(tile) &&
			!(_settings_game.game_creation.landscape == LT_TROPIC && HasGrfMiscBit(GMB_DESERT_PAVED_ROADS) &&
				roadside != ROADSIDE_BARREN && roadside != ROADSIDE_GRASS && roadside != ROADSIDE_GRASS_ROAD_WORKS));
}

/**
 * Draws the catenary for the given tile
 * @param ti   information about the tile (slopes, height etc)
 * @param tram the roadbits for the tram
 */
void DrawTramCatenary(const TileInfo *ti, RoadBits tram)
{
	/* Do not draw catenary if it is invisible */
	if (IsInvisibilitySet(TO_CATENARY)) return;

	/* Don't draw the catenary under a low bridge */
	if (MayHaveBridgeAbove(ti->tile) && IsBridgeAbove(ti->tile) && !IsTransparencySet(TO_CATENARY)) {
		uint height = GetBridgeHeight(GetNorthernBridgeEnd(ti->tile));

		if (height <= GetTileMaxZ(ti->tile) + TILE_HEIGHT) return;
	}

	SpriteID front;
	SpriteID back;

	if (ti->tileh != SLOPE_FLAT) {
		back  = SPR_TRAMWAY_BACK_WIRES_SLOPED  + _road_sloped_sprites[ti->tileh - 1];
		front = SPR_TRAMWAY_FRONT_WIRES_SLOPED + _road_sloped_sprites[ti->tileh - 1];
	} else {
		back  = SPR_TRAMWAY_BASE + _road_backpole_sprites_1[tram];
		front = SPR_TRAMWAY_BASE + _road_frontwire_sprites_1[tram];
	}

	AddSortableSpriteToDraw(back,  PAL_NONE, ti->x, ti->y, 16, 16, TILE_HEIGHT + BB_HEIGHT_UNDER_BRIDGE, ti->z, IsTransparencySet(TO_CATENARY));
	AddSortableSpriteToDraw(front, PAL_NONE, ti->x, ti->y, 16, 16, TILE_HEIGHT + BB_HEIGHT_UNDER_BRIDGE, ti->z, IsTransparencySet(TO_CATENARY));
}

/**
 * Draws details on/around the road
 * @param img the sprite to draw
 * @param ti  the tile to draw on
 * @param dx  the offset from the top of the BB of the tile
 * @param dy  the offset from the top of the BB of the tile
 * @param h   the height of the sprite to draw
 */
static void DrawRoadDetail(SpriteID img, const TileInfo *ti, int dx, int dy, int h)
{
	int x = ti->x | dx;
	int y = ti->y | dy;
	byte z = ti->z;
	if (ti->tileh != SLOPE_FLAT) z = GetSlopeZ(x, y);
	AddSortableSpriteToDraw(img, PAL_NONE, x, y, 2, 2, h, z);
}

/**
 * Draw ground sprite and road pieces
 * @param ti TileInfo
 */
static void DrawRoadBits(TileInfo *ti)
{
	RoadBits road = GetRoadBits(ti->tile, ROADTYPE_ROAD);
	RoadBits tram = GetRoadBits(ti->tile, ROADTYPE_TRAM);

	SpriteID image = 0;
	SpriteID pal = PAL_NONE;

	if (ti->tileh != SLOPE_FLAT) {
		DrawFoundation(ti, GetRoadFoundation(ti->tileh, road | tram));

		/* DrawFoundation() modifies ti.
		 * Default sloped sprites.. */
		if (ti->tileh != SLOPE_FLAT) image = _road_sloped_sprites[ti->tileh - 1] + 0x53F;
	}

	if (image == 0) image = _road_tile_sprites_1[road != ROAD_NONE ? road : tram];

	Roadside roadside = GetRoadside(ti->tile);

	if (AlwaysDrawUnpavedRoads(ti->tile, roadside)) {
		image += 19;
	} else {
		switch (roadside) {
			case ROADSIDE_BARREN:           pal = PALETTE_TO_BARE_LAND; break;
			case ROADSIDE_GRASS:            break;
			case ROADSIDE_GRASS_ROAD_WORKS: break;
			default:                        image -= 19; break; // Paved
		}
	}

	DrawGroundSprite(image, pal);

	/* For tram we overlay the road graphics with either tram tracks only
	 * (when there is actual road beneath the trams) or with tram tracks
	 * and some dirts which hides the road graphics */
	if (tram != ROAD_NONE) {
		if (ti->tileh != SLOPE_FLAT) {
			image = _road_sloped_sprites[ti->tileh - 1] + SPR_TRAMWAY_SLOPED_OFFSET;
		} else {
			image = _road_tile_sprites_1[tram] - SPR_ROAD_Y;
		}
		image += (road == ROAD_NONE) ? SPR_TRAMWAY_TRAM : SPR_TRAMWAY_OVERLAY;
		DrawGroundSprite(image, pal);
	}

	if (road != ROAD_NONE) {
		DisallowedRoadDirections drd = GetDisallowedRoadDirections(ti->tile);
		if (drd != DRD_NONE) {
			DrawRoadDetail(SPR_ONEWAY_BASE + drd - 1 + ((road == ROAD_X) ? 0 : 3), ti, 8, 8, 0);
		}
	}

	if (HasRoadWorks(ti->tile)) {
		/* Road works */
		DrawGroundSprite((road | tram) & ROAD_X ? SPR_EXCAVATION_X : SPR_EXCAVATION_Y, PAL_NONE);
		return;
	}

	if (tram != ROAD_NONE) DrawTramCatenary(ti, tram);

	/* Return if full detail is disabled, or we are zoomed fully out. */
	if (!HasBit(_display_opt, DO_FULL_DETAIL) || _cur_dpi->zoom > ZOOM_LVL_DETAIL) return;

	/* Do not draw details (street lights, trees) under low bridge */
	if (MayHaveBridgeAbove(ti->tile) && IsBridgeAbove(ti->tile) && (roadside == ROADSIDE_TREES || roadside == ROADSIDE_STREET_LIGHTS)) {
		uint height = GetBridgeHeight(GetNorthernBridgeEnd(ti->tile));
		uint minz = GetTileMaxZ(ti->tile) + 2 * TILE_HEIGHT;

		if (roadside == ROADSIDE_TREES) minz += TILE_HEIGHT;

		if (height < minz) return;
	}

	/* If there are no road bits, return, as there is nothing left to do */
	if (CountBits(road) < 2) return;

	/* Draw extra details. */
	for (const DrawRoadTileStruct *drts = _road_display_table[roadside][road | tram]; drts->image != 0; drts++) {
		DrawRoadDetail(drts->image, ti, drts->subcoord_x, drts->subcoord_y, 0x10);
	}
}

/** Tile callback function for rendering a road tile to the screen */
static void DrawTile_Road(TileInfo *ti)
{
	switch (GetRoadTileType(ti->tile)) {
		case ROAD_TILE_NORMAL:
			DrawRoadBits(ti);
			break;

		case ROAD_TILE_CROSSING: {
			if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

			SpriteID image = GetRailTypeInfo(GetRailType(ti->tile))->base_sprites.crossing;
			SpriteID pal = PAL_NONE;

			if (GetCrossingRoadAxis(ti->tile) == AXIS_X) image++;
			if (IsCrossingBarred(ti->tile)) image += 2;

			Roadside roadside = GetRoadside(ti->tile);

			if (AlwaysDrawUnpavedRoads(ti->tile, roadside)) {
				image += 8;
			} else {
				switch (roadside) {
					case ROADSIDE_BARREN: pal = PALETTE_TO_BARE_LAND; break;
					case ROADSIDE_GRASS:  break;
					default:              image += 4; break; // Paved
				}
			}

			DrawGroundSprite(image, pal);

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && GetCrossingReservation(ti->tile)) {
				DrawGroundSprite(GetCrossingRoadAxis(ti->tile) == AXIS_Y ? GetRailTypeInfo(GetRailType(ti->tile))->base_sprites.single_y : GetRailTypeInfo(GetRailType(ti->tile))->base_sprites.single_x, PALETTE_CRASH);
			}

			if (HasTileRoadType(ti->tile, ROADTYPE_TRAM)) {
				DrawGroundSprite(SPR_TRAMWAY_OVERLAY + (GetCrossingRoadAxis(ti->tile) ^ 1), pal);
				DrawTramCatenary(ti, GetCrossingRoadBits(ti->tile));
			}
			if (HasCatenaryDrawn(GetRailType(ti->tile))) DrawCatenary(ti);
			break;
		}

		default:
		case ROAD_TILE_DEPOT: {
			if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

			SpriteID palette = COMPANY_SPRITE_COLOUR(GetTileOwner(ti->tile));

			const DrawTileSprites *dts;
			if (HasTileRoadType(ti->tile, ROADTYPE_TRAM)) {
				dts =  &_tram_depot[GetRoadDepotDirection(ti->tile)];
			} else {
				dts =  &_road_depot[GetRoadDepotDirection(ti->tile)];
			}

			DrawGroundSprite(dts->ground.sprite, PAL_NONE);

			/* End now if buildings are invisible */
			if (IsInvisibilitySet(TO_BUILDINGS)) break;

			for (const DrawTileSeqStruct *dtss = dts->seq; dtss->image.sprite != 0; dtss++) {
				SpriteID image = dtss->image.sprite;
				SpriteID pal;

				if (!IsTransparencySet(TO_BUILDINGS) && HasBit(image, PALETTE_MODIFIER_COLOUR)) {
					pal = palette;
				} else {
					pal = PAL_NONE;
				}

				AddSortableSpriteToDraw(
					image, pal,
					ti->x + dtss->delta_x, ti->y + dtss->delta_y,
					dtss->size_x, dtss->size_y,
					dtss->size_z, ti->z,
					IsTransparencySet(TO_BUILDINGS)
				);
			}
			break;
		}
	}
	DrawBridgeMiddle(ti);
}

void DrawRoadDepotSprite(int x, int y, DiagDirection dir, RoadType rt)
{
	SpriteID palette = COMPANY_SPRITE_COLOUR(_local_company);
	const DrawTileSprites *dts = (rt == ROADTYPE_TRAM) ? &_tram_depot[dir] : &_road_depot[dir];

	x += 33;
	y += 17;

	DrawSprite(dts->ground.sprite, PAL_NONE, x, y);

	for (const DrawTileSeqStruct *dtss = dts->seq; dtss->image.sprite != 0; dtss++) {
		Point pt = RemapCoords(dtss->delta_x, dtss->delta_y, dtss->delta_z);
		SpriteID image = dtss->image.sprite;

		DrawSprite(image, HasBit(image, PALETTE_MODIFIER_COLOUR) ? palette : PAL_NONE, x + pt.x, y + pt.y);
	}
}

/** Updates cached nearest town for all road tiles
 * @param invalidate are we just invalidating cached data?
 * @pre invalidate == true implies _generating_world == true
 */
void UpdateNearestTownForRoadTiles(bool invalidate)
{
	assert(!invalidate || _generating_world);

	for (TileIndex t = 0; t < MapSize(); t++) {
		if (IsTileType(t, MP_ROAD) && !HasTownOwnedRoad(t)) {
			TownID tid = (TownID)INVALID_TOWN;
			if (!invalidate) {
				const Town *town = CalcClosestTownFromTile(t);
				if (town != NULL) tid = town->index;
			}
			SetTownIndex(t, tid);
		}
	}
}

static uint GetSlopeZ_Road(TileIndex tile, uint x, uint y)
{
	uint z;
	Slope tileh = GetTileSlope(tile, &z);

	if (tileh == SLOPE_FLAT) return z;
	if (IsNormalRoad(tile)) {
		Foundation f = GetRoadFoundation(tileh, GetAllRoadBits(tile));
		z += ApplyFoundationToSlope(f, &tileh);
		return z + GetPartialZ(x & 0xF, y & 0xF, tileh);
	} else {
		return z + TILE_HEIGHT;
	}
}

static Foundation GetFoundation_Road(TileIndex tile, Slope tileh)
{
	if (IsNormalRoad(tile)) {
		return GetRoadFoundation(tileh, GetAllRoadBits(tile));
	} else {
		return FlatteningFoundation(tileh);
	}
}

static void GetAcceptedCargo_Road(TileIndex tile, AcceptedCargo ac)
{
	/* not used */
}

static void AnimateTile_Road(TileIndex tile)
{
	if (IsLevelCrossing(tile)) MarkTileDirtyByTile(tile);
}


static const Roadside _town_road_types[][2] = {
	{ ROADSIDE_GRASS,         ROADSIDE_GRASS },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_TREES,         ROADSIDE_TREES },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED }
};

static const Roadside _town_road_types_2[][2] = {
	{ ROADSIDE_GRASS,         ROADSIDE_GRASS },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED }
};


static void TileLoop_Road(TileIndex tile)
{
	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC:
			if (IsOnSnow(tile) != (GetTileZ(tile) > GetSnowLine())) {
				ToggleSnow(tile);
				MarkTileDirtyByTile(tile);
			}
			break;

		case LT_TROPIC:
			if (GetTropicZone(tile) == TROPICZONE_DESERT && !IsOnDesert(tile)) {
				ToggleDesert(tile);
				MarkTileDirtyByTile(tile);
			}
			break;
	}

	if (IsRoadDepot(tile)) return;

	const Town *t = ClosestTownFromTile(tile, UINT_MAX);
	if (!HasRoadWorks(tile)) {
		HouseZonesBits grp = HZB_TOWN_EDGE;

		if (t != NULL) {
			grp = GetTownRadiusGroup(t, tile);

			/* Show an animation to indicate road work */
			if (t->road_build_months != 0 &&
					(DistanceManhattan(t->xy, tile) < 8 || grp != HZB_TOWN_EDGE) &&
					IsNormalRoad(tile) && CountBits(GetAllRoadBits(tile)) > 1 ) {
				if (GetFoundationSlope(tile, NULL) == SLOPE_FLAT && EnsureNoVehicleOnGround(tile) && Chance16(1, 40)) {
					StartRoadWorks(tile);

					SndPlayTileFx(SND_21_JACKHAMMER, tile);
					CreateEffectVehicleAbove(
						TileX(tile) * TILE_SIZE + 7,
						TileY(tile) * TILE_SIZE + 7,
						0,
						EV_BULLDOZER);
					MarkTileDirtyByTile(tile);
					return;
				}
			}
		}

		{
			/* Adjust road ground type depending on 'grp' (grp is the distance to the center) */
			const Roadside *new_rs = (_settings_game.game_creation.landscape == LT_TOYLAND) ? _town_road_types_2[grp] : _town_road_types[grp];
			Roadside cur_rs = GetRoadside(tile);

			/* We have our desired type, do nothing */
			if (cur_rs == new_rs[0]) return;

			/* We have the pre-type of the desired type, switch to the desired type */
			if (cur_rs == new_rs[1]) {
				cur_rs = new_rs[0];
			/* We have barren land, install the pre-type */
			} else if (cur_rs == ROADSIDE_BARREN) {
				cur_rs = new_rs[1];
			/* We're totally off limits, remove any installation and make barren land */
			} else {
				cur_rs = ROADSIDE_BARREN;
			}
			SetRoadside(tile, cur_rs);
			MarkTileDirtyByTile(tile);
		}
	} else if (IncreaseRoadWorksCounter(tile)) {
		TerminateRoadWorks(tile);

		if (_settings_game.economy.mod_road_rebuild) {
			/* Generate a nicer town surface */
			const RoadBits old_rb = GetAnyRoadBits(tile, ROADTYPE_ROAD);
			const RoadBits new_rb = CleanUpRoadBits(tile, old_rb);

			if (old_rb != new_rb) {
				RemoveRoad(tile, DC_EXEC | DC_AUTO | DC_NO_WATER, (old_rb ^ new_rb), ROADTYPE_ROAD, true);
			}
		}

		MarkTileDirtyByTile(tile);
	}
}

static bool ClickTile_Road(TileIndex tile)
{
	if (!IsRoadDepot(tile)) return false;

	ShowDepotWindow(tile, VEH_ROAD);
	return true;
}

/* Converts RoadBits to TrackBits */
static const byte _road_trackbits[16] = {
	0x0, 0x0, 0x0, 0x10, 0x0, 0x2, 0x8, 0x1A, 0x0, 0x4, 0x1, 0x15, 0x20, 0x26, 0x29, 0x3F,
};

static TrackStatus GetTileTrackStatus_Road(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TrackdirBits trackdirbits = TRACKDIR_BIT_NONE;
	TrackdirBits red_signals = TRACKDIR_BIT_NONE; // crossing barred
	switch (mode) {
		case TRANSPORT_RAIL:
			if (IsLevelCrossing(tile)) trackdirbits = TrackBitsToTrackdirBits(GetCrossingRailBits(tile));
			break;

		case TRANSPORT_ROAD:
			if ((GetRoadTypes(tile) & sub_mode) == 0) break;
			switch (GetRoadTileType(tile)) {
				case ROAD_TILE_NORMAL: {
					const uint drd_to_multiplier[DRD_END] = { 0x101, 0x100, 0x1, 0x0 };
					RoadType rt = (RoadType)FindFirstBit(sub_mode);
					RoadBits bits = GetRoadBits(tile, rt);

					/* no roadbit at this side of tile, return 0 */
					if (side != INVALID_DIAGDIR && (DiagDirToRoadBits(side) & bits) == 0) break;

					uint multiplier = drd_to_multiplier[rt == ROADTYPE_TRAM ? DRD_NONE : GetDisallowedRoadDirections(tile)];
					if (!HasRoadWorks(tile)) trackdirbits = (TrackdirBits)(_road_trackbits[bits] * multiplier);
					break;
				}

				case ROAD_TILE_CROSSING: {
					Axis axis = GetCrossingRoadAxis(tile);

					if (side != INVALID_DIAGDIR && axis != DiagDirToAxis(side)) break;

					trackdirbits = TrackBitsToTrackdirBits(AxisToTrackBits(axis));
					if (IsCrossingBarred(tile)) red_signals = trackdirbits;
					break;
				}

				default:
				case ROAD_TILE_DEPOT: {
					DiagDirection dir = GetRoadDepotDirection(tile);

					if (side != INVALID_DIAGDIR && side != dir) break;

					trackdirbits = TrackBitsToTrackdirBits(DiagDirToDiagTrackBits(dir));
					break;
				}
			}
			break;

		default: break;
	}
	return CombineTrackStatus(trackdirbits, red_signals);
}

static const StringID _road_tile_strings[] = {
	STR_1814_ROAD,
	STR_1814_ROAD,
	STR_1814_ROAD,
	STR_1815_ROAD_WITH_STREETLIGHTS,
	STR_1814_ROAD,
	STR_1816_TREE_LINED_ROAD,
	STR_1814_ROAD,
	STR_1814_ROAD,
};

static void GetTileDesc_Road(TileIndex tile, TileDesc *td)
{
	Owner rail_owner = INVALID_OWNER;
	Owner road_owner = INVALID_OWNER;
	Owner tram_owner = INVALID_OWNER;

	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_CROSSING: {
			td->str = STR_1818_ROAD_RAIL_LEVEL_CROSSING;
			RoadTypes rts = GetRoadTypes(tile);
			rail_owner = GetTileOwner(tile);
			if (HasBit(rts, ROADTYPE_ROAD)) road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
			if (HasBit(rts, ROADTYPE_TRAM)) tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);
			break;
		}

		case ROAD_TILE_DEPOT:
			td->str = STR_1817_ROAD_VEHICLE_DEPOT;
			road_owner = GetTileOwner(tile); // Tile has only one owner, roadtype does not matter
			break;

		default: {
			RoadTypes rts = GetRoadTypes(tile);
			td->str = (HasBit(rts, ROADTYPE_ROAD) ? _road_tile_strings[GetRoadside(tile)] : STR_TRAMWAY);
			if (HasBit(rts, ROADTYPE_ROAD)) road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
			if (HasBit(rts, ROADTYPE_TRAM)) tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);
			break;
		}
	}

	/* Now we have to discover, if the tile has only one owner or many:
	 *   - Find a first_owner of the tile. (Currently road or tram must be present, but this will break when the third type becomes available)
	 *   - Compare the found owner with the other owners, and test if they differ.
	 * Note: If road exists it will be the first_owner.
	 */
	Owner first_owner = (road_owner == INVALID_OWNER ? tram_owner : road_owner);
	bool mixed_owners = (tram_owner != INVALID_OWNER && tram_owner != first_owner) || (rail_owner != INVALID_OWNER && rail_owner != first_owner);

	if (mixed_owners) {
		/* Multiple owners */
		td->owner_type[0] = (rail_owner == INVALID_OWNER ? STR_NULL : STR_RAIL_OWNER);
		td->owner[0] = rail_owner;
		td->owner_type[1] = (road_owner == INVALID_OWNER ? STR_NULL : STR_ROAD_OWNER);
		td->owner[1] = road_owner;
		td->owner_type[2] = (tram_owner == INVALID_OWNER ? STR_NULL : STR_TRAM_OWNER);
		td->owner[2] = tram_owner;
	} else {
		/* One to rule them all */
		td->owner[0] = first_owner;
	}
}

/**
 * Given the direction the road depot is pointing, this is the direction the
 * vehicle should be travelling in in order to enter the depot.
 */
static const byte _roadveh_enter_depot_dir[4] = {
	TRACKDIR_X_SW, TRACKDIR_Y_NW, TRACKDIR_X_NE, TRACKDIR_Y_SE
};

static VehicleEnterTileStatus VehicleEnter_Road(Vehicle *v, TileIndex tile, int x, int y)
{
	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_CROSSING:
			if (v->type == VEH_TRAIN) {
				/* it should be barred */
				assert(IsCrossingBarred(tile));
			}
			break;

		case ROAD_TILE_DEPOT:
			if (v->type == VEH_ROAD &&
					v->u.road.frame == RVC_DEPOT_STOP_FRAME &&
					_roadveh_enter_depot_dir[GetRoadDepotDirection(tile)] == v->u.road.state) {
				v->u.road.state = RVSB_IN_DEPOT;
				v->vehstatus |= VS_HIDDEN;
				v->direction = ReverseDir(v->direction);
				if (v->Next() == NULL) VehicleEnterDepot(v);
				v->tile = tile;

				InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
				return VETSB_ENTERED_WORMHOLE;
			}
			break;

		default: break;
	}
	return VETSB_CONTINUE;
}


static void ChangeTileOwner_Road(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (IsRoadDepot(tile)) {
		if (GetTileOwner(tile) == old_owner) {
			if (new_owner == INVALID_OWNER) {
				DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
			} else {
				SetTileOwner(tile, new_owner);
			}
		}
		return;
	}

	for (RoadType rt = ROADTYPE_ROAD; rt < ROADTYPE_END; rt++) {
		/* Update all roadtypes, no matter if they are present */
		if (GetRoadOwner(tile, rt) == old_owner) {
			SetRoadOwner(tile, rt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
		}
	}

	if (IsLevelCrossing(tile)) {
		if (GetTileOwner(tile) == old_owner) {
			if (new_owner == INVALID_OWNER) {
				DoCommand(tile, 0, GetCrossingRailTrack(tile), DC_EXEC | DC_BANKRUPT, CMD_REMOVE_SINGLE_RAIL);
			} else {
				SetTileOwner(tile, new_owner);
			}
		}
	}
}

static CommandCost TerraformTile_Road(TileIndex tile, DoCommandFlag flags, uint z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled()) {
		switch (GetRoadTileType(tile)) {
			case ROAD_TILE_CROSSING:
				if (!IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new)) && HasBit(VALID_LEVEL_CROSSING_SLOPES, tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
				break;

			case ROAD_TILE_DEPOT:
				if (AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, GetRoadDepotDirection(tile))) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
				break;

			case ROAD_TILE_NORMAL: {
				RoadBits bits = GetAllRoadBits(tile);
				RoadBits bits_copy = bits;
				/* Check if the slope-road_bits combination is valid at all, i.e. it is safe to call GetRoadFoundation(). */
				if (!CmdFailed(CheckRoadSlope(tileh_new, &bits_copy, ROAD_NONE, ROAD_NONE))) {
					/* CheckRoadSlope() sometimes changes the road_bits, if it does not agree with them. */
					if (bits == bits_copy) {
						uint z_old;
						Slope tileh_old = GetTileSlope(tile, &z_old);

						/* Get the slope on top of the foundation */
						z_old += ApplyFoundationToSlope(GetRoadFoundation(tileh_old, bits), &tileh_old);
						z_new += ApplyFoundationToSlope(GetRoadFoundation(tileh_new, bits), &tileh_new);

						/* The surface slope must not be changed */
						if ((z_old == z_new) && (tileh_old == tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
					}
				}
				break;
			}

			default: NOT_REACHED();
		}
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

/** Tile callback functions for road tiles */
extern const TileTypeProcs _tile_type_road_procs = {
	DrawTile_Road,           // draw_tile_proc
	GetSlopeZ_Road,          // get_slope_z_proc
	ClearTile_Road,          // clear_tile_proc
	GetAcceptedCargo_Road,   // get_accepted_cargo_proc
	GetTileDesc_Road,        // get_tile_desc_proc
	GetTileTrackStatus_Road, // get_tile_track_status_proc
	ClickTile_Road,          // click_tile_proc
	AnimateTile_Road,        // animate_tile_proc
	TileLoop_Road,           // tile_loop_clear
	ChangeTileOwner_Road,    // change_tile_owner_clear
	NULL,                    // get_produced_cargo_proc
	VehicleEnter_Road,       // vehicle_enter_tile_proc
	GetFoundation_Road,      // get_foundation_proc
	TerraformTile_Road,      // terraform_tile_proc
};
