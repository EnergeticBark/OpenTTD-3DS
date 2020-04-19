/* $Id$ */

/** @file group.h Base class from groups. */

#ifndef GROUP_H
#define GROUP_H

#include "group_type.h"
#include "oldpool.h"
#include "company_type.h"
#include "vehicle_type.h"
#include "engine_type.h"

DECLARE_OLD_POOL(Group, Group, 5, 2047)

struct Group : PoolItem<Group, GroupID, &_Group_pool> {
	char *name;                             ///< Group Name

	uint16 num_vehicle;                     ///< Number of vehicles wich belong to the group
	OwnerByte owner;                        ///< Group Owner
	VehicleTypeByte vehicle_type;           ///< Vehicle type of the group

	bool replace_protection;                ///< If set to true, the global autoreplace have no effect on the group
	uint16 *num_engines;                    ///< Caches the number of engines of each type the company owns (no need to save this)

	Group(CompanyID owner = INVALID_COMPANY);
	virtual ~Group();

	bool IsValid() const;
};


static inline bool IsValidGroupID(GroupID index)
{
	return index < GetGroupPoolSize() && GetGroup(index)->IsValid();
}

static inline bool IsDefaultGroupID(GroupID index)
{
	return index == DEFAULT_GROUP;
}

/**
 * Checks if a GroupID stands for all vehicles of a company
 * @param id_g The GroupID to check
 * @return true is id_g is identical to ALL_GROUP
 */
static inline bool IsAllGroupID(GroupID id_g)
{
	return id_g == ALL_GROUP;
}

#define FOR_ALL_GROUPS_FROM(g, start) for (g = GetGroup(start); g != NULL; g = (g->index + 1U < GetGroupPoolSize()) ? GetGroup(g->index + 1) : NULL) if (g->IsValid())
#define FOR_ALL_GROUPS(g) FOR_ALL_GROUPS_FROM(g, 0)

/**
 * Get the current size of the GroupPool
 */
static inline uint GetGroupArraySize(void)
{
	const Group *g;
	uint num = 0;

	FOR_ALL_GROUPS(g) num++;

	return num;
}

/**
 * Get the number of engines with EngineID id_e in the group with GroupID
 * id_g
 * @param id_g The GroupID of the group used
 * @param id_e The EngineID of the engine to count
 * @return The number of engines with EngineID id_e in the group
 */
uint GetGroupNumEngines(CompanyID company, GroupID id_g, EngineID id_e);

static inline void IncreaseGroupNumVehicle(GroupID id_g)
{
	if (IsValidGroupID(id_g)) GetGroup(id_g)->num_vehicle++;
}

static inline void DecreaseGroupNumVehicle(GroupID id_g)
{
	if (IsValidGroupID(id_g)) GetGroup(id_g)->num_vehicle--;
}


void InitializeGroup();
void SetTrainGroupID(Vehicle *v, GroupID grp);
void UpdateTrainGroupID(Vehicle *v);
void RemoveVehicleFromGroup(const Vehicle *v);
void RemoveAllGroupsForCompany(const CompanyID company);

extern GroupID _new_group_id;

#endif /* GROUP_H */
