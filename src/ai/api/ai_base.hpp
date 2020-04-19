/* $Id$ */

/** @file ai_base.hpp Everything to query basic things. */

#ifndef AI_BASE_HPP
#define AI_BASE_HPP

#include "ai_object.hpp"

/**
 * Class that handles some basic functions.
 *
 * @note The random functions are not called Random and RandomRange, because
 *        RANDOM_DEBUG does some tricky stuff, which messes with those names.
 * @note In MP we cannot use Random because that will cause desyncs (AIs are
 *        ran on the server only, not on all clients). This means that
 *        we use InteractiveRandom in MP. Rand() takes care of this for you.
 */
class AIBase : public AIObject {
public:
	static const char *GetClassName() { return "AIBase"; }

	/**
	 * Get a random value.
	 * @return A random value between 0 and MAX(uint32).
	 */
	static uint32 Rand();

	/**
	 * Get a random value.
	 * @param unused_param This param is not used, but is needed to work with lists.
	 * @return A random value between 0 and MAX(uint32).
	 */
	static uint32 RandItem(int unused_param);

	/**
	 * Get a random value in a range.
	 * @param max The first number this function will never return (the maximum it returns is max - 1).
	 * @return A random value between 0 .. max - 1.
	 */
	static uint RandRange(uint max);

	/**
	 * Get a random value in a range.
	 * @param unused_param This param is not used, but is needed to work with lists.
	 * @param max The first number this function will never return (the maximum it returns is max - 1).
	 * @return A random value between 0 .. max - 1.
	 */
	static uint RandRangeItem(int unused_param, uint max);

	/**
	 * Returns approximatelly 'out' times true when called 'max' times.
	 *   After all, it is a random function.
	 * @param out How many times it should return true.
	 * @param max Out of this many times.
	 * @return True if the chance worked out.
	 */
	static bool Chance(uint out, uint max);

	/**
	 * Returns approximatelly 'out' times true when called 'max' times.
	 *   After all, it is a random function.
	 * @param unused_param This param is not used, but is needed to work with lists.
	 * @param out How many times it should return true.
	 * @param max Out of this many times.
	 * @return True if the chance worked out.
	 */
	static bool ChanceItem(int unused_param, uint out, uint max);
};

#endif /* AI_BASE_HPP */
