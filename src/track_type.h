/* $Id$ */

/** @file track_type.h All types related to tracks */

#ifndef TRACK_TYPE_H
#define TRACK_TYPE_H

#include "core/enum_type.hpp"

/**
 * These are used to specify a single track.
 * Can be translated to a trackbit with TrackToTrackbit
 */
enum Track {
	TRACK_BEGIN = 0,        ///< Used for iterations
	TRACK_X     = 0,        ///< Track along the x-axis (north-east to south-west)
	TRACK_Y     = 1,        ///< Track along the y-axis (north-west to south-east)
	TRACK_UPPER = 2,        ///< Track in the upper corner of the tile (north)
	TRACK_LOWER = 3,        ///< Track in the lower corner of the tile (south)
	TRACK_LEFT  = 4,        ///< Track in the left corner of the tile (west)
	TRACK_RIGHT = 5,        ///< Track in the right corner of the tile (east)
	TRACK_END,              ///< Used for iterations
	INVALID_TRACK = 0xFF    ///< Flag for an invalid track
};

/** Allow incrementing of Track variables */
DECLARE_POSTFIX_INCREMENT(Track);
/** Define basic enum properties */
template <> struct EnumPropsT<Track> : MakeEnumPropsT<Track, byte, TRACK_BEGIN, TRACK_END, INVALID_TRACK> {};
typedef TinyEnumT<Track> TrackByte;


/** Bitfield corresponding to Track */
enum TrackBits {
	TRACK_BIT_NONE    = 0U,                                                 ///< No track
	TRACK_BIT_X       = 1U << TRACK_X,                                      ///< X-axis track
	TRACK_BIT_Y       = 1U << TRACK_Y,                                      ///< Y-axis track
	TRACK_BIT_UPPER   = 1U << TRACK_UPPER,                                  ///< Upper track
	TRACK_BIT_LOWER   = 1U << TRACK_LOWER,                                  ///< Lower track
	TRACK_BIT_LEFT    = 1U << TRACK_LEFT,                                   ///< Left track
	TRACK_BIT_RIGHT   = 1U << TRACK_RIGHT,                                  ///< Right track
	TRACK_BIT_CROSS   = TRACK_BIT_X     | TRACK_BIT_Y,                      ///< X-Y-axis cross
	TRACK_BIT_HORZ    = TRACK_BIT_UPPER | TRACK_BIT_LOWER,                  ///< Upper and lower track
	TRACK_BIT_VERT    = TRACK_BIT_LEFT  | TRACK_BIT_RIGHT,                  ///< Left and right track
	TRACK_BIT_3WAY_NE = TRACK_BIT_X     | TRACK_BIT_UPPER | TRACK_BIT_RIGHT,///< "Arrow" to the north-east
	TRACK_BIT_3WAY_SE = TRACK_BIT_Y     | TRACK_BIT_LOWER | TRACK_BIT_RIGHT,///< "Arrow" to the south-east
	TRACK_BIT_3WAY_SW = TRACK_BIT_X     | TRACK_BIT_LOWER | TRACK_BIT_LEFT, ///< "Arrow" to the south-west
	TRACK_BIT_3WAY_NW = TRACK_BIT_Y     | TRACK_BIT_UPPER | TRACK_BIT_LEFT, ///< "Arrow" to the north-west
	TRACK_BIT_ALL     = TRACK_BIT_CROSS | TRACK_BIT_HORZ  | TRACK_BIT_VERT, ///< All possible tracks
	TRACK_BIT_MASK    = 0x3FU,                                              ///< Bitmask for the first 6 bits
	TRACK_BIT_WORMHOLE = 0x40U,                                             ///< Bitflag for a wormhole (used for tunnels)
	TRACK_BIT_DEPOT   = 0x80U,                                              ///< Bitflag for a depot
	INVALID_TRACK_BIT = 0xFF                                                ///< Flag for an invalid trackbits value
};

/** Define basic enum properties */
template <> struct EnumPropsT<TrackBits> : MakeEnumPropsT<TrackBits, byte, TRACK_BIT_NONE, TRACK_BIT_ALL, INVALID_TRACK_BIT> {};
typedef TinyEnumT<TrackBits> TrackBitsByte;

DECLARE_ENUM_AS_BIT_SET(TrackBits);

/**
 * Enumeration for tracks and directions.
 *
 * These are a combination of tracks and directions. Values are 0-5 in one
 * direction (corresponding to the Track enum) and 8-13 in the other direction.
 * 6, 7, 14 and 15 are used to encode the reversing of road vehicles. Those
 * reversing track dirs are not considered to be 'valid' except in a small
 * corner in the road vehicle controller.
 */
enum Trackdir {
	TRACKDIR_BEGIN    =  0,         ///< Used for iterations
	TRACKDIR_X_NE     =  0,         ///< X-axis and direction to north-east
	TRACKDIR_Y_SE     =  1,         ///< Y-axis and direction to south-east
	TRACKDIR_UPPER_E  =  2,         ///< Upper track and direction to east
	TRACKDIR_LOWER_E  =  3,         ///< Lower track and direction to east
	TRACKDIR_LEFT_S   =  4,         ///< Left track and direction to south
	TRACKDIR_RIGHT_S  =  5,         ///< Right track and direction to south
	TRACKDIR_RVREV_NE =  6,         ///< (Road vehicle) reverse direction north-east
	TRACKDIR_RVREV_SE =  7,         ///< (Road vehicle) reverse direction south-east
	TRACKDIR_X_SW     =  8,         ///< X-axis and direction to south-west
	TRACKDIR_Y_NW     =  9,         ///< Y-axis and direction to north-west
	TRACKDIR_UPPER_W  = 10,         ///< Upper track and direction to west
	TRACKDIR_LOWER_W  = 11,         ///< Lower track and direction to west
	TRACKDIR_LEFT_N   = 12,         ///< Left track and direction to north
	TRACKDIR_RIGHT_N  = 13,         ///< Right track and direction to north
	TRACKDIR_RVREV_SW = 14,         ///< (Road vehicle) reverse direction south-west
	TRACKDIR_RVREV_NW = 15,         ///< (Road vehicle) reverse direction north-west
	TRACKDIR_END,                   ///< Used for iterations
	INVALID_TRACKDIR  = 0xFF,       ///< Flag for an invalid trackdir
};

/** Define basic enum properties */
template <> struct EnumPropsT<Trackdir> : MakeEnumPropsT<Trackdir, byte, TRACKDIR_BEGIN, TRACKDIR_END, INVALID_TRACKDIR> {};
typedef TinyEnumT<Trackdir> TrackdirByte;

/**
 * Enumeration of bitmasks for the TrackDirs
 *
 * These are a combination of tracks and directions. Values are 0-5 in one
 * direction (corresponding to the Track enum) and 8-13 in the other direction.
 */
enum TrackdirBits {
	TRACKDIR_BIT_NONE     = 0x0000, ///< No track build
	TRACKDIR_BIT_X_NE     = 0x0001, ///< Track x-axis, direction north-east
	TRACKDIR_BIT_Y_SE     = 0x0002, ///< Track y-axis, direction south-east
	TRACKDIR_BIT_UPPER_E  = 0x0004, ///< Track upper, direction east
	TRACKDIR_BIT_LOWER_E  = 0x0008, ///< Track lower, direction east
	TRACKDIR_BIT_LEFT_S   = 0x0010, ///< Track left, direction south
	TRACKDIR_BIT_RIGHT_S  = 0x0020, ///< Track right, direction south
	/* Again, note the two missing values here. This enables trackdir -> track conversion by doing (trackdir & 0xFF) */
	TRACKDIR_BIT_X_SW     = 0x0100, ///< Track x-axis, direction south-west
	TRACKDIR_BIT_Y_NW     = 0x0200, ///< Track y-axis, direction north-west
	TRACKDIR_BIT_UPPER_W  = 0x0400, ///< Track upper, direction west
	TRACKDIR_BIT_LOWER_W  = 0x0800, ///< Track lower, direction west
	TRACKDIR_BIT_LEFT_N   = 0x1000, ///< Track left, direction north
	TRACKDIR_BIT_RIGHT_N  = 0x2000, ///< Track right, direction north
	TRACKDIR_BIT_MASK     = 0x3F3F, ///< Bitmask for bit-operations
	INVALID_TRACKDIR_BIT  = 0xFFFF, ///< Flag for an invalid trackdirbit value
};

/** Define basic enum properties */
template <> struct EnumPropsT<TrackdirBits> : MakeEnumPropsT<TrackdirBits, uint16, TRACKDIR_BIT_NONE, TRACKDIR_BIT_MASK, INVALID_TRACKDIR_BIT> {};
typedef TinyEnumT<TrackdirBits> TrackdirBitsShort;
DECLARE_ENUM_AS_BIT_SET(TrackdirBits);

typedef uint32 TrackStatus;

#endif /* TRACK_TYPE_H */
