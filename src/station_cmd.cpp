/* $Id$ */

/** @file station_cmd.cpp Handling of station tiles. */

#include "stdafx.h"
#include "openttd.h"
#include "aircraft.h"
#include "bridge_map.h"
#include "cmd_helper.h"
#include "landscape.h"
#include "viewport_func.h"
#include "command_func.h"
#include "town.h"
#include "news_func.h"
#include "train.h"
#include "roadveh.h"
#include "industry_map.h"
#include "newgrf_station.h"
#include "newgrf_commons.h"
#include "yapf/yapf.h"
#include "road_internal.h" /* For drawing catenary/checking road removal */
#include "variables.h"
#include "autoslope.h"
#include "water.h"
#include "station_gui.h"
#include "strings_func.h"
#include "functions.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "oldpool_func.h"
#include "animated_tile_func.h"
#include "elrail_func.h"

#include "table/strings.h"

DEFINE_OLD_POOL_GENERIC(Station, Station)
DEFINE_OLD_POOL_GENERIC(RoadStop, RoadStop)


/**
 * Check whether the given tile is a hangar.
 * @param t the tile to of whether it is a hangar.
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a hangar.
 */
bool IsHangar(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));

	const Station *st = GetStationByTile(t);
	const AirportFTAClass *apc = st->Airport();

	for (uint i = 0; i < apc->nof_depots; i++) {
		if (st->airport_tile + ToTileIndexDiff(apc->airport_depots[i]) == t) return true;
	}

	return false;
}

RoadStop *GetRoadStopByTile(TileIndex tile, RoadStopType type)
{
	const Station *st = GetStationByTile(tile);

	for (RoadStop *rs = st->GetPrimaryRoadStop(type);; rs = rs->next) {
		if (rs->xy == tile) return rs;
		assert(rs->next != NULL);
	}
}


static uint GetNumRoadStopsInStation(const Station *st, RoadStopType type)
{
	uint num = 0;

	assert(st != NULL);
	for (const RoadStop *rs = st->GetPrimaryRoadStop(type); rs != NULL; rs = rs->next) {
		num++;
	}

	return num;
}


#define CHECK_STATIONS_ERR ((Station*)-1)

static Station *GetStationAround(TileIndex tile, int w, int h, StationID closest_station)
{
	/* check around to see if there's any stations there */
	BEGIN_TILE_LOOP(tile_cur, w + 2, h + 2, tile - TileDiffXY(1, 1))
		if (IsTileType(tile_cur, MP_STATION)) {
			StationID t = GetStationIndex(tile_cur);

			if (closest_station == INVALID_STATION) {
				closest_station = t;
			} else if (closest_station != t) {
				_error_message = STR_3006_ADJOINS_MORE_THAN_ONE_EXISTING;
				return CHECK_STATIONS_ERR;
			}
		}
	END_TILE_LOOP(tile_cur, w + 2, h + 2, tile - TileDiffXY(1, 1))
	return (closest_station == INVALID_STATION) ? NULL : GetStation(closest_station);
}

/**
 * Function to check whether the given tile matches some criterion.
 * @param tile the tile to check
 * @return true if it matches, false otherwise
 */
typedef bool (*CMSAMatcher)(TileIndex tile);

/**
 * Counts the numbers of tiles matching a specific type in the area around
 * @param tile the center tile of the 'count area'
 * @param type the type of tile searched for
 * @param industry when type == MP_INDUSTRY, the type of the industry,
 *                 in all other cases this parameter is ignored
 * @return the result the noumber of matching tiles around
 */
static int CountMapSquareAround(TileIndex tile, CMSAMatcher cmp)
{
	int num = 0;

	for (int dx = -3; dx <= 3; dx++) {
		for (int dy = -3; dy <= 3; dy++) {
			TileIndex t = TileAddWrap(tile, dx, dy);
			if (t != INVALID_TILE && cmp(t)) num++;
		}
	}

	return num;
}

/**
 * Check whether the tile is a mine.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a mine
 */
static bool CMSAMine(TileIndex tile)
{
	/* No industry */
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	const Industry *ind = GetIndustryByTile(tile);

	/* No extractive industry */
	if ((GetIndustrySpec(ind->type)->life_type & INDUSTRYLIFE_EXTRACTIVE) == 0) return false;

	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		/* The industry extracts something non-liquid, i.e. no oil or plastic, so it is a mine */
		if (ind->produced_cargo[i] != CT_INVALID && (GetCargo(ind->produced_cargo[i])->classes & CC_LIQUID) == 0) return true;
	}

	return false;
}

/**
 * Check whether the tile is water.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a mine
 */
static bool CMSAWater(TileIndex tile)
{
	return IsTileType(tile, MP_WATER) && IsWater(tile);
}

/**
 * Check whether the tile is a tree.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a mine
 */
static bool CMSATree(TileIndex tile)
{
	return IsTileType(tile, MP_TREES);
}

/**
 * Check whether the tile is a forest.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a mine
 */
static bool CMSAForest(TileIndex tile)
{
	/* No industry */
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	const Industry *ind = GetIndustryByTile(tile);

	/* No extractive industry */
	if ((GetIndustrySpec(ind->type)->life_type & INDUSTRYLIFE_ORGANIC) == 0) return false;

	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		/* The industry produces wood. */
		if (ind->produced_cargo[i] != CT_INVALID && GetCargo(ind->produced_cargo[i])->label == 'WOOD') return true;
	}

	return false;
}

#define M(x) ((x) - STR_SV_STNAME)

enum StationNaming {
	STATIONNAMING_RAIL = 0,
	STATIONNAMING_ROAD = 0,
	STATIONNAMING_AIRPORT,
	STATIONNAMING_OILRIG,
	STATIONNAMING_DOCK,
	STATIONNAMING_BUOY,
	STATIONNAMING_HELIPORT,
};

/** Information to handle station action 0 property 24 correctly */
struct StationNameInformation {
	uint32 free_names; ///< Current bitset of free names (we can remove names).
	bool *indtypes;    ///< Array of bools telling whether an industry type has been found.
};

/**
 * Find a station action 0 property 24 station name, or reduce the
 * free_names if needed.
 * @param tile the tile to search
 * @param user_data the StationNameInformation to base the search on
 * @return true if the tile contains an industry that has not given
 *              it's name to one of the other stations in town.
 */
static bool FindNearIndustryName(TileIndex tile, void *user_data)
{
	/* All already found industry types */
	StationNameInformation *sni = (StationNameInformation*)user_data;
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	/* If the station name is undefined it means that it doesn't name a station */
	IndustryType indtype = GetIndustryType(tile);
	if (GetIndustrySpec(indtype)->station_name == STR_UNDEFINED) return false;

	/* In all cases if an industry that provides a name is found two of
	 * the standard names will be disabled. */
	sni->free_names &= ~(1 << M(STR_SV_STNAME_OILFIELD) | 1 << M(STR_SV_STNAME_MINES));
	return !sni->indtypes[indtype];
}

static StringID GenerateStationName(Station *st, TileIndex tile, int flag)
{
	static const uint32 _gen_station_name_bits[] = {
		0,                                      // 0
		1 << M(STR_SV_STNAME_AIRPORT),          // 1
		1 << M(STR_SV_STNAME_OILFIELD),         // 2
		1 << M(STR_SV_STNAME_DOCKS),            // 3
		0x1FF << M(STR_SV_STNAME_BUOY_1),       // 4
		1 << M(STR_SV_STNAME_HELIPORT),         // 5
	};

	const Town *t = st->town;
	uint32 free_names = UINT32_MAX;

	bool indtypes[NUM_INDUSTRYTYPES];
	memset(indtypes, 0, sizeof(indtypes));

	const Station *s;
	FOR_ALL_STATIONS(s) {
		if (s != st && s->town == t) {
			if (s->indtype != IT_INVALID) {
				indtypes[s->indtype] = true;
				continue;
			}
			uint str = M(s->string_id);
			if (str <= 0x20) {
				if (str == M(STR_SV_STNAME_FOREST)) {
					str = M(STR_SV_STNAME_WOODS);
				}
				ClrBit(free_names, str);
			}
		}
	}

	if (flag != STATIONNAMING_BUOY) {
		TileIndex indtile = tile;
		StationNameInformation sni = { free_names, indtypes };
		if (CircularTileSearch(&indtile, 7, FindNearIndustryName, &sni)) {
			/* An industry has been found nearby */
			IndustryType indtype = GetIndustryType(indtile);
			const IndustrySpec *indsp = GetIndustrySpec(indtype);
			/* STR_NULL means it only disables oil rig/mines */
			if (indsp->station_name != STR_NULL) {
				st->indtype = indtype;
				return STR_SV_STNAME_FALLBACK;
			}
		}

		/* Oil rigs/mines name could be marked not free by looking for a near by industry. */
		free_names = sni.free_names;
	}

	/* check default names */
	uint32 tmp = free_names & _gen_station_name_bits[flag];
	if (tmp != 0) return STR_SV_STNAME + FindFirstBit(tmp);

	/* check mine? */
	if (HasBit(free_names, M(STR_SV_STNAME_MINES))) {
		if (CountMapSquareAround(tile, CMSAMine) >= 2) {
			return STR_SV_STNAME_MINES;
		}
	}

	/* check close enough to town to get central as name? */
	if (DistanceMax(tile, t->xy) < 8) {
		if (HasBit(free_names, M(STR_SV_STNAME))) return STR_SV_STNAME;

		if (HasBit(free_names, M(STR_SV_STNAME_CENTRAL))) return STR_SV_STNAME_CENTRAL;
	}

	/* Check lakeside */
	if (HasBit(free_names, M(STR_SV_STNAME_LAKESIDE)) &&
			DistanceFromEdge(tile) < 20 &&
			CountMapSquareAround(tile, CMSAWater) >= 5) {
		return STR_SV_STNAME_LAKESIDE;
	}

	/* Check woods */
	if (HasBit(free_names, M(STR_SV_STNAME_WOODS)) && (
				CountMapSquareAround(tile, CMSATree) >= 8 ||
				CountMapSquareAround(tile, CMSAForest) >= 2)
			) {
		return _settings_game.game_creation.landscape == LT_TROPIC ? STR_SV_STNAME_FOREST : STR_SV_STNAME_WOODS;
	}

	/* check elevation compared to town */
	uint z = GetTileZ(tile);
	uint z2 = GetTileZ(t->xy);
	if (z < z2) {
		if (HasBit(free_names, M(STR_SV_STNAME_VALLEY))) return STR_SV_STNAME_VALLEY;
	} else if (z > z2) {
		if (HasBit(free_names, M(STR_SV_STNAME_HEIGHTS))) return STR_SV_STNAME_HEIGHTS;
	}

	/* check direction compared to town */
	static const int8 _direction_and_table[] = {
		~( (1 << M(STR_SV_STNAME_WEST))  | (1 << M(STR_SV_STNAME_EAST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_WEST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_EAST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_WEST)) | (1 << M(STR_SV_STNAME_EAST)) ),
	};

	free_names &= _direction_and_table[
		(TileX(tile) < TileX(t->xy)) +
		(TileY(tile) < TileY(t->xy)) * 2];

	tmp = free_names & ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 12) | (1 << 26) | (1 << 27) | (1 << 28) | (1 << 29) | (1 << 30));
	return (tmp == 0) ? STR_SV_STNAME_FALLBACK : (STR_SV_STNAME + FindFirstBit(tmp));
}
#undef M

/**
 * Find the closest deleted station of the current company
 * @param tile the tile to search from.
 * @return the closest station or NULL if too far.
 */
static Station *GetClosestDeletedStation(TileIndex tile)
{
	uint threshold = 8;
	Station *best_station = NULL;
	Station *st;

	FOR_ALL_STATIONS(st) {
		if (st->facilities == 0 && st->owner == _current_company) {
			uint cur_dist = DistanceManhattan(tile, st->xy);

			if (cur_dist < threshold) {
				threshold = cur_dist;
				best_station = st;
			}
		}
	}

	return best_station;
}

/** Update the virtual coords needed to draw the station sign.
 * @param st Station to update for.
 */
static void UpdateStationVirtCoord(Station *st)
{
	Point pt = RemapCoords2(TileX(st->xy) * TILE_SIZE, TileY(st->xy) * TILE_SIZE);

	pt.y -= 32;
	if (st->facilities & FACIL_AIRPORT && st->airport_type == AT_OILRIG) pt.y -= 16;

	SetDParam(0, st->index);
	SetDParam(1, st->facilities);
	UpdateViewportSignPos(&st->sign, pt.x, pt.y, STR_305C_0);
}

/** Update the virtual coords needed to draw the station sign for all stations. */
void UpdateAllStationVirtCoord()
{
	Station *st;

	FOR_ALL_STATIONS(st) {
		UpdateStationVirtCoord(st);
	}
}

/**
 * Update the station virt coords while making the modified parts dirty.
 *
 * This function updates the virt coords and mark the modified parts as dirty
 *
 * @param st The station to update the virt coords
 * @ingroup dirty
 */
static void UpdateStationVirtCoordDirty(Station *st)
{
	st->MarkDirty();
	UpdateStationVirtCoord(st);
	st->MarkDirty();
}

/** Get a mask of the cargo types that the station accepts.
 * @param st Station to query
 * @return the expected mask
 */
static uint GetAcceptanceMask(const Station *st)
{
	uint mask = 0;

	for (CargoID i = 0; i < NUM_CARGO; i++) {
		if (HasBit(st->goods[i].acceptance_pickup, GoodsEntry::ACCEPTANCE)) mask |= 1 << i;
	}
	return mask;
}

/** Items contains the two cargo names that are to be accepted or rejected.
 * msg is the string id of the message to display.
 */
static void ShowRejectOrAcceptNews(const Station *st, uint num_items, CargoID *cargo, StringID msg)
{
	for (uint i = 0; i < num_items; i++) {
		SetDParam(i + 1, GetCargo(cargo[i])->name);
	}

	SetDParam(0, st->index);
	AddNewsItem(msg, NS_ACCEPTANCE, st->xy, st->index);
}

/**
 * Get a list of the cargo types being produced around the tile (in a rectangle).
 * @param produced Destination array of produced cargo
 * @param tile Northtile of area
 * @param w X extent of the area
 * @param h Y extent of the area
 * @param rad Search radius in addition to the given area
 */
void GetProductionAroundTiles(AcceptedCargo produced, TileIndex tile,
	int w, int h, int rad)
{
	memset(produced, 0, sizeof(AcceptedCargo)); // sizeof(AcceptedCargo) != sizeof(produced) (== sizeof(uint *))

	int x = TileX(tile);
	int y = TileY(tile);

	/* expand the region by rad tiles on each side
	 * while making sure that we remain inside the board. */
	int x2 = min(x + w + rad, MapSizeX());
	int x1 = max(x - rad, 0);

	int y2 = min(y + h + rad, MapSizeY());
	int y1 = max(y - rad, 0);

	assert(x1 < x2);
	assert(y1 < y2);
	assert(w > 0);
	assert(h > 0);

	for (int yc = y1; yc != y2; yc++) {
		for (int xc = x1; xc != x2; xc++) {
			TileIndex tile = TileXY(xc, yc);

			if (!IsTileType(tile, MP_STATION)) {
				GetProducedCargoProc *gpc = _tile_type_procs[GetTileType(tile)]->get_produced_cargo_proc;
				if (gpc != NULL) {
					CargoID cargos[256]; // Required for CBID_HOUSE_PRODUCE_CARGO.
					memset(cargos, CT_INVALID, sizeof(cargos));

					gpc(tile, cargos);
					for (uint i = 0; i < lengthof(cargos); ++i) {
						if (cargos[i] != CT_INVALID) produced[cargos[i]]++;
					}
				}
			}
		}
	}
}

/**
 * Get a list of the cargo types that are accepted around the tile.
 * @param accepts Destination array of accepted cargo
 * @param tile Center of the search area
 * @param w X extent of area
 * @param h Y extent of area
 * @param rad Search radius in addition to given area
 */
void GetAcceptanceAroundTiles(AcceptedCargo accepts, TileIndex tile,
	int w, int h, int rad)
{
	memset(accepts, 0, sizeof(AcceptedCargo)); // sizeof(AcceptedCargo) != sizeof(accepts) (== sizeof(uint *))

	int x = TileX(tile);
	int y = TileY(tile);

	/* expand the region by rad tiles on each side
	 * while making sure that we remain inside the board. */
	int x2 = min(x + w + rad, MapSizeX());
	int y2 = min(y + h + rad, MapSizeY());
	int x1 = max(x - rad, 0);
	int y1 = max(y - rad, 0);

	assert(x1 < x2);
	assert(y1 < y2);
	assert(w > 0);
	assert(h > 0);

	for (int yc = y1; yc != y2; yc++) {
		for (int xc = x1; xc != x2; xc++) {
			TileIndex tile = TileXY(xc, yc);

			if (!IsTileType(tile, MP_STATION)) {
				AcceptedCargo ac;

				GetAcceptedCargo(tile, ac);
				for (uint i = 0; i < lengthof(ac); ++i) accepts[i] += ac[i];
			}
		}
	}
}

static inline void MergePoint(Rect *rect, TileIndex tile)
{
	int x = TileX(tile);
	int y = TileY(tile);

	if (rect->left   > x) rect->left   = x;
	if (rect->bottom > y) rect->bottom = y;
	if (rect->right  < x) rect->right  = x;
	if (rect->top    < y) rect->top    = y;
}

/** Update the acceptance for a station.
 * @param st Station to update
 * @param show_msg controls whether to display a message that acceptance was changed.
 */
static void UpdateStationAcceptance(Station *st, bool show_msg)
{
	/* Don't update acceptance for a buoy */
	if (st->IsBuoy()) return;

	Rect rect;
	rect.left   = MapSizeX();
	rect.bottom = MapSizeY();
	rect.right  = 0;
	rect.top    = 0;

	/* old accepted goods types */
	uint old_acc = GetAcceptanceMask(st);

	/* Put all the tiles that span an area in the table. */
	if (st->train_tile != INVALID_TILE) {
		MergePoint(&rect, st->train_tile);
		MergePoint(&rect, st->train_tile + TileDiffXY(st->trainst_w - 1, st->trainst_h - 1));
	}

	if (st->airport_tile != INVALID_TILE) {
		const AirportFTAClass *afc = st->Airport();

		MergePoint(&rect, st->airport_tile);
		MergePoint(&rect, st->airport_tile + TileDiffXY(afc->size_x - 1, afc->size_y - 1));
	}

	if (st->dock_tile != INVALID_TILE) {
		MergePoint(&rect, st->dock_tile);
		if (IsDockTile(st->dock_tile)) {
			MergePoint(&rect, st->dock_tile + TileOffsByDiagDir(GetDockDirection(st->dock_tile)));
		} // else OilRig
	}

	for (const RoadStop *rs = st->bus_stops; rs != NULL; rs = rs->next) {
		MergePoint(&rect, rs->xy);
	}

	for (const RoadStop *rs = st->truck_stops; rs != NULL; rs = rs->next) {
		MergePoint(&rect, rs->xy);
	}

	/* And retrieve the acceptance. */
	AcceptedCargo accepts;
	assert((rect.right >= rect.left) == !st->rect.IsEmpty());
	if (rect.right >= rect.left) {
		assert(rect.left == st->rect.left);
		assert(rect.top == st->rect.bottom);
		assert(rect.right == st->rect.right);
		assert(rect.bottom == st->rect.top);
		GetAcceptanceAroundTiles(
			accepts,
			TileXY(rect.left, rect.bottom),
			rect.right - rect.left   + 1,
			rect.top   - rect.bottom + 1,
			st->GetCatchmentRadius()
		);
	} else {
		memset(accepts, 0, sizeof(accepts));
	}

	/* Adjust in case our station only accepts fewer kinds of goods */
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		uint amt = min(accepts[i], 15);

		/* Make sure the station can accept the goods type. */
		bool is_passengers = IsCargoInClass(i, CC_PASSENGERS);
		if ((!is_passengers && !(st->facilities & (byte)~FACIL_BUS_STOP)) ||
				(is_passengers && !(st->facilities & (byte)~FACIL_TRUCK_STOP))) {
			amt = 0;
		}

		SB(st->goods[i].acceptance_pickup, GoodsEntry::ACCEPTANCE, 1, amt >= 8);
	}

	/* Only show a message in case the acceptance was actually changed. */
	uint new_acc = GetAcceptanceMask(st);
	if (old_acc == new_acc) return;

	/* show a message to report that the acceptance was changed? */
	if (show_msg && st->owner == _local_company && st->facilities) {
		/* List of accept and reject strings for different number of
		 * cargo types */
		static const StringID accept_msg[] = {
			STR_3040_NOW_ACCEPTS,
			STR_3041_NOW_ACCEPTS_AND,
		};
		static const StringID reject_msg[] = {
			STR_303E_NO_LONGER_ACCEPTS,
			STR_303F_NO_LONGER_ACCEPTS_OR,
		};

		/* Array of accepted and rejected cargo types */
		CargoID accepts[2] = { CT_INVALID, CT_INVALID };
		CargoID rejects[2] = { CT_INVALID, CT_INVALID };
		uint num_acc = 0;
		uint num_rej = 0;

		/* Test each cargo type to see if its acceptange has changed */
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (HasBit(new_acc, i)) {
				if (!HasBit(old_acc, i) && num_acc < lengthof(accepts)) {
					/* New cargo is accepted */
					accepts[num_acc++] = i;
				}
			} else {
				if (HasBit(old_acc, i) && num_rej < lengthof(rejects)) {
					/* Old cargo is no longer accepted */
					rejects[num_rej++] = i;
				}
			}
		}

		/* Show news message if there are any changes */
		if (num_acc > 0) ShowRejectOrAcceptNews(st, num_acc, accepts, accept_msg[num_acc - 1]);
		if (num_rej > 0) ShowRejectOrAcceptNews(st, num_rej, rejects, reject_msg[num_rej - 1]);
	}

	/* redraw the station view since acceptance changed */
	InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_ACCEPTLIST);
}

static void UpdateStationSignCoord(Station *st)
{
	const StationRect *r = &st->rect;

	if (r->IsEmpty()) return; // no tiles belong to this station

	/* clamp sign coord to be inside the station rect */
	st->xy = TileXY(ClampU(TileX(st->xy), r->left, r->right), ClampU(TileY(st->xy), r->top, r->bottom));
	UpdateStationVirtCoordDirty(st);
}

/** This is called right after a station was deleted.
 * It checks if the whole station is free of substations, and if so, the station will be
 * deleted after a little while.
 * @param st Station
 */
static void DeleteStationIfEmpty(Station *st)
{
	if (st->facilities == 0) {
		st->delete_ctr = 0;
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
	}
	/* station remains but it probably lost some parts - station sign should stay in the station boundaries */
	UpdateStationSignCoord(st);
}

static CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags);

/** Tries to clear the given area.
 * @param tile TileIndex to start check
 * @param w width of search area
 * @param h height of search area
 * @param flags operation to perform
 * @param invalid_dirs prohibited directions (set of DiagDirections)
 * @param station StationID to be queried and returned if available
 * @param check_clear if clearing tile should be performed (in wich case, cost will be added)
 * @return the cost in case of success, or an error code if it failed.
 */
CommandCost CheckFlatLandBelow(TileIndex tile, uint w, uint h, DoCommandFlag flags, uint invalid_dirs, StationID *station, bool check_clear = true)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	int allowed_z = -1;

	BEGIN_TILE_LOOP(tile_cur, w, h, tile) {
		if (MayHaveBridgeAbove(tile_cur) && IsBridgeAbove(tile_cur)) {
			return_cmd_error(STR_5007_MUST_DEMOLISH_BRIDGE_FIRST);
		}

		if (!EnsureNoVehicleOnGround(tile_cur)) return CMD_ERROR;

		uint z;
		Slope tileh = GetTileSlope(tile_cur, &z);

		/* Prohibit building if
		 *   1) The tile is "steep" (i.e. stretches two height levels)
		 *   2) The tile is non-flat and the build_on_slopes switch is disabled
		 */
		if (IsSteepSlope(tileh) ||
				((!_settings_game.construction.build_on_slopes) && tileh != SLOPE_FLAT)) {
			return_cmd_error(STR_0007_FLAT_LAND_REQUIRED);
		}

		int flat_z = z;
		if (tileh != SLOPE_FLAT) {
			/* need to check so the entrance to the station is not pointing at a slope.
			 * This must be valid for all station tiles, as the user can remove single station tiles. */
			if ((HasBit(invalid_dirs, DIAGDIR_NE) && !(tileh & SLOPE_NE)) ||
			    (HasBit(invalid_dirs, DIAGDIR_SE) && !(tileh & SLOPE_SE)) ||
			    (HasBit(invalid_dirs, DIAGDIR_SW) && !(tileh & SLOPE_SW)) ||
			    (HasBit(invalid_dirs, DIAGDIR_NW) && !(tileh & SLOPE_NW))) {
				return_cmd_error(STR_0007_FLAT_LAND_REQUIRED);
			}
			cost.AddCost(_price.terraform);
			flat_z += TILE_HEIGHT;
		}

		/* get corresponding flat level and make sure that all parts of the station have the same level. */
		if (allowed_z == -1) {
			/* first tile */
			allowed_z = flat_z;
		} else if (allowed_z != flat_z) {
			return_cmd_error(STR_0007_FLAT_LAND_REQUIRED);
		}

		/* if station is set, then we have special handling to allow building on top of already existing stations.
		 * so station points to INVALID_STATION if we can build on any station.
		 * Or it points to a station if we're only allowed to build on exactly that station. */
		if (station != NULL && IsTileType(tile_cur, MP_STATION)) {
			if (!IsRailwayStation(tile_cur)) {
				return ClearTile_Station(tile_cur, DC_AUTO); // get error message
			} else {
				StationID st = GetStationIndex(tile_cur);
				if (*station == INVALID_STATION) {
					*station = st;
				} else if (*station != st) {
					return_cmd_error(STR_3006_ADJOINS_MORE_THAN_ONE_EXISTING);
				}
			}
		} else if (check_clear) {
			CommandCost ret = DoCommand(tile_cur, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (CmdFailed(ret)) return ret;
			cost.AddCost(ret);
		}
	} END_TILE_LOOP(tile_cur, w, h, tile)

	return cost;
}

static bool CanExpandRailroadStation(const Station *st, uint *fin, Axis axis)
{
	uint curw = st->trainst_w;
	uint curh = st->trainst_h;
	TileIndex tile = fin[0];
	uint w = fin[1];
	uint h = fin[2];

	if (_settings_game.station.nonuniform_stations) {
		/* determine new size of train station region.. */
		int x = min(TileX(st->train_tile), TileX(tile));
		int y = min(TileY(st->train_tile), TileY(tile));
		curw = max(TileX(st->train_tile) + curw, TileX(tile) + w) - x;
		curh = max(TileY(st->train_tile) + curh, TileY(tile) + h) - y;
		tile = TileXY(x, y);
	} else {
		/* do not allow modifying non-uniform stations,
		 * the uniform-stations code wouldn't handle it well */
		BEGIN_TILE_LOOP(t, st->trainst_w, st->trainst_h, st->train_tile)
			if (!st->TileBelongsToRailStation(t)) { // there may be adjoined station
				_error_message = STR_NONUNIFORM_STATIONS_DISALLOWED;
				return false;
			}
		END_TILE_LOOP(t, st->trainst_w, st->trainst_h, st->train_tile)

		/* check so the orientation is the same */
		if (GetRailStationAxis(st->train_tile) != axis) {
			_error_message = STR_NONUNIFORM_STATIONS_DISALLOWED;
			return false;
		}

		/* check if the new station adjoins the old station in either direction */
		if (curw == w && st->train_tile == tile + TileDiffXY(0, h)) {
			/* above */
			curh += h;
		} else if (curw == w && st->train_tile == tile - TileDiffXY(0, curh)) {
			/* below */
			tile -= TileDiffXY(0, curh);
			curh += h;
		} else if (curh == h && st->train_tile == tile + TileDiffXY(w, 0)) {
			/* to the left */
			curw += w;
		} else if (curh == h && st->train_tile == tile - TileDiffXY(curw, 0)) {
			/* to the right */
			tile -= TileDiffXY(curw, 0);
			curw += w;
		} else {
			_error_message = STR_NONUNIFORM_STATIONS_DISALLOWED;
			return false;
		}
	}
	/* make sure the final size is not too big. */
	if (curw > _settings_game.station.station_spread || curh > _settings_game.station.station_spread) {
		_error_message = STR_306C_STATION_TOO_SPREAD_OUT;
		return false;
	}

	/* now tile contains the new value for st->train_tile
	 * curw, curh contain the new value for width and height */
	fin[0] = tile;
	fin[1] = curw;
	fin[2] = curh;
	return true;
}

static inline byte *CreateSingle(byte *layout, int n)
{
	int i = n;
	do *layout++ = 0; while (--i);
	layout[((n - 1) >> 1) - n] = 2;
	return layout;
}

static inline byte *CreateMulti(byte *layout, int n, byte b)
{
	int i = n;
	do *layout++ = b; while (--i);
	if (n > 4) {
		layout[0 - n] = 0;
		layout[n - 1 - n] = 0;
	}
	return layout;
}

static void GetStationLayout(byte *layout, int numtracks, int plat_len, const StationSpec *statspec)
{
	if (statspec != NULL && statspec->lengths >= plat_len &&
			statspec->platforms[plat_len - 1] >= numtracks &&
			statspec->layouts[plat_len - 1][numtracks - 1]) {
		/* Custom layout defined, follow it. */
		memcpy(layout, statspec->layouts[plat_len - 1][numtracks - 1],
			plat_len * numtracks);
		return;
	}

	if (plat_len == 1) {
		CreateSingle(layout, numtracks);
	} else {
		if (numtracks & 1) layout = CreateSingle(layout, plat_len);
		numtracks >>= 1;

		while (--numtracks >= 0) {
			layout = CreateMulti(layout, plat_len, 4);
			layout = CreateMulti(layout, plat_len, 6);
		}
	}
}

/** Build railroad station
 * @param tile_org starting position of station dragging/placement
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0- 3) - railtype (p1 & 0xF)
 * - p1 = (bit  4)    - orientation (Axis)
 * - p1 = (bit  8-15) - number of tracks
 * - p1 = (bit 16-23) - platform length
 * - p1 = (bit 24)    - allow stations directly adjacent to other stations.
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 7) - custom station class
 * - p2 = (bit  8-15) - custom station id
 * - p2 = (bit 16-31) - station ID to join (INVALID_STATION if build new one)
 */
CommandCost CmdBuildRailroadStation(TileIndex tile_org, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	/* Does the authority allow this? */
	if (!CheckIfAuthorityAllowsNewStation(tile_org, flags)) return CMD_ERROR;
	if (!ValParamRailtype((RailType)(p1 & 0xF))) return CMD_ERROR;

	/* unpack parameters */
	Axis axis = Extract<Axis, 4>(p1);
	uint numtracks = GB(p1,  8, 8);
	uint plat_len  = GB(p1, 16, 8);

	int w_org, h_org;
	if (axis == AXIS_X) {
		w_org = plat_len;
		h_org = numtracks;
	} else {
		h_org = plat_len;
		w_org = numtracks;
	}

	StationID station_to_join = GB(p2, 16, 16);
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !IsValidStationID(station_to_join))) return CMD_ERROR;

	if (h_org > _settings_game.station.station_spread || w_org > _settings_game.station.station_spread) return CMD_ERROR;

	/* these values are those that will be stored in train_tile and station_platforms */
	uint finalvalues[3];
	finalvalues[0] = tile_org;
	finalvalues[1] = w_org;
	finalvalues[2] = h_org;

	/* Make sure the area below consists of clear tiles. (OR tiles belonging to a certain rail station) */
	StationID est = INVALID_STATION;
	/* If DC_EXEC is in flag, do not want to pass it to CheckFlatLandBelow, because of a nice bug
	 * for detail info, see:
	 * https://sourceforge.net/tracker/index.php?func=detail&aid=1029064&group_id=103924&atid=636365 */
	CommandCost ret = CheckFlatLandBelow(tile_org, w_org, h_org, flags & ~DC_EXEC, 5 << axis, _settings_game.station.nonuniform_stations ? &est : NULL);
	if (CmdFailed(ret)) return ret;
	CommandCost cost(EXPENSES_CONSTRUCTION, ret.GetCost() + (numtracks * _price.train_station_track + _price.train_station_length) * plat_len);

	Station *st = NULL;
	bool check_surrounding = true;

	if (_settings_game.station.adjacent_stations) {
		if (est != INVALID_STATION) {
			if (HasBit(p1, 24) && est != station_to_join) {
				/* You can't build an adjacent station over the top of one that
				 * already exists. */
				return_cmd_error(STR_MUST_REMOVE_RAILWAY_STATION_FIRST);
			} else {
				/* Extend the current station, and don't check whether it will
				 * be near any other stations. */
				st = GetStation(est);
				check_surrounding = false;
			}
		} else {
			/* There's no station here. Don't check the tiles surrounding this
			 * one if the company wanted to build an adjacent station. */
			if (HasBit(p1, 24)) check_surrounding = false;
		}
	}

	if (check_surrounding) {
		/* Make sure there are no similar stations around us. */
		st = GetStationAround(tile_org, w_org, h_org, est);
		if (st == CHECK_STATIONS_ERR) return CMD_ERROR;
	}

	/* Distant join */
	if (st == NULL && distant_join) st = GetStation(station_to_join);

	/* See if there is a deleted station close to us. */
	if (st == NULL) st = GetClosestDeletedStation(tile_org);

	if (st != NULL) {
		/* Reuse an existing station. */
		if (st->owner != _current_company)
			return_cmd_error(STR_3009_TOO_CLOSE_TO_ANOTHER_STATION);

		if (st->train_tile != INVALID_TILE) {
			/* check if we want to expanding an already existing station? */
			if (!_settings_game.station.join_stations)
				return_cmd_error(STR_3005_TOO_CLOSE_TO_ANOTHER_RAILROAD);
			if (!CanExpandRailroadStation(st, finalvalues, axis))
				return CMD_ERROR;
		}

		/* XXX can't we pack this in the "else" part of the if above? */
		if (!st->rect.BeforeAddRect(tile_org, w_org, h_org, StationRect::ADD_TEST)) return CMD_ERROR;
	} else {
		/* allocate and initialize new station */
		if (!Station::CanAllocateItem()) return_cmd_error(STR_3008_TOO_MANY_STATIONS_LOADING);

		if (flags & DC_EXEC) {
			st = new Station(tile_org);

			st->town = ClosestTownFromTile(tile_org, UINT_MAX);
			st->string_id = GenerateStationName(st, tile_org, STATIONNAMING_RAIL);

			if (IsValidCompanyID(_current_company)) {
				SetBit(st->town->have_ratings, _current_company);
			}
		}
	}

	/* Check if the given station class is valid */
	if (GB(p2, 0, 8) >= GetNumStationClasses()) return CMD_ERROR;

	/* Check if we can allocate a custom stationspec to this station */
	const StationSpec *statspec = GetCustomStationSpec((StationClassID)GB(p2, 0, 8), GB(p2, 8, 8));
	int specindex = AllocateSpecToStation(statspec, st, (flags & DC_EXEC) != 0);
	if (specindex == -1) return_cmd_error(STR_TOO_MANY_STATION_SPECS);

	if (statspec != NULL) {
		/* Perform NewStation checks */

		/* Check if the station size is permitted */
		if (HasBit(statspec->disallowed_platforms, numtracks - 1) || HasBit(statspec->disallowed_lengths, plat_len - 1)) {
			return CMD_ERROR;
		}

		/* Check if the station is buildable */
		if (HasBit(statspec->callbackmask, CBM_STATION_AVAIL) && GB(GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, NULL, INVALID_TILE), 0, 8) == 0) {
			return CMD_ERROR;
		}
	}

	if (flags & DC_EXEC) {
		TileIndexDiff tile_delta;
		byte *layout_ptr;
		byte numtracks_orig;
		Track track;

		/* Now really clear the land below the station
		 * It should never return CMD_ERROR.. but you never know ;)
		 * (a bit strange function name for it, but it really does clear the land, when DC_EXEC is in flags) */
		ret = CheckFlatLandBelow(tile_org, w_org, h_org, flags, 5 << axis, _settings_game.station.nonuniform_stations ? &est : NULL);
		if (CmdFailed(ret)) return ret;

		st->train_tile = finalvalues[0];
		st->AddFacility(FACIL_TRAIN, finalvalues[0]);

		st->trainst_w = finalvalues[1];
		st->trainst_h = finalvalues[2];

		st->rect.BeforeAddRect(tile_org, w_org, h_org, StationRect::ADD_TRY);

		if (statspec != NULL) {
			/* Include this station spec's animation trigger bitmask
			 * in the station's cached copy. */
			st->cached_anim_triggers |= statspec->anim_triggers;
		}

		tile_delta = (axis == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));
		track = AxisToTrack(axis);

		layout_ptr = AllocaM(byte, numtracks * plat_len);
		GetStationLayout(layout_ptr, numtracks, plat_len, statspec);

		numtracks_orig = numtracks;

		SmallVector<Vehicle*, 4> affected_vehicles;
		do {
			TileIndex tile = tile_org;
			int w = plat_len;
			do {
				byte layout = *layout_ptr++;
				if (IsRailwayStationTile(tile) && GetRailwayStationReservation(tile)) {
					/* Check for trains having a reservation for this tile. */
					Vehicle *v = GetTrainForReservation(tile, AxisToTrack(GetRailStationAxis(tile)));
					if (v != NULL) {
						FreeTrainTrackReservation(v);
						*affected_vehicles.Append() = v;
						if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(GetVehicleTrackdir(v)), false);
						for (; v->Next() != NULL; v = v->Next()) ;
						if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(ReverseTrackdir(GetVehicleTrackdir(v))), false);
					}
				}

				byte old_specindex = IsTileType(tile, MP_STATION) ? GetCustomStationSpecIndex(tile) : 0;
				MakeRailStation(tile, st->owner, st->index, axis, layout & ~1, (RailType)GB(p1, 0, 4));
				/* Free the spec if we overbuild something */
				DeallocateSpecFromStation(st, old_specindex);

				SetCustomStationSpecIndex(tile, specindex);
				SetStationTileRandomBits(tile, GB(Random(), 0, 4));
				SetStationAnimationFrame(tile, 0);

				if (statspec != NULL) {
					/* Use a fixed axis for GetPlatformInfo as our platforms / numtracks are always the right way around */
					uint32 platinfo = GetPlatformInfo(AXIS_X, 0, plat_len, numtracks_orig, plat_len - w, numtracks_orig - numtracks, false);

					/* As the station is not yet completely finished, the station does not yet exist. */
					uint16 callback = GetStationCallback(CBID_STATION_TILE_LAYOUT, platinfo, 0, statspec, NULL, tile);
					if (callback != CALLBACK_FAILED && callback < 8) SetStationGfx(tile, (callback & ~1) + axis);

					/* Trigger station animation -- after building? */
					StationAnimationTrigger(st, tile, STAT_ANIM_BUILT);
				}

				tile += tile_delta;
			} while (--w);
			AddTrackToSignalBuffer(tile_org, track, _current_company);
			YapfNotifyTrackLayoutChange(tile_org, track);
			tile_org += tile_delta ^ TileDiffXY(1, 1); // perpendicular to tile_delta
		} while (--numtracks);

		for (uint i = 0; i < affected_vehicles.Length(); ++i) {
			/* Restore reservations of trains. */
			Vehicle *v = affected_vehicles[i];
			if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(GetVehicleTrackdir(v)), true);
			TryPathReserve(v, true, true);
			for (; v->Next() != NULL; v = v->Next()) ;
			if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(ReverseTrackdir(GetVehicleTrackdir(v))), true);
		}

		st->MarkTilesDirty(false);
		UpdateStationVirtCoordDirty(st);
		UpdateStationAcceptance(st, false);
		InvalidateWindowData(WC_SELECT_STATION, 0, 0);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_TRAINS);
	}

	return cost;
}

static void MakeRailwayStationAreaSmaller(Station *st)
{
	uint w = st->trainst_w;
	uint h = st->trainst_h;
	TileIndex tile = st->train_tile;

restart:

	/* too small? */
	if (w != 0 && h != 0) {
		/* check the left side, x = constant, y changes */
		for (uint i = 0; !st->TileBelongsToRailStation(tile + TileDiffXY(0, i));) {
			/* the left side is unused? */
			if (++i == h) {
				tile += TileDiffXY(1, 0);
				w--;
				goto restart;
			}
		}

		/* check the right side, x = constant, y changes */
		for (uint i = 0; !st->TileBelongsToRailStation(tile + TileDiffXY(w - 1, i));) {
			/* the right side is unused? */
			if (++i == h) {
				w--;
				goto restart;
			}
		}

		/* check the upper side, y = constant, x changes */
		for (uint i = 0; !st->TileBelongsToRailStation(tile + TileDiffXY(i, 0));) {
			/* the left side is unused? */
			if (++i == w) {
				tile += TileDiffXY(0, 1);
				h--;
				goto restart;
			}
		}

		/* check the lower side, y = constant, x changes */
		for (uint i = 0; !st->TileBelongsToRailStation(tile + TileDiffXY(i, h - 1));) {
			/* the left side is unused? */
			if (++i == w) {
				h--;
				goto restart;
			}
		}
	} else {
		tile = INVALID_TILE;
	}

	st->trainst_w = w;
	st->trainst_h = h;
	st->train_tile = tile;
}

/** Remove a single tile from a railroad station.
 * This allows for custom-built station with holes and weird layouts
 * @param tile tile of station piece to remove
 * @param flags operation to perform
 * @param p1 start_tile
 * @param p2 unused
 */
CommandCost CmdRemoveFromRailroadStation(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TileIndex start = p1 == 0 ? tile : p1;

	/* Count of the number of tiles removed */
	int quantity = 0;

	if (tile >= MapSize() || start >= MapSize()) return CMD_ERROR;

	/* make sure sx,sy are smaller than ex,ey */
	int ex = TileX(tile);
	int ey = TileY(tile);
	int sx = TileX(start);
	int sy = TileY(start);
	if (ex < sx) Swap(ex, sx);
	if (ey < sy) Swap(ey, sy);
	tile = TileXY(sx, sy);

	int size_x = ex - sx + 1;
	int size_y = ey - sy + 1;

	/* Do the action for every tile into the area */
	BEGIN_TILE_LOOP(tile2, size_x, size_y, tile) {
		/* Make sure the specified tile is a railroad station */
		if (!IsTileType(tile2, MP_STATION) || !IsRailwayStation(tile2)) {
			continue;
		}

		/* If there is a vehicle on ground, do not allow to remove (flood) the tile */
		if (!EnsureNoVehicleOnGround(tile2)) {
			continue;
		}

		/* Check ownership of station */
		Station *st = GetStationByTile(tile2);
		if (_current_company != OWNER_WATER && !CheckOwnership(st->owner)) {
			continue;
		}

		/* Do not allow removing from stations if non-uniform stations are not enabled
		 * The check must be here to give correct error message
		 */
		if (!_settings_game.station.nonuniform_stations) return_cmd_error(STR_NONUNIFORM_STATIONS_DISALLOWED);

		/* If we reached here, the tile is valid so increase the quantity of tiles we will remove */
		quantity++;

		if (flags & DC_EXEC) {
			/* read variables before the station tile is removed */
			uint specindex = GetCustomStationSpecIndex(tile2);
			Track track = GetRailStationTrack(tile2);
			Owner owner = GetTileOwner(tile2);
			Vehicle *v = NULL;

			if (GetRailwayStationReservation(tile2)) {
				v = GetTrainForReservation(tile2, track);
				if (v != NULL) {
					/* Free train reservation. */
					FreeTrainTrackReservation(v);
					if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(GetVehicleTrackdir(v)), false);
					Vehicle *temp = v;
					for (; temp->Next() != NULL; temp = temp->Next()) ;
					if (IsRailwayStationTile(temp->tile)) SetRailwayStationPlatformReservation(temp->tile, TrackdirToExitdir(ReverseTrackdir(GetVehicleTrackdir(temp))), false);
				}
			}

			DoClearSquare(tile2);
			st->rect.AfterRemoveTile(st, tile2);
			AddTrackToSignalBuffer(tile2, track, owner);
			YapfNotifyTrackLayoutChange(tile2, track);

			DeallocateSpecFromStation(st, specindex);

			/* now we need to make the "spanned" area of the railway station smaller
			 * if we deleted something at the edges.
			 * we also need to adjust train_tile. */
			MakeRailwayStationAreaSmaller(st);
			st->MarkTilesDirty(false);
			UpdateStationSignCoord(st);

			if (v != NULL) {
				/* Restore station reservation. */
				if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(GetVehicleTrackdir(v)), true);
				TryPathReserve(v, true, true);
				for (; v->Next() != NULL; v = v->Next()) ;
				if (IsRailwayStationTile(v->tile)) SetRailwayStationPlatformReservation(v->tile, TrackdirToExitdir(ReverseTrackdir(GetVehicleTrackdir(v))), true);
			}

			/* if we deleted the whole station, delete the train facility. */
			if (st->train_tile == INVALID_TILE) {
				st->facilities &= ~FACIL_TRAIN;
				InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_TRAINS);
				UpdateStationVirtCoordDirty(st);
				DeleteStationIfEmpty(st);
			}
		}
	} END_TILE_LOOP(tile2, size_x, size_y, tile)

	/* If we've not removed any tiles, give an error */
	if (quantity == 0) return CMD_ERROR;

	return CommandCost(EXPENSES_CONSTRUCTION, _price.remove_rail_station * quantity);
}


static CommandCost RemoveRailroadStation(Station *st, TileIndex tile, DoCommandFlag flags)
{
	/* if there is flooding and non-uniform stations are enabled, remove platforms tile by tile */
	if (_current_company == OWNER_WATER && _settings_game.station.nonuniform_stations) {
		return DoCommand(tile, 0, 0, DC_EXEC, CMD_REMOVE_FROM_RAILROAD_STATION);
	}

	/* Current company owns the station? */
	if (_current_company != OWNER_WATER && !CheckOwnership(st->owner)) return CMD_ERROR;

	/* determine width and height of platforms */
	tile = st->train_tile;
	int w = st->trainst_w;
	int h = st->trainst_h;

	assert(w != 0 && h != 0);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	/* clear all areas of the station */
	do {
		int w_bak = w;
		do {
			/* for nonuniform stations, only remove tiles that are actually train station tiles */
			if (st->TileBelongsToRailStation(tile)) {
				if (!EnsureNoVehicleOnGround(tile))
					return CMD_ERROR;
				cost.AddCost(_price.remove_rail_station);
				if (flags & DC_EXEC) {
					/* read variables before the station tile is removed */
					Track track = GetRailStationTrack(tile);
					Owner owner = GetTileOwner(tile); // _current_company can be OWNER_WATER
					Vehicle *v = NULL;
					if (GetRailwayStationReservation(tile)) {
						v = GetTrainForReservation(tile, track);
						if (v != NULL) FreeTrainTrackReservation(v);
					}
					DoClearSquare(tile);
					AddTrackToSignalBuffer(tile, track, owner);
					YapfNotifyTrackLayoutChange(tile, track);
					if (v != NULL) TryPathReserve(v, true);
				}
			}
			tile += TileDiffXY(1, 0);
		} while (--w);
		w = w_bak;
		tile += TileDiffXY(-w, 1);
	} while (--h);

	if (flags & DC_EXEC) {
		st->rect.AfterRemoveRect(st, st->train_tile, st->trainst_w, st->trainst_h);

		st->train_tile = INVALID_TILE;
		st->trainst_w = st->trainst_h = 0;
		st->facilities &= ~FACIL_TRAIN;

		free(st->speclist);
		st->num_specs = 0;
		st->speclist  = NULL;
		st->cached_anim_triggers = 0;

		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_TRAINS);
		UpdateStationVirtCoordDirty(st);
		DeleteStationIfEmpty(st);
	}

	return cost;
}

/**
 * @param truck_station Determines whether a stop is ROADSTOP_BUS or ROADSTOP_TRUCK
 * @param st The Station to do the whole procedure for
 * @return a pointer to where to link a new RoadStop*
 */
static RoadStop **FindRoadStopSpot(bool truck_station, Station *st)
{
	RoadStop **primary_stop = (truck_station) ? &st->truck_stops : &st->bus_stops;

	if (*primary_stop == NULL) {
		/* we have no roadstop of the type yet, so write a "primary stop" */
		return primary_stop;
	} else {
		/* there are stops already, so append to the end of the list */
		RoadStop *stop = *primary_stop;
		while (stop->next != NULL) stop = stop->next;
		return &stop->next;
	}
}

/** Build a bus or truck stop
 * @param tile tile to build the stop at
 * @param flags operation to perform
 * @param p1 entrance direction (DiagDirection)
 * @param p2 bit 0: 0 for Bus stops, 1 for truck stops
 *           bit 1: 0 for normal, 1 for drive-through
 *           bit 2..3: the roadtypes
 *           bit 5: allow stations directly adjacent to other stations.
 *           bit 16..31: station ID to join (INVALID_STATION if build new one)
 */
CommandCost CmdBuildRoadStop(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	bool type = HasBit(p2, 0);
	bool is_drive_through = HasBit(p2, 1);
	bool build_over_road  = is_drive_through && IsNormalRoadTile(tile);
	RoadTypes rts = (RoadTypes)GB(p2, 2, 2);
	StationID station_to_join = GB(p2, 16, 16);
	bool distant_join = (station_to_join != INVALID_STATION);
	Owner tram_owner = _current_company;
	Owner road_owner = _current_company;

	if (distant_join && (!_settings_game.station.distant_join_stations || !IsValidStationID(station_to_join))) return CMD_ERROR;

	if (!AreValidRoadTypes(rts) || !HasRoadTypesAvail(_current_company, rts)) return CMD_ERROR;

	/* Trams only have drive through stops */
	if (!is_drive_through && HasBit(rts, ROADTYPE_TRAM)) return CMD_ERROR;

	/* Saveguard the parameters */
	if (!IsValidDiagDirection((DiagDirection)p1)) return CMD_ERROR;
	/* If it is a drive-through stop check for valid axis */
	if (is_drive_through && !IsValidAxis((Axis)p1)) return CMD_ERROR;
	/* Road bits in the wrong direction */
	if (build_over_road && (GetAllRoadBits(tile) & ((Axis)p1 == AXIS_X ? ROAD_Y : ROAD_X)) != 0) return_cmd_error(STR_DRIVE_THROUGH_ERROR_DIRECTION);

	if (!CheckIfAuthorityAllowsNewStation(tile, flags)) return CMD_ERROR;

	RoadTypes cur_rts = IsNormalRoadTile(tile) ? GetRoadTypes(tile) : ROADTYPES_NONE;
	uint num_roadbits = 0;
	/* Not allowed to build over this road */
	if (build_over_road) {
		/* there is a road, check if we can build road+tram stop over it */
		if (HasBit(cur_rts, ROADTYPE_ROAD)) {
			road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
			if (road_owner == OWNER_TOWN) {
				if (!_settings_game.construction.road_stop_on_town_road) return_cmd_error(STR_DRIVE_THROUGH_ERROR_ON_TOWN_ROAD);
			} else if (!_settings_game.construction.road_stop_on_competitor_road && road_owner != OWNER_NONE && !CheckOwnership(road_owner)) {
				return CMD_ERROR;
			}
			num_roadbits += CountBits(GetRoadBits(tile, ROADTYPE_ROAD));
		}

		/* there is a tram, check if we can build road+tram stop over it */
		if (HasBit(cur_rts, ROADTYPE_TRAM)) {
			tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);
			if (!_settings_game.construction.road_stop_on_competitor_road && tram_owner != OWNER_NONE && !CheckOwnership(tram_owner)) {
				return CMD_ERROR;
			}
			num_roadbits += CountBits(GetRoadBits(tile, ROADTYPE_TRAM));
		}

		/* Don't allow building the roadstop when vehicles are already driving on it */
		if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

		/* Do not remove roadtypes! */
		rts |= cur_rts;
	}

	CommandCost cost = CheckFlatLandBelow(tile, 1, 1, flags, is_drive_through ? 5 << p1 : 1 << p1, NULL, !build_over_road);
	if (CmdFailed(cost)) return cost;
	uint roadbits_to_build = CountBits(rts) * 2 - num_roadbits;
	cost.AddCost(_price.build_road * roadbits_to_build);

	Station *st = NULL;

	if (!_settings_game.station.adjacent_stations || !HasBit(p2, 5)) {
		st = GetStationAround(tile, 1, 1, INVALID_STATION);
		if (st == CHECK_STATIONS_ERR) return CMD_ERROR;
	}

	/* Distant join */
	if (st == NULL && distant_join) st = GetStation(station_to_join);

	/* Find a deleted station close to us */
	if (st == NULL) st = GetClosestDeletedStation(tile);

	/* give us a road stop in the list, and check if something went wrong */
	if (!RoadStop::CanAllocateItem()) return_cmd_error(type ? STR_TOO_MANY_TRUCK_STOPS : STR_TOO_MANY_BUS_STOPS);

	if (st != NULL &&
			GetNumRoadStopsInStation(st, ROADSTOP_BUS) + GetNumRoadStopsInStation(st, ROADSTOP_TRUCK) >= RoadStop::LIMIT) {
		return_cmd_error(type ? STR_TOO_MANY_TRUCK_STOPS : STR_TOO_MANY_BUS_STOPS);
	}

	if (st != NULL) {
		if (st->owner != _current_company) {
			return_cmd_error(STR_3009_TOO_CLOSE_TO_ANOTHER_STATION);
		}

		if (!st->rect.BeforeAddTile(tile, StationRect::ADD_TEST)) return CMD_ERROR;
	} else {
		/* allocate and initialize new station */
		if (!Station::CanAllocateItem()) return_cmd_error(STR_3008_TOO_MANY_STATIONS_LOADING);

		if (flags & DC_EXEC) {
			st = new Station(tile);

			st->town = ClosestTownFromTile(tile, UINT_MAX);
			st->string_id = GenerateStationName(st, tile, STATIONNAMING_ROAD);

			if (IsValidCompanyID(_current_company)) {
				SetBit(st->town->have_ratings, _current_company);
			}
			st->sign.width_1 = 0;
		}
	}

	cost.AddCost((type) ? _price.build_truck_station : _price.build_bus_station);

	if (flags & DC_EXEC) {
		RoadStop *road_stop = new RoadStop(tile);
		/* Insert into linked list of RoadStops */
		RoadStop **currstop = FindRoadStopSpot(type, st);
		*currstop = road_stop;

		/* initialize an empty station */
		st->AddFacility((type) ? FACIL_TRUCK_STOP : FACIL_BUS_STOP, tile);

		st->rect.BeforeAddTile(tile, StationRect::ADD_TRY);

		RoadStopType rs_type = type ? ROADSTOP_TRUCK : ROADSTOP_BUS;
		if (is_drive_through) {
			MakeDriveThroughRoadStop(tile, st->owner, road_owner, tram_owner, st->index, rs_type, rts, (Axis)p1);
		} else {
			MakeRoadStop(tile, st->owner, st->index, rs_type, rts, (DiagDirection)p1);
		}

		UpdateStationVirtCoordDirty(st);
		UpdateStationAcceptance(st, false);
		InvalidateWindowData(WC_SELECT_STATION, 0, 0);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_ROADVEHS);
	}
	return cost;
}


static Vehicle *ClearRoadStopStatusEnum(Vehicle *v, void *)
{
	if (v->type == VEH_ROAD) ClrBit(v->u.road.state, RVS_IN_DT_ROAD_STOP);

	return NULL;
}


/** Remove a bus station
 * @param st Station to remove
 * @param flags operation to perform
 * @param tile TileIndex been queried
 * @return cost or failure of operation
 */
static CommandCost RemoveRoadStop(Station *st, DoCommandFlag flags, TileIndex tile)
{
	if (_current_company != OWNER_WATER && !CheckOwnership(st->owner)) {
		return CMD_ERROR;
	}

	bool is_truck = IsTruckStop(tile);

	RoadStop **primary_stop;
	RoadStop *cur_stop;
	if (is_truck) { // truck stop
		primary_stop = &st->truck_stops;
		cur_stop = GetRoadStopByTile(tile, ROADSTOP_TRUCK);
	} else {
		primary_stop = &st->bus_stops;
		cur_stop = GetRoadStopByTile(tile, ROADSTOP_BUS);
	}

	assert(cur_stop != NULL);

	/* don't do the check for drive-through road stops when company bankrupts */
	if (IsDriveThroughStopTile(tile) && (flags & DC_BANKRUPT)) {
		/* remove the 'going through road stop' status from all vehicles on that tile */
		if (flags & DC_EXEC) FindVehicleOnPos(tile, NULL, &ClearRoadStopStatusEnum);
	} else {
		if (!EnsureNoVehicleOnGround(tile)) return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		if (*primary_stop == cur_stop) {
			/* removed the first stop in the list */
			*primary_stop = cur_stop->next;
			/* removed the only stop? */
			if (*primary_stop == NULL) {
				st->facilities &= (is_truck ? ~FACIL_TRUCK_STOP : ~FACIL_BUS_STOP);
			}
		} else {
			/* tell the predecessor in the list to skip this stop */
			RoadStop *pred = *primary_stop;
			while (pred->next != cur_stop) pred = pred->next;
			pred->next = cur_stop->next;
		}

		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_ROADVEHS);
		delete cur_stop;

		/* Make sure no vehicle is going to the old roadstop */
		Vehicle *v;
		FOR_ALL_VEHICLES(v) {
			if (v->type == VEH_ROAD &&
					v->First() == v &&
					v->current_order.IsType(OT_GOTO_STATION) &&
					v->dest_tile == tile) {
				v->dest_tile = v->GetOrderStationLocation(st->index);
			}
		}

		DoClearSquare(tile);
		st->rect.AfterRemoveTile(st, tile);

		UpdateStationVirtCoordDirty(st);
		DeleteStationIfEmpty(st);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, (is_truck) ? _price.remove_truck_station : _price.remove_bus_station);
}

/** Remove a bus or truck stop
 * @param tile tile to remove the stop from
 * @param flags operation to perform
 * @param p1 not used
 * @param p2 bit 0: 0 for Bus stops, 1 for truck stops
 */
CommandCost CmdRemoveRoadStop(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	/* Make sure the specified tile is a road stop of the correct type */
	if (!IsTileType(tile, MP_STATION) || !IsRoadStop(tile) || (uint32)GetRoadStopType(tile) != GB(p2, 0, 1)) return CMD_ERROR;
	Station *st = GetStationByTile(tile);
	/* Save the stop info before it is removed */
	bool is_drive_through = IsDriveThroughStopTile(tile);
	RoadTypes rts = GetRoadTypes(tile);
	RoadBits road_bits = IsDriveThroughStopTile(tile) ?
			((GetRoadStopDir(tile) == DIAGDIR_NE) ? ROAD_X : ROAD_Y) :
			DiagDirToRoadBits(GetRoadStopDir(tile));

	Owner road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
	Owner tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);
	CommandCost ret = RemoveRoadStop(st, flags, tile);

	/* If the stop was a drive-through stop replace the road */
	if ((flags & DC_EXEC) && CmdSucceeded(ret) && is_drive_through) {
		/* Rebuild the drive throuhg road stop. As a road stop can only be
		 * removed by the owner of the roadstop, _current_company is the
		 * owner of the road stop. */
		MakeRoadNormal(tile, road_bits, rts, ClosestTownFromTile(tile, UINT_MAX)->index,
				road_owner, tram_owner);
	}

	return ret;
}

/* FIXME -- need to move to its corresponding Airport variable*/

/* Country Airfield (small) */
static const byte _airport_sections_country[] = {
	54, 53, 52, 65,
	58, 57, 56, 55,
	64, 63, 63, 62
};

/* City Airport (large) */
static const byte _airport_sections_town[] = {
	31,  9, 33,  9,  9, 32,
	27, 36, 29, 34,  8, 10,
	30, 11, 35, 13, 20, 21,
	51, 12, 14, 17, 19, 28,
	38, 13, 15, 16, 18, 39,
	26, 22, 23, 24, 25, 26
};

/* Metropolitain Airport (large) - 2 runways */
static const byte _airport_sections_metropolitan[] = {
	 31,  9, 33,  9,  9, 32,
	 27, 36, 29, 34,  8, 10,
	 30, 11, 35, 13, 20, 21,
	102,  8,  8,  8,  8, 28,
	 83, 84, 84, 84, 84, 83,
	 26, 23, 23, 23, 23, 26
};

/* International Airport (large) - 2 runways */
static const byte _airport_sections_international[] = {
	88, 89, 89, 89, 89, 89,  88,
	51,  8,  8,  8,  8,  8,  32,
	30,  8, 11, 27, 11,  8,  10,
	32,  8, 11, 27, 11,  8, 114,
	87,  8, 11, 85, 11,  8, 114,
	87,  8,  8,  8,  8,  8,  90,
	26, 23, 23, 23, 23, 23,  26
};

/* Intercontinental Airport (vlarge) - 4 runways */
static const byte _airport_sections_intercontinental[] = {
	102, 120,  89,  89,  89,  89,  89,  89, 118,
	120,  23,  23,  23,  23,  23,  23, 119, 117,
	 87,  54,  87,   8,   8,   8,   8,  51, 117,
	 87, 162,  87,  85, 116, 116,   8,   9,  10,
	 87,   8,   8,  11,  31,  11,   8, 160,  32,
	 32, 160,   8,  11,  27,  11,   8,   8,  10,
	 87,   8,   8,  11,  30,  11,   8,   8,  10,
	 87, 142,   8,  11,  29,  11,  10, 163,  10,
	 87, 164,  87,   8,   8,   8,  10,  37, 117,
	 87, 120,  89,  89,  89,  89,  89,  89, 119,
	121,  23,  23,  23,  23,  23,  23, 119,  37
};


/* Commuter Airfield (small) */
static const byte _airport_sections_commuter[] = {
	85, 30, 115, 115, 32,
	87, 8,    8,   8, 10,
	87, 11,  11,  11, 10,
	26, 23,  23,  23, 26
};

/* Heliport */
static const byte _airport_sections_heliport[] = {
	66,
};

/* Helidepot */
static const byte _airport_sections_helidepot[] = {
	124, 32,
	122, 123
};

/* Helistation */
static const byte _airport_sections_helistation[] = {
	 32, 134, 159, 158,
	161, 142, 142, 157
};

static const byte * const _airport_sections[] = {
	_airport_sections_country,           // Country Airfield (small)
	_airport_sections_town,              // City Airport (large)
	_airport_sections_heliport,          // Heliport
	_airport_sections_metropolitan,      // Metropolitain Airport (large)
	_airport_sections_international,     // International Airport (xlarge)
	_airport_sections_commuter,          // Commuter Airport (small)
	_airport_sections_helidepot,         // Helidepot
	_airport_sections_intercontinental,  // Intercontinental Airport (xxlarge)
	_airport_sections_helistation        // Helistation
};

/**
 * Computes the minimal distance from town's xy to any airport's tile.
 * @param afc airport's description
 * @param town_tile town's tile (t->xy)
 * @param airport_tile st->airport_tile
 * @return minimal manhattan distance from town_tile to any airport's tile
 */
static uint GetMinimalAirportDistanceToTile(const AirportFTAClass *afc, TileIndex town_tile, TileIndex airport_tile)
{
	uint ttx = TileX(town_tile); // X, Y of town
	uint tty = TileY(town_tile);

	uint atx = TileX(airport_tile); // X, Y of northern airport corner
	uint aty = TileY(airport_tile);

	uint btx = TileX(airport_tile) + afc->size_x - 1; // X, Y of southern corner
	uint bty = TileY(airport_tile) + afc->size_y - 1;

	/* if ttx < atx, dx = atx - ttx
	 * if atx <= ttx <= btx, dx = 0
	 * else, dx = ttx - btx (similiar for dy) */
	uint dx = ttx < atx ? atx - ttx : (ttx <= btx ? 0 : ttx - btx);
	uint dy = tty < aty ? aty - tty : (tty <= bty ? 0 : tty - bty);

	return dx + dy;
}

/** Get a possible noise reduction factor based on distance from town center.
 * The further you get, the less noise you generate.
 * So all those folks at city council can now happily slee...  work in their offices
 * @param afc AirportFTAClass pointer of the class being proposed
 * @param town_tile TileIndex of town's center, the one who will receive the airport's candidature
 * @param tile TileIndex of northern tile of an airport (present or to-be-built), NOT the station tile
 * @return the noise that will be generated, according to distance
 */
uint8 GetAirportNoiseLevelForTown(const AirportFTAClass *afc, TileIndex town_tile, TileIndex tile)
{
	/* 0 cannot be accounted, and 1 is the lowest that can be reduced from town.
	 * So no need to go any further*/
	if (afc->noise_level < 2) return afc->noise_level;

	uint distance = GetMinimalAirportDistanceToTile(afc, town_tile, tile);

	/* The steps for measuring noise reduction are based on the "magical" (and arbitrary) 8 base distance
	 * adding the town_council_tolerance 4 times, as a way to graduate, depending of the tolerance.
	 * Basically, it says that the less tolerant a town is, the bigger the distance before
	 * an actual decrease can be granted */
	uint8 town_tolerance_distance = 8 + (_settings_game.difficulty.town_council_tolerance * 4);

	/* now, we want to have the distance segmented using the distance judged bareable by town
	 * This will give us the coefficient of reduction the distance provides. */
	uint noise_reduction = distance / town_tolerance_distance;

	/* If the noise reduction equals the airport noise itself, don't give it for free.
	 * Otherwise, simply reduce the airport's level. */
	return noise_reduction >= afc->noise_level ? 1 : afc->noise_level - noise_reduction;
}

/**
 * Finds the town nearest to given airport. Based on minimal manhattan distance to any airport's tile.
 * If two towns have the same distance, town with lower index is returned.
 * @param afc airport's description
 * @param airport_tile st->airport_tile
 * @return nearest town to airport
 */
Town *AirportGetNearestTown(const AirportFTAClass *afc, TileIndex airport_tile)
{
	Town *t, *nearest = NULL;
	uint add = afc->size_x + afc->size_y - 2; // GetMinimalAirportDistanceToTile can differ from DistanceManhattan by this much
	uint mindist = UINT_MAX - add; // prevent overflow
	FOR_ALL_TOWNS(t) {
		if (DistanceManhattan(t->xy, airport_tile) < mindist + add) { // avoid calling GetMinimalAirportDistanceToTile too often
			uint dist = GetMinimalAirportDistanceToTile(afc, t->xy, airport_tile);
			if (dist < mindist) {
				nearest = t;
				mindist = dist;
			}
		}
	}

	return nearest;
}


/** Recalculate the noise generated by the airports of each town */
void UpdateAirportsNoise()
{
	Town *t;
	const Station *st;

	FOR_ALL_TOWNS(t) t->noise_reached = 0;

	FOR_ALL_STATIONS(st) {
		if (st->airport_tile != INVALID_TILE) {
			const AirportFTAClass *afc = GetAirport(st->airport_type);
			Town *nearest = AirportGetNearestTown(afc, st->airport_tile);
			nearest->noise_reached += GetAirportNoiseLevelForTown(afc, nearest->xy, st->airport_tile);
		}
	}
}


/** Place an Airport.
 * @param tile tile where airport will be built
 * @param flags operation to perform
 * @param p1 airport type, @see airport.h
 * @param p2 various bitstuffed elements
 * - p2 = (bit     0) - allow airports directly adjacent to other airports.
 * - p2 = (bit 16-31) - station ID to join (INVALID_STATION if build new one)
 */
CommandCost CmdBuildAirport(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	bool airport_upgrade = true;
	StationID station_to_join = GB(p2, 16, 16);
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !IsValidStationID(station_to_join))) return CMD_ERROR;

	/* Check if a valid, buildable airport was chosen for construction */
	if (p1 > lengthof(_airport_sections) || !HasBit(GetValidAirports(), p1)) return CMD_ERROR;

	if (!CheckIfAuthorityAllowsNewStation(tile, flags)) {
		return CMD_ERROR;
	}

	Town *t = ClosestTownFromTile(tile, UINT_MAX);
	const AirportFTAClass *afc = GetAirport(p1);
	int w = afc->size_x;
	int h = afc->size_y;
	Station *st = NULL;

	if (w > _settings_game.station.station_spread || h > _settings_game.station.station_spread) {
		_error_message = STR_306C_STATION_TOO_SPREAD_OUT;
		return CMD_ERROR;
	}

	CommandCost cost = CheckFlatLandBelow(tile, w, h, flags, 0, NULL);
	if (CmdFailed(cost)) return cost;

	/* Go get the final noise level, that is base noise minus factor from distance to town center */
	Town *nearest = AirportGetNearestTown(afc, tile);
	uint newnoise_level = GetAirportNoiseLevelForTown(afc, nearest->xy, tile);

	/* Check if local auth would allow a new airport */
	StringID authority_refuse_message = STR_NULL;

	if (_settings_game.economy.station_noise_level) {
		/* do not allow to build a new airport if this raise the town noise over the maximum allowed by town */
		if ((nearest->noise_reached + newnoise_level) > nearest->MaxTownNoise()) {
			authority_refuse_message = STR_LOCAL_AUTHORITY_REFUSES_NOISE;
		}
	} else {
		uint num = 0;
		const Station *st;
		FOR_ALL_STATIONS(st) {
			if (st->town == t && st->facilities & FACIL_AIRPORT && st->airport_type != AT_OILRIG) num++;
		}
		if (num >= 2) {
			authority_refuse_message = STR_2035_LOCAL_AUTHORITY_REFUSES;
		}
	}

	if (authority_refuse_message != STR_NULL) {
		SetDParam(0, t->index);
		return_cmd_error(authority_refuse_message);
	}

	if (!_settings_game.station.adjacent_stations || !HasBit(p2, 0)) {
		st = GetStationAround(tile, w, h, INVALID_STATION);
		if (st == CHECK_STATIONS_ERR) return CMD_ERROR;
	} else {
		st = NULL;
	}

	/* Distant join */
	if (st == NULL && distant_join) st = GetStation(station_to_join);

	/* Find a deleted station close to us */
	if (st == NULL) st = GetClosestDeletedStation(tile);

	if (st != NULL) {
		if (st->owner != _current_company) {
			return_cmd_error(STR_3009_TOO_CLOSE_TO_ANOTHER_STATION);
		}

		if (!st->rect.BeforeAddRect(tile, w, h, StationRect::ADD_TEST)) return CMD_ERROR;

		if (st->airport_tile != INVALID_TILE) {
			return_cmd_error(STR_300D_TOO_CLOSE_TO_ANOTHER_AIRPORT);
		}
	} else {
		airport_upgrade = false;

		/* allocate and initialize new station */
		if (!Station::CanAllocateItem()) return_cmd_error(STR_3008_TOO_MANY_STATIONS_LOADING);

		if (flags & DC_EXEC) {
			st = new Station(tile);

			st->town = t;
			st->string_id = GenerateStationName(st, tile, !(afc->flags & AirportFTAClass::AIRPLANES) ? STATIONNAMING_HELIPORT : STATIONNAMING_AIRPORT);

			if (IsValidCompanyID(_current_company)) {
				SetBit(st->town->have_ratings, _current_company);
			}
			st->sign.width_1 = 0;
		}
	}

	cost.AddCost(_price.build_airport * w * h);

	if (flags & DC_EXEC) {
		/* Always add the noise, so there will be no need to recalculate when option toggles */
		nearest->noise_reached += newnoise_level;

		st->airport_tile = tile;
		st->AddFacility(FACIL_AIRPORT, tile);
		st->airport_type = (byte)p1;
		st->airport_flags = 0;

		st->rect.BeforeAddRect(tile, w, h, StationRect::ADD_TRY);

		/* if airport was demolished while planes were en-route to it, the
		 * positions can no longer be the same (v->u.air.pos), since different
		 * airports have different indexes. So update all planes en-route to this
		 * airport. Only update if
		 * 1. airport is upgraded
		 * 2. airport is added to existing station (unfortunately unavoideable)
		 */
		if (airport_upgrade) UpdateAirplanesOnNewStation(st);

		{
			const byte *b = _airport_sections[p1];

			BEGIN_TILE_LOOP(tile_cur, w, h, tile) {
				MakeAirport(tile_cur, st->owner, st->index, *b - ((*b < 67) ? 8 : 24));
				b++;
			} END_TILE_LOOP(tile_cur, w, h, tile)
		}

		UpdateStationVirtCoordDirty(st);
		UpdateStationAcceptance(st, false);
		InvalidateWindowData(WC_SELECT_STATION, 0, 0);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_PLANES);

		if (_settings_game.economy.station_noise_level) {
			InvalidateWindow(WC_TOWN_VIEW, st->town->index);
		}
	}

	return cost;
}

static CommandCost RemoveAirport(Station *st, DoCommandFlag flags)
{
	if (_current_company != OWNER_WATER && !CheckOwnership(st->owner)) {
		return CMD_ERROR;
	}

	TileIndex tile = st->airport_tile;

	const AirportFTAClass *afc = st->Airport();
	int w = afc->size_x;
	int h = afc->size_y;

	CommandCost cost(EXPENSES_CONSTRUCTION, w * h * _price.remove_airport);

	const Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (!(v->type == VEH_AIRCRAFT && IsNormalAircraft(v))) continue;

		if (v->u.air.targetairport == st->index && v->u.air.state != FLYING) return CMD_ERROR;
	}

	BEGIN_TILE_LOOP(tile_cur, w, h, tile) {
		if (!EnsureNoVehicleOnGround(tile_cur)) return CMD_ERROR;

		if (flags & DC_EXEC) {
			DeleteAnimatedTile(tile_cur);
			DoClearSquare(tile_cur);
		}
	} END_TILE_LOOP(tile_cur, w, h, tile)

	if (flags & DC_EXEC) {
		for (uint i = 0; i < afc->nof_depots; ++i) {
			DeleteWindowById(
				WC_VEHICLE_DEPOT, tile + ToTileIndexDiff(afc->airport_depots[i])
			);
		}

		/* Go get the final noise level, that is base noise minus factor from distance to town center.
		 * And as for construction, always remove it, even if the setting is not set, in order to avoid the
		 * need of recalculation */
		Town *nearest = AirportGetNearestTown(afc, tile);
		nearest->noise_reached -= GetAirportNoiseLevelForTown(afc, nearest->xy, tile);

		st->rect.AfterRemoveRect(st, tile, w, h);

		st->airport_tile = INVALID_TILE;
		st->facilities &= ~FACIL_AIRPORT;

		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_PLANES);

		if (_settings_game.economy.station_noise_level) {
			InvalidateWindow(WC_TOWN_VIEW, st->town->index);
		}

		UpdateStationVirtCoordDirty(st);
		DeleteStationIfEmpty(st);
	}

	return cost;
}

/** Build a buoy.
 * @param tile tile where to place the bouy
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 unused
 */
CommandCost CmdBuildBuoy(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!IsWaterTile(tile) || tile == 0) return_cmd_error(STR_304B_SITE_UNSUITABLE);
	if (MayHaveBridgeAbove(tile) && IsBridgeAbove(tile)) return_cmd_error(STR_5007_MUST_DEMOLISH_BRIDGE_FIRST);

	if (GetTileSlope(tile, NULL) != SLOPE_FLAT) return_cmd_error(STR_304B_SITE_UNSUITABLE);

	/* allocate and initialize new station */
	if (!Station::CanAllocateItem()) return_cmd_error(STR_3008_TOO_MANY_STATIONS_LOADING);

	if (flags & DC_EXEC) {
		Station *st = new Station(tile);

		st->town = ClosestTownFromTile(tile, UINT_MAX);
		st->string_id = GenerateStationName(st, tile, STATIONNAMING_BUOY);

		if (IsValidCompanyID(_current_company)) {
			SetBit(st->town->have_ratings, _current_company);
		}
		st->sign.width_1 = 0;
		st->dock_tile = tile;
		st->facilities |= FACIL_DOCK;
		/* Buoys are marked in the Station struct by this flag. Yes, it is this
		 * braindead.. */
		st->had_vehicle_of_type |= HVOT_BUOY;
		st->owner = OWNER_NONE;

		st->build_date = _date;

		MakeBuoy(tile, st->index, GetWaterClass(tile));

		UpdateStationVirtCoordDirty(st);
		UpdateStationAcceptance(st, false);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_SHIPS);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price.build_dock);
}

/**
 * Tests whether the company's vehicles have this station in orders
 * When company == INVALID_COMPANY, then check all vehicles
 * @param station station ID
 * @param company company ID, INVALID_COMPANY to disable the check
 */
bool HasStationInUse(StationID station, CompanyID company)
{
	const Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (company == INVALID_COMPANY || v->owner == company) {
			const Order *order;
			FOR_VEHICLE_ORDERS(v, order) {
				if (order->IsType(OT_GOTO_STATION) && order->GetDestination() == station) {
					return true;
				}
			}
		}
	}
	return false;
}

static CommandCost RemoveBuoy(Station *st, DoCommandFlag flags)
{
	/* XXX: strange stuff */
	if (!IsValidCompanyID(_current_company)) return_cmd_error(INVALID_STRING_ID);

	TileIndex tile = st->dock_tile;

	if (HasStationInUse(st->index, INVALID_COMPANY)) return_cmd_error(STR_BUOY_IS_IN_USE);
	/* remove the buoy if there is a ship on tile when company goes bankrupt... */
	if (!(flags & DC_BANKRUPT) && !EnsureNoVehicleOnGround(tile)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		st->dock_tile = INVALID_TILE;
		/* Buoys are marked in the Station struct by this flag. Yes, it is this
		 * braindead.. */
		st->facilities &= ~FACIL_DOCK;
		st->had_vehicle_of_type &= ~HVOT_BUOY;

		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_SHIPS);

		/* We have to set the water tile's state to the same state as before the
		 * buoy was placed. Otherwise one could plant a buoy on a canal edge,
		 * remove it and flood the land (if the canal edge is at level 0) */
		MakeWaterKeepingClass(tile, GetTileOwner(tile));
		MarkTileDirtyByTile(tile);

		UpdateStationVirtCoordDirty(st);
		DeleteStationIfEmpty(st);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price.remove_truck_station);
}

static const TileIndexDiffC _dock_tileoffs_chkaround[] = {
	{-1,  0},
	{ 0,  0},
	{ 0,  0},
	{ 0, -1}
};
static const byte _dock_w_chk[4] = { 2, 1, 2, 1 };
static const byte _dock_h_chk[4] = { 1, 2, 1, 2 };

/** Build a dock/haven.
 * @param tile tile where dock will be built
 * @param flags operation to perform
 * @param p1 (bit 0) - allow docks directly adjacent to other docks.
 * @param p2 bit 16-31: station ID to join (INVALID_STATION if build new one)
 */
CommandCost CmdBuildDock(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	StationID station_to_join = GB(p2, 16, 16);
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !IsValidStationID(station_to_join))) return CMD_ERROR;

	DiagDirection direction = GetInclinedSlopeDirection(GetTileSlope(tile, NULL));
	if (direction == INVALID_DIAGDIR) return_cmd_error(STR_304B_SITE_UNSUITABLE);
	direction = ReverseDiagDir(direction);

	/* Docks cannot be placed on rapids */
	if (IsWaterTile(tile)) return_cmd_error(STR_304B_SITE_UNSUITABLE);

	if (!CheckIfAuthorityAllowsNewStation(tile, flags)) return CMD_ERROR;

	if (MayHaveBridgeAbove(tile) && IsBridgeAbove(tile)) return_cmd_error(STR_5007_MUST_DEMOLISH_BRIDGE_FIRST);

	if (CmdFailed(DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR))) return CMD_ERROR;

	TileIndex tile_cur = tile + TileOffsByDiagDir(direction);

	if (!IsTileType(tile_cur, MP_WATER) || GetTileSlope(tile_cur, NULL) != SLOPE_FLAT) {
		return_cmd_error(STR_304B_SITE_UNSUITABLE);
	}

	if (MayHaveBridgeAbove(tile_cur) && IsBridgeAbove(tile_cur)) return_cmd_error(STR_5007_MUST_DEMOLISH_BRIDGE_FIRST);

	/* Get the water class of the water tile before it is cleared.*/
	WaterClass wc = GetWaterClass(tile_cur);

	if (CmdFailed(DoCommand(tile_cur, 0, 0, flags, CMD_LANDSCAPE_CLEAR))) return CMD_ERROR;

	tile_cur += TileOffsByDiagDir(direction);
	if (!IsTileType(tile_cur, MP_WATER) || GetTileSlope(tile_cur, NULL) != SLOPE_FLAT) {
		return_cmd_error(STR_304B_SITE_UNSUITABLE);
	}

	/* middle */
	Station *st = NULL;

	if (!_settings_game.station.adjacent_stations || !HasBit(p1, 0)) {
		st = GetStationAround(
				tile + ToTileIndexDiff(_dock_tileoffs_chkaround[direction]),
				_dock_w_chk[direction], _dock_h_chk[direction], INVALID_STATION);
		if (st == CHECK_STATIONS_ERR) return CMD_ERROR;
	}

	/* Distant join */
	if (st == NULL && distant_join) st = GetStation(station_to_join);

	/* Find a deleted station close to us */
	if (st == NULL) st = GetClosestDeletedStation(tile);

	if (st != NULL) {
		if (st->owner != _current_company) {
			return_cmd_error(STR_3009_TOO_CLOSE_TO_ANOTHER_STATION);
		}

		if (!st->rect.BeforeAddRect(
				tile + ToTileIndexDiff(_dock_tileoffs_chkaround[direction]),
				_dock_w_chk[direction], _dock_h_chk[direction], StationRect::ADD_TEST)) return CMD_ERROR;

		if (st->dock_tile != INVALID_TILE) return_cmd_error(STR_304C_TOO_CLOSE_TO_ANOTHER_DOCK);
	} else {
		/* allocate and initialize new station */
		if (!Station::CanAllocateItem()) return_cmd_error(STR_3008_TOO_MANY_STATIONS_LOADING);

		if (flags & DC_EXEC) {
			st = new Station(tile);

			st->town = ClosestTownFromTile(tile, UINT_MAX);
			st->string_id = GenerateStationName(st, tile, STATIONNAMING_DOCK);

			if (IsValidCompanyID(_current_company)) {
				SetBit(st->town->have_ratings, _current_company);
			}
		}
	}

	if (flags & DC_EXEC) {
		st->dock_tile = tile;
		st->AddFacility(FACIL_DOCK, tile);

		st->rect.BeforeAddRect(
				tile + ToTileIndexDiff(_dock_tileoffs_chkaround[direction]),
				_dock_w_chk[direction], _dock_h_chk[direction], StationRect::ADD_TRY);

		MakeDock(tile, st->owner, st->index, direction, wc);

		UpdateStationVirtCoordDirty(st);
		UpdateStationAcceptance(st, false);
		InvalidateWindowData(WC_SELECT_STATION, 0, 0);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_SHIPS);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price.build_dock);
}

static CommandCost RemoveDock(Station *st, DoCommandFlag flags)
{
	if (!CheckOwnership(st->owner)) return CMD_ERROR;

	TileIndex tile1 = st->dock_tile;
	TileIndex tile2 = tile1 + TileOffsByDiagDir(GetDockDirection(tile1));

	if (!EnsureNoVehicleOnGround(tile1)) return CMD_ERROR;
	if (!EnsureNoVehicleOnGround(tile2)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DoClearSquare(tile1);
		MakeWaterKeepingClass(tile2, st->owner);

		st->rect.AfterRemoveTile(st, tile1);
		st->rect.AfterRemoveTile(st, tile2);

		MarkTileDirtyByTile(tile2);

		st->dock_tile = INVALID_TILE;
		st->facilities &= ~FACIL_DOCK;

		InvalidateWindowWidget(WC_STATION_VIEW, st->index, SVW_SHIPS);
		UpdateStationVirtCoordDirty(st);
		DeleteStationIfEmpty(st);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price.remove_dock);
}

#include "table/station_land.h"

const DrawTileSprites *GetStationTileLayout(StationType st, byte gfx)
{
	return &_station_display_datas[st][gfx];
}

static void DrawTile_Station(TileInfo *ti)
{
	const DrawTileSprites *t = NULL;
	RoadTypes roadtypes;
	int32 total_offset;
	int32 custom_ground_offset;

	if (IsRailwayStation(ti->tile)) {
		const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
		roadtypes = ROADTYPES_NONE;
		total_offset = rti->total_offset;
		custom_ground_offset = rti->custom_ground_offset;
	} else {
		roadtypes = IsRoadStop(ti->tile) ? GetRoadTypes(ti->tile) : ROADTYPES_NONE;
		total_offset = 0;
		custom_ground_offset = 0;
	}
	uint32 relocation = 0;
	const Station *st = NULL;
	const StationSpec *statspec = NULL;
	Owner owner = GetTileOwner(ti->tile);

	SpriteID palette;
	if (IsValidCompanyID(owner)) {
		palette = COMPANY_SPRITE_COLOUR(owner);
	} else {
		/* Some stations are not owner by a company, namely oil rigs */
		palette = PALETTE_TO_GREY;
	}

	/* don't show foundation for docks */
	if (ti->tileh != SLOPE_FLAT && !IsDock(ti->tile))
		DrawFoundation(ti, FOUNDATION_LEVELED);

	if (IsCustomStationSpecIndex(ti->tile)) {
		/* look for customization */
		st = GetStationByTile(ti->tile);
		statspec = st->speclist[GetCustomStationSpecIndex(ti->tile)].spec;

		if (statspec != NULL) {
			uint tile = GetStationGfx(ti->tile);

			relocation = GetCustomStationRelocation(statspec, st, ti->tile);

			if (HasBit(statspec->callbackmask, CBM_STATION_SPRITE_LAYOUT)) {
				uint16 callback = GetStationCallback(CBID_STATION_SPRITE_LAYOUT, 0, 0, statspec, st, ti->tile);
				if (callback != CALLBACK_FAILED) tile = (callback & ~1) + GetRailStationAxis(ti->tile);
			}

			/* Ensure the chosen tile layout is valid for this custom station */
			if (statspec->renderdata != NULL) {
				t = &statspec->renderdata[tile < statspec->tiles ? tile : (uint)GetRailStationAxis(ti->tile)];
			}
		}
	}

	if (t == NULL || t->seq == NULL) t = &_station_display_datas[GetStationType(ti->tile)][GetStationGfx(ti->tile)];


	if (IsBuoy(ti->tile) || IsDock(ti->tile) || (IsOilRig(ti->tile) && GetWaterClass(ti->tile) != WATER_CLASS_INVALID)) {
		if (ti->tileh == SLOPE_FLAT) {
			DrawWaterClassGround(ti);
		} else {
			assert(IsDock(ti->tile));
			TileIndex water_tile = ti->tile + TileOffsByDiagDir(GetDockDirection(ti->tile));
			WaterClass wc = GetWaterClass(water_tile);
			if (wc == WATER_CLASS_SEA) {
				DrawShoreTile(ti->tileh);
			} else {
				DrawClearLandTile(ti, 3);
			}
		}
	} else {
		SpriteID image = t->ground.sprite;
		SpriteID pal   = t->ground.pal;
		if (HasBit(image, SPRITE_MODIFIER_USE_OFFSET)) {
			image += GetCustomStationGroundRelocation(statspec, st, ti->tile);
			image += custom_ground_offset;
		} else {
			image += total_offset;
		}
		DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, palette));

		/* PBS debugging, draw reserved tracks darker */
		if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && IsRailwayStation(ti->tile) && GetRailwayStationReservation(ti->tile)) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			DrawGroundSprite(GetRailStationAxis(ti->tile) == AXIS_X ? rti->base_sprites.single_y : rti->base_sprites.single_x, PALETTE_CRASH);
		}
	}

	if (IsRailwayStation(ti->tile) && HasCatenaryDrawn(GetRailType(ti->tile)) && IsStationTileElectrifiable(ti->tile)) DrawCatenary(ti);

	if (HasBit(roadtypes, ROADTYPE_TRAM)) {
		Axis axis = GetRoadStopDir(ti->tile) == DIAGDIR_NE ? AXIS_X : AXIS_Y;
		DrawGroundSprite((HasBit(roadtypes, ROADTYPE_ROAD) ? SPR_TRAMWAY_OVERLAY : SPR_TRAMWAY_TRAM) + (axis ^ 1), PAL_NONE);
		DrawTramCatenary(ti, axis == AXIS_X ? ROAD_X : ROAD_Y);
	}

	const DrawTileSeqStruct *dtss;
	foreach_draw_tile_seq(dtss, t->seq) {
		SpriteID image = dtss->image.sprite;

		/* Stop drawing sprite sequence once we meet a sprite that doesn't have to be opaque */
		if (IsInvisibilitySet(TO_BUILDINGS) && !HasBit(image, SPRITE_MODIFIER_OPAQUE)) return;

		if (relocation == 0 || HasBit(image, SPRITE_MODIFIER_USE_OFFSET)) {
			image += total_offset;
		} else {
			image += relocation;
		}

		SpriteID pal = SpriteLayoutPaletteTransform(image, dtss->image.pal, palette);

		if ((byte)dtss->delta_z != 0x80) {
			AddSortableSpriteToDraw(
				image, pal,
				ti->x + dtss->delta_x, ti->y + dtss->delta_y,
				dtss->size_x, dtss->size_y,
				dtss->size_z, ti->z + dtss->delta_z,
				!HasBit(image, SPRITE_MODIFIER_OPAQUE) && IsTransparencySet(TO_BUILDINGS)
			);
		} else {
			/* For stations and original spritelayouts delta_x and delta_y are signed */
			AddChildSpriteScreen(image, pal, dtss->delta_x, dtss->delta_y, !HasBit(image, SPRITE_MODIFIER_OPAQUE) && IsTransparencySet(TO_BUILDINGS));
		}
	}
}

void StationPickerDrawSprite(int x, int y, StationType st, RailType railtype, RoadType roadtype, int image)
{
	int32 total_offset = 0;
	SpriteID pal = COMPANY_SPRITE_COLOUR(_local_company);
	const DrawTileSprites *t = &_station_display_datas[st][image];

	if (railtype != INVALID_RAILTYPE) {
		const RailtypeInfo *rti = GetRailTypeInfo(railtype);
		total_offset = rti->total_offset;
	}

	SpriteID img = t->ground.sprite;
	DrawSprite(img + total_offset, HasBit(img, PALETTE_MODIFIER_COLOUR) ? pal : PAL_NONE, x, y);

	if (roadtype == ROADTYPE_TRAM) {
		DrawSprite(SPR_TRAMWAY_TRAM + (t->ground.sprite == SPR_ROAD_PAVED_STRAIGHT_X ? 1 : 0), PAL_NONE, x, y);
	}

	const DrawTileSeqStruct *dtss;
	foreach_draw_tile_seq(dtss, t->seq) {
		Point pt = RemapCoords(dtss->delta_x, dtss->delta_y, dtss->delta_z);
		DrawSprite(dtss->image.sprite + total_offset, pal, x + pt.x, y + pt.y);
	}
}

static uint GetSlopeZ_Station(TileIndex tile, uint x, uint y)
{
	return GetTileMaxZ(tile);
}

static Foundation GetFoundation_Station(TileIndex tile, Slope tileh)
{
	return FlatteningFoundation(tileh);
}

static void GetAcceptedCargo_Station(TileIndex tile, AcceptedCargo ac)
{
	/* not used */
}

static void GetTileDesc_Station(TileIndex tile, TileDesc *td)
{
	td->owner[0] = GetTileOwner(tile);
	if (IsDriveThroughStopTile(tile)) {
		Owner road_owner = INVALID_OWNER;
		Owner tram_owner = INVALID_OWNER;
		RoadTypes rts = GetRoadTypes(tile);
		if (HasBit(rts, ROADTYPE_ROAD)) road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
		if (HasBit(rts, ROADTYPE_TRAM)) tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);

		/* Is there a mix of owners? */
		if ((tram_owner != INVALID_OWNER && tram_owner != td->owner[0]) ||
				(road_owner != INVALID_OWNER && road_owner != td->owner[0])) {
			uint i = 1;
			if (road_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_ROAD_OWNER;
				td->owner[i] = road_owner;
				i++;
			}
			if (tram_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_TRAM_OWNER;
				td->owner[i] = tram_owner;
			}
		}
	}
	td->build_date = GetStationByTile(tile)->build_date;

	const StationSpec *spec = GetStationSpec(tile);

	if (spec != NULL) {
		td->station_class = GetStationClassName(spec->sclass);
		td->station_name = spec->name;

		if (spec->grffile != NULL) {
			const GRFConfig *gc = GetGRFConfig(spec->grffile->grfid);
			td->grf = gc->name;
		}
	}

	StringID str;
	switch (GetStationType(tile)) {
		default: NOT_REACHED();
		case STATION_RAIL:    str = STR_305E_RAILROAD_STATION; break;
		case STATION_AIRPORT:
			str = (IsHangar(tile) ? STR_305F_AIRCRAFT_HANGAR : STR_3060_AIRPORT);
			break;
		case STATION_TRUCK:   str = STR_3061_TRUCK_LOADING_AREA; break;
		case STATION_BUS:     str = STR_3062_BUS_STATION; break;
		case STATION_OILRIG:  str = STR_4807_OIL_RIG; break;
		case STATION_DOCK:    str = STR_3063_SHIP_DOCK; break;
		case STATION_BUOY:    str = STR_3069_BUOY; break;
	}
	td->str = str;
}


static TrackStatus GetTileTrackStatus_Station(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TrackBits trackbits = TRACK_BIT_NONE;

	switch (mode) {
		case TRANSPORT_RAIL:
			if (IsRailwayStation(tile) && !IsStationTileBlocked(tile)) {
				trackbits = TrackToTrackBits(GetRailStationTrack(tile));
			}
			break;

		case TRANSPORT_WATER:
			/* buoy is coded as a station, it is always on open water */
			if (IsBuoy(tile)) {
				trackbits = TRACK_BIT_ALL;
				/* remove tracks that connect NE map edge */
				if (TileX(tile) == 0) trackbits &= ~(TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_RIGHT);
				/* remove tracks that connect NW map edge */
				if (TileY(tile) == 0) trackbits &= ~(TRACK_BIT_Y | TRACK_BIT_LEFT | TRACK_BIT_UPPER);
			}
			break;

		case TRANSPORT_ROAD:
			if ((GetRoadTypes(tile) & sub_mode) != 0 && IsRoadStop(tile)) {
				DiagDirection dir = GetRoadStopDir(tile);
				Axis axis = DiagDirToAxis(dir);

				if (side != INVALID_DIAGDIR) {
					if (axis != DiagDirToAxis(side) || (IsStandardRoadStopTile(tile) && dir != side)) break;
				}

				trackbits = AxisToTrackBits(axis);
			}
			break;

		default:
			break;
	}

	return CombineTrackStatus(TrackBitsToTrackdirBits(trackbits), TRACKDIR_BIT_NONE);
}


static void TileLoop_Station(TileIndex tile)
{
	/* FIXME -- GetTileTrackStatus_Station -> animated stationtiles
	 * hardcoded.....not good */
	switch (GetStationType(tile)) {
		case STATION_AIRPORT:
			switch (GetStationGfx(tile)) {
				case GFX_RADAR_LARGE_FIRST:
				case GFX_WINDSACK_FIRST : // for small airport
				case GFX_RADAR_INTERNATIONAL_FIRST:
				case GFX_RADAR_METROPOLITAN_FIRST:
				case GFX_RADAR_DISTRICTWE_FIRST: // radar district W-E airport
				case GFX_WINDSACK_INTERCON_FIRST : // for intercontinental airport
					AddAnimatedTile(tile);
					break;
			}
			break;

		case STATION_DOCK:
			if (GetTileSlope(tile, NULL) != SLOPE_FLAT) break; // only handle water part
		/* FALL THROUGH */
		case STATION_OILRIG: //(station part)
		case STATION_BUOY:
			TileLoop_Water(tile);
			break;

		default: break;
	}
}


static void AnimateTile_Station(TileIndex tile)
{
	struct AnimData {
		StationGfx from; // first sprite
		StationGfx to;   // last sprite
		byte delay;
	};

	static const AnimData data[] = {
		{ GFX_RADAR_LARGE_FIRST,         GFX_RADAR_LARGE_LAST,         3 },
		{ GFX_WINDSACK_FIRST,            GFX_WINDSACK_LAST,            1 },
		{ GFX_RADAR_INTERNATIONAL_FIRST, GFX_RADAR_INTERNATIONAL_LAST, 3 },
		{ GFX_RADAR_METROPOLITAN_FIRST,  GFX_RADAR_METROPOLITAN_LAST,  3 },
		{ GFX_RADAR_DISTRICTWE_FIRST,    GFX_RADAR_DISTRICTWE_LAST,    3 },
		{ GFX_WINDSACK_INTERCON_FIRST,   GFX_WINDSACK_INTERCON_LAST,   1 }
	};

	if (IsRailwayStation(tile)) {
		AnimateStationTile(tile);
		return;
	}

	StationGfx gfx = GetStationGfx(tile);

	for (const AnimData *i = data; i != endof(data); i++) {
		if (i->from <= gfx && gfx <= i->to) {
			if ((_tick_counter & i->delay) == 0) {
				SetStationGfx(tile, gfx < i->to ? gfx + 1 : i->from);
				MarkTileDirtyByTile(tile);
			}
			break;
		}
	}
}


static bool ClickTile_Station(TileIndex tile)
{
	if (IsHangar(tile)) {
		ShowDepotWindow(tile, VEH_AIRCRAFT);
	} else {
		ShowStationViewWindow(GetStationIndex(tile));
	}
	return true;
}

static VehicleEnterTileStatus VehicleEnter_Station(Vehicle *v, TileIndex tile, int x, int y)
{
	StationID station_id = GetStationIndex(tile);

	if (v->type == VEH_TRAIN) {
		if (!v->current_order.ShouldStopAtStation(v, station_id)) return VETSB_CONTINUE;
		if (IsRailwayStation(tile) && IsFrontEngine(v) &&
				!IsCompatibleTrainStationTile(tile + TileOffsByDiagDir(DirToDiagDir(v->direction)), tile)) {
			DiagDirection dir = DirToDiagDir(v->direction);

			x &= 0xF;
			y &= 0xF;

			if (DiagDirToAxis(dir) != AXIS_X) Swap(x, y);
			if (y == TILE_SIZE / 2) {
				if (dir != DIAGDIR_SE && dir != DIAGDIR_SW) x = TILE_SIZE - 1 - x;
				int stop = TILE_SIZE - (v->u.rail.cached_veh_length + 1) / 2;
				if (x == stop) return VETSB_ENTERED_STATION | (VehicleEnterTileStatus)(station_id << VETS_STATION_ID_OFFSET); // enter station
				if (x < stop) {
					uint16 spd;

					v->vehstatus |= VS_TRAIN_SLOWING;
					spd = max(0, (stop - x) * 20 - 15);
					if (spd < v->cur_speed) v->cur_speed = spd;
				}
			}
		}
	} else if (v->type == VEH_ROAD) {
		if (v->u.road.state < RVSB_IN_ROAD_STOP && !IsReversingRoadTrackdir((Trackdir)v->u.road.state) && v->u.road.frame == 0) {
			if (IsRoadStop(tile) && IsRoadVehFront(v)) {
				/* Attempt to allocate a parking bay in a road stop */
				RoadStop *rs = GetRoadStopByTile(tile, GetRoadStopType(tile));

				if (IsDriveThroughStopTile(tile)) {
					if (!v->current_order.ShouldStopAtStation(v, station_id)) return VETSB_CONTINUE;

					/* Vehicles entering a drive-through stop from the 'normal' side use first bay (bay 0). */
					byte side = ((DirToDiagDir(v->direction) == ReverseDiagDir(GetRoadStopDir(tile))) == (v->u.road.overtaking == 0)) ? 0 : 1;

					if (!rs->IsFreeBay(side)) return VETSB_CANNOT_ENTER;

					/* Check if the vehicle is stopping at this road stop */
					if (GetRoadStopType(tile) == (IsCargoInClass(v->cargo_type, CC_PASSENGERS) ? ROADSTOP_BUS : ROADSTOP_TRUCK) &&
							v->current_order.GetDestination() == GetStationIndex(tile)) {
						SetBit(v->u.road.state, RVS_IS_STOPPING);
						rs->AllocateDriveThroughBay(side);
					}

					/* Indicate if vehicle is using second bay. */
					if (side == 1) SetBit(v->u.road.state, RVS_USING_SECOND_BAY);
					/* Indicate a drive-through stop */
					SetBit(v->u.road.state, RVS_IN_DT_ROAD_STOP);
					return VETSB_CONTINUE;
				}

				/* For normal (non drive-through) road stops
				 * Check if station is busy or if there are no free bays or whether it is a articulated vehicle. */
				if (rs->IsEntranceBusy() || !rs->HasFreeBay() || RoadVehHasArticPart(v)) return VETSB_CANNOT_ENTER;

				SetBit(v->u.road.state, RVS_IN_ROAD_STOP);

				/* Allocate a bay and update the road state */
				uint bay_nr = rs->AllocateBay();
				SB(v->u.road.state, RVS_USING_SECOND_BAY, 1, bay_nr);

				/* Mark the station entrace as busy */
				rs->SetEntranceBusy(true);
			}
		}
	}

	return VETSB_CONTINUE;
}

/* this function is called for one station each tick */
static void StationHandleBigTick(Station *st)
{
	UpdateStationAcceptance(st, true);

	if (st->facilities == 0 && ++st->delete_ctr >= 8) delete st;

}

static inline void byte_inc_sat(byte *p)
{
	byte b = *p + 1;
	if (b != 0) *p = b;
}

static void UpdateStationRating(Station *st)
{
	bool waiting_changed = false;

	byte_inc_sat(&st->time_since_load);
	byte_inc_sat(&st->time_since_unload);

	GoodsEntry *ge = st->goods;
	do {
		/* Slowly increase the rating back to his original level in the case we
		 *  didn't deliver cargo yet to this station. This happens when a bribe
		 *  failed while you didn't moved that cargo yet to a station. */
		if (!HasBit(ge->acceptance_pickup, GoodsEntry::PICKUP) && ge->rating < INITIAL_STATION_RATING) {
			ge->rating++;
		}

		/* Only change the rating if we are moving this cargo */
		if (HasBit(ge->acceptance_pickup, GoodsEntry::PICKUP)) {
			byte_inc_sat(&ge->days_since_pickup);

			int rating = 0;

			{
				int b = ge->last_speed - 85;
				if (b >= 0)
					rating += b >> 2;
			}

			{
				byte age = ge->last_age;
				(age >= 3) ||
				(rating += 10, age >= 2) ||
				(rating += 10, age >= 1) ||
				(rating += 13, true);
			}

			if (IsValidCompanyID(st->owner) && HasBit(st->town->statues, st->owner)) rating += 26;

			{
				byte days = ge->days_since_pickup;
				if (st->last_vehicle_type == VEH_SHIP) days >>= 2;
				(days > 21) ||
				(rating += 25, days > 12) ||
				(rating += 25, days > 6) ||
				(rating += 45, days > 3) ||
				(rating += 35, true);
			}

			uint waiting = ge->cargo.Count();
			(rating -= 90, waiting > 1500) ||
			(rating += 55, waiting > 1000) ||
			(rating += 35, waiting > 600) ||
			(rating += 10, waiting > 300) ||
			(rating += 20, waiting > 100) ||
			(rating += 10, true);

			{
				int or_ = ge->rating; // old rating

				/* only modify rating in steps of -2, -1, 0, 1 or 2 */
				ge->rating = rating = or_ + Clamp(Clamp(rating, 0, 255) - or_, -2, 2);

				/* if rating is <= 64 and more than 200 items waiting,
				 * remove some random amount of goods from the station */
				if (rating <= 64 && waiting >= 200) {
					int dec = Random() & 0x1F;
					if (waiting < 400) dec &= 7;
					waiting -= dec + 1;
					waiting_changed = true;
				}

				/* if rating is <= 127 and there are any items waiting, maybe remove some goods. */
				if (rating <= 127 && waiting != 0) {
					uint32 r = Random();
					if (rating <= (int)GB(r, 0, 7)) {
						/* Need to have int, otherwise it will just overflow etc. */
						waiting = max((int)waiting - (int)GB(r, 8, 2) - 1, 0);
						waiting_changed = true;
					}
				}

				/* At some point we really must cap the cargo. Previously this
				 * was a strict 4095, but now we'll have a less strict, but
				 * increasingly agressive truncation of the amount of cargo. */
				static const uint WAITING_CARGO_THRESHOLD  = 1 << 12;
				static const uint WAITING_CARGO_CUT_FACTOR = 1 <<  6;
				static const uint MAX_WAITING_CARGO        = 1 << 15;

				if (waiting > WAITING_CARGO_THRESHOLD) {
					uint difference = waiting - WAITING_CARGO_THRESHOLD;
					waiting -= (difference / WAITING_CARGO_CUT_FACTOR);

					waiting = min(waiting, MAX_WAITING_CARGO);
					waiting_changed = true;
				}

				if (waiting_changed) ge->cargo.Truncate(waiting);
			}
		}
	} while (++ge != endof(st->goods));

	StationID index = st->index;
	if (waiting_changed) {
		InvalidateWindow(WC_STATION_VIEW, index); // update whole window
	} else {
		InvalidateWindowWidget(WC_STATION_VIEW, index, SVW_RATINGLIST); // update only ratings list
	}
}

/* called for every station each tick */
static void StationHandleSmallTick(Station *st)
{
	if (st->facilities == 0) return;

	byte b = st->delete_ctr + 1;
	if (b >= 185) b = 0;
	st->delete_ctr = b;

	if (b == 0) UpdateStationRating(st);
}

void OnTick_Station()
{
	if (_game_mode == GM_EDITOR) return;

	uint i = _station_tick_ctr;
	if (++_station_tick_ctr > GetMaxStationIndex()) _station_tick_ctr = 0;

	if (IsValidStationID(i)) StationHandleBigTick(GetStation(i));

	Station *st;
	FOR_ALL_STATIONS(st) {
		StationHandleSmallTick(st);

		/* Run 250 tick interval trigger for station animation.
		 * Station index is included so that triggers are not all done
		 * at the same time. */
		if ((_tick_counter + st->index) % 250 == 0) {
			StationAnimationTrigger(st, st->xy, STAT_ANIM_250_TICKS);
		}
	}
}

void StationMonthlyLoop()
{
	/* not used */
}


void ModifyStationRatingAround(TileIndex tile, Owner owner, int amount, uint radius)
{
	Station *st;

	FOR_ALL_STATIONS(st) {
		if (st->owner == owner &&
				DistanceManhattan(tile, st->xy) <= radius) {
			for (CargoID i = 0; i < NUM_CARGO; i++) {
				GoodsEntry *ge = &st->goods[i];

				if (ge->acceptance_pickup != 0) {
					ge->rating = Clamp(ge->rating + amount, 0, 255);
				}
			}
		}
	}
}

static void UpdateStationWaiting(Station *st, CargoID type, uint amount)
{
	st->goods[type].cargo.Append(new CargoPacket(st->index, amount));
	SetBit(st->goods[type].acceptance_pickup, GoodsEntry::PICKUP);

	StationAnimationTrigger(st, st->xy, STAT_ANIM_NEW_CARGO, type);

	InvalidateWindow(WC_STATION_VIEW, st->index);
	st->MarkTilesDirty(true);
}

static bool IsUniqueStationName(const char *name)
{
	const Station *st;

	FOR_ALL_STATIONS(st) {
		if (st->name != NULL && strcmp(st->name, name) == 0) return false;
	}

	return true;
}

/** Rename a station
 * @param tile unused
 * @param flags operation to perform
 * @param p1 station ID that is to be renamed
 * @param p2 unused
 */
CommandCost CmdRenameStation(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!IsValidStationID(p1)) return CMD_ERROR;

	Station *st = GetStation(p1);
	if (!CheckOwnership(st->owner)) return CMD_ERROR;

	bool reset = StrEmpty(text);

	if (!reset) {
		if (strlen(text) >= MAX_LENGTH_STATION_NAME_BYTES) return CMD_ERROR;
		if (!IsUniqueStationName(text)) return_cmd_error(STR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		free(st->name);
		st->name = reset ? NULL : strdup(text);

		UpdateStationVirtCoord(st);
		InvalidateWindowData(WC_STATION_LIST, st->owner, 1);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}

/**
 * Find all (non-buoy) stations around a rectangular producer (industry, house, headquarter, ...)
 *
 * @param tile North tile of producer
 * @param w_prod X extent of producer
 * @param h_prod Y extent of producer
 *
 * @return: Set of found stations
 */
void FindStationsAroundTiles(TileIndex tile, int w_prod, int h_prod, StationList *stations)
{
	/* area to search = producer plus station catchment radius */
	int max_rad = (_settings_game.station.modified_catchment ? MAX_CATCHMENT : CA_UNMODIFIED);

	for (int dy = -max_rad; dy < h_prod + max_rad; dy++) {
		for (int dx = -max_rad; dx < w_prod + max_rad; dx++) {
			TileIndex cur_tile = TileAddWrap(tile, dx, dy);
			if (cur_tile == INVALID_TILE || !IsTileType(cur_tile, MP_STATION)) continue;

			Station *st = GetStationByTile(cur_tile);

			if (st->IsBuoy()) continue; // bouys don't accept cargo

			if (_settings_game.station.modified_catchment) {
				int rad = st->GetCatchmentRadius();
				if (dx < -rad || dx >= rad + w_prod || dy < -rad || dy >= rad + h_prod) continue;
			}

			/* Insert the station in the set. This will fail if it has
			 * already been added.
			 */
			stations->Include(st);
		}
	}
}

uint MoveGoodsToStation(TileIndex tile, int w, int h, CargoID type, uint amount)
{
	Station *st1 = NULL;   // Station with best rating
	Station *st2 = NULL;   // Second best station
	uint best_rating1 = 0; // rating of st1
	uint best_rating2 = 0; // rating of st2

	StationList all_stations;
	FindStationsAroundTiles(tile, w, h, &all_stations);
	for (Station **st_iter = all_stations.Begin(); st_iter != all_stations.End(); ++st_iter) {
		Station *st = *st_iter;

		/* Is the station reserved exclusively for somebody else? */
		if (st->town->exclusive_counter > 0 && st->town->exclusivity != st->owner) continue;

		if (st->goods[type].rating == 0) continue; // Lowest possible rating, better not to give cargo anymore

		if (_settings_game.order.selectgoods && st->goods[type].last_speed == 0) continue; // Selectively servicing stations, and not this one

		if (IsCargoInClass(type, CC_PASSENGERS)) {
			if (st->facilities == FACIL_TRUCK_STOP) continue; // passengers are never served by just a truck stop
		} else {
			if (st->facilities == FACIL_BUS_STOP) continue; // non-passengers are never served by just a bus stop
		}

		/* This station can be used, add it to st1/st2 */
		if (st1 == NULL || st->goods[type].rating >= best_rating1) {
			st2 = st1; best_rating2 = best_rating1; st1 = st; best_rating1 = st->goods[type].rating;
		} else if (st2 == NULL || st->goods[type].rating >= best_rating2) {
			st2 = st; best_rating2 = st->goods[type].rating;
		}
	}

	/* no stations around at all? */
	if (st1 == NULL) return 0;

	if (st2 == NULL) {
		/* only one station around */
		uint moved = amount * best_rating1 / 256 + 1;
		UpdateStationWaiting(st1, type, moved);
		return moved;
	}

	/* several stations around, the best two (highest rating) are in st1 and st2 */
	assert(st1 != NULL);
	assert(st2 != NULL);
	assert(best_rating1 != 0 || best_rating2 != 0);

	/* the 2nd highest one gets a penalty */
	best_rating2 >>= 1;

	/* amount given to station 1 */
	uint t = (best_rating1 * (amount + 1)) / (best_rating1 + best_rating2);

	uint moved = 0;
	if (t != 0) {
		moved = t * best_rating1 / 256 + 1;
		amount -= t;
		UpdateStationWaiting(st1, type, moved);
	}

	if (amount != 0) {
		amount = amount * best_rating2 / 256 + 1;
		moved += amount;
		UpdateStationWaiting(st2, type, amount);
	}

	return moved;
}

void BuildOilRig(TileIndex tile)
{
	if (!Station::CanAllocateItem()) {
		DEBUG(misc, 0, "Can't allocate station for oilrig at 0x%X, reverting to oilrig only", tile);
		return;
	}

	Station *st = new Station(tile);
	st->town = ClosestTownFromTile(tile, UINT_MAX);
	st->sign.width_1 = 0;

	st->string_id = GenerateStationName(st, tile, STATIONNAMING_OILRIG);

	assert(IsTileType(tile, MP_INDUSTRY));
	MakeOilrig(tile, st->index, GetWaterClass(tile));

	st->owner = OWNER_NONE;
	st->airport_flags = 0;
	st->airport_type = AT_OILRIG;
	st->xy = tile;
	st->bus_stops = NULL;
	st->truck_stops = NULL;
	st->airport_tile = tile;
	st->dock_tile = tile;
	st->train_tile = INVALID_TILE;
	st->had_vehicle_of_type = 0;
	st->time_since_load = 255;
	st->time_since_unload = 255;
	st->delete_ctr = 0;
	st->last_vehicle_type = VEH_INVALID;
	st->facilities = FACIL_AIRPORT | FACIL_DOCK;
	st->build_date = _date;

	st->rect.BeforeAddTile(tile, StationRect::ADD_FORCE);

	for (CargoID j = 0; j < NUM_CARGO; j++) {
		st->goods[j].acceptance_pickup = 0;
		st->goods[j].days_since_pickup = 255;
		st->goods[j].rating = INITIAL_STATION_RATING;
		st->goods[j].last_speed = 0;
		st->goods[j].last_age = 255;
	}

	UpdateStationVirtCoordDirty(st);
	UpdateStationAcceptance(st, false);
}

void DeleteOilRig(TileIndex tile)
{
	Station *st = GetStationByTile(tile);

	MakeWaterKeepingClass(tile, OWNER_NONE);
	MarkTileDirtyByTile(tile);

	st->dock_tile = INVALID_TILE;
	st->airport_tile = INVALID_TILE;
	st->facilities &= ~(FACIL_AIRPORT | FACIL_DOCK);
	st->airport_flags = 0;

	st->rect.AfterRemoveTile(st, tile);

	UpdateStationVirtCoordDirty(st);
	if (st->facilities == 0) delete st;
}

static void ChangeTileOwner_Station(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (IsDriveThroughStopTile(tile)) {
		for (RoadType rt = ROADTYPE_ROAD; rt < ROADTYPE_END; rt++) {
			/* Update all roadtypes, no matter if they are present */
			if (GetRoadOwner(tile, rt) == old_owner) {
				SetRoadOwner(tile, rt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
			}
		}
	}

	if (!IsTileOwner(tile, old_owner)) return;

	if (new_owner != INVALID_OWNER) {
		/* for buoys, owner of tile is owner of water, st->owner == OWNER_NONE */
		SetTileOwner(tile, new_owner);
		InvalidateWindowClassesData(WC_STATION_LIST, 0);
	} else {
		if (IsDriveThroughStopTile(tile)) {
			/* Remove the drive-through road stop */
			DoCommand(tile, 0, (GetStationType(tile) == STATION_TRUCK) ? ROADSTOP_TRUCK : ROADSTOP_BUS, DC_EXEC | DC_BANKRUPT, CMD_REMOVE_ROAD_STOP);
			assert(IsTileType(tile, MP_ROAD));
			/* Change owner of tile and all roadtypes */
			ChangeTileOwner(tile, old_owner, new_owner);
		} else {
			DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
			/* Set tile owner of water under (now removed) buoy and dock to OWNER_NONE.
			 * Update owner of buoy if it was not removed (was in orders).
			 * Do not update when owned by OWNER_WATER (sea and rivers). */
			if ((IsTileType(tile, MP_WATER) || IsBuoyTile(tile)) && IsTileOwner(tile, old_owner)) SetTileOwner(tile, OWNER_NONE);
		}
	}
}

/**
 * Check if a drive-through road stop tile can be cleared.
 * Road stops built on town-owned roads check the conditions
 * that would allow clearing of the original road.
 * @param tile road stop tile to check
 * @param flags command flags
 * @return true if the road can be cleared
 */
static bool CanRemoveRoadWithStop(TileIndex tile, DoCommandFlag flags)
{
	Owner road_owner = _current_company;
	Owner tram_owner = _current_company;

	RoadTypes rts = GetRoadTypes(tile);
	if (HasBit(rts, ROADTYPE_ROAD)) road_owner = GetRoadOwner(tile, ROADTYPE_ROAD);
	if (HasBit(rts, ROADTYPE_TRAM)) tram_owner = GetRoadOwner(tile, ROADTYPE_TRAM);

	if ((road_owner != OWNER_TOWN && !CheckOwnership(road_owner)) || !CheckOwnership(tram_owner)) return false;

	return road_owner != OWNER_TOWN || CheckAllowRemoveRoad(tile, GetAnyRoadBits(tile, ROADTYPE_ROAD), OWNER_TOWN, ROADTYPE_ROAD, flags);
}

static CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags)
{
	if (flags & DC_AUTO) {
		switch (GetStationType(tile)) {
			case STATION_RAIL:    return_cmd_error(STR_300B_MUST_DEMOLISH_RAILROAD);
			case STATION_AIRPORT: return_cmd_error(STR_300E_MUST_DEMOLISH_AIRPORT_FIRST);
			case STATION_TRUCK:   return_cmd_error(HasTileRoadType(tile, ROADTYPE_TRAM) ? STR_MUST_DEMOLISH_CARGO_TRAM_STATION : STR_3047_MUST_DEMOLISH_TRUCK_STATION);
			case STATION_BUS:     return_cmd_error(HasTileRoadType(tile, ROADTYPE_TRAM) ? STR_MUST_DEMOLISH_PASSENGER_TRAM_STATION : STR_3046_MUST_DEMOLISH_BUS_STATION);
			case STATION_BUOY:    return_cmd_error(STR_306A_BUOY_IN_THE_WAY);
			case STATION_DOCK:    return_cmd_error(STR_304D_MUST_DEMOLISH_DOCK_FIRST);
			case STATION_OILRIG:
				SetDParam(0, STR_4807_OIL_RIG);
				return_cmd_error(STR_4800_IN_THE_WAY);
		}
	}

	Station *st = GetStationByTile(tile);

	switch (GetStationType(tile)) {
		case STATION_RAIL:    return RemoveRailroadStation(st, tile, flags);
		case STATION_AIRPORT: return RemoveAirport(st, flags);
		case STATION_TRUCK:
			if (IsDriveThroughStopTile(tile) && !CanRemoveRoadWithStop(tile, flags))
				return_cmd_error(STR_3047_MUST_DEMOLISH_TRUCK_STATION);
			return RemoveRoadStop(st, flags, tile);
		case STATION_BUS:
			if (IsDriveThroughStopTile(tile) && !CanRemoveRoadWithStop(tile, flags))
				return_cmd_error(STR_3046_MUST_DEMOLISH_BUS_STATION);
			return RemoveRoadStop(st, flags, tile);
		case STATION_BUOY:    return RemoveBuoy(st, flags);
		case STATION_DOCK:    return RemoveDock(st, flags);
		default: break;
	}

	return CMD_ERROR;
}

void InitializeStations()
{
	/* Clean the station pool and create 1 block in it */
	_Station_pool.CleanPool();
	_Station_pool.AddBlockToPool();

	/* Clean the roadstop pool and create 1 block in it */
	_RoadStop_pool.CleanPool();
	_RoadStop_pool.AddBlockToPool();

	_station_tick_ctr = 0;
}

static CommandCost TerraformTile_Station(TileIndex tile, DoCommandFlag flags, uint z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled()) {
		/* TODO: If you implement newgrf callback 149 'land slope check', you have to decide what to do with it here.
		 *       TTDP does not call it.
		 */
		if (!IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new))) {
			switch (GetStationType(tile)) {
				case STATION_RAIL: {
					DiagDirection direction = AxisToDiagDir(GetRailStationAxis(tile));
					if (!AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, direction)) break;
					if (!AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, ReverseDiagDir(direction))) break;
					return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
				}

				case STATION_AIRPORT:
					return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);

				case STATION_TRUCK:
				case STATION_BUS: {
					DiagDirection direction = GetRoadStopDir(tile);
					if (!AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, direction)) break;
					if (IsDriveThroughStopTile(tile)) {
						if (!AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, ReverseDiagDir(direction))) break;
					}
					return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
				}

				default: break;
			}
		}
	}
	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}


extern const TileTypeProcs _tile_type_station_procs = {
	DrawTile_Station,           // draw_tile_proc
	GetSlopeZ_Station,          // get_slope_z_proc
	ClearTile_Station,          // clear_tile_proc
	GetAcceptedCargo_Station,   // get_accepted_cargo_proc
	GetTileDesc_Station,        // get_tile_desc_proc
	GetTileTrackStatus_Station, // get_tile_track_status_proc
	ClickTile_Station,          // click_tile_proc
	AnimateTile_Station,        // animate_tile_proc
	TileLoop_Station,           // tile_loop_clear
	ChangeTileOwner_Station,    // change_tile_owner_clear
	NULL,                       // get_produced_cargo_proc
	VehicleEnter_Station,       // vehicle_enter_tile_proc
	GetFoundation_Station,      // get_foundation_proc
	TerraformTile_Station,      // terraform_tile_proc
};
