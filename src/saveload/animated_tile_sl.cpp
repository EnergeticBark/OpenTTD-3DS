/* $Id$ */

/** @file animated_tile_sl.cpp Code handling saving and loading of animated tiles */

#include "../stdafx.h"
#include "../tile_type.h"
#include "../core/alloc_func.hpp"

#include "saveload.h"

extern TileIndex *_animated_tile_list;
extern uint _animated_tile_count;
extern uint _animated_tile_allocated;

/**
 * Save the ANIT chunk.
 */
static void Save_ANIT()
{
	SlSetLength(_animated_tile_count * sizeof(*_animated_tile_list));
	SlArray(_animated_tile_list, _animated_tile_count, SLE_UINT32);
}

/**
 * Load the ANIT chunk; the chunk containing the animated tiles.
 */
static void Load_ANIT()
{
	/* Before version 80 we did NOT have a variable length animated tile table */
	if (CheckSavegameVersion(80)) {
		/* In pre version 6, we has 16bit per tile, now we have 32bit per tile, convert it ;) */
		SlArray(_animated_tile_list, 256, CheckSavegameVersion(6) ? (SLE_FILE_U16 | SLE_VAR_U32) : SLE_UINT32);

		for (_animated_tile_count = 0; _animated_tile_count < 256; _animated_tile_count++) {
			if (_animated_tile_list[_animated_tile_count] == 0) break;
		}
		return;
	}

	_animated_tile_count = (uint)SlGetFieldLength() / sizeof(*_animated_tile_list);

	/* Determine a nice rounded size for the amount of allocated tiles */
	_animated_tile_allocated = 256;
	while (_animated_tile_allocated < _animated_tile_count) _animated_tile_allocated *= 2;

	_animated_tile_list = ReallocT<TileIndex>(_animated_tile_list, _animated_tile_allocated);
	SlArray(_animated_tile_list, _animated_tile_count, SLE_UINT32);
}

/**
 * "Definition" imported by the saveload code to be able to load and save
 * the animated tile table.
 */
extern const ChunkHandler _animated_tile_chunk_handlers[] = {
	{ 'ANIT', Save_ANIT, Load_ANIT, CH_RIFF | CH_LAST},
};
