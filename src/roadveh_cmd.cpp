/* $Id$ */

/** @file roadveh_cmd.cpp Handling of road vehicles. */

#include "stdafx.h"
#include "landscape.h"
#include "roadveh.h"
#include "station_map.h"
#include "command_func.h"
#include "news_func.h"
#include "pathfind.h"
#include "npf.h"
#include "company_func.h"
#include "vehicle_gui.h"
#include "articulated_vehicles.h"
#include "newgrf_engine.h"
#include "newgrf_sound.h"
#include "yapf/yapf.h"
#include "strings_func.h"
#include "tunnelbridge_map.h"
#include "functions.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "variables.h"
#include "autoreplace_gui.h"
#include "gfx_func.h"
#include "ai/ai.hpp"
#include "depot_base.h"
#include "effectvehicle_func.h"
#include "settings_type.h"

#include "table/strings.h"
#include "table/sprites.h"

static const uint16 _roadveh_images[63] = {
	0xCD4, 0xCDC, 0xCE4, 0xCEC, 0xCF4, 0xCFC, 0xD0C, 0xD14,
	0xD24, 0xD1C, 0xD2C, 0xD04, 0xD1C, 0xD24, 0xD6C, 0xD74,
	0xD7C, 0xC14, 0xC1C, 0xC24, 0xC2C, 0xC34, 0xC3C, 0xC4C,
	0xC54, 0xC64, 0xC5C, 0xC6C, 0xC44, 0xC5C, 0xC64, 0xCAC,
	0xCB4, 0xCBC, 0xD94, 0xD9C, 0xDA4, 0xDAC, 0xDB4, 0xDBC,
	0xDCC, 0xDD4, 0xDE4, 0xDDC, 0xDEC, 0xDC4, 0xDDC, 0xDE4,
	0xE2C, 0xE34, 0xE3C, 0xC14, 0xC1C, 0xC2C, 0xC3C, 0xC4C,
	0xC5C, 0xC64, 0xC6C, 0xC74, 0xC84, 0xC94, 0xCA4
};

static const uint16 _roadveh_full_adder[63] = {
	 0,  88,   0,   0,   0,   0,  48,  48,
	48,  48,   0,   0,  64,  64,   0,  16,
	16,   0,  88,   0,   0,   0,   0,  48,
	48,  48,  48,   0,   0,  64,  64,   0,
	16,  16,   0,  88,   0,   0,   0,   0,
	48,  48,  48,  48,   0,   0,  64,  64,
	 0,  16,  16,   0,   8,   8,   8,   8,
	 0,   0,   0,   8,   8,   8,   8
};

/** 'Convert' the DiagDirection where a road vehicle enters to the trackdirs it can drive onto */
static const TrackdirBits _road_enter_dir_to_reachable_trackdirs[DIAGDIR_END] = {
	TRACKDIR_BIT_LEFT_N  | TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_X_NE,    // Enter from north east
	TRACKDIR_BIT_LEFT_S  | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_Y_SE,    // Enter from south east
	TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_X_SW    | TRACKDIR_BIT_RIGHT_S, // Enter from south west
	TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_Y_NW     // Enter from north west
};

static const Trackdir _road_reverse_table[DIAGDIR_END] = {
	TRACKDIR_RVREV_NE, TRACKDIR_RVREV_SE, TRACKDIR_RVREV_SW, TRACKDIR_RVREV_NW
};

/** 'Convert' the DiagDirection where a road vehicle should exit to
 * the trackdirs it can use to drive to the exit direction*/
static const TrackdirBits _road_exit_dir_to_incoming_trackdirs[DIAGDIR_END] = {
	TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_X_SW    | TRACKDIR_BIT_LEFT_S,
	TRACKDIR_BIT_LEFT_N  | TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_Y_NW,
	TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_X_NE,
	TRACKDIR_BIT_RIGHT_S | TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_Y_SE
};

/** Converts the exit direction of a depot to trackdir the vehicle is going to drive to */
static const Trackdir _roadveh_depot_exit_trackdir[DIAGDIR_END] = {
	TRACKDIR_X_NE, TRACKDIR_Y_SE, TRACKDIR_X_SW, TRACKDIR_Y_NW
};

static SpriteID GetRoadVehIcon(EngineID engine)
{
	uint8 spritenum = RoadVehInfo(engine)->image_index;

	if (is_custom_sprite(spritenum)) {
		SpriteID sprite = GetCustomVehicleIcon(engine, DIR_W);
		if (sprite != 0) return sprite;

		spritenum = GetEngine(engine)->image_index;
	}

	return 6 + _roadveh_images[spritenum];
}

SpriteID RoadVehicle::GetImage(Direction direction) const
{
	uint8 spritenum = this->spritenum;
	SpriteID sprite;

	if (is_custom_sprite(spritenum)) {
		sprite = GetCustomVehicleSprite(this, (Direction)(direction + 4 * IS_CUSTOM_SECONDHEAD_SPRITE(spritenum)));
		if (sprite != 0) return sprite;

		spritenum = GetEngine(this->engine_type)->image_index;
	}

	sprite = direction + _roadveh_images[spritenum];

	if (this->cargo.Count() >= this->cargo_cap / 2U) sprite += _roadveh_full_adder[spritenum];

	return sprite;
}

void DrawRoadVehEngine(int x, int y, EngineID engine, SpriteID pal)
{
	DrawSprite(GetRoadVehIcon(engine), pal, x, y);
}

byte GetRoadVehLength(const Vehicle *v)
{
	byte length = 8;

	uint16 veh_len = GetVehicleCallback(CBID_VEHICLE_LENGTH, 0, 0, v->engine_type, v);
	if (veh_len != CALLBACK_FAILED) {
		length -= Clamp(veh_len, 0, 7);
	}

	return length;
}

void RoadVehUpdateCache(Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	assert(IsRoadVehFront(v));

	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		/* Check the v->first cache. */
		assert(u->First() == v);

		/* Update the 'first engine' */
		u->u.road.first_engine = (v == u) ? INVALID_ENGINE : v->engine_type;

		/* Update the length of the vehicle. */
		u->u.road.cached_veh_length = GetRoadVehLength(u);

		/* Invalidate the vehicle colour map */
		u->colourmap = PAL_NONE;
	}
}

/** Build a road vehicle.
 * @param tile tile of depot where road vehicle is built
 * @param flags operation to perform
 * @param p1 bus/truck type being built (engine)
 * @param p2 unused
 */
CommandCost CmdBuildRoadVeh(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v;
	UnitID unit_num;

	if (!IsEngineBuildable(p1, VEH_ROAD, _current_company)) return_cmd_error(STR_ROAD_VEHICLE_NOT_AVAILABLE);

	const Engine *e = GetEngine(p1);
	/* Engines without valid cargo should not be available */
	if (e->GetDefaultCargoType() == CT_INVALID) return CMD_ERROR;

	CommandCost cost(EXPENSES_NEW_VEHICLES, e->GetCost());
	if (flags & DC_QUERY_COST) return cost;

	/* The ai_new queries the vehicle cost before building the route,
	 * so we must check against cheaters no sooner than now. --pasky */
	if (!IsRoadDepotTile(tile)) return CMD_ERROR;
	if (!IsTileOwner(tile, _current_company)) return CMD_ERROR;

	if (HasTileRoadType(tile, ROADTYPE_TRAM) != HasBit(EngInfo(p1)->misc_flags, EF_ROAD_TRAM)) return_cmd_error(STR_DEPOT_WRONG_DEPOT_TYPE);

	uint num_vehicles = 1 + CountArticulatedParts(p1, false);

	/* Allow for the front and the articulated parts, plus one to "terminate" the list. */
	Vehicle **vl = AllocaM(Vehicle*, num_vehicles + 1);
	memset(vl, 0, sizeof(*vl) * (num_vehicles + 1));

	if (!Vehicle::AllocateList(vl, num_vehicles)) {
		return_cmd_error(STR_00E1_TOO_MANY_VEHICLES_IN_GAME);
	}

	v = vl[0];

	/* find the first free roadveh id */
	unit_num = (flags & DC_AUTOREPLACE) ? 0 : GetFreeUnitNumber(VEH_ROAD);
	if (unit_num > _settings_game.vehicle.max_roadveh)
		return_cmd_error(STR_00E1_TOO_MANY_VEHICLES_IN_GAME);

	if (flags & DC_EXEC) {
		int x;
		int y;

		const RoadVehicleInfo *rvi = RoadVehInfo(p1);

		v = new (v) RoadVehicle();
		v->unitnumber = unit_num;
		v->direction = DiagDirToDir(GetRoadDepotDirection(tile));
		v->owner = _current_company;

		v->tile = tile;
		x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
		y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopeZ(x, y);

		v->running_ticks = 0;

		v->u.road.state = RVSB_IN_DEPOT;
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;

		v->spritenum = rvi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		v->cargo_subtype = 0;
		v->cargo_cap = rvi->capacity;
//		v->cargo_count = 0;
		v->value = cost.GetCost();
//		v->day_counter = 0;
//		v->next_order_param = v->next_order = 0;
//		v->load_unload_time_rem = 0;
//		v->progress = 0;

//	v->u.road.overtaking = 0;

		v->last_station_visited = INVALID_STATION;
		v->max_speed = rvi->max_speed;
		v->engine_type = (EngineID)p1;

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->lifelength * DAYS_IN_LEAP_YEAR;
		_new_vehicle_id = v->index;

		v->name = NULL;

		v->service_interval = _settings_game.vehicle.servint_roadveh;

		v->date_of_last_service = _date;
		v->build_year = _cur_year;

		v->cur_image = 0xC15;
		v->random_bits = VehicleRandomBits();
		SetRoadVehFront(v);

		v->u.road.roadtype = HasBit(EngInfo(v->engine_type)->misc_flags, EF_ROAD_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD;
		v->u.road.compatible_roadtypes = RoadTypeToRoadTypes(v->u.road.roadtype);
		v->u.road.cached_veh_length = 8;

		v->vehicle_flags = 0;
		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);

		v->cargo_cap = rvi->capacity;

		AddArticulatedParts(vl, VEH_ROAD);

		/* Call various callbacks after the whole consist has been constructed */
		for (Vehicle *u = v; u != NULL; u = u->Next()) {
			u->u.road.cached_veh_length = GetRoadVehLength(u);
			/* Cargo capacity is zero if and only if the vehicle cannot carry anything */
			if (u->cargo_cap != 0) u->cargo_cap = GetVehicleProperty(u, 0x0F, u->cargo_cap);
		}

		VehicleMove(v, false);

		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
		InvalidateWindowClassesData(WC_ROADVEH_LIST, 0);
		InvalidateWindow(WC_COMPANY, v->owner);
		if (IsLocalCompany()) {
			InvalidateAutoreplaceWindow(v->engine_type, v->group_id); // updates the replace Road window
		}

		GetCompany(_current_company)->num_engines[p1]++;

		CheckConsistencyOfArticulatedVehicle(v);
	}

	return cost;
}

void ClearSlot(Vehicle *v)
{
	RoadStop *rs = v->u.road.slot;
	if (v->u.road.slot == NULL) return;

	v->u.road.slot = NULL;
	v->u.road.slot_age = 0;

	assert(rs->num_vehicles != 0);
	rs->num_vehicles--;

	DEBUG(ms, 3, "Clearing slot at 0x%X", rs->xy);
}

bool RoadVehicle::IsStoppedInDepot() const
{
	TileIndex tile = this->tile;

	if (!IsRoadDepotTile(tile)) return false;
	if (IsRoadVehFront(this) && !(this->vehstatus & VS_STOPPED)) return false;

	for (const Vehicle *v = this; v != NULL; v = v->Next()) {
		if (v->u.road.state != RVSB_IN_DEPOT || v->tile != tile) return false;
	}
	return true;
}

/** Sell a road vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 vehicle ID to be sold
 * @param p2 unused
 */
CommandCost CmdSellRoadVeh(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v;

	if (!IsValidVehicleID(p1)) return CMD_ERROR;

	v = GetVehicle(p1);

	if (v->type != VEH_ROAD || !CheckOwnership(v->owner)) return CMD_ERROR;

	if (HASBITS(v->vehstatus, VS_CRASHED)) return_cmd_error(STR_CAN_T_SELL_DESTROYED_VEHICLE);

	if (!v->IsStoppedInDepot()) {
		return_cmd_error(STR_9013_MUST_BE_STOPPED_INSIDE);
	}

	CommandCost ret(EXPENSES_NEW_VEHICLES, -v->value);

	if (flags & DC_EXEC) {
		delete v;
	}

	return ret;
}

struct RoadFindDepotData {
	uint best_length;
	TileIndex tile;
	OwnerByte owner;
};

static const DiagDirection _road_pf_directions[] = {
	DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_SW, DIAGDIR_SE, INVALID_DIAGDIR, INVALID_DIAGDIR,
	DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NW, DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NE, INVALID_DIAGDIR, INVALID_DIAGDIR
};

static bool EnumRoadSignalFindDepot(TileIndex tile, void *data, Trackdir trackdir, uint length)
{
	RoadFindDepotData *rfdd = (RoadFindDepotData*)data;

	tile += TileOffsByDiagDir(_road_pf_directions[trackdir]);

	if (IsRoadDepotTile(tile) &&
			IsTileOwner(tile, rfdd->owner) &&
			length < rfdd->best_length) {
		rfdd->best_length = length;
		rfdd->tile = tile;
	}
	return false;
}

static const Depot *FindClosestRoadDepot(const Vehicle *v)
{
	switch (_settings_game.pf.pathfinder_for_roadvehs) {
		case VPF_YAPF: // YAPF
			return YapfFindNearestRoadDepot(v);

		case VPF_NPF: { // NPF
			/* See where we are now */
			Trackdir trackdir = GetVehicleTrackdir(v);

			NPFFoundTargetData ftd = NPFRouteToDepotBreadthFirstTwoWay(v->tile, trackdir, false, v->tile, ReverseTrackdir(trackdir), false, TRANSPORT_ROAD, v->u.road.compatible_roadtypes, v->owner, INVALID_RAILTYPES, 0);

			if (ftd.best_bird_dist == 0) return GetDepotByTile(ftd.node.tile); // Target found
		} break;

		default:
		case VPF_OPF: { // OPF
			RoadFindDepotData rfdd;

			rfdd.owner = v->owner;
			rfdd.best_length = UINT_MAX;

			/* search in all directions */
			for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
				FollowTrack(v->tile, PATHFIND_FLAGS_NONE, TRANSPORT_ROAD, v->u.road.compatible_roadtypes, d, EnumRoadSignalFindDepot, NULL, &rfdd);
			}

			if (rfdd.best_length != UINT_MAX) return GetDepotByTile(rfdd.tile);
		} break;
	}

	return NULL; // Target not found
}

bool RoadVehicle::FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse)
{
	const Depot *depot = FindClosestRoadDepot(this);

	if (depot == NULL) return false;

	if (location    != NULL) *location    = depot->xy;
	if (destination != NULL) *destination = depot->index;

	return true;
}

/** Send a road vehicle to the depot.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 vehicle ID to send to the depot
 * @param p2 various bitmasked elements
 * - p2 bit 0-3 - DEPOT_ flags (see vehicle.h)
 * - p2 bit 8-10 - VLW flag (for mass goto depot)
 */
CommandCost CmdSendRoadVehToDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (p2 & DEPOT_MASS_SEND) {
		/* Mass goto depot requested */
		if (!ValidVLWFlags(p2 & VLW_MASK)) return CMD_ERROR;
		return SendAllVehiclesToDepot(VEH_ROAD, flags, p2 & DEPOT_SERVICE, _current_company, (p2 & VLW_MASK), p1);
	}

	if (!IsValidVehicleID(p1)) return CMD_ERROR;

	Vehicle *v = GetVehicle(p1);

	if (v->type != VEH_ROAD) return CMD_ERROR;

	return v->SendToDepot(flags, (DepotCommand)(p2 & DEPOT_COMMAND_MASK));
}

/** Turn a roadvehicle around.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 vehicle ID to turn
 * @param p2 unused
 */
CommandCost CmdTurnRoadVeh(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v;

	if (!IsValidVehicleID(p1)) return CMD_ERROR;

	v = GetVehicle(p1);

	if (v->type != VEH_ROAD || !CheckOwnership(v->owner)) return CMD_ERROR;

	if (v->vehstatus & VS_STOPPED ||
			v->vehstatus & VS_CRASHED ||
			v->breakdown_ctr != 0 ||
			v->u.road.overtaking != 0 ||
			v->u.road.state == RVSB_WORMHOLE ||
			v->IsInDepot() ||
			v->cur_speed < 5) {
		return CMD_ERROR;
	}

	if (IsNormalRoadTile(v->tile) && GetDisallowedRoadDirections(v->tile) != DRD_NONE) return CMD_ERROR;

	if (IsTileType(v->tile, MP_TUNNELBRIDGE) && DirToDiagDir(v->direction) == GetTunnelBridgeDirection(v->tile)) return CMD_ERROR;

	if (flags & DC_EXEC) v->u.road.reverse_ctr = 180;

	return CommandCost();
}


void RoadVehicle::MarkDirty()
{
	for (Vehicle *v = this; v != NULL; v = v->Next()) {
		v->cur_image = v->GetImage(v->direction);
		MarkSingleVehicleDirty(v);
	}
}

void RoadVehicle::UpdateDeltaXY(Direction direction)
{
#define MKIT(a, b, c, d) ((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((c & 0xFF) << 8) | ((d & 0xFF) << 0)
	static const uint32 _delta_xy_table[8] = {
		MKIT(3, 3, -1, -1),
		MKIT(3, 7, -1, -3),
		MKIT(3, 3, -1, -1),
		MKIT(7, 3, -3, -1),
		MKIT(3, 3, -1, -1),
		MKIT(3, 7, -1, -3),
		MKIT(3, 3, -1, -1),
		MKIT(7, 3, -3, -1),
	};
#undef MKIT

	uint32 x = _delta_xy_table[direction];
	this->x_offs        = GB(x,  0, 8);
	this->y_offs        = GB(x,  8, 8);
	this->x_extent      = GB(x, 16, 8);
	this->y_extent      = GB(x, 24, 8);
	this->z_extent      = 6;
}

static void ClearCrashedStation(Vehicle *v)
{
	RoadStop *rs = GetRoadStopByTile(v->tile, GetRoadStopType(v->tile));

	/* Mark the station entrance as not busy */
	rs->SetEntranceBusy(false);

	/* Free the parking bay */
	rs->FreeBay(HasBit(v->u.road.state, RVS_USING_SECOND_BAY));
}

static void DeleteLastRoadVeh(Vehicle *v)
{
	Vehicle *u = v;
	for (; v->Next() != NULL; v = v->Next()) u = v;
	u->SetNext(NULL);

	if (IsTileType(v->tile, MP_STATION)) ClearCrashedStation(v);

	delete v;
}

static byte SetRoadVehPosition(Vehicle *v, int x, int y)
{
	byte new_z, old_z;

	/* need this hint so it returns the right z coordinate on bridges. */
	v->x_pos = x;
	v->y_pos = y;
	new_z = GetSlopeZ(x, y);

	old_z = v->z_pos;
	v->z_pos = new_z;

	VehicleMove(v, true);
	return old_z;
}

static void RoadVehSetRandomDirection(Vehicle *v)
{
	static const DirDiff delta[] = {
		DIRDIFF_45LEFT, DIRDIFF_SAME, DIRDIFF_SAME, DIRDIFF_45RIGHT
	};

	do {
		uint32 r = Random();

		v->direction = ChangeDir(v->direction, delta[r & 3]);
		v->UpdateDeltaXY(v->direction);
		v->cur_image = v->GetImage(v->direction);
		SetRoadVehPosition(v, v->x_pos, v->y_pos);
	} while ((v = v->Next()) != NULL);
}

static void RoadVehIsCrashed(Vehicle *v)
{
	v->u.road.crashed_ctr++;
	if (v->u.road.crashed_ctr == 2) {
		CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	} else if (v->u.road.crashed_ctr <= 45) {
		if ((v->tick_counter & 7) == 0) RoadVehSetRandomDirection(v);
	} else if (v->u.road.crashed_ctr >= 2220 && !(v->tick_counter & 0x1F)) {
		DeleteLastRoadVeh(v);
	}
}

static Vehicle *EnumCheckRoadVehCrashTrain(Vehicle *v, void *data)
{
	const Vehicle *u = (Vehicle*)data;

	return
		v->type == VEH_TRAIN &&
		abs(v->z_pos - u->z_pos) <= 6 &&
		abs(v->x_pos - u->x_pos) <= 4 &&
		abs(v->y_pos - u->y_pos) <= 4 ?
			v : NULL;
}

static void RoadVehCrash(Vehicle *v)
{
	uint16 pass = 1;

	v->u.road.crashed_ctr++;

	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		if (IsCargoInClass(u->cargo_type, CC_PASSENGERS)) pass += u->cargo.Count();

		u->vehstatus |= VS_CRASHED;

		MarkSingleVehicleDirty(u);
	}

	ClearSlot(v);

	InvalidateWindowWidget(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);

	AI::NewEvent(v->owner, new AIEventVehicleCrashed(v->index, v->tile, AIEventVehicleCrashed::CRASH_RV_LEVEL_CROSSING));

	SetDParam(0, pass);
	AddNewsItem(
		(pass == 1) ?
			STR_9031_ROAD_VEHICLE_CRASH_DRIVER : STR_9032_ROAD_VEHICLE_CRASH_DIE,
		NS_ACCIDENT_VEHICLE,
		v->index,
		0
	);

	ModifyStationRatingAround(v->tile, v->owner, -160, 22);
	SndPlayVehicleFx(SND_12_EXPLOSION, v);
}

static bool RoadVehCheckTrainCrash(Vehicle *v)
{
	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		if (u->u.road.state == RVSB_WORMHOLE) continue;

		TileIndex tile = u->tile;

		if (!IsLevelCrossingTile(tile)) continue;

		if (HasVehicleOnPosXY(v->x_pos, v->y_pos, u, EnumCheckRoadVehCrashTrain)) {
			RoadVehCrash(v);
			return true;
		}
	}

	return false;
}

static void HandleBrokenRoadVeh(Vehicle *v)
{
	if (v->breakdown_ctr != 1) {
		v->breakdown_ctr = 1;
		v->cur_speed = 0;

		if (v->breakdowns_since_last_service != 255)
			v->breakdowns_since_last_service++;

		InvalidateWindow(WC_VEHICLE_VIEW, v->index);
		InvalidateWindow(WC_VEHICLE_DETAILS, v->index);

		if (!PlayVehicleSound(v, VSE_BREAKDOWN)) {
			SndPlayVehicleFx((_settings_game.game_creation.landscape != LT_TOYLAND) ?
				SND_0F_VEHICLE_BREAKDOWN : SND_35_COMEDY_BREAKDOWN, v);
		}

		if (!(v->vehstatus & VS_HIDDEN)) {
			Vehicle *u = CreateEffectVehicleRel(v, 4, 4, 5, EV_BREAKDOWN_SMOKE);
			if (u != NULL) u->u.effect.animation_state = v->breakdown_delay * 2;
		}
	}

	if ((v->tick_counter & 1) == 0) {
		if (--v->breakdown_delay == 0) {
			v->breakdown_ctr = 0;
			InvalidateWindow(WC_VEHICLE_VIEW, v->index);
		}
	}
}

TileIndex RoadVehicle::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	TileIndex dest = INVALID_TILE;
	const RoadStop *rs = GetStation(station)->GetPrimaryRoadStop(this);
	if (rs != NULL) {
		uint mindist = UINT_MAX;

		for (; rs != NULL; rs = rs->GetNextRoadStop(this)) {
			uint dist = DistanceManhattan(this->tile, rs->xy);

			if (dist < mindist) {
				mindist = dist;
				dest = rs->xy;
			}
		}
	}

	if (dest != INVALID_TILE) {
		return dest;
	} else {
		/* There is no stop left at the station, so don't even TRY to go there */
		this->cur_order_index++;
		return 0;
	}
}

static void StartRoadVehSound(const Vehicle *v)
{
	if (!PlayVehicleSound(v, VSE_START)) {
		SoundFx s = RoadVehInfo(v->engine_type)->sfx;
		if (s == SND_19_BUS_START_PULL_AWAY && (v->tick_counter & 3) == 0)
			s = SND_1A_BUS_START_PULL_AWAY_WITH_HORN;
		SndPlayVehicleFx(s, v);
	}
}

struct RoadVehFindData {
	int x;
	int y;
	const Vehicle *veh;
	Vehicle *best;
	uint best_diff;
	Direction dir;
};

static Vehicle *EnumCheckRoadVehClose(Vehicle *v, void *data)
{
	static const int8 dist_x[] = { -4, -8, -4, -1, 4, 8, 4, 1 };
	static const int8 dist_y[] = { -4, -1, 4, 8, 4, 1, -4, -8 };

	RoadVehFindData *rvf = (RoadVehFindData*)data;

	short x_diff = v->x_pos - rvf->x;
	short y_diff = v->y_pos - rvf->y;

	if (v->type == VEH_ROAD &&
			!v->IsInDepot() &&
			abs(v->z_pos - rvf->veh->z_pos) < 6 &&
			v->direction == rvf->dir &&
			rvf->veh->First() != v->First() &&
			(dist_x[v->direction] >= 0 || (x_diff > dist_x[v->direction] && x_diff <= 0)) &&
			(dist_x[v->direction] <= 0 || (x_diff < dist_x[v->direction] && x_diff >= 0)) &&
			(dist_y[v->direction] >= 0 || (y_diff > dist_y[v->direction] && y_diff <= 0)) &&
			(dist_y[v->direction] <= 0 || (y_diff < dist_y[v->direction] && y_diff >= 0))) {
		uint diff = abs(x_diff) + abs(y_diff);

		if (diff < rvf->best_diff || (diff == rvf->best_diff && v->index < rvf->best->index)) {
			rvf->best = v;
			rvf->best_diff = diff;
		}
	}

	return NULL;
}

static Vehicle *RoadVehFindCloseTo(Vehicle *v, int x, int y, Direction dir)
{
	RoadVehFindData rvf;
	Vehicle *front = v->First();

	if (front->u.road.reverse_ctr != 0) return NULL;

	rvf.x = x;
	rvf.y = y;
	rvf.dir = dir;
	rvf.veh = v;
	rvf.best_diff = UINT_MAX;

	if (front->u.road.state == RVSB_WORMHOLE) {
		FindVehicleOnPos(v->tile, &rvf, EnumCheckRoadVehClose);
		FindVehicleOnPos(GetOtherTunnelBridgeEnd(v->tile), &rvf, EnumCheckRoadVehClose);
	} else {
		FindVehicleOnPosXY(x, y, &rvf, EnumCheckRoadVehClose);
	}

	/* This code protects a roadvehicle from being blocked for ever
	 * If more than 1480 / 74 days a road vehicle is blocked, it will
	 * drive just through it. The ultimate backup-code of TTD.
	 * It can be disabled. */
	if (rvf.best_diff == UINT_MAX) {
		front->u.road.blocked_ctr = 0;
		return NULL;
	}

	if (++front->u.road.blocked_ctr > 1480) return NULL;

	return rvf.best;
}

static void RoadVehArrivesAt(const Vehicle *v, Station *st)
{
	if (IsCargoInClass(v->cargo_type, CC_PASSENGERS)) {
		/* Check if station was ever visited before */
		if (!(st->had_vehicle_of_type & HVOT_BUS)) {
			st->had_vehicle_of_type |= HVOT_BUS;
			SetDParam(0, st->index);
			AddNewsItem(
				v->u.road.roadtype == ROADTYPE_ROAD ? STR_902F_CITIZENS_CELEBRATE_FIRST : STR_CITIZENS_CELEBRATE_FIRST_PASSENGER_TRAM,
				(v->owner == _local_company) ? NS_ARRIVAL_COMPANY : NS_ARRIVAL_OTHER,
				v->index,
				st->index
			);
			AI::NewEvent(v->owner, new AIEventStationFirstVehicle(st->index, v->index));
		}
	} else {
		/* Check if station was ever visited before */
		if (!(st->had_vehicle_of_type & HVOT_TRUCK)) {
			st->had_vehicle_of_type |= HVOT_TRUCK;
			SetDParam(0, st->index);
			AddNewsItem(
				v->u.road.roadtype == ROADTYPE_ROAD ? STR_9030_CITIZENS_CELEBRATE_FIRST : STR_CITIZENS_CELEBRATE_FIRST_CARGO_TRAM,
				(v->owner == _local_company) ? NS_ARRIVAL_COMPANY : NS_ARRIVAL_OTHER,
				v->index,
				st->index
			);
			AI::NewEvent(v->owner, new AIEventStationFirstVehicle(st->index, v->index));
		}
	}
}

static int RoadVehAccelerate(Vehicle *v)
{
	uint oldspeed = v->cur_speed;
	uint accel = 256 + (v->u.road.overtaking != 0 ? 256 : 0);
	uint spd = v->subspeed + accel;

	v->subspeed = (uint8)spd;

	int tempmax = v->max_speed;
	if (v->cur_speed > v->max_speed) {
		tempmax = v->cur_speed - (v->cur_speed / 10) - 1;
	}

	v->cur_speed = spd = Clamp(v->cur_speed + ((int)spd >> 8), 0, tempmax);

	/* Apply bridge speed limit */
	if (v->u.road.state == RVSB_WORMHOLE && !(v->vehstatus & VS_HIDDEN)) {
		v->cur_speed = min(v->cur_speed, GetBridgeSpec(GetBridgeType(v->tile))->speed * 2);
	}

	/* Update statusbar only if speed has changed to save CPU time */
	if (oldspeed != v->cur_speed) {
		if (_settings_client.gui.vehicle_speed) {
			InvalidateWindowWidget(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);
		}
	}

	/* Speed is scaled in the same manner as for trains. @see train_cmd.cpp */
	int scaled_spd = spd * 3 >> 2;

	scaled_spd += v->progress;
	v->progress = 0;
	return scaled_spd;
}

static Direction RoadVehGetNewDirection(const Vehicle *v, int x, int y)
{
	static const Direction _roadveh_new_dir[] = {
		DIR_N , DIR_NW, DIR_W , INVALID_DIR,
		DIR_NE, DIR_N , DIR_SW, INVALID_DIR,
		DIR_E , DIR_SE, DIR_S
	};

	x = x - v->x_pos + 1;
	y = y - v->y_pos + 1;

	if ((uint)x > 2 || (uint)y > 2) return v->direction;
	return _roadveh_new_dir[y * 4 + x];
}

static Direction RoadVehGetSlidingDirection(const Vehicle *v, int x, int y)
{
	Direction new_dir = RoadVehGetNewDirection(v, x, y);
	Direction old_dir = v->direction;
	DirDiff delta;

	if (new_dir == old_dir) return old_dir;
	delta = (DirDifference(new_dir, old_dir) > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
	return ChangeDir(old_dir, delta);
}

struct OvertakeData {
	const Vehicle *u;
	const Vehicle *v;
	TileIndex tile;
	Trackdir trackdir;
};

static Vehicle *EnumFindVehBlockingOvertake(Vehicle *v, void *data)
{
	const OvertakeData *od = (OvertakeData*)data;

	return
		v->type == VEH_ROAD && v->First() == v && v != od->u && v != od->v ?
			v : NULL;
}

/**
 * Check if overtaking is possible on a piece of track
 *
 * @param od Information about the tile and the involved vehicles
 * @return true if we have to abort overtaking
 */
static bool CheckRoadBlockedForOvertaking(OvertakeData *od)
{
	TrackStatus ts = GetTileTrackStatus(od->tile, TRANSPORT_ROAD, od->v->u.road.compatible_roadtypes);
	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts);
	TrackdirBits red_signals = TrackStatusToRedSignals(ts); // barred level crossing
	TrackBits trackbits = TrackdirBitsToTrackBits(trackdirbits);

	/* Track does not continue along overtaking direction || track has junction || levelcrossing is barred */
	if (!HasBit(trackdirbits, od->trackdir) || (trackbits & ~TRACK_BIT_CROSS) || (red_signals != TRACKDIR_BIT_NONE)) return true;

	/* Are there more vehicles on the tile except the two vehicles involved in overtaking */
	return HasVehicleOnPos(od->tile, od, EnumFindVehBlockingOvertake);
}

static void RoadVehCheckOvertake(Vehicle *v, Vehicle *u)
{
	OvertakeData od;

	od.v = v;
	od.u = u;

	if (u->max_speed >= v->max_speed &&
			!(u->vehstatus & VS_STOPPED) &&
			u->cur_speed != 0) {
		return;
	}

	/* Trams can't overtake other trams */
	if (v->u.road.roadtype == ROADTYPE_TRAM) return;

	/* Don't overtake in stations */
	if (IsTileType(v->tile, MP_STATION)) return;

	/* For now, articulated road vehicles can't overtake anything. */
	if (RoadVehHasArticPart(v)) return;

	/* Vehicles are not driving in same direction || direction is not a diagonal direction */
	if (v->direction != u->direction || !(v->direction & 1)) return;

	/* Check if vehicle is in a road stop, depot, tunnel or bridge or not on a straight road */
	if (v->u.road.state >= RVSB_IN_ROAD_STOP || !IsStraightRoadTrackdir((Trackdir)(v->u.road.state & RVSB_TRACKDIR_MASK))) return;

	od.trackdir = DiagDirToDiagTrackdir(DirToDiagDir(v->direction));

	/* Are the current and the next tile suitable for overtaking?
	 *  - Does the track continue along od.trackdir
	 *  - No junctions
	 *  - No barred levelcrossing
	 *  - No other vehicles in the way
	 */
	od.tile = v->tile;
	if (CheckRoadBlockedForOvertaking(&od)) return;

	od.tile = v->tile + TileOffsByDiagDir(DirToDiagDir(v->direction));
	if (CheckRoadBlockedForOvertaking(&od)) return;

	if (od.u->cur_speed == 0 || od.u->vehstatus& VS_STOPPED) {
		v->u.road.overtaking_ctr = 0x11;
		v->u.road.overtaking = 0x10;
	} else {
//		if (CheckRoadBlockedForOvertaking(&od)) return;
		v->u.road.overtaking_ctr = 0;
		v->u.road.overtaking = 0x10;
	}
}

static void RoadZPosAffectSpeed(Vehicle *v, byte old_z)
{
	if (old_z == v->z_pos) return;

	if (old_z < v->z_pos) {
		v->cur_speed = v->cur_speed * 232 / 256; // slow down by ~10%
	} else {
		uint16 spd = v->cur_speed + 2;
		if (spd <= v->max_speed) v->cur_speed = spd;
	}
}

static int PickRandomBit(uint bits)
{
	uint i;
	uint num = RandomRange(CountBits(bits));

	for (i = 0; !(bits & 1) || (int)--num >= 0; bits >>= 1, i++) {}
	return i;
}

struct FindRoadToChooseData {
	TileIndex dest;
	uint maxtracklen;
	uint mindist;
};

static bool EnumRoadTrackFindDist(TileIndex tile, void *data, Trackdir trackdir, uint length)
{
	FindRoadToChooseData *frd = (FindRoadToChooseData*)data;
	uint dist = DistanceManhattan(tile, frd->dest);

	if (dist <= frd->mindist) {
		if (dist != frd->mindist || length < frd->maxtracklen) {
			frd->maxtracklen = length;
		}
		frd->mindist = dist;
	}
	return false;
}

static inline NPFFoundTargetData PerfNPFRouteToStationOrTile(TileIndex tile, Trackdir trackdir, bool ignore_start_tile, NPFFindStationOrTileData *target, TransportType type, uint sub_type, Owner owner, RailTypes railtypes)
{

	void *perf = NpfBeginInterval();
	NPFFoundTargetData ret = NPFRouteToStationOrTile(tile, trackdir, ignore_start_tile, target, type, sub_type, owner, railtypes);
	int t = NpfEndInterval(perf);
	DEBUG(yapf, 4, "[NPFR] %d us - %d rounds - %d open - %d closed -- ", t, 0, _aystar_stats_open_size, _aystar_stats_closed_size);
	return ret;
}

/**
 * Returns direction to for a road vehicle to take or
 * INVALID_TRACKDIR if the direction is currently blocked
 * @param v        the Vehicle to do the pathfinding for
 * @param tile     the where to start the pathfinding
 * @param enterdir the direction the vehicle enters the tile from
 * @return the Trackdir to take
 */
static Trackdir RoadFindPathToDest(Vehicle *v, TileIndex tile, DiagDirection enterdir)
{
#define return_track(x) { best_track = (Trackdir)x; goto found_best_track; }

	TileIndex desttile;
	FindRoadToChooseData frd;
	Trackdir best_track;

	TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_ROAD, v->u.road.compatible_roadtypes);
	TrackdirBits red_signals = TrackStatusToRedSignals(ts); // crossing
	TrackdirBits trackdirs = TrackStatusToTrackdirBits(ts);

	if (IsTileType(tile, MP_ROAD)) {
		if (IsRoadDepot(tile) && (!IsTileOwner(tile, v->owner) || GetRoadDepotDirection(tile) == enterdir || (GetRoadTypes(tile) & v->u.road.compatible_roadtypes) == 0)) {
			/* Road depot owned by another company or with the wrong orientation */
			trackdirs = TRACKDIR_BIT_NONE;
		}
	} else if (IsTileType(tile, MP_STATION) && IsStandardRoadStopTile(tile)) {
		/* Standard road stop (drive-through stops are treated as normal road) */

		if (!IsTileOwner(tile, v->owner) || GetRoadStopDir(tile) == enterdir || RoadVehHasArticPart(v)) {
			/* different station owner or wrong orientation or the vehicle has articulated parts */
			trackdirs = TRACKDIR_BIT_NONE;
		} else {
			/* Our station */
			RoadStopType rstype = IsCargoInClass(v->cargo_type, CC_PASSENGERS) ? ROADSTOP_BUS : ROADSTOP_TRUCK;

			if (GetRoadStopType(tile) != rstype) {
				/* Wrong station type */
				trackdirs = TRACKDIR_BIT_NONE;
			} else {
				/* Proper station type, check if there is free loading bay */
				if (!_settings_game.pf.roadveh_queue && IsStandardRoadStopTile(tile) &&
						!GetRoadStopByTile(tile, rstype)->HasFreeBay()) {
					/* Station is full and RV queuing is off */
					trackdirs = TRACKDIR_BIT_NONE;
				}
			}
		}
	}
	/* The above lookups should be moved to GetTileTrackStatus in the
	 * future, but that requires more changes to the pathfinder and other
	 * stuff, probably even more arguments to GTTS.
	 */

	/* Remove tracks unreachable from the enter dir */
	trackdirs &= _road_enter_dir_to_reachable_trackdirs[enterdir];
	if (trackdirs == TRACKDIR_BIT_NONE) {
		/* No reachable tracks, so we'll reverse */
		return_track(_road_reverse_table[enterdir]);
	}

	if (v->u.road.reverse_ctr != 0) {
		bool reverse = true;
		if (v->u.road.roadtype == ROADTYPE_TRAM) {
			/* Trams may only reverse on a tile if it contains at least the straight
			 * trackbits or when it is a valid turning tile (i.e. one roadbit) */
			RoadBits rb = GetAnyRoadBits(tile, ROADTYPE_TRAM);
			RoadBits straight = AxisToRoadBits(DiagDirToAxis(enterdir));
			reverse = ((rb & straight) == straight) ||
			          (rb == DiagDirToRoadBits(enterdir));
		}
		if (reverse) {
			v->u.road.reverse_ctr = 0;
			if (v->tile != tile) {
				return_track(_road_reverse_table[enterdir]);
			}
		}
	}

	desttile = v->dest_tile;
	if (desttile == 0) {
		/* We've got no destination, pick a random track */
		return_track(PickRandomBit(trackdirs));
	}

	/* Only one track to choose between? */
	if (KillFirstBit(trackdirs) == TRACKDIR_BIT_NONE) {
		return_track(FindFirstBit2x64(trackdirs));
	}

	switch (_settings_game.pf.pathfinder_for_roadvehs) {
		case VPF_YAPF: { // YAPF
			Trackdir trackdir = YapfChooseRoadTrack(v, tile, enterdir);
			if (trackdir != INVALID_TRACKDIR) return_track(trackdir);
			return_track(PickRandomBit(trackdirs));
		} break;

		case VPF_NPF: { // NPF
			NPFFindStationOrTileData fstd;

			NPFFillWithOrderData(&fstd, v);
			Trackdir trackdir = DiagDirToDiagTrackdir(enterdir);
			/* debug("Finding path. Enterdir: %d, Trackdir: %d", enterdir, trackdir); */

			NPFFoundTargetData ftd = PerfNPFRouteToStationOrTile(tile - TileOffsByDiagDir(enterdir), trackdir, true, &fstd, TRANSPORT_ROAD, v->u.road.compatible_roadtypes, v->owner, INVALID_RAILTYPES);
			if (ftd.best_trackdir == INVALID_TRACKDIR) {
				/* We are already at our target. Just do something
				 * @todo: maybe display error?
				 * @todo: go straight ahead if possible? */
				return_track(FindFirstBit2x64(trackdirs));
			} else {
				/* If ftd.best_bird_dist is 0, we found our target and ftd.best_trackdir contains
				 * the direction we need to take to get there, if ftd.best_bird_dist is not 0,
				 * we did not find our target, but ftd.best_trackdir contains the direction leading
				 * to the tile closest to our target. */
				return_track(ftd.best_trackdir);
			}
		} break;

		default:
		case VPF_OPF: { // OPF
			DiagDirection dir;

			if (IsTileType(desttile, MP_ROAD)) {
				if (IsRoadDepot(desttile)) {
					dir = GetRoadDepotDirection(desttile);
					goto do_it;
				}
			} else if (IsTileType(desttile, MP_STATION)) {
				/* For drive-through stops we can head for the actual station tile */
				if (IsStandardRoadStopTile(desttile)) {
					dir = GetRoadStopDir(desttile);
do_it:;
					/* When we are heading for a depot or station, we just
					 * pretend we are heading for the tile in front, we'll
					 * see from there */
					desttile += TileOffsByDiagDir(dir);
					if (desttile == tile && trackdirs & _road_exit_dir_to_incoming_trackdirs[dir]) {
						/* If we are already in front of the
						 * station/depot and we can get in from here,
						 * we enter */
						return_track(FindFirstBit2x64(trackdirs & _road_exit_dir_to_incoming_trackdirs[dir]));
					}
				}
			}
			/* Do some pathfinding */
			frd.dest = desttile;

			best_track = INVALID_TRACKDIR;
			uint best_dist = UINT_MAX;
			uint best_maxlen = UINT_MAX;
			uint bitmask = (uint)trackdirs;
			uint i;
			FOR_EACH_SET_BIT(i, bitmask) {
				if (best_track == INVALID_TRACKDIR) best_track = (Trackdir)i; // in case we don't find the path, just pick a track
				frd.maxtracklen = UINT_MAX;
				frd.mindist = UINT_MAX;
				FollowTrack(tile, PATHFIND_FLAGS_NONE, TRANSPORT_ROAD, v->u.road.compatible_roadtypes, _road_pf_directions[i], EnumRoadTrackFindDist, NULL, &frd);

				if (frd.mindist < best_dist || (frd.mindist == best_dist && frd.maxtracklen < best_maxlen)) {
					best_dist = frd.mindist;
					best_maxlen = frd.maxtracklen;
					best_track = (Trackdir)i;
				}
			}
		} break;
	}

found_best_track:;

	if (HasBit(red_signals, best_track)) return INVALID_TRACKDIR;

	return best_track;
}

static uint RoadFindPathToStop(const Vehicle *v, TileIndex tile)
{
	if (_settings_game.pf.pathfinder_for_roadvehs == VPF_YAPF) {
		/* use YAPF */
		return YapfRoadVehDistanceToTile(v, tile);
	}

	/* use NPF */
	Trackdir trackdir = GetVehicleTrackdir(v);
	assert(trackdir != INVALID_TRACKDIR);

	NPFFindStationOrTileData fstd;
	fstd.dest_coords = tile;
	fstd.station_index = INVALID_STATION; // indicates that the destination is a tile, not a station

	uint dist = NPFRouteToStationOrTile(v->tile, trackdir, false, &fstd, TRANSPORT_ROAD, v->u.road.compatible_roadtypes, v->owner, INVALID_RAILTYPES).best_path_dist;
	/* change units from NPF_TILE_LENGTH to # of tiles */
	if (dist != UINT_MAX) dist = (dist + NPF_TILE_LENGTH - 1) / NPF_TILE_LENGTH;

	return dist;
}

struct RoadDriveEntry {
	byte x, y;
};

#include "table/roadveh_movement.h"

static const byte _road_veh_data_1[] = {
	20, 20, 16, 16, 0, 0, 0, 0,
	19, 19, 15, 15, 0, 0, 0, 0,
	16, 16, 12, 12, 0, 0, 0, 0,
	15, 15, 11, 11
};

static bool RoadVehLeaveDepot(Vehicle *v, bool first)
{
	/* Don't leave if not all the wagons are in the depot. */
	for (const Vehicle *u = v; u != NULL; u = u->Next()) {
		if (u->u.road.state != RVSB_IN_DEPOT || u->tile != v->tile) return false;
	}

	DiagDirection dir = GetRoadDepotDirection(v->tile);
	v->direction = DiagDirToDir(dir);

	Trackdir tdir = _roadveh_depot_exit_trackdir[dir];
	const RoadDriveEntry *rdp = _road_drive_data[v->u.road.roadtype][(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE) + tdir];

	int x = TileX(v->tile) * TILE_SIZE + (rdp[RVC_DEPOT_START_FRAME].x & 0xF);
	int y = TileY(v->tile) * TILE_SIZE + (rdp[RVC_DEPOT_START_FRAME].y & 0xF);

	if (first) {
		if (RoadVehFindCloseTo(v, x, y, v->direction) != NULL) return true;

		VehicleServiceInDepot(v);

		StartRoadVehSound(v);

		/* Vehicle is about to leave a depot */
		v->cur_speed = 0;
	}

	v->vehstatus &= ~VS_HIDDEN;
	v->u.road.state = tdir;
	v->u.road.frame = RVC_DEPOT_START_FRAME;

	v->UpdateDeltaXY(v->direction);
	SetRoadVehPosition(v, x, y);

	InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);

	return true;
}

static Trackdir FollowPreviousRoadVehicle(const Vehicle *v, const Vehicle *prev, TileIndex tile, DiagDirection entry_dir, bool already_reversed)
{
	if (prev->tile == v->tile && !already_reversed) {
		/* If the previous vehicle is on the same tile as this vehicle is
		 * then it must have reversed. */
		return _road_reverse_table[entry_dir];
	}

	byte prev_state = prev->u.road.state;
	Trackdir dir;

	if (prev_state == RVSB_WORMHOLE || prev_state == RVSB_IN_DEPOT) {
		DiagDirection diag_dir = INVALID_DIAGDIR;

		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			diag_dir = GetTunnelBridgeDirection(tile);
		} else if (IsRoadDepotTile(tile)) {
			diag_dir = ReverseDiagDir(GetRoadDepotDirection(tile));
		}

		if (diag_dir == INVALID_DIAGDIR) return INVALID_TRACKDIR;
		dir = DiagDirToDiagTrackdir(diag_dir);
	} else {
		if (already_reversed && prev->tile != tile) {
			/*
			 * The vehicle has reversed, but did not go straight back.
			 * It immediatelly turn onto another tile. This means that
			 * the roadstate of the previous vehicle cannot be used
			 * as the direction we have to go with this vehicle.
			 *
			 * Next table is build in the following way:
			 *  - first row for when the vehicle in front went to the northern or
			 *    western tile, second for southern and eastern.
			 *  - columns represent the entry direction.
			 *  - cell values are determined by the Trackdir one has to take from
			 *    the entry dir (column) to the tile in north or south by only
			 *    going over the trackdirs used for turning 90 degrees, i.e.
			 *    TRACKDIR_{UPPER,RIGHT,LOWER,LEFT}_{N,E,S,W}.
			 */
			Trackdir reversed_turn_lookup[2][DIAGDIR_END] = {
				{ TRACKDIR_UPPER_W, TRACKDIR_RIGHT_N, TRACKDIR_LEFT_N,  TRACKDIR_UPPER_E },
				{ TRACKDIR_RIGHT_S, TRACKDIR_LOWER_W, TRACKDIR_LOWER_E, TRACKDIR_LEFT_S  }};
			dir = reversed_turn_lookup[prev->tile < tile ? 0 : 1][ReverseDiagDir(entry_dir)];
		} else if (HasBit(prev_state, RVS_IN_DT_ROAD_STOP)) {
			dir = (Trackdir)(prev_state & RVSB_ROAD_STOP_TRACKDIR_MASK);
		} else if (prev_state < TRACKDIR_END) {
			dir = (Trackdir)prev_state;
		} else {
			return INVALID_TRACKDIR;
		}
	}

	/* Do some sanity checking. */
	static const RoadBits required_roadbits[] = {
		ROAD_X,            ROAD_Y,            ROAD_NW | ROAD_NE, ROAD_SW | ROAD_SE,
		ROAD_NW | ROAD_SW, ROAD_NE | ROAD_SE, ROAD_X,            ROAD_Y
	};
	RoadBits required = required_roadbits[dir & 0x07];

	if ((required & GetAnyRoadBits(tile, v->u.road.roadtype, true)) == ROAD_NONE) {
		dir = INVALID_TRACKDIR;
	}

	return dir;
}

/**
 * Can a tram track build without destruction on the given tile?
 * @param c the company that would be building the tram tracks
 * @param t the tile to build on.
 * @param r the road bits needed.
 * @return true when a track track can be build on 't'
 */
static bool CanBuildTramTrackOnTile(CompanyID c, TileIndex t, RoadBits r)
{
	/* The 'current' company is not necessarily the owner of the vehicle. */
	CompanyID original_company = _current_company;
	_current_company = c;

	CommandCost ret = DoCommand(t, ROADTYPE_TRAM << 4 | r, 0, DC_NONE, CMD_BUILD_ROAD);

	_current_company = original_company;
	return CmdSucceeded(ret);
}

static bool IndividualRoadVehicleController(Vehicle *v, const Vehicle *prev)
{
	if (v->u.road.overtaking != 0)  {
		if (IsTileType(v->tile, MP_STATION)) {
			/* Force us to be not overtaking! */
			v->u.road.overtaking = 0;
		} else if (++v->u.road.overtaking_ctr >= 35) {
			/* If overtaking just aborts at a random moment, we can have a out-of-bound problem,
			 *  if the vehicle started a corner. To protect that, only allow an abort of
			 *  overtake if we are on straight roads */
			if (v->u.road.state < RVSB_IN_ROAD_STOP && IsStraightRoadTrackdir((Trackdir)v->u.road.state)) {
				v->u.road.overtaking = 0;
			}
		}
	}

	/* If this vehicle is in a depot and we've reached this point it must be
	 * one of the articulated parts. It will stay in the depot until activated
	 * by the previous vehicle in the chain when it gets to the right place. */
	if (v->IsInDepot()) return true;

	if (v->u.road.state == RVSB_WORMHOLE) {
		/* Vehicle is entering a depot or is on a bridge or in a tunnel */
		GetNewVehiclePosResult gp = GetNewVehiclePos(v);

		if (IsRoadVehFront(v)) {
			const Vehicle *u = RoadVehFindCloseTo(v, gp.x, gp.y, v->direction);
			if (u != NULL) {
				v->cur_speed = u->First()->cur_speed;
				return false;
			}
		}

		if (IsTileType(gp.new_tile, MP_TUNNELBRIDGE) && HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
			/* Vehicle has just entered a bridge or tunnel */
			v->UpdateDeltaXY(v->direction);
			SetRoadVehPosition(v, gp.x, gp.y);
			return true;
		}

		v->x_pos = gp.x;
		v->y_pos = gp.y;
		VehicleMove(v, !(v->vehstatus & VS_HIDDEN));
		return true;
	}

	/* Get move position data for next frame.
	 * For a drive-through road stop use 'straight road' move data.
	 * In this case v->u.road.state is masked to give the road stop entry direction. */
	RoadDriveEntry rd = _road_drive_data[v->u.road.roadtype][(
		(HasBit(v->u.road.state, RVS_IN_DT_ROAD_STOP) ? v->u.road.state & RVSB_ROAD_STOP_TRACKDIR_MASK : v->u.road.state) +
		(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)) ^ v->u.road.overtaking][v->u.road.frame + 1];

	if (rd.x & RDE_NEXT_TILE) {
		TileIndex tile = v->tile + TileOffsByDiagDir((DiagDirection)(rd.x & 3));
		Trackdir dir;

		if (IsRoadVehFront(v)) {
			/* If this is the front engine, look for the right path. */
			dir = RoadFindPathToDest(v, tile, (DiagDirection)(rd.x & 3));
		} else {
			dir = FollowPreviousRoadVehicle(v, prev, tile, (DiagDirection)(rd.x & 3), false);
		}

		if (dir == INVALID_TRACKDIR) {
			if (!IsRoadVehFront(v)) error("Disconnecting road vehicle.");
			v->cur_speed = 0;
			return false;
		}

again:
		uint start_frame = RVC_DEFAULT_START_FRAME;
		if (IsReversingRoadTrackdir(dir)) {
			/* Turning around */
			if (v->u.road.roadtype == ROADTYPE_TRAM) {
				/* Determine the road bits the tram needs to be able to turn around
				 * using the 'big' corner loop. */
				RoadBits needed;
				switch (dir) {
					default: NOT_REACHED();
					case TRACKDIR_RVREV_NE: needed = ROAD_SW; break;
					case TRACKDIR_RVREV_SE: needed = ROAD_NW; break;
					case TRACKDIR_RVREV_SW: needed = ROAD_NE; break;
					case TRACKDIR_RVREV_NW: needed = ROAD_SE; break;
				}
				if ((v->Previous() != NULL && v->Previous()->tile == tile) ||
						(IsRoadVehFront(v) && IsNormalRoadTile(tile) && !HasRoadWorks(tile) &&
							(needed & GetRoadBits(tile, ROADTYPE_TRAM)) != ROAD_NONE)) {
					/*
					 * Taking the 'big' corner for trams only happens when:
					 * - The previous vehicle in this (articulated) tram chain is
					 *   already on the 'next' tile, we just follow them regardless of
					 *   anything. When it is NOT on the 'next' tile, the tram started
					 *   doing a reversing turn when the piece of tram track on the next
					 *   tile did not exist yet. Do not use the big tram loop as that is
					 *   going to cause the tram to split up.
					 * - Or the front of the tram can drive over the next tile.
					 */
				} else if (!IsRoadVehFront(v) || !CanBuildTramTrackOnTile(v->owner, tile, needed) || ((~needed & GetAnyRoadBits(v->tile, ROADTYPE_TRAM, false)) == ROAD_NONE)) {
					/*
					 * Taking the 'small' corner for trams only happens when:
					 * - We are not the from vehicle of an articulated tram.
					 * - Or when the company cannot build on the next tile.
					 *
					 * The 'small' corner means that the vehicle is on the end of a
					 * tram track and needs to start turning there. To do this properly
					 * the tram needs to start at an offset in the tram turning 'code'
					 * for 'big' corners. It furthermore does not go to the next tile,
					 * so that needs to be fixed too.
					 */
					tile = v->tile;
					start_frame = RVC_TURN_AROUND_START_FRAME_SHORT_TRAM;
				} else {
					/* The company can build on the next tile, so wait till (s)he does. */
					v->cur_speed = 0;
					return false;
				}
			} else if (IsNormalRoadTile(v->tile) && GetDisallowedRoadDirections(v->tile) != DRD_NONE) {
				v->cur_speed = 0;
				return false;
			} else {
				tile = v->tile;
			}
		}

		/* Get position data for first frame on the new tile */
		const RoadDriveEntry *rdp = _road_drive_data[v->u.road.roadtype][(dir + (_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)) ^ v->u.road.overtaking];

		int x = TileX(tile) * TILE_SIZE + rdp[start_frame].x;
		int y = TileY(tile) * TILE_SIZE + rdp[start_frame].y;

		Direction new_dir = RoadVehGetSlidingDirection(v, x, y);
		if (IsRoadVehFront(v)) {
			Vehicle *u = RoadVehFindCloseTo(v, x, y, new_dir);
			if (u != NULL) {
				v->cur_speed = u->First()->cur_speed;
				return false;
			}
		}

		uint32 r = VehicleEnterTile(v, tile, x, y);
		if (HasBit(r, VETS_CANNOT_ENTER)) {
			if (!IsTileType(tile, MP_TUNNELBRIDGE)) {
				v->cur_speed = 0;
				return false;
			}
			/* Try an about turn to re-enter the previous tile */
			dir = _road_reverse_table[rd.x & 3];
			goto again;
		}

		if (IsInsideMM(v->u.road.state, RVSB_IN_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) && IsTileType(v->tile, MP_STATION)) {
			if (IsReversingRoadTrackdir(dir) && IsInsideMM(v->u.road.state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END)) {
				/* New direction is trying to turn vehicle around.
				 * We can't turn at the exit of a road stop so wait.*/
				v->cur_speed = 0;
				return false;
			}
			if (IsRoadStop(v->tile)) {
				RoadStop *rs = GetRoadStopByTile(v->tile, GetRoadStopType(v->tile));

				/* Vehicle is leaving a road stop tile, mark bay as free
				 * For drive-through stops, only do it if the vehicle stopped here */
				if (IsStandardRoadStopTile(v->tile) || HasBit(v->u.road.state, RVS_IS_STOPPING)) {
					rs->FreeBay(HasBit(v->u.road.state, RVS_USING_SECOND_BAY));
					ClrBit(v->u.road.state, RVS_IS_STOPPING);
				}
				if (IsStandardRoadStopTile(v->tile)) rs->SetEntranceBusy(false);
			}
		}

		if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
			v->tile = tile;
			v->u.road.state = (byte)dir;
			v->u.road.frame = start_frame;
		}
		if (new_dir != v->direction) {
			v->direction = new_dir;
			v->cur_speed -= v->cur_speed >> 2;
		}

		v->UpdateDeltaXY(v->direction);
		RoadZPosAffectSpeed(v, SetRoadVehPosition(v, x, y));
		return true;
	}

	if (rd.x & RDE_TURNED) {
		/* Vehicle has finished turning around, it will now head back onto the same tile */
		Trackdir dir;
		uint turn_around_start_frame = RVC_TURN_AROUND_START_FRAME;

		RoadBits tram;
		if (v->u.road.roadtype == ROADTYPE_TRAM && !IsRoadDepotTile(v->tile) && CountBits(tram = GetAnyRoadBits(v->tile, ROADTYPE_TRAM, true)) == 1) {
			/*
			 * The tram is turning around with one tram 'roadbit'. This means that
			 * it is using the 'big' corner 'drive data'. However, to support the
			 * trams to take a small corner, there is a 'turned' marker in the middle
			 * of the turning 'drive data'. When the tram took the long corner, we
			 * will still use the 'big' corner drive data, but we advance it one
			 * frame. We furthermore set the driving direction so the turning is
			 * going to be properly shown.
			 */
			turn_around_start_frame = RVC_START_FRAME_AFTER_LONG_TRAM;
			switch (rd.x & 0x3) {
				default: NOT_REACHED();
				case DIAGDIR_NW: dir = TRACKDIR_RVREV_SE; break;
				case DIAGDIR_NE: dir = TRACKDIR_RVREV_SW; break;
				case DIAGDIR_SE: dir = TRACKDIR_RVREV_NW; break;
				case DIAGDIR_SW: dir = TRACKDIR_RVREV_NE; break;
			}
		} else {
			if (IsRoadVehFront(v)) {
				/* If this is the front engine, look for the right path. */
				dir = RoadFindPathToDest(v, v->tile, (DiagDirection)(rd.x & 3));
			} else {
				dir = FollowPreviousRoadVehicle(v, prev, v->tile, (DiagDirection)(rd.x & 3), true);
			}
		}

		if (dir == INVALID_TRACKDIR) {
			v->cur_speed = 0;
			return false;
		}

		const RoadDriveEntry *rdp = _road_drive_data[v->u.road.roadtype][(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE) + dir];

		int x = TileX(v->tile) * TILE_SIZE + rdp[turn_around_start_frame].x;
		int y = TileY(v->tile) * TILE_SIZE + rdp[turn_around_start_frame].y;

		Direction new_dir = RoadVehGetSlidingDirection(v, x, y);
		if (IsRoadVehFront(v) && RoadVehFindCloseTo(v, x, y, new_dir) != NULL) return false;

		uint32 r = VehicleEnterTile(v, v->tile, x, y);
		if (HasBit(r, VETS_CANNOT_ENTER)) {
			v->cur_speed = 0;
			return false;
		}

		v->u.road.state = dir;
		v->u.road.frame = turn_around_start_frame;

		if (new_dir != v->direction) {
			v->direction = new_dir;
			v->cur_speed -= v->cur_speed >> 2;
		}

		v->UpdateDeltaXY(v->direction);
		RoadZPosAffectSpeed(v, SetRoadVehPosition(v, x, y));
		return true;
	}

	/* This vehicle is not in a wormhole and it hasn't entered a new tile. If
	 * it's on a depot tile, check if it's time to activate the next vehicle in
	 * the chain yet. */
	if (v->Next() != NULL && IsRoadDepotTile(v->tile)) {
		if (v->u.road.frame == v->u.road.cached_veh_length + RVC_DEPOT_START_FRAME) {
			RoadVehLeaveDepot(v->Next(), false);
		}
	}

	/* Calculate new position for the vehicle */
	int x = (v->x_pos & ~15) + (rd.x & 15);
	int y = (v->y_pos & ~15) + (rd.y & 15);

	Direction new_dir = RoadVehGetSlidingDirection(v, x, y);

	if (IsRoadVehFront(v) && !IsInsideMM(v->u.road.state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END)) {
		/* Vehicle is not in a road stop.
		 * Check for another vehicle to overtake */
		Vehicle *u = RoadVehFindCloseTo(v, x, y, new_dir);

		if (u != NULL) {
			u = u->First();
			/* There is a vehicle in front overtake it if possible */
			if (v->u.road.overtaking == 0) RoadVehCheckOvertake(v, u);
			if (v->u.road.overtaking == 0) v->cur_speed = u->cur_speed;
			return false;
		}
	}

	Direction old_dir = v->direction;
	if (new_dir != old_dir) {
		v->direction = new_dir;
		v->cur_speed -= (v->cur_speed >> 2);
		if (old_dir != v->u.road.state) {
			/* The vehicle is in a road stop */
			v->UpdateDeltaXY(v->direction);
			SetRoadVehPosition(v, v->x_pos, v->y_pos);
			/* Note, return here means that the frame counter is not incremented
			 * for vehicles changing direction in a road stop. This causes frames to
			 * be repeated. (XXX) Is this intended? */
			return true;
		}
	}

	/* If the vehicle is in a normal road stop and the frame equals the stop frame OR
	 * if the vehicle is in a drive-through road stop and this is the destination station
	 * and it's the correct type of stop (bus or truck) and the frame equals the stop frame...
	 * (the station test and stop type test ensure that other vehicles, using the road stop as
	 * a through route, do not stop) */
	if (IsRoadVehFront(v) && ((IsInsideMM(v->u.road.state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END) &&
			_road_veh_data_1[v->u.road.state - RVSB_IN_ROAD_STOP + (_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)] == v->u.road.frame) ||
			(IsInsideMM(v->u.road.state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) &&
			v->current_order.ShouldStopAtStation(v, GetStationIndex(v->tile)) &&
			v->owner == GetTileOwner(v->tile) &&
			GetRoadStopType(v->tile) == (IsCargoInClass(v->cargo_type, CC_PASSENGERS) ? ROADSTOP_BUS : ROADSTOP_TRUCK) &&
			v->u.road.frame == RVC_DRIVE_THROUGH_STOP_FRAME))) {

		RoadStop *rs = GetRoadStopByTile(v->tile, GetRoadStopType(v->tile));
		Station *st = GetStationByTile(v->tile);

		/* Vehicle is at the stop position (at a bay) in a road stop.
		 * Note, if vehicle is loading/unloading it has already been handled,
		 * so if we get here the vehicle has just arrived or is just ready to leave. */
		if (!v->current_order.IsType(OT_LEAVESTATION)) {
			/* Vehicle has arrived at a bay in a road stop */

			if (IsDriveThroughStopTile(v->tile)) {
				TileIndex next_tile = TILE_ADD(v->tile, TileOffsByDir(v->direction));
				RoadStopType type = IsCargoInClass(v->cargo_type, CC_PASSENGERS) ? ROADSTOP_BUS : ROADSTOP_TRUCK;

				/* Check if next inline bay is free */
				if (IsDriveThroughStopTile(next_tile) && (GetRoadStopType(next_tile) == type) && GetStationIndex(v->tile) == GetStationIndex(next_tile)) {
					RoadStop *rs_n = GetRoadStopByTile(next_tile, type);

					if (rs_n->IsFreeBay(HasBit(v->u.road.state, RVS_USING_SECOND_BAY)) && rs_n->num_vehicles < RoadStop::MAX_VEHICLES) {
						/* Bay in next stop along is free - use it */
						ClearSlot(v);
						rs_n->num_vehicles++;
						v->u.road.slot = rs_n;
						v->dest_tile = rs_n->xy;
						v->u.road.slot_age = 14;

						v->u.road.frame++;
						RoadZPosAffectSpeed(v, SetRoadVehPosition(v, x, y));
						return true;
					}
				}
			}

			rs->SetEntranceBusy(false);

			v->last_station_visited = st->index;

			if (IsDriveThroughStopTile(v->tile) || (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == st->index)) {
				RoadVehArrivesAt(v, st);
				v->BeginLoading();
				return false;
			}
		} else {
			/* Vehicle is ready to leave a bay in a road stop */
			if (rs->IsEntranceBusy()) {
				/* Road stop entrance is busy, so wait as there is nowhere else to go */
				v->cur_speed = 0;
				return false;
			}
			v->current_order.Free();
			ClearSlot(v);
		}

		if (IsStandardRoadStopTile(v->tile)) rs->SetEntranceBusy(true);

		if (rs == v->u.road.slot) {
			/* We are leaving the correct station */
			ClearSlot(v);
		} else if (v->u.road.slot != NULL) {
			/* We are leaving the wrong station
			 * XXX The question is .. what to do? Actually we shouldn't be here
			 * but I guess we need to clear the slot */
			DEBUG(ms, 0, "Vehicle %d (index %d) arrived at wrong stop", v->unitnumber, v->index);
			if (v->tile != v->dest_tile) {
				DEBUG(ms, 2, " current tile 0x%X is not destination tile 0x%X. Route problem", v->tile, v->dest_tile);
			}
			if (v->dest_tile != v->u.road.slot->xy) {
				DEBUG(ms, 2, " stop tile 0x%X is not destination tile 0x%X. Multistop desync", v->u.road.slot->xy, v->dest_tile);
			}
			if (!v->current_order.IsType(OT_GOTO_STATION)) {
				DEBUG(ms, 2, " current order type (%d) is not OT_GOTO_STATION", v->current_order.GetType());
			} else {
				if (v->current_order.GetDestination() != st->index)
					DEBUG(ms, 2, " current station %d is not target station in current_order.station (%d)",
							st->index, v->current_order.GetDestination());
			}

			DEBUG(ms, 2, " force a slot clearing");
			ClearSlot(v);
		}

		StartRoadVehSound(v);
		InvalidateWindowWidget(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);
	}

	/* Check tile position conditions - i.e. stop position in depot,
	 * entry onto bridge or into tunnel */
	uint32 r = VehicleEnterTile(v, v->tile, x, y);
	if (HasBit(r, VETS_CANNOT_ENTER)) {
		v->cur_speed = 0;
		return false;
	}

	if (v->current_order.IsType(OT_LEAVESTATION) && IsDriveThroughStopTile(v->tile)) {
		v->current_order.Free();
		ClearSlot(v);
	}

	/* Move to next frame unless vehicle arrived at a stop position
	 * in a depot or entered a tunnel/bridge */
	if (!HasBit(r, VETS_ENTERED_WORMHOLE)) v->u.road.frame++;

	v->UpdateDeltaXY(v->direction);
	RoadZPosAffectSpeed(v, SetRoadVehPosition(v, x, y));
	return true;
}

static void RoadVehController(Vehicle *v)
{
	/* decrease counters */
	v->tick_counter++;
	v->current_order_time++;
	if (v->u.road.reverse_ctr != 0) v->u.road.reverse_ctr--;

	/* handle crashed */
	if (v->vehstatus & VS_CRASHED) {
		RoadVehIsCrashed(v);
		return;
	}

	RoadVehCheckTrainCrash(v);

	/* road vehicle has broken down? */
	if (v->breakdown_ctr != 0) {
		if (v->breakdown_ctr <= 2) {
			HandleBrokenRoadVeh(v);
			return;
		}
		if (!v->current_order.IsType(OT_LOADING)) v->breakdown_ctr--;
	}

	if (v->vehstatus & VS_STOPPED) return;

	ProcessOrders(v);
	v->HandleLoading();

	if (v->current_order.IsType(OT_LOADING)) return;

	if (v->IsInDepot() && RoadVehLeaveDepot(v, true)) return;

	/* Check how far the vehicle needs to proceed */
	int j = RoadVehAccelerate(v);

	int adv_spd = (v->direction & 1) ? 192 : 256;
	while (j >= adv_spd) {
		j -= adv_spd;

		Vehicle *u = v;
		for (Vehicle *prev = NULL; u != NULL; prev = u, u = u->Next()) {
			if (!IndividualRoadVehicleController(u, prev)) break;
		}

		/* 192 spd used for going straight, 256 for going diagonally. */
		adv_spd = (v->direction & 1) ? 192 : 256;

		/* Test for a collision, but only if another movement will occur. */
		if (j >= adv_spd && RoadVehCheckTrainCrash(v)) break;
	}

	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		if ((u->vehstatus & VS_HIDDEN) != 0) continue;

		uint16 old_image = u->cur_image;
		u->cur_image = u->GetImage(u->direction);
		if (old_image != u->cur_image) VehicleMove(u, true);
	}

	if (v->progress == 0) v->progress = j;
}

static void AgeRoadVehCargo(Vehicle *v)
{
	if (_age_cargo_skip_counter != 0) return;
	v->cargo.AgeCargo();
}

void RoadVehicle::Tick()
{
	AgeRoadVehCargo(this);

	if (IsRoadVehFront(this)) {
		if (!(this->vehstatus & VS_STOPPED)) this->running_ticks++;
		RoadVehController(this);
	}
}

static void CheckIfRoadVehNeedsService(Vehicle *v)
{
	/* If we already got a slot at a stop, use that FIRST, and go to a depot later */
	if (v->u.road.slot != NULL || _settings_game.vehicle.servint_roadveh == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	/* XXX If we already have a depot order, WHY do we search over and over? */
	const Depot *depot = FindClosestRoadDepot(v);

	if (depot == NULL || DistanceManhattan(v->tile, depot->xy) > 12) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			v->current_order.MakeDummy();
			InvalidateWindowWidget(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);
		}
		return;
	}

	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS &&
			!Chance16(1, 20)) {
		return;
	}

	if (v->current_order.IsType(OT_LOADING)) v->LeaveStation();
	ClearSlot(v);

	v->current_order.MakeGoToDepot(depot->index, ODTFB_SERVICE);
	v->dest_tile = depot->xy;
	InvalidateWindowWidget(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);
}

void RoadVehicle::OnNewDay()
{
	if (!IsRoadVehFront(this)) return;

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);
	if (this->u.road.blocked_ctr == 0) CheckVehicleBreakdown(this);

	AgeVehicle(this);
	CheckIfRoadVehNeedsService(this);

	CheckOrders(this);

	/* Current slot has expired */
	if (this->current_order.IsType(OT_GOTO_STATION) && this->u.road.slot != NULL && this->u.road.slot_age-- == 0) {
		DEBUG(ms, 3, "Slot expired for vehicle %d (index %d) at stop 0x%X",
			this->unitnumber, this->index, this->u.road.slot->xy);
		ClearSlot(this);
	}

	/* update destination */
	if (!(this->vehstatus & VS_STOPPED) && this->current_order.IsType(OT_GOTO_STATION) && !(this->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) && this->u.road.slot == NULL && !(this->vehstatus & VS_CRASHED)) {
		Station *st = GetStation(this->current_order.GetDestination());
		RoadStop *rs = st->GetPrimaryRoadStop(this);
		RoadStop *best = NULL;

		if (rs != NULL) {
			/* We try to obtain a slot if:
			 * 1) we're reasonably close to the primary road stop
			 * or
			 * 2) we're somewhere close to the station rectangle (to make sure we do assign
			 *    slots even if the station and its road stops are incredibly spread out)
			 */
			if (DistanceManhattan(this->tile, rs->xy) < 16 || st->rect.PtInExtendedRect(TileX(this->tile), TileY(this->tile), 2)) {
				uint dist, badness;
				uint minbadness = UINT_MAX;

				DEBUG(ms, 2, "Attempting to obtain a slot for vehicle %d (index %d) at station %d (0x%X)",
					this->unitnumber, this->index, st->index, st->xy
				);
				/* Now we find the nearest road stop that has a free slot */
				for (; rs != NULL; rs = rs->GetNextRoadStop(this)) {
					if (rs->num_vehicles >= RoadStop::MAX_VEHICLES) {
						DEBUG(ms, 4, " stop 0x%X's queue is full, not treating further", rs->xy);
						continue;
					}
					dist = RoadFindPathToStop(this, rs->xy);
					if (dist == UINT_MAX) {
						DEBUG(ms, 4, " stop 0x%X is unreachable, not treating further", rs->xy);
						continue;
					}
					badness = (rs->num_vehicles + 1) * (rs->num_vehicles + 1) + dist;

					DEBUG(ms, 4, " stop 0x%X has %d vehicle%s waiting", rs->xy, rs->num_vehicles, rs->num_vehicles == 1 ? "":"s");
					DEBUG(ms, 4, " distance is %u", dist);
					DEBUG(ms, 4, " badness %u", badness);

					if (badness < minbadness) {
						best = rs;
						minbadness = badness;
					}
				}

				if (best != NULL) {
					best->num_vehicles++;
					DEBUG(ms, 3, "Assigned to stop 0x%X", best->xy);

					this->u.road.slot = best;
					this->dest_tile = best->xy;
					this->u.road.slot_age = 14;
				} else {
					DEBUG(ms, 3, "Could not find a suitable stop");
				}
			} else {
				DEBUG(ms, 5, "Distance from station too far. Postponing slotting for vehicle %d (index %d) at station %d, (0x%X)",
						this->unitnumber, this->index, st->index, st->xy);
			}
		} else {
			DEBUG(ms, 4, "No road stop for vehicle %d (index %d) at station %d (0x%X)",
					this->unitnumber, this->index, st->index, st->xy);
		}
	}

	if (this->running_ticks == 0) return;

	CommandCost cost(EXPENSES_ROADVEH_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR * DAY_TICKS));

	this->profit_this_year -= cost.GetCost();
	this->running_ticks = 0;

	SubtractMoneyFromCompanyFract(this->owner, cost);

	InvalidateWindow(WC_VEHICLE_DETAILS, this->index);
	InvalidateWindowClasses(WC_ROADVEH_LIST);
}

/** Refit a road vehicle to the specified cargo type
 * @param tile unused
 * @param flags operation to perform
 * @param p1 Vehicle ID of the vehicle to refit
 * @param p2 Bitstuffed elements
 * - p2 = (bit 0-7) - the new cargo type to refit to
 * - p2 = (bit 8-15) - the new cargo subtype to refit to
 * - p2 = (bit 16) - refit only this vehicle
 * @return cost of refit or error
 */
CommandCost CmdRefitRoadVeh(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v;
	CommandCost cost(EXPENSES_ROADVEH_RUN);
	CargoID new_cid = GB(p2, 0, 8);
	byte new_subtype = GB(p2, 8, 8);
	bool only_this = HasBit(p2, 16);
	uint16 capacity = CALLBACK_FAILED;
	uint total_capacity = 0;

	if (!IsValidVehicleID(p1)) return CMD_ERROR;

	v = GetVehicle(p1);

	if (v->type != VEH_ROAD || !CheckOwnership(v->owner)) return CMD_ERROR;
	if (!v->IsStoppedInDepot()) return_cmd_error(STR_9013_MUST_BE_STOPPED_INSIDE);
	if (v->vehstatus & VS_CRASHED) return_cmd_error(STR_CAN_T_REFIT_DESTROYED_VEHICLE);

	if (new_cid >= NUM_CARGO) return CMD_ERROR;

	for (; v != NULL; v = (only_this ? NULL : v->Next())) {
		/* XXX: We refit all the attached wagons en-masse if they can be
		 * refitted. This is how TTDPatch does it.  TODO: Have some nice
		 * [Refit] button near each wagon. */
		if (!CanRefitTo(v->engine_type, new_cid)) continue;

		const Engine *e = GetEngine(v->engine_type);
		if (!e->CanCarryCargo()) continue;

		if (HasBit(EngInfo(v->engine_type)->callbackmask, CBM_VEHICLE_REFIT_CAPACITY)) {
			/* Back up the cargo type */
			CargoID temp_cid = v->cargo_type;
			byte temp_subtype = v->cargo_subtype;
			v->cargo_type = new_cid;
			v->cargo_subtype = new_subtype;

			/* Check the refit capacity callback */
			capacity = GetVehicleCallback(CBID_VEHICLE_REFIT_CAPACITY, 0, 0, v->engine_type, v);

			/* Restore the original cargo type */
			v->cargo_type = temp_cid;
			v->cargo_subtype = temp_subtype;
		}

		if (capacity == CALLBACK_FAILED) {
			/* callback failed or not used, use default capacity */

			CargoID old_cid = e->GetDefaultCargoType();
			/* normally, the capacity depends on the cargo type, a vehicle can
			 * carry twice as much mail/goods as normal cargo, and four times as
			 * many passengers
			 */
			capacity = GetVehicleProperty(v, 0x0F, e->u.road.capacity);
			switch (old_cid) {
				case CT_PASSENGERS: break;
				case CT_MAIL:
				case CT_GOODS: capacity *= 2; break;
				default:       capacity *= 4; break;
			}
			switch (new_cid) {
				case CT_PASSENGERS: break;
				case CT_MAIL:
				case CT_GOODS: capacity /= 2; break;
				default:       capacity /= 4; break;
			}
		}

		total_capacity += capacity;

		if (new_cid != v->cargo_type) {
			cost.AddCost(GetRefitCost(v->engine_type));
		}

		if (flags & DC_EXEC) {
			v->cargo_cap = capacity;
			v->cargo.Truncate((v->cargo_type == new_cid) ? capacity : 0);
			v->cargo_type = new_cid;
			v->cargo_subtype = new_subtype;
			InvalidateWindow(WC_VEHICLE_DETAILS, v->index);
			InvalidateWindow(WC_VEHICLE_DEPOT, v->tile);
			InvalidateWindowClassesData(WC_ROADVEH_LIST, 0);
		}
	}

	if (flags & DC_EXEC) RoadVehUpdateCache(GetVehicle(p1)->First());

	_returned_refit_capacity = total_capacity;

	return cost;
}
