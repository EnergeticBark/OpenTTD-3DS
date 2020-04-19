/* $Id$ */

/** @file group_cmd.cpp Handling of the engine groups */

#include "stdafx.h"
#include "command_func.h"
#include "group.h"
#include "train.h"
#include "engine_base.h"
#include "vehicle_gui.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "autoreplace_base.h"
#include "autoreplace_func.h"
#include "string_func.h"
#include "company_func.h"
#include "oldpool_func.h"
#include "core/alloc_func.hpp"

#include "table/strings.h"

GroupID _new_group_id;

/**
 * Update the num engines of a groupID. Decrease the old one and increase the new one
 * @note called in SetTrainGroupID and UpdateTrainGroupID
 * @param i     EngineID we have to update
 * @param old_g index of the old group
 * @param new_g index of the new group
 */
static inline void UpdateNumEngineGroup(EngineID i, GroupID old_g, GroupID new_g)
{
	if (old_g != new_g) {
		/* Decrease the num engines of EngineID i of the old group if it's not the default one */
		if (!IsDefaultGroupID(old_g) && IsValidGroupID(old_g)) GetGroup(old_g)->num_engines[i]--;

		/* Increase the num engines of EngineID i of the new group if it's not the default one */
		if (!IsDefaultGroupID(new_g) && IsValidGroupID(new_g)) GetGroup(new_g)->num_engines[i]++;
	}
}


DEFINE_OLD_POOL_GENERIC(Group, Group)


Group::Group(Owner owner)
{
	this->owner = owner;

	if (this->IsValid()) this->num_engines = CallocT<uint16>(GetEnginePoolSize());
}

Group::~Group()
{
	free(this->name);
	this->owner = INVALID_OWNER;
	free(this->num_engines);
}

bool Group::IsValid() const
{
	return this->owner != INVALID_OWNER;
}

void InitializeGroup(void)
{
	_Group_pool.CleanPool();
	_Group_pool.AddBlockToPool();
}


/**
 * Create a new vehicle group.
 * @param tile unused
 * @param p1   vehicle type
 * @param p2   unused
 */
CommandCost CmdCreateGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleType vt = (VehicleType)p1;
	if (!IsCompanyBuildableVehicleType(vt)) return CMD_ERROR;

	if (!Group::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Group *g = new Group(_current_company);
		g->replace_protection = false;
		g->vehicle_type = vt;

		_new_group_id = g->index;

		InvalidateWindowData(GetWindowClassForVehicleType(vt), (vt << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}


/**
 * Add all vehicles in the given group to the default group and then deletes the group.
 * @param tile unused
 * @param p1   index of array group
 *      - p1 bit 0-15 : GroupID
 * @param p2   unused
 */
CommandCost CmdDeleteGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!IsValidGroupID(p1)) return CMD_ERROR;

	Group *g = GetGroup(p1);
	if (g->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Vehicle *v;

		/* Add all vehicles belong to the group to the default group */
		FOR_ALL_VEHICLES(v) {
			if (v->group_id == g->index && v->type == g->vehicle_type) v->group_id = DEFAULT_GROUP;
		}

		/* Update backupped orders if needed */
		if (_backup_orders_data.group == g->index) _backup_orders_data.group = DEFAULT_GROUP;

		/* If we set an autoreplace for the group we delete, remove it. */
		if (_current_company < MAX_COMPANIES) {
			Company *c;
			EngineRenew *er;

			c = GetCompany(_current_company);
			FOR_ALL_ENGINE_RENEWS(er) {
				if (er->group_id == g->index) RemoveEngineReplacementForCompany(c, er->from, g->index, flags);
			}
		}

		VehicleType vt = g->vehicle_type;

		/* Delete the Replace Vehicle Windows */
		DeleteWindowById(WC_REPLACE_VEHICLE, g->vehicle_type);
		delete g;

		InvalidateWindowData(GetWindowClassForVehicleType(vt), (vt << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}

static bool IsUniqueGroupName(const char *name)
{
	const Group *g;

	FOR_ALL_GROUPS(g) {
		if (g->name != NULL && strcmp(g->name, name) == 0) return false;
	}

	return true;
}

/**
 * Rename a group
 * @param tile unused
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 * @param p2   unused
 */
CommandCost CmdRenameGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!IsValidGroupID(p1)) return CMD_ERROR;

	Group *g = GetGroup(p1);
	if (g->owner != _current_company) return CMD_ERROR;

	bool reset = StrEmpty(text);

	if (!reset) {
		if (strlen(text) >= MAX_LENGTH_GROUP_NAME_BYTES) return CMD_ERROR;
		if (!IsUniqueGroupName(text)) return_cmd_error(STR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		/* Delete the old name */
		free(g->name);
		/* Assign the new one */
		g->name = reset ? NULL : strdup(text);

		InvalidateWindowData(GetWindowClassForVehicleType(g->vehicle_type), (g->vehicle_type << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}


/**
 * Add a vehicle to a group
 * @param tile unused
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 * @param p2   vehicle to add to a group
 *   - p2 bit 0-15 : VehicleID
 */
CommandCost CmdAddVehicleGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	GroupID new_g = p1;

	if (!IsValidVehicleID(p2) || (!IsValidGroupID(new_g) && !IsDefaultGroupID(new_g))) return CMD_ERROR;

	Vehicle *v = GetVehicle(p2);

	if (IsValidGroupID(new_g)) {
		Group *g = GetGroup(new_g);
		if (g->owner != _current_company || g->vehicle_type != v->type) return CMD_ERROR;
	}

	if (v->owner != _current_company || !v->IsPrimaryVehicle()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DecreaseGroupNumVehicle(v->group_id);
		IncreaseGroupNumVehicle(new_g);

		switch (v->type) {
			default: NOT_REACHED();
			case VEH_TRAIN:
				SetTrainGroupID(v, new_g);
				break;
			case VEH_ROAD:
			case VEH_SHIP:
			case VEH_AIRCRAFT:
				if (IsEngineCountable(v)) UpdateNumEngineGroup(v->engine_type, v->group_id, new_g);
				v->group_id = new_g;
				break;
		}

		/* Update the Replace Vehicle Windows */
		InvalidateWindow(WC_REPLACE_VEHICLE, v->type);
		InvalidateWindowData(GetWindowClassForVehicleType(v->type), (v->type << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}

/**
 * Add all shared vehicles of all vehicles from a group
 * @param tile unused
 * @param p1   index of group array
 *  - p1 bit 0-15 : GroupID
 * @param p2   type of vehicles
 */
CommandCost CmdAddSharedVehicleGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleType type = (VehicleType)p2;
	if (!IsValidGroupID(p1) || !IsCompanyBuildableVehicleType(type)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Vehicle *v;
		VehicleType type = (VehicleType)p2;
		GroupID id_g = p1;

		/* Find the first front engine which belong to the group id_g
		 * then add all shared vehicles of this front engine to the group id_g */
		FOR_ALL_VEHICLES(v) {
			if (v->type == type && v->IsPrimaryVehicle()) {
				if (v->group_id != id_g) continue;

				/* For each shared vehicles add it to the group */
				for (Vehicle *v2 = v->FirstShared(); v2 != NULL; v2 = v2->NextShared()) {
					if (v2->group_id != id_g) CmdAddVehicleGroup(tile, flags, id_g, v2->index, text);
				}
			}
		}

		InvalidateWindowData(GetWindowClassForVehicleType(type), (type << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}


/**
 * Remove all vehicles from a group
 * @param tile unused
 * @param p1   index of group array
 * - p1 bit 0-15 : GroupID
 * @param p2   type of vehicles
 */
CommandCost CmdRemoveAllVehiclesGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleType type = (VehicleType)p2;
	if (!IsValidGroupID(p1) || !IsCompanyBuildableVehicleType(type)) return CMD_ERROR;

	Group *g = GetGroup(p1);
	if (g->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		GroupID old_g = p1;
		Vehicle *v;

		/* Find each Vehicle that belongs to the group old_g and add it to the default group */
		FOR_ALL_VEHICLES(v) {
			if (v->type == type && v->IsPrimaryVehicle()) {
				if (v->group_id != old_g) continue;

				/* Add The Vehicle to the default group */
				CmdAddVehicleGroup(tile, flags, DEFAULT_GROUP, v->index, text);
			}
		}

		InvalidateWindowData(GetWindowClassForVehicleType(type), (type << 11) | VLW_GROUP_LIST | _current_company);
	}

	return CommandCost();
}


/**
 * (Un)set global replace protection from a group
 * @param tile unused
 * @param p1   index of group array
 * - p1 bit 0-15 : GroupID
 * @param p2
 * - p2 bit 0    : 1 to set or 0 to clear protection.
 */
CommandCost CmdSetGroupReplaceProtection(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!IsValidGroupID(p1)) return CMD_ERROR;

	Group *g = GetGroup(p1);
	if (g->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		g->replace_protection = HasBit(p2, 0);

		InvalidateWindowData(GetWindowClassForVehicleType(g->vehicle_type), (g->vehicle_type << 11) | VLW_GROUP_LIST | _current_company);
		InvalidateWindowData(WC_REPLACE_VEHICLE, g->vehicle_type);
	}

	return CommandCost();
}

/**
 * Decrease the num_vehicle variable before delete an front engine from a group
 * @note Called in CmdSellRailWagon and DeleteLasWagon,
 * @param v     FrontEngine of the train we want to remove.
 */
void RemoveVehicleFromGroup(const Vehicle *v)
{
	if (!v->IsValid() || !v->IsPrimaryVehicle()) return;

	if (!IsDefaultGroupID(v->group_id)) DecreaseGroupNumVehicle(v->group_id);
}


/**
 * Affect the groupID of a train to new_g.
 * @note called in CmdAddVehicleGroup and CmdMoveRailVehicle
 * @param v     First vehicle of the chain.
 * @param new_g index of array group
 */
void SetTrainGroupID(Vehicle *v, GroupID new_g)
{
	if (!IsValidGroupID(new_g) && !IsDefaultGroupID(new_g)) return;

	assert(v->IsValid() && v->type == VEH_TRAIN && IsFrontEngine(v));

	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		if (IsEngineCountable(u)) UpdateNumEngineGroup(u->engine_type, u->group_id, new_g);

		u->group_id = new_g;
	}

	/* Update the Replace Vehicle Windows */
	InvalidateWindow(WC_REPLACE_VEHICLE, VEH_TRAIN);
}


/**
 * Recalculates the groupID of a train. Should be called each time a vehicle is added
 * to/removed from the chain,.
 * @note this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @note Called in CmdBuildRailVehicle, CmdBuildRailWagon, CmdMoveRailVehicle, CmdSellRailWagon
 * @param v First vehicle of the chain.
 */
void UpdateTrainGroupID(Vehicle *v)
{
	assert(v->IsValid() && v->type == VEH_TRAIN && (IsFrontEngine(v) || IsFreeWagon(v)));

	GroupID new_g = IsFrontEngine(v) ? v->group_id : (GroupID)DEFAULT_GROUP;
	for (Vehicle *u = v; u != NULL; u = u->Next()) {
		if (IsEngineCountable(u)) UpdateNumEngineGroup(u->engine_type, u->group_id, new_g);

		u->group_id = new_g;
	}

	/* Update the Replace Vehicle Windows */
	InvalidateWindow(WC_REPLACE_VEHICLE, VEH_TRAIN);
}

uint GetGroupNumEngines(CompanyID company, GroupID id_g, EngineID id_e)
{
	if (IsValidGroupID(id_g)) return GetGroup(id_g)->num_engines[id_e];

	uint num = GetCompany(company)->num_engines[id_e];
	if (!IsDefaultGroupID(id_g)) return num;

	const Group *g;
	FOR_ALL_GROUPS(g) {
		if (g->owner == company) num -= g->num_engines[id_e];
	}
	return num;
}

void RemoveAllGroupsForCompany(const CompanyID company)
{
	Group *g;

	FOR_ALL_GROUPS(g) {
		if (company == g->owner) delete g;
	}
}
