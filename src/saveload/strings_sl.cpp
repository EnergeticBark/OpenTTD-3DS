/* $Id$ */

/** @file strings_sl.cpp Code handling saving and loading of strings */

#include "../stdafx.h"
#include "../core/alloc_func.hpp"
#include "../string_func.h"
#include "saveload_internal.h"

#include "table/strings.h"

/**
 * Remap a string ID from the old format to the new format
 * @param s StringID that requires remapping
 * @return translated ID
 */
StringID RemapOldStringID(StringID s)
{
	switch (s) {
		case 0x0006: return STR_SV_EMPTY;
		case 0x7000: return STR_SV_UNNAMED;
		case 0x70E4: return SPECSTR_PLAYERNAME_ENGLISH;
		case 0x70E9: return SPECSTR_PLAYERNAME_ENGLISH;
		case 0x8864: return STR_SV_TRAIN_NAME;
		case 0x902B: return STR_SV_ROADVEH_NAME;
		case 0x9830: return STR_SV_SHIP_NAME;
		case 0xA02F: return STR_SV_AIRCRAFT_NAME;

		default:
			if (IsInsideMM(s, 0x300F, 0x3030)) {
				return s - 0x300F + STR_SV_STNAME;
			} else {
				return s;
			}
	}
}

/** Location to load the old names to. */
char *_old_name_array = NULL;

/**
 * Copy and convert old custom names to UTF-8.
 * They were all stored in a 512 by 32 (200 by 24 for TTO) long string array
 * and are now stored with stations, waypoints and other places with names.
 * @param id the StringID of the custom name to clone.
 * @return the clones custom name.
 */
char *CopyFromOldName(StringID id)
{
	/* Is this name an (old) custom name? */
	if (GB(id, 11, 5) != 15) return NULL;

	if (CheckSavegameVersion(37)) {
		/* Old names were 24/32 characters long, so 128 characters should be
		 * plenty to allow for expansion when converted to UTF-8. */
		char tmp[128];
		uint offs = _savegame_type == SGT_TTO ? 24 * GB(id, 0, 8) : 32 * GB(id, 0, 9);
		const char *strfrom = &_old_name_array[offs];
		char *strto = tmp;

		for (; *strfrom != '\0'; strfrom++) {
			WChar c = (byte)*strfrom;

			/* Map from non-ISO8859-15 characters to UTF-8. */
			switch (c) {
				case 0xA4: c = 0x20AC; break; // Euro
				case 0xA6: c = 0x0160; break; // S with caron
				case 0xA8: c = 0x0161; break; // s with caron
				case 0xB4: c = 0x017D; break; // Z with caron
				case 0xB8: c = 0x017E; break; // z with caron
				case 0xBC: c = 0x0152; break; // OE ligature
				case 0xBD: c = 0x0153; break; // oe ligature
				case 0xBE: c = 0x0178; break; // Y with diaresis
				default: break;
			}

			/* Check character will fit into our buffer. */
			if (strto + Utf8CharLen(c) > lastof(tmp)) break;

			strto += Utf8Encode(strto, c);
		}

		/* Terminate the new string and copy it back to the name array */
		*strto = '\0';

		return strdup(tmp);
	} else {
		/* Name will already be in UTF-8. */
		return strdup(&_old_name_array[32 * GB(id, 0, 9)]);
	}
}

/**
 * Free the memory of the old names array.
 * Should be called once the old names have all been converted.
 */
void ResetOldNames()
{
	free(_old_name_array);
	_old_name_array = NULL;
}

/**
 * Initialize the old names table memory.
 */
void InitializeOldNames()
{
	free(_old_name_array);
	_old_name_array = CallocT<char>(512 * 32); // 200 * 24 would be enough for TTO savegames
}

static void Load_NAME()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		SlArray(&_old_name_array[32 * index], SlGetFieldLength(), SLE_UINT8);
	}
}

extern const ChunkHandler _name_chunk_handlers[] = {
	{ 'NAME', NULL, Load_NAME, CH_ARRAY | CH_LAST},
};
