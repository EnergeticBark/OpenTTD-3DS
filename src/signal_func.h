/* $Id$ */

/** @file signal_func.h Functions related to signals. */

#ifndef SIGNAL_FUNC_H
#define SIGNAL_FUNC_H

#include "track_type.h"
#include "tile_type.h"
#include "direction_type.h"
#include "company_type.h"

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction along with the trackdir.
 */
static inline byte SignalAlongTrackdir(Trackdir trackdir)
{
	extern const byte _signal_along_trackdir[TRACKDIR_END];
	return _signal_along_trackdir[trackdir];
}

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction against the trackdir.
 */
static inline byte SignalAgainstTrackdir(Trackdir trackdir)
{
	extern const byte _signal_against_trackdir[TRACKDIR_END];
	return _signal_against_trackdir[trackdir];
}

/**
 * Maps a Track to the bits that store the status of the two signals that can
 * be present on the given track.
 */
static inline byte SignalOnTrack(Track track)
{
	extern const byte _signal_on_track[TRACK_END];
	return _signal_on_track[track];
}

/** State of the signal segment */
enum SigSegState {
	SIGSEG_FREE,    ///< Free and has no pre-signal exits or at least one green exit
	SIGSEG_FULL,    ///< Occupied by a train
	SIGSEG_PBS,     ///< Segment is a PBS segment
};

SigSegState UpdateSignalsOnSegment(TileIndex tile, DiagDirection side, Owner owner);
void SetSignalsOnBothDir(TileIndex tile, Track track, Owner owner);
void AddTrackToSignalBuffer(TileIndex tile, Track track, Owner owner);
void AddSideToSignalBuffer(TileIndex tile, DiagDirection side, Owner owner);
void UpdateSignalsInBuffer();

#endif /* SIGNAL_FUNC_H */
