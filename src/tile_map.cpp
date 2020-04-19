/* $Id$ */

/** @file tile_map.cpp Global tile accessors. */

#include "stdafx.h"
#include "tile_map.h"
#include "core/math_func.hpp"
#include "settings_type.h"

/**
 * Return the slope of a given tile
 * @param tile Tile to compute slope of
 * @param h    If not \c NULL, pointer to storage of z height
 * @return Slope of the tile, except for the HALFTILE part */
Slope GetTileSlope(TileIndex tile, uint *h)
{
	assert(tile < MapSize());

	if (TileX(tile) == MapMaxX() || TileY(tile) == MapMaxY() ||
			(_settings_game.construction.freeform_edges && (TileX(tile) == 0 || TileY(tile) == 0))) {
		if (h != NULL) *h = TileHeight(tile) * TILE_HEIGHT;
		return SLOPE_FLAT;
	}

	uint a = TileHeight(tile); // Height of the N corner
	uint min = a; // Minimal height of all corners examined so far
	uint b = TileHeight(tile + TileDiffXY(1, 0)); // Height of the W corner
	if (min > b) min = b;
	uint c = TileHeight(tile + TileDiffXY(0, 1)); // Height of the E corner
	if (min > c) min = c;
	uint d = TileHeight(tile + TileDiffXY(1, 1)); // Height of the S corner
	if (min > d) min = d;

	/* Due to the fact that tiles must connect with each other without leaving gaps, the
	 * biggest difference in height between any corner and 'min' is between 0, 1, or 2.
	 *
	 * Also, there is at most 1 corner with height difference of 2.
	 */

	uint r = SLOPE_FLAT; // Computed slope of the tile

	/* For each corner if not equal to minimum height:
	 *  - set the SLOPE_STEEP flag if the difference is 2
	 *  - add the corresponding SLOPE_X constant to the computed slope
	 */
	if ((a -= min) != 0) r += (--a << 4) + SLOPE_N;
	if ((c -= min) != 0) r += (--c << 4) + SLOPE_E;
	if ((d -= min) != 0) r += (--d << 4) + SLOPE_S;
	if ((b -= min) != 0) r += (--b << 4) + SLOPE_W;

	if (h != NULL) *h = min * TILE_HEIGHT;

	return (Slope)r;
}

/**
 * Get bottom height of the tile
 * @param tile Tile to compute height of
 * @return Minimum height of the tile */
uint GetTileZ(TileIndex tile)
{
	if (TileX(tile) == MapMaxX() || TileY(tile) == MapMaxY()) return 0;

	uint h = TileHeight(tile); // N corner
	h = min(h, TileHeight(tile + TileDiffXY(1, 0))); // W corner
	h = min(h, TileHeight(tile + TileDiffXY(0, 1))); // E corner
	h = min(h, TileHeight(tile + TileDiffXY(1, 1))); // S corner

	return h * TILE_HEIGHT;
}

/**
 * Get top height of the tile
 * @param tile Tile to compute height of
 * @return Maximum height of the tile */
uint GetTileMaxZ(TileIndex t)
{
	if (TileX(t) == MapMaxX() || TileY(t) == MapMaxY()) return 0;

	uint h = TileHeight(t); // N corner
	h = max(h, TileHeight(t + TileDiffXY(1, 0))); // W corner
	h = max(h, TileHeight(t + TileDiffXY(0, 1))); // E corner
	h = max(h, TileHeight(t + TileDiffXY(1, 1))); // S corner

	return h * TILE_HEIGHT;
}
