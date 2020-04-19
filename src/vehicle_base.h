/* $Id$ */

/** @file  vehicle_base.h Base class for all vehicles. */

#ifndef VEHICLE_BASE_H
#define VEHICLE_BASE_H

#include "vehicle_type.h"
#include "track_type.h"
#include "rail_type.h"
#include "road_type.h"
#include "cargo_type.h"
#include "direction_type.h"
#include "gfx_type.h"
#include "command_type.h"
#include "date_type.h"
#include "company_base.h"
#include "company_type.h"
#include "oldpool.h"
#include "order_base.h"
#include "cargopacket.h"
#include "texteff.hpp"
#include "group_type.h"
#include "engine_type.h"
#include "order_func.h"
#include "transport_type.h"

/** Road vehicle states */
enum RoadVehicleStates {
	/*
	 * Lower 4 bits are used for vehicle track direction. (Trackdirs)
	 * When in a road stop (bit 5 or bit 6 set) these bits give the
	 * track direction of the entry to the road stop.
	 * As the entry direction will always be a diagonal
	 * direction (X_NE, Y_SE, X_SW or Y_NW) only bits 0 and 3
	 * are needed to hold this direction. Bit 1 is then used to show
	 * that the vehicle is using the second road stop bay.
	 * Bit 2 is then used for drive-through stops to show the vehicle
	 * is stopping at this road stop.
	 */

	/* Numeric values */
	RVSB_IN_DEPOT                = 0xFE,                      ///< The vehicle is in a depot
	RVSB_WORMHOLE                = 0xFF,                      ///< The vehicle is in a tunnel and/or bridge

	/* Bit numbers */
	RVS_USING_SECOND_BAY         =    1,                      ///< Only used while in a road stop
	RVS_IS_STOPPING              =    2,                      ///< Only used for drive-through stops. Vehicle will stop here
	RVS_DRIVE_SIDE               =    4,                      ///< Only used when retrieving move data
	RVS_IN_ROAD_STOP             =    5,                      ///< The vehicle is in a road stop
	RVS_IN_DT_ROAD_STOP          =    6,                      ///< The vehicle is in a drive-through road stop

	/* Bit sets of the above specified bits */
	RVSB_IN_ROAD_STOP            = 1 << RVS_IN_ROAD_STOP,     ///< The vehicle is in a road stop
	RVSB_IN_ROAD_STOP_END        = RVSB_IN_ROAD_STOP + TRACKDIR_END,
	RVSB_IN_DT_ROAD_STOP         = 1 << RVS_IN_DT_ROAD_STOP,  ///< The vehicle is in a drive-through road stop
	RVSB_IN_DT_ROAD_STOP_END     = RVSB_IN_DT_ROAD_STOP + TRACKDIR_END,

	RVSB_TRACKDIR_MASK           = 0x0F,                      ///< The mask used to extract track dirs
	RVSB_ROAD_STOP_TRACKDIR_MASK = 0x09                       ///< Only bits 0 and 3 are used to encode the trackdir for road stops
};

enum VehStatus {
	VS_HIDDEN          = 0x01,
	VS_STOPPED         = 0x02,
	VS_UNCLICKABLE     = 0x04,
	VS_DEFPAL          = 0x08,
	VS_TRAIN_SLOWING   = 0x10,
	VS_SHADOW          = 0x20,
	VS_AIRCRAFT_BROKEN = 0x40,
	VS_CRASHED         = 0x80,
};

enum VehicleFlags {
	VF_LOADING_FINISHED,
	VF_CARGO_UNLOADING,
	VF_BUILT_AS_PROTOTYPE,
	VF_TIMETABLE_STARTED,       ///< Whether the vehicle has started running on the timetable yet.
	VF_AUTOFILL_TIMETABLE,      ///< Whether the vehicle should fill in the timetable automatically.
	VF_AUTOFILL_PRES_WAIT_TIME, ///< Whether non-destructive auto-fill should preserve waiting times
};

struct VehicleRail {
	/* Link between the two ends of a multiheaded engine */
	Vehicle *other_multiheaded_part;

	/* Cached wagon override spritegroup */
	const struct SpriteGroup *cached_override;

	uint16 last_speed; // NOSAVE: only used in UI
	uint16 crash_anim_pos;

	/* cached values, recalculated on load and each time a vehicle is added to/removed from the consist. */
	uint32 cached_power;        ///< total power of the consist.
	uint16 cached_max_speed;    ///< max speed of the consist. (minimum of the max speed of all vehicles in the consist)
	uint16 cached_total_length; ///< Length of the whole train, valid only for first engine.
	uint8 cached_veh_length;    ///< length of this vehicle in units of 1/8 of normal length, cached because this can be set by a callback
	bool cached_tilt;           ///< train can tilt; feature provides a bonus in curves

	/* cached values, recalculated when the cargo on a train changes (in addition to the conditions above) */
	uint32 cached_weight;     ///< total weight of the consist.
	uint32 cached_veh_weight; ///< weight of the vehicle.
	uint32 cached_max_te;     ///< max tractive effort of consist

	/**
	 * Position/type of visual effect.
	 * bit 0 - 3 = position of effect relative to vehicle. (0 = front, 8 = centre, 15 = rear)
	 * bit 4 - 5 = type of effect. (0 = default for engine class, 1 = steam, 2 = diesel, 3 = electric)
	 * bit     6 = disable visual effect.
	 * bit     7 = disable powered wagons.
	 */
	byte cached_vis_effect;
	byte user_def_data;

	/* NOSAVE: for wagon override - id of the first engine in train
	 * 0xffff == not in train */
	EngineID first_engine;

	uint16 flags;
	TrackBitsByte track;
	byte force_proceed;
	RailTypeByte railtype;
	RailTypes compatible_railtypes;
};

enum VehicleRailFlags {
	VRF_REVERSING         = 0,

	/* used to calculate if train is going up or down */
	VRF_GOINGUP           = 1,
	VRF_GOINGDOWN         = 2,

	/* used to store if a wagon is powered or not */
	VRF_POWEREDWAGON      = 3,

	/* used to reverse the visible direction of the vehicle */
	VRF_REVERSE_DIRECTION = 4,

	/* used to mark train as lost because PF can't find the route */
	VRF_NO_PATH_TO_DESTINATION = 5,

	/* used to mark that electric train engine is allowed to run on normal rail */
	VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL = 6,

	/* used for vehicle var 0xFE bit 8 (toggled each time the train is reversed, accurate for first vehicle only) */
	VRF_TOGGLE_REVERSE = 7,

	/* used to mark a train that can't get a path reservation */
	VRF_TRAIN_STUCK    = 8,
};

struct VehicleAir {
	uint16 crashed_counter;
	uint16 cached_max_speed;
	byte pos;
	byte previous_pos;
	StationID targetairport;
	byte state;
};

struct VehicleRoad {
	byte state;             ///< @see RoadVehicleStates
	byte frame;
	uint16 blocked_ctr;
	byte overtaking;
	byte overtaking_ctr;
	uint16 crashed_ctr;
	byte reverse_ctr;
	struct RoadStop *slot;
	byte slot_age;
	EngineID first_engine;
	byte cached_veh_length;

	RoadType roadtype;
	RoadTypes compatible_roadtypes;
};

struct VehicleEffect {
	uint16 animation_state;
	byte animation_substate;
};

struct VehicleDisaster {
	uint16 image_override;
	VehicleID big_ufo_destroyer_target;
};

struct VehicleShip {
	TrackBitsByte state;
};

DECLARE_OLD_POOL(Vehicle, Vehicle, 9, 125)

/* Some declarations of functions, so we can make them friendly */
struct SaveLoad;
extern const SaveLoad *GetVehicleDescription(VehicleType vt);
struct LoadgameState;
extern bool LoadOldVehicle(LoadgameState *ls, int num);

struct Vehicle : PoolItem<Vehicle, VehicleID, &_Vehicle_pool>, BaseVehicle {
private:
	Vehicle *next;           ///< pointer to the next vehicle in the chain
	Vehicle *previous;       ///< NOSAVE: pointer to the previous vehicle in the chain
	Vehicle *first;          ///< NOSAVE: pointer to the first vehicle in the chain

	Vehicle *next_shared;     ///< pointer to the next vehicle that shares the order
	Vehicle *previous_shared; ///< NOSAVE: pointer to the previous vehicle in the shared order chain
public:
	friend const SaveLoad *GetVehicleDescription(VehicleType vt); ///< So we can use private/protected variables in the saveload code
	friend void AfterLoadVehicles(bool part_of_load);             ///< So we can set the previous and first pointers while loading
	friend bool LoadOldVehicle(LoadgameState *ls, int num);       ///< So we can set the proper next pointer while loading

	char *name;              ///< Name of vehicle

	TileIndex tile;          ///< Current tile index

	/**
	 * Heading for this tile.
	 * For airports and train stations this tile does not necessarily belong to the destination station,
	 * but it can be used for heuristical purposes to estimate the distance.
	 */
	TileIndex dest_tile;

	Money profit_this_year;        ///< Profit this year << 8, low 8 bits are fract
	Money profit_last_year;        ///< Profit last year << 8, low 8 bits are fract
	Money value;

	/* Used for timetabling. */
	uint32 current_order_time;     ///< How many ticks have passed since this order started.
	int32 lateness_counter;        ///< How many ticks late (or early if negative) this vehicle is.

	/* Boundaries for the current position in the world and a next hash link.
	 * NOSAVE: All of those can be updated with VehiclePositionChanged() */
	Rect coord;
	Vehicle *next_hash;
	Vehicle *next_new_hash;
	Vehicle **old_new_hash;

	SpriteID colourmap; // NOSAVE: cached colour mapping

	/* Related to age and service time */
	Year build_year;
	Date age;     // Age in days
	Date max_age; // Maximum age
	Date date_of_last_service;
	Date service_interval;
	uint16 reliability;
	uint16 reliability_spd_dec;
	byte breakdown_ctr;
	byte breakdown_delay;
	byte breakdowns_since_last_service;
	byte breakdown_chance;

	int32 x_pos;             // coordinates
	int32 y_pos;
	byte z_pos;
	DirectionByte direction; // facing

	OwnerByte owner;         // which company owns the vehicle?
	byte spritenum;          // currently displayed sprite index
	                         // 0xfd == custom sprite, 0xfe == custom second head sprite
	                         // 0xff == reserved for another custom sprite
	uint16 cur_image;        // sprite number for this vehicle
	byte x_extent;           // x-extent of vehicle bounding box
	byte y_extent;           // y-extent of vehicle bounding box
	byte z_extent;           // z-extent of vehicle bounding box
	int8 x_offs;             // x offset for vehicle sprite
	int8 y_offs;             // y offset for vehicle sprite
	EngineID engine_type;

	TextEffectID fill_percent_te_id; // a text-effect id to a loading indicator object
	UnitID unitnumber;       // unit number, for display purposes only

	uint16 max_speed;        ///< maximum speed
	uint16 cur_speed;        ///< current speed
	byte subspeed;           ///< fractional speed
	byte acceleration;       ///< used by train & aircraft
	uint32 motion_counter;
	byte progress;

	/* for randomized variational spritegroups
	 * bitmask used to resolve them; parts of it get reseeded when triggers
	 * of corresponding spritegroups get matched */
	byte random_bits;
	byte waiting_triggers;   ///< triggers to be yet matched

	StationID last_station_visited;

	CargoID cargo_type;      ///< type of cargo this vehicle is carrying
	byte cargo_subtype;      ///< Used for livery refits (NewGRF variations)
	uint16 cargo_cap;        ///< total capacity
	CargoList cargo;         ///< The cargo this vehicle is carrying

	byte day_counter;        ///< Increased by one for each day
	byte tick_counter;       ///< Increased by one for each tick
	byte running_ticks;      ///< Number of ticks this vehicle was not stopped this day

	byte vehstatus;                 ///< Status
	Order current_order;            ///< The current order (+ status, like: loading)
	VehicleOrderID cur_order_index; ///< The index to the current order

	union {
		OrderList *list;              ///< Pointer to the order list for this vehicle
		Order     *old;               ///< Only used during conversion of old save games
	} orders;

	byte vehicle_flags;             ///< Used for gradual loading and other miscellaneous things (@see VehicleFlags enum)
	uint16 load_unload_time_rem;

	GroupID group_id;               ///< Index of group Pool array

	byte subtype;                   ///< subtype (Filled with values from EffectVehicles/TrainSubTypes/AircraftSubTypes)

	union {
		VehicleRail rail;
		VehicleAir air;
		VehicleRoad road;
		VehicleEffect effect;
		VehicleDisaster disaster;
		VehicleShip ship;
	} u;

	/* cached oftenly queried NewGRF values */
	uint8 cache_valid;   ///< Whether the caches are valid
	uint32 cached_var40; ///< Cache for NewGRF var 40
	uint32 cached_var41; ///< Cache for NewGRF var 41
	uint32 cached_var42; ///< Cache for NewGRF var 42
	uint32 cached_var43; ///< Cache for NewGRF var 43

	/**
	 * Allocates a lot of vehicles.
	 * @param vl pointer to an array of vehicles to get allocated. Can be NULL if the vehicles aren't needed (makes it test only)
	 * @param num number of vehicles to allocate room for
	 * @return true if there is room to allocate all the vehicles
	 */
	static bool AllocateList(Vehicle **vl, int num);

	/** Create a new vehicle */
	Vehicle();

	/** Destroy all stuff that (still) needs the virtual functions to work properly */
	void PreDestructor();
	/** We want to 'destruct' the right class. */
	virtual ~Vehicle();

	void BeginLoading();
	void LeaveStation();

	/**
	 * Handle the loading of the vehicle; when not it skips through dummy
	 * orders and does nothing in all other cases.
	 * @param mode is the non-first call for this vehicle in this tick?
	 */
	void HandleLoading(bool mode = false);

	/**
	 * Get a string 'representation' of the vehicle type.
	 * @return the string representation.
	 */
	virtual const char *GetTypeString() const { return "base vehicle"; }

	/**
	 * Marks the vehicles to be redrawn and updates cached variables
	 *
	 * This method marks the area of the vehicle on the screen as dirty.
	 * It can be use to repaint the vehicle.
	 *
	 * @ingroup dirty
	 */
	virtual void MarkDirty() {}

	/**
	 * Updates the x and y offsets and the size of the sprite used
	 * for this vehicle.
	 * @param direction the direction the vehicle is facing
	 */
	virtual void UpdateDeltaXY(Direction direction) {}

	/**
	 * Sets the expense type associated to this vehicle type
	 * @param income whether this is income or (running) expenses of the vehicle
	 */
	virtual ExpensesType GetExpenseType(bool income) const { return EXPENSES_OTHER; }

	/**
	 * Play the sound associated with leaving the station
	 */
	virtual void PlayLeaveStationSound() const {}

	/**
	 * Whether this is the primary vehicle in the chain.
	 */
	virtual bool IsPrimaryVehicle() const { return false; }

	/**
	 * Gets the sprite to show for the given direction
	 * @param direction the direction the vehicle is facing
	 * @return the sprite for the given vehicle in the given direction
	 */
	virtual SpriteID GetImage(Direction direction) const { return 0; }

	/**
	 * Gets the speed in km-ish/h that can be sent into SetDParam for string processing.
	 * @return the vehicle's speed
	 */
	virtual int GetDisplaySpeed() const { return 0; }

	/**
	 * Gets the maximum speed in km-ish/h that can be sent into SetDParam for string processing.
	 * @return the vehicle's maximum speed
	 */
	virtual int GetDisplayMaxSpeed() const { return 0; }

	/**
	 * Gets the running cost of a vehicle
	 * @return the vehicle's running cost
	 */
	virtual Money GetRunningCost() const { return 0; }

	/**
	 * Check whether the vehicle is in the depot.
	 * @return true if and only if the vehicle is in the depot.
	 */
	virtual bool IsInDepot() const { return false; }

	/**
	 * Check whether the vehicle is in the depot *and* stopped.
	 * @return true if and only if the vehicle is in the depot and stopped.
	 */
	virtual bool IsStoppedInDepot() const { return this->IsInDepot() && (this->vehstatus & VS_STOPPED) != 0; }

	/**
	 * Calls the tick handler of the vehicle
	 */
	virtual void Tick() {};

	/**
	 * Calls the new day handler of the vehicle
	 */
	virtual void OnNewDay() {};

	/**
	 * Gets the running cost of a vehicle  that can be sent into SetDParam for string processing.
	 * @return the vehicle's running cost
	 */
	Money GetDisplayRunningCost() const { return (this->GetRunningCost() >> 8); }

	/**
	 * Gets the profit vehicle had this year. It can be sent into SetDParam for string processing.
	 * @return the vehicle's profit this year
	 */
	Money GetDisplayProfitThisYear() const { return (this->profit_this_year >> 8); }

	/**
	 * Gets the profit vehicle had last year. It can be sent into SetDParam for string processing.
	 * @return the vehicle's profit last year
	 */
	Money GetDisplayProfitLastYear() const { return (this->profit_last_year >> 8); }

	/**
	 * Set the next vehicle of this vehicle.
	 * @param next the next vehicle. NULL removes the next vehicle.
	 */
	void SetNext(Vehicle *next);

	/**
	 * Get the next vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the next vehicle or NULL when there isn't a next vehicle.
	 */
	inline Vehicle *Next() const { return this->next; }

	/**
	 * Get the previous vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the previous vehicle or NULL when there isn't a previous vehicle.
	 */
	inline Vehicle *Previous() const { return this->previous; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *First() const { return this->first; }


	/**
	 * Get the first order of the vehicles order list.
	 * @return first order of order list.
	 */
	inline Order *GetFirstOrder() const { return (this->orders.list == NULL) ? NULL : this->orders.list->GetFirstOrder(); }

	/**
	 * Adds this vehicle to a shared vehicle chain.
	 * @param shared_chain a vehicle of the chain with shared vehicles.
	 * @pre !this->IsOrderListShared()
	 */
	void AddToShared(Vehicle *shared_chain);

	/**
	 * Removes the vehicle from the shared order list.
	 */
	void RemoveFromShared();

	/**
	 * Get the next vehicle of the shared vehicle chain.
	 * @return the next shared vehicle or NULL when there isn't a next vehicle.
	 */
	inline Vehicle *NextShared() const { return this->next_shared; }

	/**
	 * Get the previous vehicle of the shared vehicle chain
	 * @return the previous shared vehicle or NULL when there isn't a previous vehicle.
	 */
	inline Vehicle *PreviousShared() const { return this->previous_shared; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *FirstShared() const { return (this->orders.list == NULL) ? this->First() : this->orders.list->GetFirstSharedVehicle(); }

	/**
	 * Check if we share our orders with another vehicle.
	 * @return true if there are other vehicles sharing the same order
	 */
	inline bool IsOrderListShared() const { return this->orders.list != NULL && this->orders.list->IsShared(); }

	/**
	 * Get the number of orders this vehicle has.
	 * @return the number of orders this vehicle has.
	 */
	inline VehicleOrderID GetNumOrders() const { return (this->orders.list == NULL) ? 0 : this->orders.list->GetNumOrders(); }

	/**
	 * Copy certain configurations and statistics of a vehicle after successful autoreplace/renew
	 * The function shall copy everything that cannot be copied by a command (like orders / group etc),
	 * and that shall not be resetted for the new vehicle.
	 * @param src The old vehicle
	 */
	inline void CopyVehicleConfigAndStatistics(const Vehicle *src)
	{
		this->unitnumber = src->unitnumber;

		this->cur_order_index = src->cur_order_index;
		this->current_order = src->current_order;
		this->dest_tile  = src->dest_tile;

		this->profit_this_year = src->profit_this_year;
		this->profit_last_year = src->profit_last_year;

		this->current_order_time = src->current_order_time;
		this->lateness_counter = src->lateness_counter;

		this->service_interval = src->service_interval;
	}

	bool NeedsAutorenewing(const Company *c) const;

	/**
	 * Check if the vehicle needs to go to a depot in near future (if a opportunity presents itself) for service or replacement.
	 *
	 * @see NeedsAutomaticServicing()
	 * @return true if the vehicle should go to a depot if a opportunity presents itself.
	 */
	bool NeedsServicing() const;

	/**
	 * Checks if the current order should be interupted for a service-in-depot-order.
	 * @see NeedsServicing()
	 * @return true if the current order should be interupted.
	 */
	bool NeedsAutomaticServicing() const;

	/**
	 * Determine the location for the station where the vehicle goes to next.
	 * Things done for example are allocating slots in a road stop or exact
	 * location of the platform is determined for ships.
	 * @param station the station to make the next location of the vehicle.
	 * @return the location (tile) to aim for.
	 */
	virtual TileIndex GetOrderStationLocation(StationID station) { return INVALID_TILE; }

	/**
	 * Find the closest depot for this vehicle and tell us the location,
	 * DestinationID and whether we should reverse.
	 * @param location    where do we go to?
	 * @param destination what hangar do we go to?
	 * @param reverse     should the vehicle be reversed?
	 * @return true if a depot could be found.
	 */
	virtual bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse) { return false; }

	/**
	 * Send this vehicle to the depot using the given command(s).
	 * @param flags   the command flags (like execute and such).
	 * @param command the command to execute.
	 * @return the cost of the depot action.
	 */
	CommandCost SendToDepot(DoCommandFlag flags, DepotCommand command);
};

/**
 * This class 'wraps' Vehicle; you do not actually instantiate this class.
 * You create a Vehicle using AllocateVehicle, so it is added to the pool
 * and you reinitialize that to a Train using:
 *   v = new (v) Train();
 *
 * As side-effect the vehicle type is set correctly.
 */
struct DisasterVehicle : public Vehicle {
	/** Initializes the Vehicle to a disaster vehicle */
	DisasterVehicle() { this->type = VEH_DISASTER; }

	/** We want to 'destruct' the right class. */
	virtual ~DisasterVehicle() {}

	const char *GetTypeString() const { return "disaster vehicle"; }
	void UpdateDeltaXY(Direction direction);
	void Tick();
};

/**
 * This class 'wraps' Vehicle; you do not actually instantiate this class.
 * You create a Vehicle using AllocateVehicle, so it is added to the pool
 * and you reinitialize that to a Train using:
 *   v = new (v) Train();
 *
 * As side-effect the vehicle type is set correctly.
 */
struct InvalidVehicle : public Vehicle {
	/** Initializes the Vehicle to a invalid vehicle */
	InvalidVehicle() { this->type = VEH_INVALID; }

	/** We want to 'destruct' the right class. */
	virtual ~InvalidVehicle() {}

	const char *GetTypeString() const { return "invalid vehicle"; }
	void Tick() {}
};

static inline VehicleID GetMaxVehicleIndex()
{
	/* TODO - This isn't the real content of the function, but
	 *  with the new pool-system this will be replaced with one that
	 *  _really_ returns the highest index. Now it just returns
	 *  the next safe value we are sure about everything is below.
	 */
	return GetVehiclePoolSize() - 1;
}

static inline uint GetNumVehicles()
{
	return GetVehiclePoolSize();
}

#define FOR_ALL_VEHICLES_FROM(v, start) for (v = GetVehicle(start); v != NULL; v = (v->index + 1U < GetVehiclePoolSize()) ? GetVehicle(v->index + 1) : NULL) if (v->IsValid())
#define FOR_ALL_VEHICLES(v) FOR_ALL_VEHICLES_FROM(v, 0)

/**
 * Check if an index is a vehicle-index (so between 0 and max-vehicles)
 * @param index of the vehicle to query
 * @return Returns true if the vehicle-id is in range
 */
static inline bool IsValidVehicleID(uint index)
{
	return index < GetVehiclePoolSize() && GetVehicle(index)->IsValid();
}


/** Generates sequence of free UnitID numbers */
struct FreeUnitIDGenerator {
	bool *cache;  ///< array of occupied unit id numbers
	UnitID maxid; ///< maximum ID at the moment of constructor call
	UnitID curid; ///< last ID returned ; 0 if none

	/** Initializes the structure. Vehicle unit numbers are supposed not to change after
	 * struct initialization, except after each call to this->NextID() the returned value
	 * is assigned to a vehicle.
	 * @param type type of vehicle
	 * @param owner owner of vehicles
	 */
	FreeUnitIDGenerator(VehicleType type, CompanyID owner);

	/** Returns next free UnitID. Supposes the last returned value was assigned to a vehicle. */
	UnitID NextID();

	/** Releases allocated memory */
	~FreeUnitIDGenerator() { free(this->cache); }
};

/* Returns order 'index' of a vehicle or NULL when it doesn't exists */
static inline Order *GetVehicleOrder(const Vehicle *v, int index) { return (v->orders.list == NULL) ? NULL : v->orders.list->GetOrderAt(index); }

/**
 * Returns the last order of a vehicle, or NULL if it doesn't exists
 * @param v Vehicle to query
 * @return last order of a vehicle, if available
 */
static inline Order *GetLastVehicleOrder(const Vehicle *v) { return (v->orders.list == NULL) ? NULL : v->orders.list->GetLastOrder(); }

/**
 * Returns the Trackdir on which the vehicle is currently located.
 * Works for trains and ships.
 * Currently works only sortof for road vehicles, since they have a fuzzy
 * concept of being "on" a trackdir. Dunno really what it returns for a road
 * vehicle that is halfway a tile, never really understood that part. For road
 * vehicles that are at the beginning or end of the tile, should just return
 * the diagonal trackdir on which they are driving. I _think_.
 * For other vehicles types, or vehicles with no clear trackdir (such as those
 * in depots), returns 0xFF.
 */
Trackdir GetVehicleTrackdir(const Vehicle *v);

void CheckVehicle32Day(Vehicle *v);

static const int32 INVALID_COORD = 0x7fffffff;

#endif /* VEHICLE_BASE_H */
