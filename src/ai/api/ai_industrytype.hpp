/* $Id$ */

/** @file ai_industrytype.hpp Everything to query and build industries. */

#ifndef AI_INDUSTRYTYPE_HPP
#define AI_INDUSTRYTYPE_HPP

#include "ai_object.hpp"
#include "ai_error.hpp"
#include "ai_list.hpp"

/**
 * Class that handles all industry-type related functions.
 */
class AIIndustryType : public AIObject {
public:
	static const char *GetClassName() { return "AIIndustryType"; }

	/**
	 * Checks whether the given industry-type is valid.
	 * @param industry_type The type check.
	 * @return True if and only if the industry-type is valid.
	 */
	static bool IsValidIndustryType(IndustryType industry_type);

	/**
	 * Get the name of an industry-type.
	 * @param industry_type The type to get the name for.
	 * @pre IsValidIndustryType(industry_type).
	 * @return The name of an industry.
	 */
	static char *GetName(IndustryType industry_type);

	/**
	 * Get a list of CargoID possible produced by this industry-type.
	 * @warning This function only returns the default cargos of the industry type.
	 *          Industries can specify new cargotypes on construction.
	 * @param industry_type The type to get the CargoIDs for.
	 * @pre IsValidIndustryType(industry_type).
	 * @return The CargoIDs of all cargotypes this industry could produce.
	 */
	static AIList *GetProducedCargo(IndustryType industry_type);

	/**
	 * Get a list of CargoID accepted by this industry-type.
	 * @warning This function only returns the default cargos of the industry type.
	 *          Industries can specify new cargotypes on construction.
	 * @param industry_type The type to get the CargoIDs for.
	 * @pre IsValidIndustryType(industry_type).
	 * @return The CargoIDs of all cargotypes this industry accepts.
	 */
	static AIList *GetAcceptedCargo(IndustryType industry_type);

	/**
	 * Is this industry type a raw industry?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True if it should be handled as a raw industry.
	 */
	static bool IsRawIndustry(IndustryType industry_type);

	/**
	 * Can the production of this industry increase?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True if the production of this industry can increase.
	 */
	static bool ProductionCanIncrease(IndustryType industry_type);

	/**
	 * Get the cost for building this industry-type.
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return The cost for building this industry-type.
	 */
	static Money GetConstructionCost(IndustryType industry_type);

	/**
	 * Can you build this type of industry?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True if you can build this type of industry at locations of your choice.
	 * @note Returns false if you can only prospect this type of industry, or not build it at all.
	 */
	static bool CanBuildIndustry(IndustryType industry_type);

	/**
	 * Can you prospect this type of industry?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True if you can prospect this type of industry.
	 * @note If the setting "Manual primary industry construction method" is set
	 * to either "None" or "as other industries" this function always returns false.
	 */
	static bool CanProspectIndustry(IndustryType industry_type);

	/**
	 * Build an industry of the specified type.
	 * @param industry_type The type of the industry to build.
	 * @param tile The tile to build the industry on.
	 * @pre CanBuildIndustry(industry_type).
	 * @return True if the industry was succesfully build.
	 */
	static bool BuildIndustry(IndustryType industry_type, TileIndex tile);

	/**
	 * Prospect an industry of this type. Prospecting an industries let the game try to create
	 * an industry on a random place on the map.
	 * @param industry_type The type of the industry.
	 * @pre CanProspectIndustry(industry_type).
	 * @return True if no error occured while trying to prospect.
	 * @note Even if true is returned there is no guarantee a new industry is build.
	 * @note If true is returned the money is paid, whether a new industry was build or not.
	 */
	static bool ProspectIndustry(IndustryType industry_type);

	/**
	 * Is this type of industry built on water.
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True when this type is built on water.
	 */
	static bool IsBuiltOnWater(IndustryType industry_type);

	/**
	 * Does this type of industry have a heliport?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True when this type has a heliport.
	 */
	static bool HasHeliport(IndustryType industry_type);

	/**
	 * Does this type of industry have a dock?
	 * @param industry_type The type of the industry.
	 * @pre IsValidIndustryType(industry_type).
	 * @return True when this type has a dock.
	 */
	static bool HasDock(IndustryType industry_type);
};

#endif /* AI_INDUSTRYTYPE_HPP */
