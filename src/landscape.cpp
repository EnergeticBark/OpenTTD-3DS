/* $Id$ */

/** @file landscape.cpp Functions related to the landscape (slopes etc.). */

/** @defgroup SnowLineGroup Snowline functions and data structures */

#include "stdafx.h"
#include "heightmap.h"
#include "clear_map.h"
#include "spritecache.h"
#include "viewport_func.h"
#include "command_func.h"
#include "landscape.h"
#include "variables.h"
#include "void_map.h"
#include "tgp.h"
#include "genworld.h"
#include "fios.h"
#include "functions.h"
#include "date_func.h"
#include "water.h"
#include "effectvehicle_func.h"
#include "landscape_type.h"
#include "settings_type.h"

#include "table/sprites.h"

extern const TileTypeProcs
	_tile_type_clear_procs,
	_tile_type_rail_procs,
	_tile_type_road_procs,
	_tile_type_town_procs,
	_tile_type_trees_procs,
	_tile_type_station_procs,
	_tile_type_water_procs,
	_tile_type_dummy_procs,
	_tile_type_industry_procs,
	_tile_type_tunnelbridge_procs,
	_tile_type_unmovable_procs;

/** Tile callback functions for each type of tile.
 * @ingroup TileCallbackGroup
 * @see TileType */
const TileTypeProcs * const _tile_type_procs[16] = {
	&_tile_type_clear_procs,        ///< Callback functions for MP_CLEAR tiles
	&_tile_type_rail_procs,         ///< Callback functions for MP_RAILWAY tiles
	&_tile_type_road_procs,         ///< Callback functions for MP_ROAD tiles
	&_tile_type_town_procs,         ///< Callback functions for MP_HOUSE tiles
	&_tile_type_trees_procs,        ///< Callback functions for MP_TREES tiles
	&_tile_type_station_procs,      ///< Callback functions for MP_STATION tiles
	&_tile_type_water_procs,        ///< Callback functions for MP_WATER tiles
	&_tile_type_dummy_procs,        ///< Callback functions for MP_VOID tiles
	&_tile_type_industry_procs,     ///< Callback functions for MP_INDUSTRY tiles
	&_tile_type_tunnelbridge_procs, ///< Callback functions for MP_TUNNELBRIDGE tiles
	&_tile_type_unmovable_procs,    ///< Callback functions for MP_UNMOVABLE tiles
};

/* landscape slope => sprite */
const byte _tileh_to_sprite[32] = {
	0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
	0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0,
};

/**
 * Description of the snow line throughout the year.
 *
 * If it is \c NULL, a static snowline height is used, as set by \c _settings_game.game_creation.snow_line.
 * Otherwise it points to a table loaded from a newGRF file, that describes the variable snowline
 * @ingroup SnowLineGroup
 * @see GetSnowLine() GameCreationSettings */
SnowLine *_snow_line = NULL;

/**
 * Applies a foundation to a slope.
 *
 * @pre      Foundation and slope must be valid combined.
 * @param f  The #Foundation.
 * @param s  The #Slope to modify.
 * @return   Increment to the tile Z coordinate.
 */
uint ApplyFoundationToSlope(Foundation f, Slope *s)
{
	if (!IsFoundation(f)) return 0;

	if (IsLeveledFoundation(f)) {
		uint dz = TILE_HEIGHT + (IsSteepSlope(*s) ? TILE_HEIGHT : 0);
		*s = SLOPE_FLAT;
		return dz;
	}

	if (f != FOUNDATION_STEEP_BOTH && IsNonContinuousFoundation(f)) {
		*s = HalftileSlope(*s, GetHalftileFoundationCorner(f));
		return 0;
	}

	if (IsSpecialRailFoundation(f)) {
		*s = SlopeWithThreeCornersRaised(OppositeCorner(GetRailFoundationCorner(f)));
		return 0;
	}

	uint dz = IsSteepSlope(*s) ? TILE_HEIGHT : 0;
	Corner highest_corner = GetHighestSlopeCorner(*s);

	switch (f) {
		case FOUNDATION_INCLINED_X:
			*s = (((highest_corner == CORNER_W) || (highest_corner == CORNER_S)) ? SLOPE_SW : SLOPE_NE);
			break;

		case FOUNDATION_INCLINED_Y:
			*s = (((highest_corner == CORNER_S) || (highest_corner == CORNER_E)) ? SLOPE_SE : SLOPE_NW);
			break;

		case FOUNDATION_STEEP_LOWER:
			*s = SlopeWithOneCornerRaised(highest_corner);
			break;

		case FOUNDATION_STEEP_BOTH:
			*s = HalftileSlope(SlopeWithOneCornerRaised(highest_corner), highest_corner);
			break;

		default: NOT_REACHED();
	}
	return dz;
}


/**
 * Determines height at given coordinate of a slope
 * @param x x coordinate
 * @param y y coordinate
 * @param corners slope to examine
 * @return height of given point of given slope
 */
uint GetPartialZ(int x, int y, Slope corners)
{
	if (IsHalftileSlope(corners)) {
		switch (GetHalftileSlopeCorner(corners)) {
			case CORNER_W:
				if (x - y >= 0) return GetSlopeMaxZ(corners);
				break;

			case CORNER_S:
				if (x - (y ^ 0xF) >= 0) return GetSlopeMaxZ(corners);
				break;

			case CORNER_E:
				if (y - x >= 0) return GetSlopeMaxZ(corners);
				break;

			case CORNER_N:
				if ((y ^ 0xF) - x >= 0) return GetSlopeMaxZ(corners);
				break;

			default: NOT_REACHED();
		}
	}

	int z = 0;

	switch (RemoveHalftileSlope(corners)) {
		case SLOPE_W:
			if (x - y >= 0) {
				z = (x - y) >> 1;
			}
			break;

		case SLOPE_S:
			y ^= 0xF;
			if ((x - y) >= 0) {
				z = (x - y) >> 1;
			}
			break;

		case SLOPE_SW:
			z = (x >> 1) + 1;
			break;

		case SLOPE_E:
			if (y - x >= 0) {
				z = (y - x) >> 1;
			}
			break;

		case SLOPE_EW:
		case SLOPE_NS:
		case SLOPE_ELEVATED:
			z = 4;
			break;

		case SLOPE_SE:
			z = (y >> 1) + 1;
			break;

		case SLOPE_WSE:
			z = 8;
			y ^= 0xF;
			if (x - y < 0) {
				z += (x - y) >> 1;
			}
			break;

		case SLOPE_N:
			y ^= 0xF;
			if (y - x >= 0) {
				z = (y - x) >> 1;
			}
			break;

		case SLOPE_NW:
			z = (y ^ 0xF) >> 1;
			break;

		case SLOPE_NWS:
			z = 8;
			if (x - y < 0) {
				z += (x - y) >> 1;
			}
			break;

		case SLOPE_NE:
			z = (x ^ 0xF) >> 1;
			break;

		case SLOPE_ENW:
			z = 8;
			y ^= 0xF;
			if (y - x < 0) {
				z += (y - x) >> 1;
			}
			break;

		case SLOPE_SEN:
			z = 8;
			if (y - x < 0) {
				z += (y - x) >> 1;
			}
			break;

		case SLOPE_STEEP_S:
			z = 1 + ((x + y) >> 1);
			break;

		case SLOPE_STEEP_W:
			z = 1 + ((x + (y ^ 0xF)) >> 1);
			break;

		case SLOPE_STEEP_N:
			z = 1 + (((x ^ 0xF) + (y ^ 0xF)) >> 1);
			break;

		case SLOPE_STEEP_E:
			z = 1 + (((x ^ 0xF) + y) >> 1);
			break;

		default: break;
	}

	return z;
}

uint GetSlopeZ(int x, int y)
{
	TileIndex tile = TileVirtXY(x, y);

	return _tile_type_procs[GetTileType(tile)]->get_slope_z_proc(tile, x, y);
}

/**
 * Determine the Z height of a corner relative to TileZ.
 *
 * @pre The slope must not be a halftile slope.
 *
 * @param tileh The slope.
 * @param corner The corner.
 * @return Z position of corner relative to TileZ.
 */
int GetSlopeZInCorner(Slope tileh, Corner corner)
{
	assert(!IsHalftileSlope(tileh));
	return ((tileh & SlopeWithOneCornerRaised(corner)) != 0 ? TILE_HEIGHT : 0) + (tileh == SteepSlope(corner) ? TILE_HEIGHT : 0);
}

/**
 * Determine the Z height of the corners of a specific tile edge
 *
 * @note If a tile has a non-continuous halftile foundation, a corner can have different heights wrt. it's edges.
 *
 * @pre z1 and z2 must be initialized (typ. with TileZ). The corner heights just get added.
 *
 * @param tileh The slope of the tile.
 * @param edge The edge of interest.
 * @param z1 Gets incremented by the height of the first corner of the edge. (near corner wrt. the camera)
 * @param z2 Gets incremented by the height of the second corner of the edge. (far corner wrt. the camera)
 */
void GetSlopeZOnEdge(Slope tileh, DiagDirection edge, int *z1, int *z2)
{
	static const Slope corners[4][4] = {
		/*    corner     |          steep slope
		 *  z1      z2   |       z1             z2        */
		{SLOPE_E, SLOPE_N, SLOPE_STEEP_E, SLOPE_STEEP_N}, // DIAGDIR_NE, z1 = E, z2 = N
		{SLOPE_S, SLOPE_E, SLOPE_STEEP_S, SLOPE_STEEP_E}, // DIAGDIR_SE, z1 = S, z2 = E
		{SLOPE_S, SLOPE_W, SLOPE_STEEP_S, SLOPE_STEEP_W}, // DIAGDIR_SW, z1 = S, z2 = W
		{SLOPE_W, SLOPE_N, SLOPE_STEEP_W, SLOPE_STEEP_N}, // DIAGDIR_NW, z1 = W, z2 = N
	};

	int halftile_test = (IsHalftileSlope(tileh) ? SlopeWithOneCornerRaised(GetHalftileSlopeCorner(tileh)) : 0);
	if (halftile_test == corners[edge][0]) *z2 += TILE_HEIGHT; // The slope is non-continuous in z2. z2 is on the upper side.
	if (halftile_test == corners[edge][1]) *z1 += TILE_HEIGHT; // The slope is non-continuous in z1. z1 is on the upper side.

	if ((tileh & corners[edge][0]) != 0) *z1 += TILE_HEIGHT; // z1 is raised
	if ((tileh & corners[edge][1]) != 0) *z2 += TILE_HEIGHT; // z2 is raised
	if (RemoveHalftileSlope(tileh) == corners[edge][2]) *z1 += TILE_HEIGHT; // z1 is highest corner of a steep slope
	if (RemoveHalftileSlope(tileh) == corners[edge][3]) *z2 += TILE_HEIGHT; // z2 is highest corner of a steep slope
}

/**
 * Get slope of a tile on top of a (possible) foundation
 * If a tile does not have a foundation, the function returns the same as GetTileSlope.
 *
 * @param tile The tile of interest.
 * @param z returns the z of the foundation slope. (Can be NULL, if not needed)
 * @return The slope on top of the foundation.
 */
Slope GetFoundationSlope(TileIndex tile, uint *z)
{
	Slope tileh = GetTileSlope(tile, z);
	Foundation f = _tile_type_procs[GetTileType(tile)]->get_foundation_proc(tile, tileh);
	uint z_inc = ApplyFoundationToSlope(f, &tileh);
	if (z != NULL) *z += z_inc;
	return tileh;
}


static bool HasFoundationNW(TileIndex tile, Slope slope_here, uint z_here)
{
	uint z;

	int z_W_here = z_here;
	int z_N_here = z_here;
	GetSlopeZOnEdge(slope_here, DIAGDIR_NW, &z_W_here, &z_N_here);

	Slope slope = GetFoundationSlope(TILE_ADDXY(tile, 0, -1), &z);
	int z_W = z;
	int z_N = z;
	GetSlopeZOnEdge(slope, DIAGDIR_SE, &z_W, &z_N);

	return (z_N_here > z_N) || (z_W_here > z_W);
}


static bool HasFoundationNE(TileIndex tile, Slope slope_here, uint z_here)
{
	uint z;

	int z_E_here = z_here;
	int z_N_here = z_here;
	GetSlopeZOnEdge(slope_here, DIAGDIR_NE, &z_E_here, &z_N_here);

	Slope slope = GetFoundationSlope(TILE_ADDXY(tile, -1, 0), &z);
	int z_E = z;
	int z_N = z;
	GetSlopeZOnEdge(slope, DIAGDIR_SW, &z_E, &z_N);

	return (z_N_here > z_N) || (z_E_here > z_E);
}

/**
 * Draw foundation \a f at tile \a ti. Updates \a ti.
 * @param ti Tile to draw foundation on
 * @param f  Foundation to draw
 */
void DrawFoundation(TileInfo *ti, Foundation f)
{
	if (!IsFoundation(f)) return;

	/* Two part foundations must be drawn separately */
	assert(f != FOUNDATION_STEEP_BOTH);

	uint sprite_block = 0;
	uint z;
	Slope slope = GetFoundationSlope(ti->tile, &z);

	/* Select the needed block of foundations sprites
	 * Block 0: Walls at NW and NE edge
	 * Block 1: Wall  at        NE edge
	 * Block 2: Wall  at NW        edge
	 * Block 3: No walls at NW or NE edge
	 */
	if (!HasFoundationNW(ti->tile, slope, z)) sprite_block += 1;
	if (!HasFoundationNE(ti->tile, slope, z)) sprite_block += 2;

	/* Use the original slope sprites if NW and NE borders should be visible */
	SpriteID leveled_base = (sprite_block == 0 ? (int)SPR_FOUNDATION_BASE : (SPR_SLOPES_VIRTUAL_BASE + sprite_block * SPR_TRKFOUND_BLOCK_SIZE));
	SpriteID inclined_base = SPR_SLOPES_VIRTUAL_BASE + SPR_SLOPES_INCLINED_OFFSET + sprite_block * SPR_TRKFOUND_BLOCK_SIZE;
	SpriteID halftile_base = SPR_HALFTILE_FOUNDATION_BASE + sprite_block * SPR_HALFTILE_BLOCK_SIZE;

	if (IsSteepSlope(ti->tileh)) {
		if (!IsNonContinuousFoundation(f)) {
			/* Lower part of foundation */
			AddSortableSpriteToDraw(
				leveled_base + (ti->tileh & ~SLOPE_STEEP), PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z
			);
		}

		Corner highest_corner = GetHighestSlopeCorner(ti->tileh);
		ti->z += ApplyFoundationToSlope(f, &ti->tileh);

		if (IsInclinedFoundation(f)) {
			/* inclined foundation */
			byte inclined = highest_corner * 2 + (f == FOUNDATION_INCLINED_Y ? 1 : 0);

			AddSortableSpriteToDraw(inclined_base + inclined, PAL_NONE, ti->x, ti->y,
				f == FOUNDATION_INCLINED_X ? 16 : 1,
				f == FOUNDATION_INCLINED_Y ? 16 : 1,
				TILE_HEIGHT, ti->z
			);
			OffsetGroundSprite(31, 9);
		} else if (IsLeveledFoundation(f)) {
			AddSortableSpriteToDraw(leveled_base + SlopeWithOneCornerRaised(highest_corner), PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z - TILE_HEIGHT);
			OffsetGroundSprite(31, 1);
		} else if (f == FOUNDATION_STEEP_LOWER) {
			/* one corner raised */
			OffsetGroundSprite(31, 1);
		} else {
			/* halftile foundation */
			int x_bb = (((highest_corner == CORNER_W) || (highest_corner == CORNER_S)) ? 8 : 0);
			int y_bb = (((highest_corner == CORNER_S) || (highest_corner == CORNER_E)) ? 8 : 0);

			AddSortableSpriteToDraw(halftile_base + highest_corner, PAL_NONE, ti->x + x_bb, ti->y + y_bb, 8, 8, 7, ti->z + TILE_HEIGHT);
			OffsetGroundSprite(31, 9);
		}
	} else {
		if (IsLeveledFoundation(f)) {
			/* leveled foundation */
			AddSortableSpriteToDraw(leveled_base + ti->tileh, PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z);
			OffsetGroundSprite(31, 1);
		} else if (IsNonContinuousFoundation(f)) {
			/* halftile foundation */
			Corner halftile_corner = GetHalftileFoundationCorner(f);
			int x_bb = (((halftile_corner == CORNER_W) || (halftile_corner == CORNER_S)) ? 8 : 0);
			int y_bb = (((halftile_corner == CORNER_S) || (halftile_corner == CORNER_E)) ? 8 : 0);

			AddSortableSpriteToDraw(halftile_base + halftile_corner, PAL_NONE, ti->x + x_bb, ti->y + y_bb, 8, 8, 7, ti->z);
			OffsetGroundSprite(31, 9);
		} else if (IsSpecialRailFoundation(f)) {
			/* anti-zig-zag foundation */
			SpriteID spr;
			if (ti->tileh == SLOPE_NS || ti->tileh == SLOPE_EW) {
				/* half of leveled foundation under track corner */
				spr = leveled_base + SlopeWithThreeCornersRaised(GetRailFoundationCorner(f));
			} else {
				/* tile-slope = sloped along X/Y, foundation-slope = three corners raised */
				spr = inclined_base + 2 * GetRailFoundationCorner(f) + ((ti->tileh == SLOPE_SW || ti->tileh == SLOPE_NE) ? 1 : 0);
			}
			AddSortableSpriteToDraw(spr, PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z);
			OffsetGroundSprite(31, 9);
		} else {
			/* inclined foundation */
			byte inclined = GetHighestSlopeCorner(ti->tileh) * 2 + (f == FOUNDATION_INCLINED_Y ? 1 : 0);

			AddSortableSpriteToDraw(inclined_base + inclined, PAL_NONE, ti->x, ti->y,
				f == FOUNDATION_INCLINED_X ? 16 : 1,
				f == FOUNDATION_INCLINED_Y ? 16 : 1,
				TILE_HEIGHT, ti->z
			);
			OffsetGroundSprite(31, 9);
		}
		ti->z += ApplyFoundationToSlope(f, &ti->tileh);
	}
}

void DoClearSquare(TileIndex tile)
{
	MakeClear(tile, CLEAR_GRASS, _generating_world ? 3 : 0);
	MarkTileDirtyByTile(tile);
}

/** Returns information about trackdirs and signal states.
 * If there is any trackbit at 'side', return all trackdirbits.
 * For TRANSPORT_ROAD, return no trackbits if there is no roadbit (of given subtype) at given side.
 * @param tile tile to get info about
 * @param mode transport type
 * @param sub_mode for TRANSPORT_ROAD, roadtypes to check
 * @param side side we are entering from, INVALID_DIAGDIR to return all trackbits
 * @return trackdirbits and other info depending on 'mode'
 */
TrackStatus GetTileTrackStatus(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return _tile_type_procs[GetTileType(tile)]->get_tile_track_status_proc(tile, mode, sub_mode, side);
}

/**
 * Change the owner of a tile
 * @param tile      Tile to change
 * @param old_owner Current owner of the tile
 * @param new_owner New owner of the tile
 */
void ChangeTileOwner(TileIndex tile, Owner old_owner, Owner new_owner)
{
	_tile_type_procs[GetTileType(tile)]->change_tile_owner_proc(tile, old_owner, new_owner);
}

void GetAcceptedCargo(TileIndex tile, AcceptedCargo ac)
{
	memset(ac, 0, sizeof(AcceptedCargo));
	_tile_type_procs[GetTileType(tile)]->get_accepted_cargo_proc(tile, ac);
}

void AnimateTile(TileIndex tile)
{
	_tile_type_procs[GetTileType(tile)]->animate_tile_proc(tile);
}

bool ClickTile(TileIndex tile)
{
	return _tile_type_procs[GetTileType(tile)]->click_tile_proc(tile);
}

void GetTileDesc(TileIndex tile, TileDesc *td)
{
	_tile_type_procs[GetTileType(tile)]->get_tile_desc_proc(tile, td);
}

/**
 * Has a snow line table already been loaded.
 * @return true if the table has been loaded already.
 * @ingroup SnowLineGroup
 */
bool IsSnowLineSet(void)
{
	return _snow_line != NULL;
}

/**
 * Set a variable snow line, as loaded from a newgrf file.
 * @param table the 12 * 32 byte table containing the snowline for each day
 * @ingroup SnowLineGroup
 */
void SetSnowLine(byte table[SNOW_LINE_MONTHS][SNOW_LINE_DAYS])
{
	_snow_line = CallocT<SnowLine>(1);
	_snow_line->lowest_value = 0xFF;
	memcpy(_snow_line->table, table, sizeof(_snow_line->table));

	for (uint i = 0; i < SNOW_LINE_MONTHS; i++) {
		for (uint j = 0; j < SNOW_LINE_DAYS; j++) {
			_snow_line->highest_value = max(_snow_line->highest_value, table[i][j]);
			_snow_line->lowest_value = min(_snow_line->lowest_value, table[i][j]);
		}
	}
}

/**
 * Get the current snow line, either variable or static.
 * @return the snow line height.
 * @ingroup SnowLineGroup
 */
byte GetSnowLine(void)
{
	if (_snow_line == NULL) return _settings_game.game_creation.snow_line;

	YearMonthDay ymd;
	ConvertDateToYMD(_date, &ymd);
	return _snow_line->table[ymd.month][ymd.day];
}

/**
 * Get the highest possible snow line height, either variable or static.
 * @return the highest snow line height.
 * @ingroup SnowLineGroup
 */
byte HighestSnowLine(void)
{
	return _snow_line == NULL ? _settings_game.game_creation.snow_line : _snow_line->highest_value;
}

/**
 * Get the lowest possible snow line height, either variable or static.
 * @return the lowest snow line height.
 * @ingroup SnowLineGroup
 */
byte LowestSnowLine(void)
{
	return _snow_line == NULL ? _settings_game.game_creation.snow_line : _snow_line->lowest_value;
}

/**
 * Clear the variable snow line table and free the memory.
 * @ingroup SnowLineGroup
 */
void ClearSnowLine(void)
{
	free(_snow_line);
	_snow_line = NULL;
}

/** Clear a piece of landscape
 * @param tile tile to clear
 * @param flags of operation to conduct
 * @param p1 unused
 * @param p2 unused
 */
CommandCost CmdLandscapeClear(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return _tile_type_procs[GetTileType(tile)]->clear_tile_proc(tile, flags);
}

/** Clear a big piece of landscape
 * @param tile end tile of area dragging
 * @param p1 start tile of area dragging
 * @param flags of operation to conduct
 * @param p2 unused
 */
CommandCost CmdClearArea(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (p1 >= MapSize()) return CMD_ERROR;

	/* make sure sx,sy are smaller than ex,ey */
	int ex = TileX(tile);
	int ey = TileY(tile);
	int sx = TileX(p1);
	int sy = TileY(p1);
	if (ex < sx) Swap(ex, sx);
	if (ey < sy) Swap(ey, sy);

	Money money = GetAvailableMoneyForCommand();
	CommandCost cost(EXPENSES_CONSTRUCTION);
	bool success = false;

	for (int x = sx; x <= ex; ++x) {
		for (int y = sy; y <= ey; ++y) {
			CommandCost ret = DoCommand(TileXY(x, y), 0, 0, flags & ~DC_EXEC, CMD_LANDSCAPE_CLEAR);
			if (CmdFailed(ret)) continue;
			success = true;

			if (flags & DC_EXEC) {
				money -= ret.GetCost();
				if (ret.GetCost() > 0 && money < 0) {
					_additional_cash_required = ret.GetCost();
					return cost;
				}
				DoCommand(TileXY(x, y), 0, 0, flags, CMD_LANDSCAPE_CLEAR);

				/* draw explosion animation... */
				if ((x == sx || x == ex) && (y == sy || y == ey)) {
					/* big explosion in each corner, or small explosion for single tiles */
					CreateEffectVehicleAbove(x * TILE_SIZE + TILE_SIZE / 2, y * TILE_SIZE + TILE_SIZE / 2, 2,
						sy == ey && sx == ex ? EV_EXPLOSION_SMALL : EV_EXPLOSION_LARGE
					);
				}
			}
			cost.AddCost(ret);
		}
	}

	return (success) ? cost : CMD_ERROR;
}


TileIndex _cur_tileloop_tile;
#define TILELOOP_BITS 4
#define TILELOOP_SIZE (1 << TILELOOP_BITS)
#define TILELOOP_ASSERTMASK ((TILELOOP_SIZE - 1) + ((TILELOOP_SIZE - 1) << MapLogX()))
#define TILELOOP_CHKMASK (((1 << (MapLogX() - TILELOOP_BITS))-1) << TILELOOP_BITS)

void RunTileLoop()
{
	TileIndex tile = _cur_tileloop_tile;

	assert((tile & ~TILELOOP_ASSERTMASK) == 0);
	uint count = (MapSizeX() / TILELOOP_SIZE) * (MapSizeY() / TILELOOP_SIZE);
	do {
		_tile_type_procs[GetTileType(tile)]->tile_loop_proc(tile);

		if (TileX(tile) < MapSizeX() - TILELOOP_SIZE) {
			tile += TILELOOP_SIZE; // no overflow
		} else {
			tile = TILE_MASK(tile - TILELOOP_SIZE * (MapSizeX() / TILELOOP_SIZE - 1) + TileDiffXY(0, TILELOOP_SIZE)); // x would overflow, also increase y
		}
	} while (--count != 0);
	assert((tile & ~TILELOOP_ASSERTMASK) == 0);

	tile += 9;
	if (tile & TILELOOP_CHKMASK) {
		tile = (tile + MapSizeX()) & TILELOOP_ASSERTMASK;
	}
	_cur_tileloop_tile = tile;
}

void InitializeLandscape()
{
	uint maxx = MapMaxX();
	uint maxy = MapMaxY();
	uint sizex = MapSizeX();

	uint y;
	for (y = _settings_game.construction.freeform_edges ? 1 : 0; y < maxy; y++) {
		uint x;
		for (x = _settings_game.construction.freeform_edges ? 1 : 0; x < maxx; x++) {
			MakeClear(sizex * y + x, CLEAR_GRASS, 3);
			SetTileHeight(sizex * y + x, 0);
			SetTropicZone(sizex * y + x, TROPICZONE_NORMAL);
			ClearBridgeMiddle(sizex * y + x);
		}
		MakeVoid(sizex * y + x);
	}
	for (uint x = 0; x < sizex; x++) MakeVoid(sizex * y + x);
}

static const byte _genterrain_tbl_1[5] = { 10, 22, 33, 37, 4  };
static const byte _genterrain_tbl_2[5] = {  0,  0,  0,  0, 33 };

static void GenerateTerrain(int type, uint flag)
{
	uint32 r = Random();

	const Sprite *templ = GetSprite((((r >> 24) * _genterrain_tbl_1[type]) >> 8) + _genterrain_tbl_2[type] + 4845, ST_MAPGEN);

	uint x = r & MapMaxX();
	uint y = (r >> MapLogX()) & MapMaxY();

	if (x < 2 || y < 2) return;

	DiagDirection direction = (DiagDirection)GB(r, 22, 2);
	uint w = templ->width;
	uint h = templ->height;

	if (DiagDirToAxis(direction) == AXIS_Y) Swap(w, h);

	const byte *p = templ->data;

	if ((flag & 4) != 0) {
		uint xw = x * MapSizeY();
		uint yw = y * MapSizeX();
		uint bias = (MapSizeX() + MapSizeY()) * 16;

		switch (flag & 3) {
			default: NOT_REACHED();
			case 0:
				if (xw + yw > MapSize() - bias) return;
				break;

			case 1:
				if (yw < xw + bias) return;
				break;

			case 2:
				if (xw + yw < MapSize() + bias) return;
				break;

			case 3:
				if (xw < yw + bias) return;
				break;
		}
	}

	if (x + w >= MapMaxX() - 1) return;
	if (y + h >= MapMaxY() - 1) return;

	Tile *tile = &_m[TileXY(x, y)];

	switch (direction) {
		default: NOT_REACHED();
		case DIAGDIR_NE:
			do {
				Tile *tile_cur = tile;

				for (uint w_cur = w; w_cur != 0; --w_cur) {
					if (GB(*p, 0, 4) >= tile_cur->type_height) tile_cur->type_height = GB(*p, 0, 4);
					p++;
					tile_cur++;
				}
				tile += TileDiffXY(0, 1);
			} while (--h != 0);
			break;

		case DIAGDIR_SE:
			do {
				Tile *tile_cur = tile;

				for (uint h_cur = h; h_cur != 0; --h_cur) {
					if (GB(*p, 0, 4) >= tile_cur->type_height) tile_cur->type_height = GB(*p, 0, 4);
					p++;
					tile_cur += TileDiffXY(0, 1);
				}
				tile += TileDiffXY(1, 0);
			} while (--w != 0);
			break;

		case DIAGDIR_SW:
			tile += TileDiffXY(w - 1, 0);
			do {
				Tile *tile_cur = tile;

				for (uint w_cur = w; w_cur != 0; --w_cur) {
					if (GB(*p, 0, 4) >= tile_cur->type_height) tile_cur->type_height = GB(*p, 0, 4);
					p++;
					tile_cur--;
				}
				tile += TileDiffXY(0, 1);
			} while (--h != 0);
			break;

		case DIAGDIR_NW:
			tile += TileDiffXY(0, h - 1);
			do {
				Tile *tile_cur = tile;

				for (uint h_cur = h; h_cur != 0; --h_cur) {
					if (GB(*p, 0, 4) >= tile_cur->type_height) tile_cur->type_height = GB(*p, 0, 4);
					p++;
					tile_cur -= TileDiffXY(0, 1);
				}
				tile += TileDiffXY(1, 0);
			} while (--w != 0);
			break;
	}
}


#include "table/genland.h"

static void CreateDesertOrRainForest()
{
	TileIndex update_freq = MapSize() / 4;
	const TileIndexDiffC *data;

	for (TileIndex tile = 0; tile != MapSize(); ++tile) {
		if ((tile % update_freq) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		for (data = _make_desert_or_rainforest_data;
				data != endof(_make_desert_or_rainforest_data); ++data) {
			TileIndex t = AddTileIndexDiffCWrap(tile, *data);
			if (t != INVALID_TILE && (TileHeight(t) >= 4 || IsTileType(t, MP_WATER))) break;
		}
		if (data == endof(_make_desert_or_rainforest_data))
			SetTropicZone(tile, TROPICZONE_DESERT);
	}

	for (uint i = 0; i != 256; i++) {
		if ((i % 64) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		RunTileLoop();
	}

	for (TileIndex tile = 0; tile != MapSize(); ++tile) {
		if ((tile % update_freq) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		for (data = _make_desert_or_rainforest_data;
				data != endof(_make_desert_or_rainforest_data); ++data) {
			TileIndex t = AddTileIndexDiffCWrap(tile, *data);
			if (t != INVALID_TILE && IsTileType(t, MP_CLEAR) && IsClearGround(t, CLEAR_DESERT)) break;
		}
		if (data == endof(_make_desert_or_rainforest_data))
			SetTropicZone(tile, TROPICZONE_RAINFOREST);
	}
}

void GenerateLandscape(byte mode)
{
	static const int gwp_desert_amount = 4 + 8;

	if (mode == GW_HEIGHTMAP) {
		SetGeneratingWorldProgress(GWP_LANDSCAPE, (_settings_game.game_creation.landscape == LT_TROPIC) ? 1 + gwp_desert_amount : 1);
		LoadHeightmap(_file_to_saveload.name);
		IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);
	} else if (_settings_game.game_creation.land_generator == LG_TERRAGENESIS) {
		SetGeneratingWorldProgress(GWP_LANDSCAPE, (_settings_game.game_creation.landscape == LT_TROPIC) ? 3 + gwp_desert_amount : 3);
		GenerateTerrainPerlin();
	} else {
		if (_settings_game.construction.freeform_edges) {
			for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, 0));
			for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(0, y));
		}
		switch (_settings_game.game_creation.landscape) {
			case LT_ARCTIC: {
				SetGeneratingWorldProgress(GWP_LANDSCAPE, 2);

				uint32 r = Random();

				for (uint i = ScaleByMapSize(GB(r, 0, 7) + 950); i != 0; --i) {
					GenerateTerrain(2, 0);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

				uint flag = GB(r, 7, 2) | 4;
				for (uint i = ScaleByMapSize(GB(r, 9, 7) + 450); i != 0; --i) {
					GenerateTerrain(4, flag);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);
			} break;

			case LT_TROPIC: {
				SetGeneratingWorldProgress(GWP_LANDSCAPE, 3 + gwp_desert_amount);

				uint32 r = Random();

				for (uint i = ScaleByMapSize(GB(r, 0, 7) + 170); i != 0; --i) {
					GenerateTerrain(0, 0);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

				uint flag = GB(r, 7, 2) | 4;
				for (uint i = ScaleByMapSize(GB(r, 9, 8) + 1700); i != 0; --i) {
					GenerateTerrain(0, flag);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

				flag ^= 2;

				for (uint i = ScaleByMapSize(GB(r, 17, 7) + 410); i != 0; --i) {
					GenerateTerrain(3, flag);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);
			} break;

			default: {
				SetGeneratingWorldProgress(GWP_LANDSCAPE, 1);

				uint32 r = Random();

				uint i = ScaleByMapSize(GB(r, 0, 7) + (3 - _settings_game.difficulty.quantity_sea_lakes) * 256 + 100);
				for (; i != 0; --i) {
					GenerateTerrain(_settings_game.difficulty.terrain_type, 0);
				}
				IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);
			} break;
		}
	}

	FixSlopes();
	ConvertGroundTilesIntoWaterTiles();

	if (_settings_game.game_creation.landscape == LT_TROPIC) CreateDesertOrRainForest();
}

void OnTick_Town();
void OnTick_Trees();
void OnTick_Station();
void OnTick_Industry();

void OnTick_Companies();
void OnTick_Train();

void CallLandscapeTick()
{
	OnTick_Town();
	OnTick_Trees();
	OnTick_Station();
	OnTick_Industry();

	OnTick_Companies();
	OnTick_Train();
}
