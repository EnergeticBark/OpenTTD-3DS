/* $Id$ */

/** @file newgrf_engine.cpp NewGRF handling of engines. */

#include "stdafx.h"
#include "debug.h"
#include "train.h"
#include "company_func.h"
#include "newgrf_engine.h"
#include "newgrf_spritegroup.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "core/random_func.hpp"
#include "aircraft.h"
#include "core/smallmap_type.hpp"
#include "settings_type.h"

int _traininfo_vehicle_pitch = 0;
int _traininfo_vehicle_width = 29;

struct WagonOverride {
	EngineID *train_id;
	uint trains;
	CargoID cargo;
	const SpriteGroup *group;
};

void SetWagonOverrideSprites(EngineID engine, CargoID cargo, const SpriteGroup *group, EngineID *train_id, uint trains)
{
	Engine *e = GetEngine(engine);
	WagonOverride *wo;

	assert(cargo < NUM_CARGO + 2); // Include CT_DEFAULT and CT_PURCHASE pseudo cargos.

	e->overrides_count++;
	e->overrides = ReallocT(e->overrides, e->overrides_count);

	wo = &e->overrides[e->overrides_count - 1];
	wo->group = group;
	wo->cargo = cargo;
	wo->trains = trains;
	wo->train_id = MallocT<EngineID>(trains);
	memcpy(wo->train_id, train_id, trains * sizeof *train_id);
}

const SpriteGroup *GetWagonOverrideSpriteSet(EngineID engine, CargoID cargo, EngineID overriding_engine)
{
	const Engine *e = GetEngine(engine);

	/* XXX: This could turn out to be a timesink on profiles. We could
	 * always just dedicate 65535 bytes for an [engine][train] trampoline
	 * for O(1). Or O(logMlogN) and searching binary tree or smt. like
	 * that. --pasky */

	for (uint i = 0; i < e->overrides_count; i++) {
		const WagonOverride *wo = &e->overrides[i];

		if (wo->cargo != cargo && wo->cargo != CT_DEFAULT) continue;

		for (uint j = 0; j < wo->trains; j++) {
			if (wo->train_id[j] == overriding_engine) return wo->group;
		}
	}
	return NULL;
}

/**
 * Unload all wagon override sprite groups.
 */
void UnloadWagonOverrides(Engine *e)
{
	for (uint i = 0; i < e->overrides_count; i++) {
		WagonOverride *wo = &e->overrides[i];
		free(wo->train_id);
	}
	free(e->overrides);
	e->overrides_count = 0;
	e->overrides = NULL;
}


void SetCustomEngineSprites(EngineID engine, byte cargo, const SpriteGroup *group)
{
	Engine *e = GetEngine(engine);
	assert(cargo < lengthof(e->group));

	if (e->group[cargo] != NULL) {
		grfmsg(6, "SetCustomEngineSprites: engine %d cargo %d already has group -- replacing", engine, cargo);
	}
	e->group[cargo] = group;
}


/**
 * Tie a GRFFile entry to an engine, to allow us to retrieve GRF parameters
 * etc during a game.
 * @param engine Engine ID to tie the GRFFile to.
 * @param file   Pointer of GRFFile to tie.
 */
void SetEngineGRF(EngineID engine, const GRFFile *file)
{
	Engine *e = GetEngine(engine);
	e->grffile = file;
}


/**
 * Retrieve the GRFFile tied to an engine
 * @param engine Engine ID to retrieve.
 * @return Pointer to GRFFile.
 */
const GRFFile *GetEngineGRF(EngineID engine)
{
	return GetEngine(engine)->grffile;
}


/**
 * Retrieve the GRF ID of the GRFFile tied to an engine
 * @param engine Engine ID to retrieve.
 * @return 32 bit GRFID value.
 */
uint32 GetEngineGRFID(EngineID engine)
{
	const GRFFile *file = GetEngineGRF(engine);
	return file == NULL ? 0 : file->grfid;
}


static int MapOldSubType(const Vehicle *v)
{
	if (v->type != VEH_TRAIN) return v->subtype;
	if (IsTrainEngine(v)) return 0;
	if (IsFreeWagon(v)) return 4;
	return 2;
}


/* TTDP style aircraft movement states for GRF Action 2 Var 0xE2 */
enum {
	AMS_TTDP_HANGAR,
	AMS_TTDP_TO_HANGAR,
	AMS_TTDP_TO_PAD1,
	AMS_TTDP_TO_PAD2,
	AMS_TTDP_TO_PAD3,
	AMS_TTDP_TO_ENTRY_2_AND_3,
	AMS_TTDP_TO_ENTRY_2_AND_3_AND_H,
	AMS_TTDP_TO_JUNCTION,
	AMS_TTDP_LEAVE_RUNWAY,
	AMS_TTDP_TO_INWAY,
	AMS_TTDP_TO_RUNWAY,
	AMS_TTDP_TO_OUTWAY,
	AMS_TTDP_WAITING,
	AMS_TTDP_TAKEOFF,
	AMS_TTDP_TO_TAKEOFF,
	AMS_TTDP_CLIMBING,
	AMS_TTDP_FLIGHT_APPROACH,
	AMS_TTDP_UNUSED_0x11,
	AMS_TTDP_FLIGHT_TO_TOWER,
	AMS_TTDP_UNUSED_0x13,
	AMS_TTDP_FLIGHT_FINAL,
	AMS_TTDP_FLIGHT_DESCENT,
	AMS_TTDP_BRAKING,
	AMS_TTDP_HELI_TAKEOFF_AIRPORT,
	AMS_TTDP_HELI_TO_TAKEOFF_AIRPORT,
	AMS_TTDP_HELI_LAND_AIRPORT,
	AMS_TTDP_HELI_TAKEOFF_HELIPORT,
	AMS_TTDP_HELI_TO_TAKEOFF_HELIPORT,
	AMS_TTDP_HELI_LAND_HELIPORT,
};


/**
 * Map OTTD aircraft movement states to TTDPatch style movement states
 * (VarAction 2 Variable 0xE2)
 */
static byte MapAircraftMovementState(const Vehicle *v)
{
	const Station *st = GetTargetAirportIfValid(v);
	if (st == NULL) return AMS_TTDP_FLIGHT_TO_TOWER;

	const AirportFTAClass *afc = st->Airport();
	uint16 amdflag = afc->MovingData(v->u.air.pos)->flag;

	switch (v->u.air.state) {
		case HANGAR:
			/* The international airport is a special case as helicopters can land in
			 * front of the hanger. Helicopters also change their air.state to
			 * AMED_HELI_LOWER some time before actually descending. */

			/* This condition only occurs for helicopters, during descent,
			 * to a landing by the hanger of an international airport. */
			if (amdflag & AMED_HELI_LOWER) return AMS_TTDP_HELI_LAND_AIRPORT;

			/* This condition only occurs for helicopters, before starting descent,
			 * to a landing by the hanger of an international airport. */
			if (amdflag & AMED_SLOWTURN) return AMS_TTDP_FLIGHT_TO_TOWER;

			/* The final two conditions apply to helicopters or aircraft.
			 * Has reached hanger? */
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_HANGAR;

			/* Still moving towards hanger. */
			return AMS_TTDP_TO_HANGAR;

		case TERM1:
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD1;
			return AMS_TTDP_TO_JUNCTION;

		case TERM2:
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD2;
			return AMS_TTDP_TO_ENTRY_2_AND_3_AND_H;

		case TERM3:
		case TERM4:
		case TERM5:
		case TERM6:
		case TERM7:
		case TERM8:
			/* TTDPatch only has 3 terminals, so treat these states the same */
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD3;
			return AMS_TTDP_TO_ENTRY_2_AND_3_AND_H;

		case HELIPAD1:
		case HELIPAD2:
		case HELIPAD3:
		case HELIPAD4: // Will only occur for helicopters.
			if (amdflag & AMED_HELI_LOWER) return AMS_TTDP_HELI_LAND_AIRPORT; // Descending.
			if (amdflag & AMED_SLOWTURN)   return AMS_TTDP_FLIGHT_TO_TOWER;   // Still hasn't started descent.
			return AMS_TTDP_TO_JUNCTION; // On the ground.

		case TAKEOFF: // Moving to takeoff position.
			return AMS_TTDP_TO_OUTWAY;

		case STARTTAKEOFF: // Accelerating down runway.
			return AMS_TTDP_TAKEOFF;

		case ENDTAKEOFF: // Ascent
			return AMS_TTDP_CLIMBING;

		case HELITAKEOFF: // Helicopter is moving to take off position.
			if (afc->delta_z == 0) {
				return amdflag & AMED_HELI_RAISE ?
					AMS_TTDP_HELI_TAKEOFF_AIRPORT : AMS_TTDP_TO_JUNCTION;
			} else {
				return AMS_TTDP_HELI_TAKEOFF_HELIPORT;
			}

		case FLYING:
			return amdflag & AMED_HOLD ? AMS_TTDP_FLIGHT_APPROACH : AMS_TTDP_FLIGHT_TO_TOWER;

		case LANDING: // Descent
			return AMS_TTDP_FLIGHT_DESCENT;

		case ENDLANDING: // On the runway braking
			if (amdflag & AMED_BRAKE) return AMS_TTDP_BRAKING;
			/* Landed - moving off runway */
			return AMS_TTDP_TO_INWAY;

		case HELILANDING:
		case HELIENDLANDING: // Helicoptor is decending.
			if (amdflag & AMED_HELI_LOWER) {
				return afc->delta_z == 0 ?
					AMS_TTDP_HELI_LAND_AIRPORT : AMS_TTDP_HELI_LAND_HELIPORT;
			} else {
				return AMS_TTDP_FLIGHT_TO_TOWER;
			}

		default:
			return AMS_TTDP_HANGAR;
	}
}


/* TTDP style aircraft movement action for GRF Action 2 Var 0xE6 */
enum {
	AMA_TTDP_IN_HANGAR,
	AMA_TTDP_ON_PAD1,
	AMA_TTDP_ON_PAD2,
	AMA_TTDP_ON_PAD3,
	AMA_TTDP_HANGAR_TO_PAD1,
	AMA_TTDP_HANGAR_TO_PAD2,
	AMA_TTDP_HANGAR_TO_PAD3,
	AMA_TTDP_LANDING_TO_PAD1,
	AMA_TTDP_LANDING_TO_PAD2,
	AMA_TTDP_LANDING_TO_PAD3,
	AMA_TTDP_PAD1_TO_HANGAR,
	AMA_TTDP_PAD2_TO_HANGAR,
	AMA_TTDP_PAD3_TO_HANGAR,
	AMA_TTDP_PAD1_TO_TAKEOFF,
	AMA_TTDP_PAD2_TO_TAKEOFF,
	AMA_TTDP_PAD3_TO_TAKEOFF,
	AMA_TTDP_HANGAR_TO_TAKOFF,
	AMA_TTDP_LANDING_TO_HANGAR,
	AMA_TTDP_IN_FLIGHT,
};


/**
 * Map OTTD aircraft movement states to TTDPatch style movement actions
 * (VarAction 2 Variable 0xE6)
 * This is not fully supported yet but it's enough for Planeset.
 */
static byte MapAircraftMovementAction(const Vehicle *v)
{
	switch (v->u.air.state) {
		case HANGAR:
			return (v->cur_speed > 0) ? AMA_TTDP_LANDING_TO_HANGAR : AMA_TTDP_IN_HANGAR;

		case TERM1:
		case HELIPAD1:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD1 : AMA_TTDP_LANDING_TO_PAD1;

		case TERM2:
		case HELIPAD2:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD2 : AMA_TTDP_LANDING_TO_PAD2;

		case TERM3:
		case TERM4:
		case TERM5:
		case TERM6:
		case TERM7:
		case TERM8:
		case HELIPAD3:
		case HELIPAD4:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD3 : AMA_TTDP_LANDING_TO_PAD3;

		case TAKEOFF:      // Moving to takeoff position
		case STARTTAKEOFF: // Accelerating down runway
		case ENDTAKEOFF:   // Ascent
		case HELITAKEOFF:
			/* @todo Need to find which terminal (or hanger) we've come from. How? */
			return AMA_TTDP_PAD1_TO_TAKEOFF;

		case FLYING:
			return AMA_TTDP_IN_FLIGHT;

		case LANDING:    // Descent
		case ENDLANDING: // On the runway braking
		case HELILANDING:
		case HELIENDLANDING:
			/* @todo Need to check terminal we're landing to. Is it known yet? */
			return (v->current_order.IsType(OT_GOTO_DEPOT)) ?
				AMA_TTDP_LANDING_TO_HANGAR : AMA_TTDP_LANDING_TO_PAD1;

		default:
			return AMA_TTDP_IN_HANGAR;
	}
}


/* TTDP airport types. Used to map our types to TTDPatch's */
enum {
	ATP_TTDP_SMALL,
	ATP_TTDP_LARGE,
	ATP_TTDP_HELIPORT,
	ATP_TTDP_OILRIG,
};


/* Vehicle Resolver Functions */
static inline const Vehicle *GRV(const ResolverObject *object)
{
	switch (object->scope) {
		default: NOT_REACHED();
		case VSG_SCOPE_SELF: return object->u.vehicle.self;
		case VSG_SCOPE_PARENT: return object->u.vehicle.parent;
		case VSG_SCOPE_RELATIVE: {
			const Vehicle *v = NULL;
			switch (GB(object->count, 6, 2)) {
				default: NOT_REACHED();
				case 0x00: // count back (away from the engine), starting at this vehicle
				case 0x01: // count forward (toward the engine), starting at this vehicle
					v = object->u.vehicle.self;
					break;
				case 0x02: // count back, starting at the engine
					v = object->u.vehicle.parent;
					break;
				case 0x03: { // count back, starting at the first vehicle in this chain of vehicles with the same ID, as for vehicle variable 41
					const Vehicle *self = object->u.vehicle.self;
					for (const Vehicle *u = self->First(); u != self; u = u->Next()) {
						if (u->engine_type != self->engine_type) {
							v = NULL;
						} else {
							if (v == NULL) v = u;
						}
					}
					if (v == NULL) v = self;
				} break;
			}
			uint32 count = GB(object->count, 0, 4);
			if (count == 0) count = GetRegister(0x100);
			while (v != NULL && count-- != 0) v = (GB(object->count, 6, 2) == 0x01) ? v->Previous() : v->Next();
			return v;
		}
	}
}


static uint32 VehicleGetRandomBits(const ResolverObject *object)
{
	return GRV(object) == NULL ? 0 : GRV(object)->random_bits;
}


static uint32 VehicleGetTriggers(const ResolverObject *object)
{
	return GRV(object) == NULL ? 0 : GRV(object)->waiting_triggers;
}


static void VehicleSetTriggers(const ResolverObject *object, int triggers)
{
	/* Evil cast to get around const-ness. This used to be achieved by an
	 * innocent looking function pointer cast... Currently I cannot see a
	 * way of avoiding this without removing consts deep within gui code.
	 */
	Vehicle *v = (Vehicle*)GRV(object);

	/* This function must only be called when processing triggers -- any
	 * other time is an error. */
	assert(object->trigger != 0);

	if (v != NULL) v->waiting_triggers = triggers;
}


static uint8 LiveryHelper(EngineID engine, const Vehicle *v)
{
	const Livery *l;

	if (v == NULL) {
		if (!IsValidCompanyID(_current_company)) return 0;
		l = GetEngineLivery(engine, _current_company, INVALID_ENGINE, NULL);
	} else if (v->type == VEH_TRAIN) {
		l = GetEngineLivery(v->engine_type, v->owner, v->u.rail.first_engine, v);
	} else if (v->type == VEH_ROAD) {
		l = GetEngineLivery(v->engine_type, v->owner, v->u.road.first_engine, v);
	} else {
		l = GetEngineLivery(v->engine_type, v->owner, INVALID_ENGINE, v);
	}

	return l->colour1 + l->colour2 * 16;
}

/**
 * Helper to get the position of a vehicle within a chain of vehicles.
 * @param v the vehicle to get the position of.
 * @param consecutive whether to look at the whole chain or the vehicles
 *                    with the same 'engine type'.
 * @return the position in the chain from front and tail and chain length.
 */
static uint32 PositionHelper(const Vehicle *v, bool consecutive)
{
	const Vehicle *u;
	byte chain_before = 0;
	byte chain_after  = 0;

	for (u = v->First(); u != v; u = u->Next()) {
		chain_before++;
		if (consecutive && u->engine_type != v->engine_type) chain_before = 0;
	}

	while (u->Next() != NULL && (!consecutive || u->Next()->engine_type == v->engine_type)) {
		chain_after++;
		u = u->Next();
	}

	return chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16;
}

static uint32 VehicleGetVariable(const ResolverObject *object, byte variable, byte parameter, bool *available)
{
	Vehicle *v = const_cast<Vehicle*>(GRV(object));

	if (v == NULL) {
		/* Vehicle does not exist, so we're in a purchase list */
		switch (variable) {
			case 0x43: return _current_company | (LiveryHelper(object->u.vehicle.self_type, NULL) << 24); // Owner information
			case 0x46: return 0;               // Motion counter
			case 0x47: { // Vehicle cargo info
				const Engine *e = GetEngine(object->u.vehicle.self_type);
				CargoID cargo_type = e->GetDefaultCargoType();
				if (cargo_type != CT_INVALID) {
					const CargoSpec *cs = GetCargo(cargo_type);
					return (cs->classes << 16) | (cs->weight << 8) | GetEngineGRF(e->index)->cargo_map[cargo_type];
				} else {
					return 0x000000FF;
				}
			}
			case 0x48: return GetEngine(object->u.vehicle.self_type)->flags; // Vehicle Type Info
			case 0x49: return _cur_year; // 'Long' format build year
			case 0xC4: return Clamp(_cur_year, ORIGINAL_BASE_YEAR, ORIGINAL_MAX_YEAR) - ORIGINAL_BASE_YEAR; // Build year
			case 0xDA: return INVALID_VEHICLE; // Next vehicle
			case 0xF2: return 0; // Cargo subtype
		}

		*available = false;
		return UINT_MAX;
	}

	/* Calculated vehicle parameters */
	switch (variable) {
		case 0x25: // Get engine GRF ID
			return GetEngineGRFID(v->engine_type);

		case 0x40: // Get length of consist
			if (!HasBit(v->cache_valid, 0)) {
				v->cached_var40 = PositionHelper(v, false);
				SetBit(v->cache_valid, 0);
			}
			return v->cached_var40;

		case 0x41: // Get length of same consecutive wagons
			if (!HasBit(v->cache_valid, 1)) {
				v->cached_var41 = PositionHelper(v, true);
				SetBit(v->cache_valid, 1);
			}
			return v->cached_var41;

		case 0x42: // Consist cargo information
			if (!HasBit(v->cache_valid, 2)) {
				const Vehicle *u;
				byte cargo_classes = 0;
				CargoID common_cargo_best = CT_INVALID;
				uint8 common_cargos[NUM_CARGO];
				uint8 common_subtype_best = 0xFF; // Return 0xFF if nothing is carried
				uint8 common_subtypes[256];
				byte user_def_data = 0;
				CargoID common_cargo_type = CT_PASSENGERS;
				uint8 common_subtype = 0;

				/* Reset our arrays */
				memset(common_cargos, 0, sizeof(common_cargos));
				memset(common_subtypes, 0, sizeof(common_subtypes));

				for (u = v; u != NULL; u = u->Next()) {
					if (v->type == VEH_TRAIN) user_def_data |= u->u.rail.user_def_data;

					/* Skip empty engines */
					if (u->cargo_cap == 0) continue;

					cargo_classes |= GetCargo(u->cargo_type)->classes;
					common_cargos[u->cargo_type]++;
				}

				/* Pick the most common cargo type */
				for (CargoID cargo = 0; cargo < NUM_CARGO; cargo++) {
					if (common_cargos[cargo] > common_cargo_best) {
						common_cargo_best = common_cargos[cargo];
						common_cargo_type = cargo;
					}
				}

				/* Count subcargo types of common_cargo_type */
				for (u = v; u != NULL; u = u->Next()) {
					/* Skip empty engines and engines not carrying common_cargo_type */
					if (u->cargo_cap == 0 || u->cargo_type != common_cargo_type) continue;

					common_subtypes[u->cargo_subtype]++;
				}

				/* Pick the most common subcargo type*/
				for (uint i = 0; i < lengthof(common_subtypes); i++) {
					if (common_subtypes[i] > common_subtype_best) {
						common_subtype_best = common_subtypes[i];
						common_subtype = i;
					}
				}

				uint8 common_bitnum = (common_cargo_type == CT_INVALID ? 0xFF : GetCargo(common_cargo_type)->bitnum);
				v->cached_var42 = cargo_classes | (common_bitnum << 8) | (common_subtype << 16) | (user_def_data << 24);
				SetBit(v->cache_valid, 2);
			}
			return v->cached_var42;

		case 0x43: // Company information
			if (!HasBit(v->cache_valid, 3)) {
				v->cached_var43 = v->owner | (GetCompany(v->owner)->is_ai ? 0x10000 : 0) | (LiveryHelper(v->engine_type, v) << 24);
				SetBit(v->cache_valid, 3);
			}
			return v->cached_var43;

		case 0x44: // Aircraft information
			if (v->type != VEH_AIRCRAFT) return UINT_MAX;

			{
				const Vehicle *w = v->Next();
				uint16 altitude = v->z_pos - w->z_pos; // Aircraft height - shadow height
				byte airporttype = ATP_TTDP_LARGE;

				const Station *st = GetTargetAirportIfValid(v);

				if (st != NULL) {
					switch (st->airport_type) {
						/* Note, Helidepot and Helistation are treated as small airports
						 * as they are at ground level. */
						case AT_HELIDEPOT:
						case AT_HELISTATION:
						case AT_COMMUTER:
						case AT_SMALL:         airporttype = ATP_TTDP_SMALL; break;
						case AT_METROPOLITAN:
						case AT_INTERNATIONAL:
						case AT_INTERCON:
						case AT_LARGE:         airporttype = ATP_TTDP_LARGE; break;
						case AT_HELIPORT:      airporttype = ATP_TTDP_HELIPORT; break;
						case AT_OILRIG:        airporttype = ATP_TTDP_OILRIG; break;
						default:               airporttype = ATP_TTDP_LARGE; break;
					}
				}

				return (altitude << 8) | airporttype;
			}

		case 0x45: { // Curvature info
			/* Format: xxxTxBxF
			 * F - previous wagon to current wagon, 0 if vehicle is first
			 * B - current wagon to next wagon, 0 if wagon is last
			 * T - previous wagon to next wagon, 0 in an S-bend
			 */
			if (v->type != VEH_TRAIN && v->type != VEH_ROAD) return 0;

			const Vehicle *u_p = v->Previous();
			const Vehicle *u_n = v->Next();
			DirDiff f = (u_p == NULL) ?  DIRDIFF_SAME : DirDifference(u_p->direction, v->direction);
			DirDiff b = (u_n == NULL) ?  DIRDIFF_SAME : DirDifference(v->direction, u_n->direction);
			DirDiff t = ChangeDirDiff(f, b);

			return ((t > DIRDIFF_REVERSE ? t | 8 : t) << 16) |
			       ((b > DIRDIFF_REVERSE ? b | 8 : b) <<  8) |
			       ( f > DIRDIFF_REVERSE ? f | 8 : f);
		}

		case 0x46: // Motion counter
			return v->motion_counter;

		case 0x47: { // Vehicle cargo info
			/* Format: ccccwwtt
			 * tt - the cargo type transported by the vehicle,
			 *     translated if a translation table has been installed.
			 * ww - cargo unit weight in 1/16 tons, same as cargo prop. 0F.
			 * cccc - the cargo class value of the cargo transported by the vehicle.
			 */
			const CargoSpec *cs = GetCargo(v->cargo_type);

			return (cs->classes << 16) | (cs->weight << 8) | GetEngineGRF(v->engine_type)->cargo_map[v->cargo_type];
		}

		case 0x48: return GetEngine(v->engine_type)->flags; // Vehicle Type Info
		case 0x49: return v->build_year;

		/* Variables which use the parameter */
		case 0x60: // Count consist's engine ID occurance
			//EngineID engine = GetNewEngineID(GetEngineGRF(v->engine_type), v->type, parameter);
			if (v->type != VEH_TRAIN) return GetEngine(v->engine_type)->internal_id == parameter;

			{
				uint count = 0;
				for (; v != NULL; v = v->Next()) {
					if (GetEngine(v->engine_type)->internal_id == parameter) count++;
				}
				return count;
			}

		case 0xFE:
		case 0xFF: {
			uint16 modflags = 0;

			if (v->type == VEH_TRAIN) {
				const Vehicle *u = IsTrainWagon(v) && HasBit(v->vehicle_flags, VRF_POWEREDWAGON) ? v->First() : v;
				RailType railtype = GetRailType(v->tile);
				bool powered = IsTrainEngine(v) || (IsTrainWagon(v) && HasBit(v->vehicle_flags, VRF_POWEREDWAGON));
				bool has_power = powered && HasPowerOnRail(u->u.rail.railtype, railtype);
				bool is_electric = powered && u->u.rail.railtype == RAILTYPE_ELECTRIC;

				if (has_power) SetBit(modflags, 5);
				if (is_electric && !has_power) SetBit(modflags, 6);
				if (HasBit(v->u.rail.flags, VRF_TOGGLE_REVERSE)) SetBit(modflags, 8);
			}
			if (HasBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE)) SetBit(modflags, 10);

			return variable == 0xFE ? modflags : GB(modflags, 8, 8);
		}
	}

	/* General vehicle properties */
	switch (variable - 0x80) {
		case 0x00: return v->type;
		case 0x01: return MapOldSubType(v);
		case 0x04: return v->index;
		case 0x05: return GB(v->index, 8, 8);
		case 0x0A: return v->current_order.Pack();
		case 0x0B: return GB(v->current_order.Pack(), 8, 8);
		case 0x0C: return v->GetNumOrders();
		case 0x0D: return v->cur_order_index;
		case 0x10: return v->load_unload_time_rem;
		case 0x11: return GB(v->load_unload_time_rem, 8, 8);
		case 0x12: return max(v->date_of_last_service - DAYS_TILL_ORIGINAL_BASE_YEAR, 0);
		case 0x13: return GB(max(v->date_of_last_service - DAYS_TILL_ORIGINAL_BASE_YEAR, 0), 8, 8);
		case 0x14: return v->service_interval;
		case 0x15: return GB(v->service_interval, 8, 8);
		case 0x16: return v->last_station_visited;
		case 0x17: return v->tick_counter;
		case 0x18: return v->max_speed;
		case 0x19: return GB(v->max_speed, 8, 8);
		case 0x1A: return v->x_pos;
		case 0x1B: return GB(v->x_pos, 8, 8);
		case 0x1C: return v->y_pos;
		case 0x1D: return GB(v->y_pos, 8, 8);
		case 0x1E: return v->z_pos;
		case 0x1F: return object->info_view ? DIR_W : v->direction;
		case 0x28: return v->cur_image;
		case 0x29: return GB(v->cur_image, 8, 8);
		case 0x32: return v->vehstatus;
		case 0x33: return 0; // non-existent high byte of vehstatus
		case 0x34: return v->cur_speed;
		case 0x35: return GB(v->cur_speed, 8, 8);
		case 0x36: return v->subspeed;
		case 0x37: return v->acceleration;
		case 0x39: return v->cargo_type;
		case 0x3A: return v->cargo_cap;
		case 0x3B: return GB(v->cargo_cap, 8, 8);
		case 0x3C: return v->cargo.Count();
		case 0x3D: return GB(v->cargo.Count(), 8, 8);
		case 0x3E: return v->cargo.Source();
		case 0x3F: return v->cargo.DaysInTransit();
		case 0x40: return v->age;
		case 0x41: return GB(v->age, 8, 8);
		case 0x42: return v->max_age;
		case 0x43: return GB(v->max_age, 8, 8);
		case 0x44: return Clamp(v->build_year, ORIGINAL_BASE_YEAR, ORIGINAL_MAX_YEAR) - ORIGINAL_BASE_YEAR;
		case 0x45: return v->unitnumber;
		case 0x46: return GetEngine(v->engine_type)->internal_id;
		case 0x47: return GB(GetEngine(v->engine_type)->internal_id, 8, 8);
		case 0x48:
			if (v->type != VEH_TRAIN || v->spritenum != 0xFD) return v->spritenum;
			return HasBit(v->u.rail.flags, VRF_REVERSE_DIRECTION) ? 0xFE : 0xFD;

		case 0x49: return v->day_counter;
		case 0x4A: return v->breakdowns_since_last_service;
		case 0x4B: return v->breakdown_ctr;
		case 0x4C: return v->breakdown_delay;
		case 0x4D: return v->breakdown_chance;
		case 0x4E: return v->reliability;
		case 0x4F: return GB(v->reliability, 8, 8);
		case 0x50: return v->reliability_spd_dec;
		case 0x51: return GB(v->reliability_spd_dec, 8, 8);
		case 0x52: return ClampToI32(v->GetDisplayProfitThisYear());
		case 0x53: return GB(ClampToI32(v->GetDisplayProfitThisYear()),  8, 24);
		case 0x54: return GB(ClampToI32(v->GetDisplayProfitThisYear()), 16, 16);
		case 0x55: return GB(ClampToI32(v->GetDisplayProfitThisYear()), 24,  8);
		case 0x56: return ClampToI32(v->GetDisplayProfitLastYear());
		case 0x57: return GB(ClampToI32(v->GetDisplayProfitLastYear()),  8, 24);
		case 0x58: return GB(ClampToI32(v->GetDisplayProfitLastYear()), 16, 16);
		case 0x59: return GB(ClampToI32(v->GetDisplayProfitLastYear()), 24,  8);
		case 0x5A: return v->Next() == NULL ? INVALID_VEHICLE : v->Next()->index;
		case 0x5C: return ClampToI32(v->value);
		case 0x5D: return GB(ClampToI32(v->value),  8, 24);
		case 0x5E: return GB(ClampToI32(v->value), 16, 16);
		case 0x5F: return GB(ClampToI32(v->value), 24,  8);
		case 0x72: return v->cargo_subtype;
		case 0x7A: return v->random_bits;
		case 0x7B: return v->waiting_triggers;
	}

	/* Vehicle specific properties */
	switch (v->type) {
		case VEH_TRAIN:
			switch (variable - 0x80) {
				case 0x62: return v->u.rail.track;
				case 0x66: return v->u.rail.railtype;
				case 0x73: return v->u.rail.cached_veh_length;
				case 0x74: return v->u.rail.cached_power;
				case 0x75: return GB(v->u.rail.cached_power,  8, 24);
				case 0x76: return GB(v->u.rail.cached_power, 16, 16);
				case 0x77: return GB(v->u.rail.cached_power, 24,  8);
				case 0x7C: return v->First()->index;
				case 0x7D: return GB(v->First()->index, 8, 8);
				case 0x7F: return 0; // Used for vehicle reversing hack in TTDP
			}
			break;

		case VEH_ROAD:
			switch (variable - 0x80) {
				case 0x62: return v->u.road.state;
				case 0x64: return v->u.road.blocked_ctr;
				case 0x65: return GB(v->u.road.blocked_ctr, 8, 8);
				case 0x66: return v->u.road.overtaking;
				case 0x67: return v->u.road.overtaking_ctr;
				case 0x68: return v->u.road.crashed_ctr;
				case 0x69: return GB(v->u.road.crashed_ctr, 8, 8);
			}
			break;

		case VEH_AIRCRAFT:
			switch (variable - 0x80) {
				case 0x62: return MapAircraftMovementState(v);  // Current movement state
				case 0x63: return v->u.air.targetairport;       // Airport to which the action refers
				case 0x66: return MapAircraftMovementAction(v); // Current movement action
			}
			break;

		default: break;
	}

	DEBUG(grf, 1, "Unhandled vehicle property 0x%X, type 0x%X", variable, v->type);

	*available = false;
	return UINT_MAX;
}


static const SpriteGroup *VehicleResolveReal(const ResolverObject *object, const SpriteGroup *group)
{
	const Vehicle *v = object->u.vehicle.self;

	if (v == NULL) {
		if (group->g.real.num_loading > 0) return group->g.real.loading[0];
		if (group->g.real.num_loaded  > 0) return group->g.real.loaded[0];
		return NULL;
	}

	bool in_motion = !v->First()->current_order.IsType(OT_LOADING);

	uint totalsets = in_motion ? group->g.real.num_loaded : group->g.real.num_loading;

	uint set = (v->cargo.Count() * totalsets) / max((uint16)1, v->cargo_cap);
	set = min(set, totalsets - 1);

	return in_motion ? group->g.real.loaded[set] : group->g.real.loading[set];
}


static inline void NewVehicleResolver(ResolverObject *res, EngineID engine_type, const Vehicle *v)
{
	res->GetRandomBits = &VehicleGetRandomBits;
	res->GetTriggers   = &VehicleGetTriggers;
	res->SetTriggers   = &VehicleSetTriggers;
	res->GetVariable   = &VehicleGetVariable;
	res->ResolveReal   = &VehicleResolveReal;

	res->u.vehicle.self   = v;
	res->u.vehicle.parent = (v != NULL) ? v->First() : v;

	res->u.vehicle.self_type = engine_type;

	res->info_view = false;

	res->callback        = CBID_NO_CALLBACK;
	res->callback_param1 = 0;
	res->callback_param2 = 0;
	res->last_value      = 0;
	res->trigger         = 0;
	res->reseed          = 0;
	res->count           = 0;

	const Engine *e = GetEngine(engine_type);
	res->grffile         = (e != NULL ? e->grffile : NULL);
}


/** Retrieve the SpriteGroup for the specified vehicle.
 * If the vehicle is not specified, the purchase list group for the engine is
 * chosen. For trains, an additional engine override lookup is performed.
 * @param engine    Engine type of the vehicle.
 * @param v         The vehicle itself.
 * @param use_cache Use cached override
 * @returns         The selected SpriteGroup for the vehicle.
 */
static const SpriteGroup *GetVehicleSpriteGroup(EngineID engine, const Vehicle *v, bool use_cache = true)
{
	const SpriteGroup *group;
	CargoID cargo;

	if (v == NULL) {
		cargo = CT_PURCHASE;
	} else {
		cargo = v->cargo_type;

		if (v->type == VEH_TRAIN) {
			/* We always use cached value, except for callbacks because the override spriteset
			 * to use may be different than the one cached. It happens for callback 0x15 (refit engine),
			 * as v->cargo_type is temporary changed to the new type */
			group = use_cache ? v->u.rail.cached_override : GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->u.rail.first_engine);
			if (group != NULL) return group;
		} else if (v->type == VEH_ROAD) {
			group = GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->u.road.first_engine);
			if (group != NULL) return group;
		}
	}

	const Engine *e = GetEngine(engine);

	assert(cargo < lengthof(e->group));
	group = e->group[cargo];
	if (group != NULL) return group;

	/* Fall back to the default set if the selected cargo type is not defined */
	return e->group[CT_DEFAULT];
}


SpriteID GetCustomEngineSprite(EngineID engine, const Vehicle *v, Direction direction)
{
	const SpriteGroup *group;
	ResolverObject object;

	NewVehicleResolver(&object, engine, v);

	group = Resolve(GetVehicleSpriteGroup(engine, v), &object);
	if (group == NULL || group->type != SGT_RESULT || group->g.result.num_sprites == 0) return 0;

	return group->g.result.sprite + (direction % group->g.result.num_sprites);
}


SpriteID GetRotorOverrideSprite(EngineID engine, const Vehicle *v, bool info_view)
{
	const Engine *e = GetEngine(engine);

	/* Only valid for helicopters */
	assert(e->type == VEH_AIRCRAFT);
	assert(!(e->u.air.subtype & AIR_CTOL));

	ResolverObject object;

	NewVehicleResolver(&object, engine, v);

	object.info_view = info_view;

	const SpriteGroup *group = GetWagonOverrideSpriteSet(engine, CT_DEFAULT, engine);
	group = Resolve(group, &object);

	if (group == NULL || group->type != SGT_RESULT || group->g.result.num_sprites == 0) return 0;

	if (v == NULL) return group->g.result.sprite;

	return group->g.result.sprite + (info_view ? 0 : (v->Next()->Next()->u.air.state % group->g.result.num_sprites));
}


/**
 * Check if a wagon is currently using a wagon override
 * @param v The wagon to check
 * @return true if it is using an override, false otherwise
 */
bool UsesWagonOverride(const Vehicle *v)
{
	assert(v->type == VEH_TRAIN);
	return v->u.rail.cached_override != NULL;
}

/**
 * Evaluate a newgrf callback for vehicles
 * @param callback The callback to evalute
 * @param param1   First parameter of the callback
 * @param param2   Second parameter of the callback
 * @param engine   Engine type of the vehicle to evaluate the callback for
 * @param v        The vehicle to evaluate the callback for, or NULL if it doesnt exist yet
 * @return The value the callback returned, or CALLBACK_FAILED if it failed
 */
uint16 GetVehicleCallback(CallbackID callback, uint32 param1, uint32 param2, EngineID engine, const Vehicle *v)
{
	const SpriteGroup *group;
	ResolverObject object;

	NewVehicleResolver(&object, engine, v);

	object.callback        = callback;
	object.callback_param1 = param1;
	object.callback_param2 = param2;

	group = Resolve(GetVehicleSpriteGroup(engine, v, false), &object);
	if (group == NULL || group->type != SGT_CALLBACK) return CALLBACK_FAILED;

	return group->g.callback.result;
}

/**
 * Evaluate a newgrf callback for vehicles with a different vehicle for parent scope.
 * @param callback The callback to evalute
 * @param param1   First parameter of the callback
 * @param param2   Second parameter of the callback
 * @param engine   Engine type of the vehicle to evaluate the callback for
 * @param v        The vehicle to evaluate the callback for, or NULL if it doesnt exist yet
 * @param parent   The vehicle to use for parent scope
 * @return The value the callback returned, or CALLBACK_FAILED if it failed
 */
uint16 GetVehicleCallbackParent(CallbackID callback, uint32 param1, uint32 param2, EngineID engine, const Vehicle *v, const Vehicle *parent)
{
	const SpriteGroup *group;
	ResolverObject object;

	NewVehicleResolver(&object, engine, v);

	object.callback        = callback;
	object.callback_param1 = param1;
	object.callback_param2 = param2;

	object.u.vehicle.parent = parent;

	group = Resolve(GetVehicleSpriteGroup(engine, v, false), &object);
	if (group == NULL || group->type != SGT_CALLBACK) return CALLBACK_FAILED;

	return group->g.callback.result;
}


/* Callback 36 handlers */
uint GetVehicleProperty(const Vehicle *v, uint8 property, uint orig_value)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_MODIFY_PROPERTY, property, 0, v->engine_type, v);
	if (callback != CALLBACK_FAILED) return callback;

	return orig_value;
}


uint GetEngineProperty(EngineID engine, uint8 property, uint orig_value)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_MODIFY_PROPERTY, property, 0, engine, NULL);
	if (callback != CALLBACK_FAILED) return callback;

	return orig_value;
}


static void DoTriggerVehicle(Vehicle *v, VehicleTrigger trigger, byte base_random_bits, bool first)
{
	const SpriteGroup *group;
	ResolverObject object;
	byte new_random_bits;

	/* We can't trigger a non-existent vehicle... */
	assert(v != NULL);

	NewVehicleResolver(&object, v->engine_type, v);
	object.callback = CBID_RANDOM_TRIGGER;
	object.trigger = trigger;

	group = Resolve(GetVehicleSpriteGroup(v->engine_type, v), &object);
	if (group == NULL) return;

	new_random_bits = Random();
	v->random_bits &= ~object.reseed;
	v->random_bits |= (first ? new_random_bits : base_random_bits) & object.reseed;

	switch (trigger) {
		case VEHICLE_TRIGGER_NEW_CARGO:
			/* All vehicles in chain get ANY_NEW_CARGO trigger now.
			 * So we call it for the first one and they will recurse.
			 * Indexing part of vehicle random bits needs to be
			 * same for all triggered vehicles in the chain (to get
			 * all the random-cargo wagons carry the same cargo,
			 * i.e.), so we give them all the NEW_CARGO triggered
			 * vehicle's portion of random bits. */
			assert(first);
			DoTriggerVehicle(v->First(), VEHICLE_TRIGGER_ANY_NEW_CARGO, new_random_bits, false);
			break;

		case VEHICLE_TRIGGER_DEPOT:
			/* We now trigger the next vehicle in chain recursively.
			 * The random bits portions may be different for each
			 * vehicle in chain. */
			if (v->Next() != NULL) DoTriggerVehicle(v->Next(), trigger, 0, true);
			break;

		case VEHICLE_TRIGGER_EMPTY:
			/* We now trigger the next vehicle in chain
			 * recursively.  The random bits portions must be same
			 * for each vehicle in chain, so we give them all
			 * first chained vehicle's portion of random bits. */
			if (v->Next() != NULL) DoTriggerVehicle(v->Next(), trigger, first ? new_random_bits : base_random_bits, false);
			break;

		case VEHICLE_TRIGGER_ANY_NEW_CARGO:
			/* Now pass the trigger recursively to the next vehicle
			 * in chain. */
			assert(!first);
			if (v->Next() != NULL) DoTriggerVehicle(v->Next(), VEHICLE_TRIGGER_ANY_NEW_CARGO, base_random_bits, false);
			break;

		case VEHICLE_TRIGGER_CALLBACK_32:
			/* Do not do any recursion */
			break;
	}
}

void TriggerVehicle(Vehicle *v, VehicleTrigger trigger)
{
	if (trigger == VEHICLE_TRIGGER_DEPOT) {
		/* store that the vehicle entered a depot this tick */
		VehicleEnteredDepotThisTick(v);
	}

	DoTriggerVehicle(v, trigger, 0, true);
}

/* Functions for changing the order of vehicle purchase lists
 * This is currently only implemented for rail vehicles. */

/**
 * Get the list position of an engine.
 * Used when sorting a list of engines.
 * @param engine ID of the engine.
 * @return The list position of the engine.
 */
uint ListPositionOfEngine(EngineID engine)
{
	const Engine *e = GetEngine(engine);
	if (e->grffile == NULL) return e->list_position;

	/* Crude sorting to group by GRF ID */
	return (e->grffile->grfid * 256) + e->list_position;
}

struct ListOrderChange {
	EngineID engine;
	EngineID target;
};

static SmallVector<ListOrderChange, 16> _list_order_changes;

void AlterVehicleListOrder(EngineID engine, EngineID target)
{
	/* Add the list order change to a queue */
	ListOrderChange *loc = _list_order_changes.Append();
	loc->engine = engine;
	loc->target = target;
}

void CommitVehicleListOrderChanges()
{
	/* List position to Engine map */
	typedef SmallMap<uint16, Engine *, 16> ListPositionMap;
	ListPositionMap lptr_map;

	const ListOrderChange *end = _list_order_changes.End();
	for (const ListOrderChange *it = _list_order_changes.Begin(); it != end; ++it) {
		EngineID engine = it->engine;
		EngineID target = it->target;

		if (engine == target) continue;

		Engine *source_e = GetEngine(engine);
		Engine *target_e = NULL;

		/* Populate map with current list positions */
		Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, source_e->type) {
			if (!_settings_game.vehicle.dynamic_engines || e->grffile == source_e->grffile) {
				if (e->internal_id == target) target_e = e;
				lptr_map[e->list_position] = e;
			}
		}

		/* std::map sorted by default, SmallMap does not */
		lptr_map.SortByKey();

		/* Get the target position, if it exists */
		if (target_e != NULL) {
			uint16 target_position = target_e->list_position;

			bool moving = false;
			const ListPositionMap::Pair *end = lptr_map.End();
			for (ListPositionMap::Pair *it = lptr_map.Begin(); it != end; ++it) {
				if (it->first == target_position) moving = true;
				if (moving) it->second->list_position++;
			}

			source_e->list_position = target_position;
		}

		lptr_map.Clear();
	}

	/* Clear out the queue */
	_list_order_changes.Reset();
}
