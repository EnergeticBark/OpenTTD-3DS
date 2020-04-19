/* $Id$ */

/** @file pathfind.h The oldest pathfinder that's supported. */

#ifndef PATHFIND_H
#define PATHFIND_H

#include "direction_type.h"

enum {
	STR_FACTOR  = 2,
	DIAG_FACTOR = 3
};

//#define PF_BENCH // perform simple benchmarks on the train pathfinder (not
//supported on all archs)

struct TrackPathFinder;
typedef bool TPFEnumProc(TileIndex tile, void *data, Trackdir trackdir, uint length);
typedef void TPFAfterProc(TrackPathFinder *tpf);

typedef bool NTPEnumProc(TileIndex tile, void *data, int track, uint length);

#define PATHFIND_GET_LINK_OFFS(tpf, link) ((byte*)(link) - (byte*)tpf->links)
#define PATHFIND_GET_LINK_PTR(tpf, link_offs) (TrackPathFinderLink*)((byte*)tpf->links + (link_offs))

/* y7 y6 y5 y4 y3 y2 y1 y0 x7 x6 x5 x4 x3 x2 x1 x0
 * y7 y6 y5 y4 y3 y2 y1 y0 x4 x3 x2 x1 x0  0  0  0
 *  0  0 y7 y6 y5 y4 y3 y2 y1 y0 x4 x3 x2 x1 x0  0
 *  0  0  0  0 y5 y4 y3 y2 y1 y0 x4 x3 x2 x1 x0  0
 */
#define PATHFIND_HASH_TILE(tile) (TileX(tile) & 0x1F) + ((TileY(tile) & 0x1F) << 5)

struct TrackPathFinderLink {
	TileIndex tile;
	uint16 flags;
	uint16 next;
};

struct RememberData {
	uint16 cur_length;
	byte depth;
	Track last_choosen_track;
};

struct TrackPathFinder {
	int num_links_left;
	TrackPathFinderLink *new_link;

	TPFEnumProc *enum_proc;

	void *userdata;

	RememberData rd;

	TrackdirByte the_dir;

	TransportType tracktype;
	uint sub_type;

	bool disable_tile_hash;

	uint16 hash_head[0x400];
	TileIndex hash_tile[0x400];       ///< stores the link index when multi link.

	TrackPathFinderLink links[0x400]; ///< hopefully, this is enough.
};

/** Some flags to modify the behaviour of original pathfinder */
enum PathfindFlags {
	PATHFIND_FLAGS_NONE              = 0,
	PATHFIND_FLAGS_SHIP_MODE         = 0x0800, ///< pathfinder with some optimizations for ships, but does not work for other types.
	PATHFIND_FLAGS_DISABLE_TILE_HASH = 0x1000, ///< do not check for searching in circles
};
DECLARE_ENUM_AS_BIT_SET(PathfindFlags)

void FollowTrack(TileIndex tile, PathfindFlags flags, TransportType tt, uint sub_type, DiagDirection direction, TPFEnumProc *enum_proc, TPFAfterProc *after_proc, void *data);
void NewTrainPathfind(TileIndex tile, TileIndex dest, RailTypes railtypes, DiagDirection direction, NTPEnumProc *enum_proc, void *data);


/**
 * Calculates the tile of given station that is closest to a given tile
 * for this we assume the station is a rectangle,
 * as defined by its top tile (st->train_tile) and its width/height (st->trainst_w, st->trainst_h)
 * @param station The station to calculate the distance to
 * @param tile The tile from where to calculate the distance
 * @return The closest station tile to the given tile.
 */
static inline TileIndex CalcClosestStationTile(StationID station, TileIndex tile)
{
	const Station *st = GetStation(station);

	/* If the rail station is (temporarily) not present, use the station sign to drive near the station */
	if (st->train_tile == INVALID_TILE) return st->xy;

	uint minx = TileX(st->train_tile);  // topmost corner of station
	uint miny = TileY(st->train_tile);
	uint maxx = minx + st->trainst_w - 1; // lowermost corner of station
	uint maxy = miny + st->trainst_h - 1;

	/* we are going the aim for the x coordinate of the closest corner
	 * but if we are between those coordinates, we will aim for our own x coordinate */
	uint x = ClampU(TileX(tile), minx, maxx);

	/* same for y coordinate, see above comment */
	uint y = ClampU(TileY(tile), miny, maxy);

	/* return the tile of our target coordinates */
	return TileXY(x, y);
}

#endif /* PATHFIND_H */
