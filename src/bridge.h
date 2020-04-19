/* $Id$ */

/** @file bridge.h Header file for bridges */

#ifndef BRIDGE_H
#define BRIDGE_H

#include "gfx_type.h"
#include "direction_type.h"
#include "tile_cmd.h"

/** This enum is related to the definition of bridge pieces,
 * which is used to determine the proper sprite table to use
 * while drawing a given bridge part.
 */
enum BridgePieces {
	BRIDGE_PIECE_NORTH = 0,
	BRIDGE_PIECE_SOUTH,
	BRIDGE_PIECE_INNER_NORTH,
	BRIDGE_PIECE_INNER_SOUTH,
	BRIDGE_PIECE_MIDDLE_ODD,
	BRIDGE_PIECE_MIDDLE_EVEN,
	BRIDGE_PIECE_HEAD,
	BRIDGE_PIECE_INVALID,
};

DECLARE_POSTFIX_INCREMENT(BridgePieces);

enum {
	MAX_BRIDGES = 13
};

typedef uint BridgeType;

/** Struct containing information about a single bridge type
 */
struct BridgeSpec {
	Year avail_year;             ///< the year where it becomes available
	byte min_length;             ///< the minimum length (not counting start and end tile)
	byte max_length;             ///< the maximum length (not counting start and end tile)
	uint16 price;                ///< the price multiplier
	uint16 speed;                ///< maximum travel speed
	SpriteID sprite;             ///< the sprite which is used in the GUI
	SpriteID pal;                ///< the palette which is used in the GUI
	StringID material;           ///< the string that contains the bridge description
	StringID transport_name[2];  ///< description of the bridge, when built for road or rail
	PalSpriteID **sprite_table;  ///< table of sprites for drawing the bridge
	byte flags;                  ///< bit 0 set: disable drawing of far pillars.
};

extern BridgeSpec _bridge[MAX_BRIDGES];

Foundation GetBridgeFoundation(Slope tileh, Axis axis);
bool HasBridgeFlatRamp(Slope tileh, Axis axis);

static inline const BridgeSpec *GetBridgeSpec(BridgeType i)
{
	assert(i < lengthof(_bridge));
	return &_bridge[i];
}

void DrawBridgeMiddle(const TileInfo *ti);

bool CheckBridge_Stuff(BridgeType bridge_type, uint bridge_len, DoCommandFlag flags = DC_NONE);
int CalcBridgeLenCostFactor(int x);

void ResetBridges();

#endif /* BRIDGE_H */
