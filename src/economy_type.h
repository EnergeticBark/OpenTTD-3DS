/* $Id$ */

/** @file economy_type.h Types related to the economy. */

#ifndef ECONOMY_TYPE_H
#define ECONOMY_TYPE_H

#include "core/overflowsafe_type.hpp"
#include "core/enum_type.hpp"
#include "cargo_type.h"

typedef OverflowSafeInt64 Money;

struct Economy {
	Money max_loan;                       ///< Maximum possible loan
	Money max_loan_unround;               ///< Economy fluctuation status
	uint16 max_loan_unround_fract;        ///< Fraction of the unrounded max loan
	int16 fluct;
	byte interest_rate;                   ///< Interest
	byte infl_amount;                     ///< inflation amount
	byte infl_amount_pr;                  ///< inflation rate for payment rates
	uint32 industry_daily_change_counter; ///< Bits 31-16 are number of industry to be performed, 15-0 are fractional collected daily
	uint32 industry_daily_increment;      ///< The value which will increment industry_daily_change_counter. Computed value. NOSAVE
};

struct Subsidy {
	CargoID cargo_type;
	byte age;
	/* from and to can either be TownID, StationID or IndustryID */
	uint16 from;
	uint16 to;
};

enum ScoreID {
	SCORE_BEGIN      = 0,
	SCORE_VEHICLES   = 0,
	SCORE_STATIONS   = 1,
	SCORE_MIN_PROFIT = 2,
	SCORE_MIN_INCOME = 3,
	SCORE_MAX_INCOME = 4,
	SCORE_DELIVERED  = 5,
	SCORE_CARGO      = 6,
	SCORE_MONEY      = 7,
	SCORE_LOAN       = 8,
	SCORE_TOTAL      = 9,  ///< This must always be the last entry
	SCORE_END        = 10, ///< How many scores are there..

	SCORE_MAX = 1000       ///< The max score that can be in the performance history
	/* the scores together of score_info is allowed to be more! */
};
DECLARE_POSTFIX_INCREMENT(ScoreID);

struct ScoreInfo {
	byte id;    ///< Unique ID of the score
	int needed; ///< How much you need to get the perfect score
	int score;  ///< How much score it will give
};

struct Prices {
	Money station_value;
	Money build_rail;
	Money build_road;
	Money build_signals;
	Money build_bridge;
	Money build_train_depot;
	Money build_road_depot;
	Money build_ship_depot;
	Money build_tunnel;
	Money train_station_track;
	Money train_station_length;
	Money build_airport;
	Money build_bus_station;
	Money build_truck_station;
	Money build_dock;
	Money build_railvehicle;
	Money build_railwagon;
	Money aircraft_base;
	Money roadveh_base;
	Money ship_base;
	Money build_trees;
	Money terraform;
	Money clear_grass;
	Money clear_roughland;
	Money clear_rocks;
	Money clear_fields;
	Money remove_trees;
	Money remove_rail;
	Money remove_signals;
	Money clear_bridge;
	Money remove_train_depot;
	Money remove_road_depot;
	Money remove_ship_depot;
	Money clear_tunnel;
	Money clear_water;
	Money remove_rail_station;
	Money remove_airport;
	Money remove_bus_station;
	Money remove_truck_station;
	Money remove_dock;
	Money remove_house;
	Money remove_road;
	Money running_rail[3];
	Money aircraft_running;
	Money roadveh_running;
	Money ship_running;
	Money build_industry;
};

enum {
	NUM_PRICES = 49,
};

assert_compile(NUM_PRICES * sizeof(Money) == sizeof(Prices));

enum ExpensesType {
	EXPENSES_CONSTRUCTION =  0,
	EXPENSES_NEW_VEHICLES,
	EXPENSES_TRAIN_RUN,
	EXPENSES_ROADVEH_RUN,
	EXPENSES_AIRCRAFT_RUN,
	EXPENSES_SHIP_RUN,
	EXPENSES_PROPERTY,
	EXPENSES_TRAIN_INC,
	EXPENSES_ROADVEH_INC,
	EXPENSES_AIRCRAFT_INC,
	EXPENSES_SHIP_INC,
	EXPENSES_LOAN_INT,
	EXPENSES_OTHER,
	EXPENSES_END,
	INVALID_EXPENSES      = 0xFF,
};

/* The "steps" in loan size, in British Pounds! */
enum {
	LOAN_INTERVAL        = 10000,
	LOAN_INTERVAL_OLD_AI = 50000,
};

#endif /* ECONOMY_TYPE_H */
