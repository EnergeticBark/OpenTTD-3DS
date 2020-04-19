/* $Id$ */

/** @file sprite.h Base for drawing complex sprites. */

#ifndef SPRITE_H
#define SPRITE_H

#include "gfx_type.h"

#define GENERAL_SPRITE_COLOUR(colour) ((colour) + PALETTE_RECOLOUR_START)
#define COMPANY_SPRITE_COLOUR(owner) (GENERAL_SPRITE_COLOUR(_company_colours[owner]))

/**
 * Whether a sprite comes from the original graphics files or a new grf file
 * (either supplied by OpenTTD or supplied by the user).
 *
 * @param sprite The sprite to check
 * @return True if it is a new sprite, or false if it is original.
 */
#define IS_CUSTOM_SPRITE(sprite) ((sprite) >= SPR_SIGNALS_BASE)

/* The following describes bunch of sprites to be drawn together in a single 3D
 * bounding box. Used especially for various multi-sprite buildings (like
 * depots or stations): */

/** A tile child sprite and palette to draw for stations etc, with 3D bounding box */
struct DrawTileSeqStruct {
	int8 delta_x; ///< \c 0x80 is sequence terminator
	int8 delta_y;
	int8 delta_z;
	byte size_x;
	byte size_y;
	byte size_z;
	PalSpriteID image;
};

/** Ground palette sprite of a tile, together with its child sprites */
struct DrawTileSprites {
	PalSpriteID ground;           ///< Palette and sprite for the ground
	const DrawTileSeqStruct *seq; ///< Array of child sprites. Terminated with a terminator entry
};

/**
 * This structure is the same for both Industries and Houses.
 * Buildings here reference a general type of construction
 */
struct DrawBuildingsTileStruct {
	PalSpriteID ground;
	PalSpriteID building;
	byte subtile_x;
	byte subtile_y;
	byte width;
	byte height;
	byte dz;
	byte draw_proc;  // this allows to specify a special drawing procedure.
};

/** Iterate through all DrawTileSeqStructs in DrawTileSprites. */
#define foreach_draw_tile_seq(idx, list) for (idx = list; ((byte) idx->delta_x) != 0x80; idx++)

bool SkipSpriteData(byte type, uint16 num);

#endif /* SPRITE_H */
