/* $Id$ */

/** @file town.h Base of the town class. */

#ifndef TOWN_H
#define TOWN_H

#include "oldpool.h"
#include "core/bitmath_func.hpp"
#include "core/random_func.hpp"
#include "cargo_type.h"
#include "tile_type.h"
#include "date_type.h"
#include "town_type.h"
#include "company_type.h"
#include "settings_type.h"
#include "strings_type.h"
#include "viewport_type.h"
#include "economy_type.h"
#include "map_type.h"
#include "command_type.h"

enum {
	HOUSE_NO_CLASS   = 0,
	NEW_HOUSE_OFFSET = 110,
	HOUSE_MAX        = 512,
	INVALID_TOWN     = 0xFFFF,
	INVALID_HOUSE_ID = 0xFFFF,

	/* There can only be as many classes as there are new houses, plus one for
	 * NO_CLASS, as the original houses don't have classes. */
	HOUSE_CLASS_MAX  = HOUSE_MAX - NEW_HOUSE_OFFSET + 1,
};

enum BuildingFlags {
	TILE_NO_FLAG         =       0,
	TILE_SIZE_1x1        = 1U << 0,
	TILE_NOT_SLOPED      = 1U << 1,
	TILE_SIZE_2x1        = 1U << 2,
	TILE_SIZE_1x2        = 1U << 3,
	TILE_SIZE_2x2        = 1U << 4,
	BUILDING_IS_ANIMATED = 1U << 5,
	BUILDING_IS_CHURCH   = 1U << 6,
	BUILDING_IS_STADIUM  = 1U << 7,
	BUILDING_HAS_1_TILE  = TILE_SIZE_1x1 | TILE_SIZE_2x1 | TILE_SIZE_1x2 | TILE_SIZE_2x2,
	BUILDING_2_TILES_X   = TILE_SIZE_2x1 | TILE_SIZE_2x2,
	BUILDING_2_TILES_Y   = TILE_SIZE_1x2 | TILE_SIZE_2x2,
	BUILDING_HAS_4_TILES = TILE_SIZE_2x2,
};

DECLARE_ENUM_AS_BIT_SET(BuildingFlags)

enum HouseZonesBits {
	HZB_BEGIN     = 0,
	HZB_TOWN_EDGE = 0,
	HZB_TOWN_OUTSKIRT,
	HZB_TOWN_OUTER_SUBURB,
	HZB_TOWN_INNER_SUBURB,
	HZB_TOWN_CENTRE,
	HZB_END,
};
assert_compile(HZB_END == 5);

DECLARE_POSTFIX_INCREMENT(HouseZonesBits)

enum HouseZones {                  ///< Bit  Value       Meaning
	HZ_NOZNS             = 0x0000,  ///<       0          This is just to get rid of zeros, meaning none
	HZ_ZON1              = 1U << HZB_TOWN_EDGE,    ///< 0..4 1,2,4,8,10  which town zones the building can be built in, Zone1 been the further suburb
	HZ_ZON2              = 1U << HZB_TOWN_OUTSKIRT,
	HZ_ZON3              = 1U << HZB_TOWN_OUTER_SUBURB,
	HZ_ZON4              = 1U << HZB_TOWN_INNER_SUBURB,
	HZ_ZON5              = 1U << HZB_TOWN_CENTRE,  ///<  center of town
	HZ_ZONALL            = 0x001F,  ///<       1F         This is just to englobe all above types at once
	HZ_SUBARTC_ABOVE     = 0x0800,  ///< 11    800        can appear in sub-arctic climate above the snow line
	HZ_TEMP              = 0x1000,  ///< 12   1000        can appear in temperate climate
	HZ_SUBARTC_BELOW     = 0x2000,  ///< 13   2000        can appear in sub-arctic climate below the snow line
	HZ_SUBTROPIC         = 0x4000,  ///< 14   4000        can appear in subtropical climate
	HZ_TOYLND            = 0x8000   ///< 15   8000        can appear in toyland climate
};

DECLARE_ENUM_AS_BIT_SET(HouseZones)

enum HouseExtraFlags {
	NO_EXTRA_FLAG            =       0,
	BUILDING_IS_HISTORICAL   = 1U << 0,  ///< this house will only appear during town generation in random games, thus the historical
	BUILDING_IS_PROTECTED    = 1U << 1,  ///< towns and AI will not remove this house, while human players will be able to
	SYNCHRONISED_CALLBACK_1B = 1U << 2,  ///< synchronized callback 1B will be performed, on multi tile houses
	CALLBACK_1A_RANDOM_BITS  = 1U << 3,  ///< callback 1A needs random bits
};

DECLARE_ENUM_AS_BIT_SET(HouseExtraFlags)

template <typename T>
struct BuildingCounts {
	T id_count[HOUSE_MAX];
	T class_count[HOUSE_CLASS_MAX];
};

static const uint CUSTOM_TOWN_NUMBER_DIFFICULTY  = 4; ///< value for custom town number in difficulty settings
static const uint CUSTOM_TOWN_MAX_NUMBER = 5000;  ///< this is the maximum number of towns a user can specify in customisation

DECLARE_OLD_POOL(Town, Town, 3, 8000)

struct Town : PoolItem<Town, TownID, &_Town_pool> {
	TileIndex xy;

	/* Current population of people and amount of houses. */
	uint32 num_houses;
	uint32 population;

	/* Town name */
	uint32 townnamegrfid;
	uint16 townnametype;
	uint32 townnameparts;
	char *name;

	/* NOSAVE: Location of name sign, UpdateTownVirtCoord updates this. */
	ViewportSign sign;

	/* Makes sure we don't build certain house types twice.
	 * bit 0 = Building funds received
	 * bit 1 = CHURCH
	 * bit 2 = STADIUM */
	byte flags12;

	/* level of noise that all the airports are generating */
	uint16 noise_reached;

	/* Which companies have a statue? */
	CompanyMask statues;

	/* Company ratings as well as a mask that determines which companies have a rating. */
	CompanyMask have_ratings;
	uint8 unwanted[MAX_COMPANIES]; ///< how many months companies aren't wanted by towns (bribe)
	CompanyByte exclusivity;       ///< which company has exclusivity
	uint8 exclusive_counter;       ///< months till the exclusivity expires
	int16 ratings[MAX_COMPANIES];

	/* Maximum amount of passengers and mail that can be transported. */
	uint32 max_pass;
	uint32 max_mail;
	uint32 new_max_pass;
	uint32 new_max_mail;
	uint32 act_pass;
	uint32 act_mail;
	uint32 new_act_pass;
	uint32 new_act_mail;

	/* Amount of passengers that were transported. */
	byte pct_pass_transported;
	byte pct_mail_transported;

	/* Amount of food and paper that was transported. Actually a bit mask would be enough. */
	uint16 act_food;
	uint16 act_water;
	uint16 new_act_food;
	uint16 new_act_water;

	/* Time until we rebuild a house. */
	uint16 time_until_rebuild;

	/* When to grow town next time. */
	uint16 grow_counter;
	int16 growth_rate;

	/* Fund buildings program in action? */
	byte fund_buildings_months;

	/* Fund road reconstruction in action? */
	byte road_build_months;

	/* If this is a larger town, and should grow more quickly. */
	bool larger_town;
	TownLayoutByte layout; ///< town specific road layout

	/* NOSAVE: UpdateTownRadius updates this given the house count. */
	uint32 squared_town_zone_radius[HZB_END];

	/* NOSAVE: The number of each type of building in the town. */
	BuildingCounts<uint16> building_counts;

	/**
	 * Creates a new town
	 */
	Town(TileIndex tile = INVALID_TILE);

	/** Destroy the town */
	~Town();

	inline bool IsValid() const { return this->xy != INVALID_TILE; }

	void InitializeLayout(TownLayout layout);

	/** Calculate the max town noise
	 * The value is counted using the population divided by the content of the
	 * entry in town_noise_population corespondig to the town's tolerance.
	 * To this result, we add 3, which is the noise of the lowest airport.
	 * So user can at least buld that airport
	 * @return the maximum noise level the town will tolerate */
	inline uint16 MaxTownNoise() const
	{
		if (this->population == 0) return 0; // no population? no noise

		return ((this->population / _settings_game.economy.town_noise_population[_settings_game.difficulty.town_council_tolerance]) + 3);
	}
};

struct HouseSpec {
	/* Standard properties */
	Year min_year;                     ///< introduction year of the house
	Year max_year;                     ///< last year it can be built
	byte population;                   ///< population (Zero on other tiles in multi tile house.)
	byte removal_cost;                 ///< cost multiplier for removing it
	StringID building_name;            ///< building name
	uint16 remove_rating_decrease;     ///< rating decrease if removed
	byte mail_generation;              ///< mail generation multiplier (tile based, as the acceptances below)
	byte cargo_acceptance[3];          ///< acceptance level for the cargo slots
	CargoID accepts_cargo[3];          ///< 3 input cargo slots
	BuildingFlags building_flags;      ///< some flags that describe the house (size, stadium etc...)
	HouseZones building_availability;  ///< where can it be built (climates, zones)
	bool enabled;                      ///< the house is available to build (true by default, but can be disabled by newgrf)

	/* NewHouses properties */
	HouseID substitute_id;             ///< which original house this one is based on
	struct SpriteGroup *spritegroup;   ///< pointer to the different sprites of the house
	HouseID override;                  ///< which house this one replaces
	uint16 callback_mask;              ///< House callback flags
	byte random_colour[4];             ///< 4 "random" colours
	byte probability;                  ///< Relative probability of appearing (16 is the standard value)
	HouseExtraFlags extra_flags;       ///< some more flags
	HouseClassID class_id;             ///< defines the class this house has (grf file based) @See HouseGetVariable, prop 0x44
	byte animation_frames;             ///< number of animation frames
	byte animation_speed;              ///< amount of time between each of those frames
	byte processing_time;              ///< Periodic refresh multiplier
	byte minimum_life;                 ///< The minimum number of years this house will survive before the town rebuilds it

	/* grf file related properties*/
	uint8 local_id;                    ///< id defined by the grf file for this house
	const struct GRFFile *grffile;     ///< grf file that introduced this house

	/**
	 * Get the cost for removing this house
	 * @return the cost (inflation corrected etc)
	 */
	Money GetRemovalCost() const;

};

extern HouseSpec _house_specs[HOUSE_MAX];

uint32 GetWorldPopulation();

void UpdateTownVirtCoord(Town *t);
void UpdateAllTownVirtCoords();
void InitializeTown();
void ShowTownViewWindow(TownID town);
void ExpandTown(Town *t);
Town *CreateRandomTown(uint attempts, TownSize size, bool city, TownLayout layout);

enum {
	ROAD_REMOVE         = 0,
	UNMOVEABLE_REMOVE   = 1,
	TUNNELBRIDGE_REMOVE = 1,
	INDUSTRY_REMOVE     = 2
};

/** This is the number of ticks between towns being processed for building new
 * houses or roads. This value originally came from the size of the town array
 * in TTD. */
static const byte TOWN_GROWTH_FREQUENCY = 70;

/** Simple value that indicates the house has reached the final stage of
 * construction. */
static const byte TOWN_HOUSE_COMPLETED = 3;

/** This enum is used in conjonction with town->flags12.
 * IT simply states what bit is used for.
 * It is pretty unrealistic (IMHO) to only have one church/stadium
 * per town, NO MATTER the population of it.
 * And there are 5 more bits available on flags12...
 */
enum {
	TOWN_IS_FUNDED      = 0,   ///< Town has received some funds for
	TOWN_HAS_CHURCH     = 1,   ///< There can be only one church by town.
	TOWN_HAS_STADIUM    = 2    ///< There can be only one stadium by town.
};

bool CheckforTownRating(DoCommandFlag flags, Town *t, byte type);

static inline HouseSpec *GetHouseSpecs(HouseID house_id)
{
	assert(house_id < HOUSE_MAX);
	return &_house_specs[house_id];
}

TileIndexDiff GetHouseNorthPart(HouseID &house);

/**
 * Check if a TownID is valid.
 * @param index to inquiry in the pool of town
 * @return true if it exists
 */
static inline bool IsValidTownID(TownID index)
{
	return index < GetTownPoolSize() && GetTown(index)->IsValid();
}

static inline TownID GetMaxTownIndex()
{
	/* TODO - This isn't the real content of the function, but
	 *  with the new pool-system this will be replaced with one that
	 *  _really_ returns the highest index. Now it just returns
	 *  the next safe value we are sure about everything is below.
	 */
	return GetTownPoolSize() - 1;
}

static inline uint GetNumTowns()
{
	extern uint _total_towns;

	return _total_towns;
}

/**
 * Return a random valid town.
 */
static inline Town *GetRandomTown()
{
	int num = RandomRange(GetNumTowns());
	TownID index = INVALID_TOWN;

	while (num >= 0) {
		num--;

		index++;
		/* Make sure we have a valid town */
		while (!IsValidTownID(index)) {
			index++;
			assert(index <= GetMaxTownIndex());
		}
	}

	return GetTown(index);
}

Town *CalcClosestTownFromTile(TileIndex tile, uint threshold = UINT_MAX);

#define FOR_ALL_TOWNS_FROM(t, start) for (t = GetTown(start); t != NULL; t = (t->index + 1U < GetTownPoolSize()) ? GetTown(t->index + 1U) : NULL) if (t->IsValid())
#define FOR_ALL_TOWNS(t) FOR_ALL_TOWNS_FROM(t, 0)

extern Town *_cleared_town;
extern int _cleared_town_rating;
extern uint32 _cur_town_ctr;
extern uint32 _cur_town_iter;

void ResetHouses();

void ClearTownHouse(Town *t, TileIndex tile);
void UpdateTownMaxPass(Town *t);
void UpdateTownRadius(Town *t);
bool CheckIfAuthorityAllowsNewStation(TileIndex tile, DoCommandFlag flags);
Town *ClosestTownFromTile(TileIndex tile, uint threshold);
void ChangeTownRating(Town *t, int add, int max, DoCommandFlag flags);
HouseZonesBits GetTownRadiusGroup(const Town *t, TileIndex tile);
void SetTownRatingTestMode(bool mode);
uint GetMaskOfTownActions(int *nump, CompanyID cid, const Town *t);
bool GenerateTowns(TownLayout layout);
bool GenerateTownName(uint32 *townnameparts);

/**
 * Calculate a hash value from a tile position
 *
 * @param x The X coordinate
 * @param y The Y coordinate
 * @return The hash of the tile
 */
static inline uint TileHash(uint x, uint y)
{
	uint hash = x >> 4;
	hash ^= x >> 6;
	hash ^= y >> 4;
	hash -= y >> 6;
	return hash;
}

/**
 * Get the last two bits of the TileHash
 *  from a tile position.
 *
 * @see TileHash()
 * @param x The X coordinate
 * @param y The Y coordinate
 * @return The last two bits from hash of the tile
 */
static inline uint TileHash2Bit(uint x, uint y)
{
	return GB(TileHash(x, y), 0, 2);
}

#endif /* TOWN_H */
