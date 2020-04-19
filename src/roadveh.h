/* $Id$ */

/** @file src/roadveh.h Road vehicle states */

#ifndef ROADVEH_H
#define ROADVEH_H

#include "vehicle_base.h"
#include "engine_func.h"
#include "engine_base.h"
#include "economy_func.h"

/** State information about the Road Vehicle controller */
enum {
	RDE_NEXT_TILE = 0x80, ///< We should enter the next tile
	RDE_TURNED    = 0x40, ///< We just finished turning

	/* Start frames for when a vehicle enters a tile/changes its state.
	 * The start frame is different for vehicles that turned around or
	 * are leaving the depot as the do not start at the edge of the tile.
	 * For trams there are a few different start frames as there are two
	 * places where trams can turn. */
	RVC_DEFAULT_START_FRAME                =  0,
	RVC_TURN_AROUND_START_FRAME            =  1,
	RVC_DEPOT_START_FRAME                  =  6,
	RVC_START_FRAME_AFTER_LONG_TRAM        = 21,
	RVC_TURN_AROUND_START_FRAME_SHORT_TRAM = 16,
	/* Stop frame for a vehicle in a drive-through stop */
	RVC_DRIVE_THROUGH_STOP_FRAME           =  7,
	RVC_DEPOT_STOP_FRAME                   = 11,
};

enum RoadVehicleSubType {
	RVST_FRONT,
	RVST_ARTIC_PART,
};

static inline bool IsRoadVehFront(const Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	return v->subtype == RVST_FRONT;
}

static inline void SetRoadVehFront(Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	v->subtype = RVST_FRONT;
}

static inline bool IsRoadVehArticPart(const Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	return v->subtype == RVST_ARTIC_PART;
}

static inline void SetRoadVehArticPart(Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	v->subtype = RVST_ARTIC_PART;
}

static inline bool RoadVehHasArticPart(const Vehicle *v)
{
	assert(v->type == VEH_ROAD);
	return v->Next() != NULL && IsRoadVehArticPart(v->Next());
}


void CcBuildRoadVeh(bool success, TileIndex tile, uint32 p1, uint32 p2);

byte GetRoadVehLength(const Vehicle *v);

void RoadVehUpdateCache(Vehicle *v);


/**
 * This class 'wraps' Vehicle; you do not actually instantiate this class.
 * You create a Vehicle using AllocateVehicle, so it is added to the pool
 * and you reinitialize that to a Train using:
 *   v = new (v) RoadVehicle();
 *
 * As side-effect the vehicle type is set correctly.
 */
struct RoadVehicle : public Vehicle {
	/** Initializes the Vehicle to a road vehicle */
	RoadVehicle() { this->type = VEH_ROAD; }

	/** We want to 'destruct' the right class. */
	virtual ~RoadVehicle() { this->PreDestructor(); }

	const char *GetTypeString() const { return "road vehicle"; }
	void MarkDirty();
	void UpdateDeltaXY(Direction direction);
	ExpensesType GetExpenseType(bool income) const { return income ? EXPENSES_ROADVEH_INC : EXPENSES_ROADVEH_RUN; }
	bool IsPrimaryVehicle() const { return IsRoadVehFront(this); }
	SpriteID GetImage(Direction direction) const;
	int GetDisplaySpeed() const { return this->cur_speed / 2; }
	int GetDisplayMaxSpeed() const { return this->max_speed / 2; }
	Money GetRunningCost() const { return RoadVehInfo(this->engine_type)->running_cost * GetPriceByIndex(RoadVehInfo(this->engine_type)->running_cost_class); }
	bool IsInDepot() const { return this->u.road.state == RVSB_IN_DEPOT; }
	bool IsStoppedInDepot() const;
	void Tick();
	void OnNewDay();
	TileIndex GetOrderStationLocation(StationID station);
	bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse);
};

#endif /* ROADVEH_H */
