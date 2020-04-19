/* $Id$ */

/** @file settings.cpp
 * All actions handling saving and loading of the settings/configuration goes on in this file.
 * The file consists of four parts:
 * <ol>
 * <li>Parsing the configuration file (openttd.cfg). This is achieved with the ini_ functions which
 *     handle various types, such as normal 'key = value' pairs, lists and value combinations of
 *     lists, strings, integers, 'bit'-masks and element selections.
 * <li>Defining the data structures that go into the configuration. These include for example
 *     the _settings struct, but also network-settings, banlists, newgrf, etc. There are a lot
 *     of helper macros available for the various types, and also saving/loading of these settings
 *     in a savegame is handled inside these structures.
 * <li>Handle reading and writing to the setting-structures from inside the game either from
 *     the console for example or through the gui with CMD_ functions.
 * <li>Handle saving/loading of the PATS chunk inside the savegame.
 * </ol>
 * @see SettingDesc
 * @see SaveLoad
 */

#include "stdafx.h"
#include "openttd.h"
#include "currency.h"
#include "screenshot.h"
#include "variables.h"
#include "network/network.h"
#include "network/network_func.h"
#include "settings_internal.h"
#include "command_func.h"
#include "console_func.h"
#include "npf.h"
#include "yapf/yapf.h"
#include "genworld.h"
#include "train.h"
#include "news_func.h"
#include "window_func.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "rev.h"
#ifdef WITH_FREETYPE
#include "fontcache.h"
#endif
#include "textbuf_gui.h"
#include "rail_gui.h"
#include "elrail_func.h"
#include "gui.h"
#include "town.h"
#include "video/video_driver.hpp"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "blitter/factory.hpp"
#include "gfxinit.h"
#include "gamelog.h"
#include "station_func.h"
#include "settings_func.h"
#include "ini_type.h"
#include "ai/ai_config.hpp"
#include "newgrf.h"
#include "engine_base.h"

#include "void_map.h"
#include "station_base.h"

#include "table/strings.h"

ClientSettings _settings_client;
GameSettings _settings_game;
GameSettings _settings_newgame;

typedef const char *SettingListCallbackProc(const IniItem *item, uint index);
typedef void SettingDescProc(IniFile *ini, const SettingDesc *desc, const char *grpname, void *object);
typedef void SettingDescProcList(IniFile *ini, const char *grpname, char **list, uint len, SettingListCallbackProc proc);

static bool IsSignedVarMemType(VarType vt);

/**
 * Groups in openttd.cfg that are actually lists.
 */
static const char *_list_group_names[] = {
	"bans",
	"newgrf",
	"servers",
	NULL
};

/** Find the index value of a ONEofMANY type in a string seperated by |
 * @param many full domain of values the ONEofMANY setting can have
 * @param one the current value of the setting for which a value needs found
 * @param onelen force calculation of the *one parameter
 * @return the integer index of the full-list, or -1 if not found */
static int lookup_oneofmany(const char *many, const char *one, size_t onelen = 0)
{
	const char *s;
	int idx;

	if (onelen == 0) onelen = strlen(one);

	/* check if it's an integer */
	if (*one >= '0' && *one <= '9')
		return strtoul(one, NULL, 0);

	idx = 0;
	for (;;) {
		/* find end of item */
		s = many;
		while (*s != '|' && *s != 0) s++;
		if ((size_t)(s - many) == onelen && !memcmp(one, many, onelen)) return idx;
		if (*s == 0) return -1;
		many = s + 1;
		idx++;
	}
}

/** Find the set-integer value MANYofMANY type in a string
 * @param many full domain of values the MANYofMANY setting can have
 * @param str the current string value of the setting, each individual
 * of seperated by a whitespace,tab or | character
 * @return the 'fully' set integer, or -1 if a set is not found */
static uint32 lookup_manyofmany(const char *many, const char *str)
{
	const char *s;
	int r;
	uint32 res = 0;

	for (;;) {
		/* skip "whitespace" */
		while (*str == ' ' || *str == '\t' || *str == '|') str++;
		if (*str == 0) break;

		s = str;
		while (*s != 0 && *s != ' ' && *s != '\t' && *s != '|') s++;

		r = lookup_oneofmany(many, str, s - str);
		if (r == -1) return (uint32)-1;

		SetBit(res, r); // value found, set it
		if (*s == 0) break;
		str = s + 1;
	}
	return res;
}

/** Parse an integerlist string and set each found value
 * @param p the string to be parsed. Each element in the list is seperated by a
 * comma or a space character
 * @param items pointer to the integerlist-array that will be filled with values
 * @param maxitems the maximum number of elements the integerlist-array has
 * @return returns the number of items found, or -1 on an error */
static int parse_intlist(const char *p, int *items, int maxitems)
{
	int n = 0, v;
	char *end;

	for (;;) {
		v = strtol(p, &end, 0);
		if (p == end || n == maxitems) return -1;
		p = end;
		items[n++] = v;
		if (*p == '\0') break;
		if (*p != ',' && *p != ' ') return -1;
		p++;
	}

	return n;
}

/** Load parsed string-values into an integer-array (intlist)
 * @param str the string that contains the values (and will be parsed)
 * @param array pointer to the integer-arrays that will be filled
 * @param nelems the number of elements the array holds. Maximum is 64 elements
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.)
 * @return return true on success and false on error */
static bool load_intlist(const char *str, void *array, int nelems, VarType type)
{
	int items[64];
	int i, nitems;

	if (str == NULL) {
		memset(items, 0, sizeof(items));
		nitems = nelems;
	} else {
		nitems = parse_intlist(str, items, lengthof(items));
		if (nitems != nelems) return false;
	}

	switch (type) {
	case SLE_VAR_BL:
	case SLE_VAR_I8:
	case SLE_VAR_U8:
		for (i = 0; i != nitems; i++) ((byte*)array)[i] = items[i];
		break;
	case SLE_VAR_I16:
	case SLE_VAR_U16:
		for (i = 0; i != nitems; i++) ((uint16*)array)[i] = items[i];
		break;
	case SLE_VAR_I32:
	case SLE_VAR_U32:
		for (i = 0; i != nitems; i++) ((uint32*)array)[i] = items[i];
		break;
	default: NOT_REACHED();
	}

	return true;
}

/** Convert an integer-array (intlist) to a string representation. Each value
 * is seperated by a comma or a space character
 * @param buf output buffer where the string-representation will be stored
 * @param last last item to write to in the output buffer
 * @param array pointer to the integer-arrays that is read from
 * @param nelems the number of elements the array holds.
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.) */
static void make_intlist(char *buf, const char *last, const void *array, int nelems, VarType type)
{
	int i, v = 0;
	const byte *p = (const byte*)array;

	for (i = 0; i != nelems; i++) {
		switch (type) {
		case SLE_VAR_BL:
		case SLE_VAR_I8:  v = *(int8*)p;   p += 1; break;
		case SLE_VAR_U8:  v = *(byte*)p;   p += 1; break;
		case SLE_VAR_I16: v = *(int16*)p;  p += 2; break;
		case SLE_VAR_U16: v = *(uint16*)p; p += 2; break;
		case SLE_VAR_I32: v = *(int32*)p;  p += 4; break;
		case SLE_VAR_U32: v = *(uint32*)p; p += 4; break;
		default: NOT_REACHED();
		}
		buf += seprintf(buf, last, (i == 0) ? "%d" : ",%d", v);
	}
}

/** Convert a ONEofMANY structure to a string representation.
 * @param buf output buffer where the string-representation will be stored
 * @param last last item to write to in the output buffer
 * @param many the full-domain string of possible values
 * @param id the value of the variable and whose string-representation must be found */
static void make_oneofmany(char *buf, const char *last, const char *many, int id)
{
	int orig_id = id;

	/* Look for the id'th element */
	while (--id >= 0) {
		for (; *many != '|'; many++) {
			if (*many == '\0') { // not found
				seprintf(buf, last, "%d", orig_id);
				return;
			}
		}
		many++; // pass the |-character
	}

	/* copy string until next item (|) or the end of the list if this is the last one */
	while (*many != '\0' && *many != '|' && buf < last) *buf++ = *many++;
	*buf = '\0';
}

/** Convert a MANYofMANY structure to a string representation.
 * @param buf output buffer where the string-representation will be stored
 * @param last last item to write to in the output buffer
 * @param many the full-domain string of possible values
 * @param x the value of the variable and whose string-representation must
 *        be found in the bitmasked many string */
static void make_manyofmany(char *buf, const char *last, const char *many, uint32 x)
{
	const char *start;
	int i = 0;
	bool init = true;

	for (; x != 0; x >>= 1, i++) {
		start = many;
		while (*many != 0 && *many != '|') many++; // advance to the next element

		if (HasBit(x, 0)) { // item found, copy it
			if (!init) buf += seprintf(buf, last, "|");
			init = false;
			if (start == many) {
				buf += seprintf(buf, last, "%d", i);
			} else {
				memcpy(buf, start, many - start);
				buf += many - start;
			}
		}

		if (*many == '|') many++;
	}

	*buf = '\0';
}

/** Convert a string representation (external) of a setting to the internal rep.
 * @param desc SettingDesc struct that holds all information about the variable
 * @param str input string that will be parsed based on the type of desc
 * @return return the parsed value of the setting */
static const void *string_to_val(const SettingDescBase *desc, const char *str)
{
	switch (desc->cmd) {
	case SDT_NUMX: {
		char *end;
		unsigned long val = strtoul(str, &end, 0);
		if (*end != '\0') ShowInfoF("ini: trailing characters at end of setting '%s'", desc->name);
		return (void*)val;
	}
	case SDT_ONEOFMANY: {
		long r = lookup_oneofmany(desc->many, str);
		/* if the first attempt of conversion from string to the appropriate value fails,
		 * look if we have defined a converter from old value to new value. */
		if (r == -1 && desc->proc_cnvt != NULL) r = desc->proc_cnvt(str);
		if (r != -1) return (void*)r; // and here goes converted value
		ShowInfoF("ini: invalid value '%s' for '%s'", str, desc->name); // sorry, we failed
		return 0;
	}
	case SDT_MANYOFMANY: {
		unsigned long r = lookup_manyofmany(desc->many, str);
		if (r != (unsigned long)-1) return (void*)r;
		ShowInfoF("ini: invalid value '%s' for '%s'", str, desc->name);
		return 0;
	}
	case SDT_BOOLX:
		if (strcmp(str, "true")  == 0 || strcmp(str, "on")  == 0 || strcmp(str, "1") == 0)
			return (void*)true;
		if (strcmp(str, "false") == 0 || strcmp(str, "off") == 0 || strcmp(str, "0") == 0)
			return (void*)false;
		ShowInfoF("ini: invalid setting value '%s' for '%s'", str, desc->name);
		break;

	case SDT_STRING:
	case SDT_INTLIST: return str;
	default: break;
	}

	return NULL;
}

/** Set the value of a setting and if needed clamp the value to
 * the preset minimum and maximum.
 * @param ptr the variable itself
 * @param sd pointer to the 'information'-database of the variable
 * @param val signed long version of the new value
 * @pre SettingDesc is of type SDT_BOOLX, SDT_NUMX,
 * SDT_ONEOFMANY or SDT_MANYOFMANY. Other types are not supported as of now */
static void Write_ValidateSetting(void *ptr, const SettingDesc *sd, int32 val)
{
	const SettingDescBase *sdb = &sd->desc;

	if (sdb->cmd != SDT_BOOLX &&
			sdb->cmd != SDT_NUMX &&
			sdb->cmd != SDT_ONEOFMANY &&
			sdb->cmd != SDT_MANYOFMANY) {
		return;
	}

	/* We cannot know the maximum value of a bitset variable, so just have faith */
	if (sdb->cmd != SDT_MANYOFMANY) {
		/* We need to take special care of the uint32 type as we receive from the function
		 * a signed integer. While here also bail out on 64-bit settings as those are not
		 * supported. Unsigned 8 and 16-bit variables are safe since they fit into a signed
		 * 32-bit variable
		 * TODO: Support 64-bit settings/variables */
		switch (GetVarMemType(sd->save.conv)) {
			case SLE_VAR_NULL: return;
			case SLE_VAR_BL:
			case SLE_VAR_I8:
			case SLE_VAR_U8:
			case SLE_VAR_I16:
			case SLE_VAR_U16:
			case SLE_VAR_I32: {
				/* Override the minimum value. No value below sdb->min, except special value 0 */
				if (!(sdb->flags & SGF_0ISDISABLED) || val != 0) val = Clamp(val, sdb->min, sdb->max);
			} break;
			case SLE_VAR_U32: {
				/* Override the minimum value. No value below sdb->min, except special value 0 */
				uint min = ((sdb->flags & SGF_0ISDISABLED) && (uint)val <= (uint)sdb->min) ? 0 : sdb->min;
				WriteValue(ptr, SLE_VAR_U32, (int64)ClampU(val, min, sdb->max));
				return;
			}
			case SLE_VAR_I64:
			case SLE_VAR_U64:
			default: NOT_REACHED(); break;
		}
	}

	WriteValue(ptr, sd->save.conv, (int64)val);
}

/** Load values from a group of an IniFile structure into the internal representation
 * @param ini pointer to IniFile structure that holds administrative information
 * @param sd pointer to SettingDesc structure whose internally pointed variables will
 *        be given values
 * @param grpname the group of the IniFile to search in for the new values
 * @param object pointer to the object been loaded */
static void ini_load_settings(IniFile *ini, const SettingDesc *sd, const char *grpname, void *object)
{
	IniGroup *group;
	IniGroup *group_def = ini->GetGroup(grpname);
	IniItem *item;
	const void *p;
	void *ptr;
	const char *s;

	for (; sd->save.cmd != SL_END; sd++) {
		const SettingDescBase *sdb = &sd->desc;
		const SaveLoad        *sld = &sd->save;

		if (!SlIsObjectCurrentlyValid(sld->version_from, sld->version_to)) continue;

		/* For settings.xx.yy load the settings from [xx] yy = ? */
		s = strchr(sdb->name, '.');
		if (s != NULL) {
			group = ini->GetGroup(sdb->name, s - sdb->name);
			s++;
		} else {
			s = sdb->name;
			group = group_def;
		}

		item = group->GetItem(s, false);
		if (item == NULL && group != group_def) {
			/* For settings.xx.yy load the settings from [settingss] yy = ? in case the previous
			 * did not exist (e.g. loading old config files with a [settings] section */
			item = group_def->GetItem(s, false);
		}
		if (item == NULL) {
			/* For settings.xx.zz.yy load the settings from [zz] yy = ? in case the previous
			 * did not exist (e.g. loading old config files with a [yapf] section */
			const char *sc = strchr(s, '.');
			if (sc != NULL) item = ini->GetGroup(s, sc - s)->GetItem(sc + 1, false);
		}

		p = (item == NULL) ? sdb->def : string_to_val(sdb, item->value);
		ptr = GetVariableAddress(object, sld);

		switch (sdb->cmd) {
		case SDT_BOOLX: // All four are various types of (integer) numbers
		case SDT_NUMX:
		case SDT_ONEOFMANY:
		case SDT_MANYOFMANY:
			Write_ValidateSetting(ptr, sd, (unsigned long)p); break;

		case SDT_STRING:
			switch (GetVarMemType(sld->conv)) {
				case SLE_VAR_STRB:
				case SLE_VAR_STRBQ:
					if (p != NULL) ttd_strlcpy((char*)ptr, (const char*)p, sld->length);
					break;
				case SLE_VAR_STR:
				case SLE_VAR_STRQ:
					if (p != NULL) {
						free(*(char**)ptr);
						*(char**)ptr = strdup((const char*)p);
					}
					break;
				case SLE_VAR_CHAR: *(char*)ptr = *(char*)p; break;
				default: NOT_REACHED(); break;
			}
			break;

		case SDT_INTLIST: {
			if (!load_intlist((const char*)p, ptr, sld->length, GetVarMemType(sld->conv))) {
				ShowInfoF("ini: error in array '%s'", sdb->name);
			} else if (sd->desc.proc_cnvt != NULL) {
				sd->desc.proc_cnvt((const char*)p);
			}
			break;
		}
		default: NOT_REACHED(); break;
		}
	}
}

/** Save the values of settings to the inifile.
 * @param ini pointer to IniFile structure
 * @param sd read-only SettingDesc structure which contains the unmodified,
 *        loaded values of the configuration file and various information about it
 * @param grpname holds the name of the group (eg. [network]) where these will be saved
 * @param object pointer to the object been saved
 * The function works as follows: for each item in the SettingDesc structure we
 * have a look if the value has changed since we started the game (the original
 * values are reloaded when saving). If settings indeed have changed, we get
 * these and save them.
 */
static void ini_save_settings(IniFile *ini, const SettingDesc *sd, const char *grpname, void *object)
{
	IniGroup *group_def = NULL, *group;
	IniItem *item;
	char buf[512];
	const char *s;
	void *ptr;

	for (; sd->save.cmd != SL_END; sd++) {
		const SettingDescBase *sdb = &sd->desc;
		const SaveLoad        *sld = &sd->save;

		/* If the setting is not saved to the configuration
		 * file, just continue with the next setting */
		if (!SlIsObjectCurrentlyValid(sld->version_from, sld->version_to)) continue;
		if (sld->conv & SLF_CONFIG_NO) continue;

		/* XXX - wtf is this?? (group override?) */
		s = strchr(sdb->name, '.');
		if (s != NULL) {
			group = ini->GetGroup(sdb->name, s - sdb->name);
			s++;
		} else {
			if (group_def == NULL) group_def = ini->GetGroup(grpname);
			s = sdb->name;
			group = group_def;
		}

		item = group->GetItem(s, true);
		ptr = GetVariableAddress(object, sld);

		if (item->value != NULL) {
			/* check if the value is the same as the old value */
			const void *p = string_to_val(sdb, item->value);

			/* The main type of a variable/setting is in bytes 8-15
			 * The subtype (what kind of numbers do we have there) is in 0-7 */
			switch (sdb->cmd) {
			case SDT_BOOLX:
			case SDT_NUMX:
			case SDT_ONEOFMANY:
			case SDT_MANYOFMANY:
				switch (GetVarMemType(sld->conv)) {
				case SLE_VAR_BL:
					if (*(bool*)ptr == (p != NULL)) continue;
					break;
				case SLE_VAR_I8:
				case SLE_VAR_U8:
					if (*(byte*)ptr == (byte)(unsigned long)p) continue;
					break;
				case SLE_VAR_I16:
				case SLE_VAR_U16:
					if (*(uint16*)ptr == (uint16)(unsigned long)p) continue;
					break;
				case SLE_VAR_I32:
				case SLE_VAR_U32:
					if (*(uint32*)ptr == (uint32)(unsigned long)p) continue;
					break;
				default: NOT_REACHED();
				}
				break;
			default: break; // Assume the other types are always changed
			}
		}

		/* Value has changed, get the new value and put it into a buffer */
		switch (sdb->cmd) {
		case SDT_BOOLX:
		case SDT_NUMX:
		case SDT_ONEOFMANY:
		case SDT_MANYOFMANY: {
			uint32 i = (uint32)ReadValue(ptr, sld->conv);

			switch (sdb->cmd) {
			case SDT_BOOLX:      strcpy(buf, (i != 0) ? "true" : "false"); break;
			case SDT_NUMX:       seprintf(buf, lastof(buf), IsSignedVarMemType(sld->conv) ? "%d" : "%u", i); break;
			case SDT_ONEOFMANY:  make_oneofmany(buf, lastof(buf), sdb->many, i); break;
			case SDT_MANYOFMANY: make_manyofmany(buf, lastof(buf), sdb->many, i); break;
			default: NOT_REACHED();
			}
		} break;

		case SDT_STRING:
			switch (GetVarMemType(sld->conv)) {
			case SLE_VAR_STRB: strcpy(buf, (char*)ptr); break;
			case SLE_VAR_STRBQ:seprintf(buf, lastof(buf), "\"%s\"", (char*)ptr); break;
			case SLE_VAR_STR:  strcpy(buf, *(char**)ptr); break;
			case SLE_VAR_STRQ:
				if (*(char**)ptr == NULL) {
					buf[0] = '\0';
				} else {
					seprintf(buf, lastof(buf), "\"%s\"", *(char**)ptr);
				}
				break;
			case SLE_VAR_CHAR: buf[0] = *(char*)ptr; buf[1] = '\0'; break;
			default: NOT_REACHED();
			}
			break;

		case SDT_INTLIST:
			make_intlist(buf, lastof(buf), ptr, sld->length, GetVarMemType(sld->conv));
			break;
		default: NOT_REACHED();
		}

		/* The value is different, that means we have to write it to the ini */
		free(item->value);
		item->value = strdup(buf);
	}
}

/** Loads all items from a 'grpname' section into a list
 * The list parameter can be a NULL pointer, in this case nothing will be
 * saved and a callback function should be defined that will take over the
 * list-handling and store the data itself somewhere.
 * @param ini IniFile handle to the ini file with the source data
 * @param grpname character string identifying the section-header of the ini
 * file that will be parsed
 * @param list pointer to an string(pointer) array that will store the parsed
 * entries of the given section
 * @param len the maximum number of items available for the above list
 * @param proc callback function that can override how the values are stored
 * inside the list */
static void ini_load_setting_list(IniFile *ini, const char *grpname, char **list, uint len, SettingListCallbackProc proc)
{
	IniGroup *group = ini->GetGroup(grpname);
	IniItem *item;
	const char *entry;
	uint i, j;

	if (group == NULL) return;

	for (i = j = 0, item = group->item; item != NULL; item = item->next) {
		entry = (proc != NULL) ? proc(item, i++) : item->name;

		if (entry == NULL || list == NULL) continue;

		if (j == len) break;
		list[j++] = strdup(entry);
	}
}

/** Saves all items from a list into the 'grpname' section
 * The list parameter can be a NULL pointer, in this case a callback function
 * should be defined that will provide the source data to be saved.
 * @param ini IniFile handle to the ini file where the destination data is saved
 * @param grpname character string identifying the section-header of the ini file
 * @param list pointer to an string(pointer) array that will be used as the
 *             source to be saved into the relevant ini section
 * @param len the maximum number of items available for the above list
 * @param proc callback function that can will provide the source data if defined */
static void ini_save_setting_list(IniFile *ini, const char *grpname, char **list, uint len, SettingListCallbackProc proc)
{
	IniGroup *group = ini->GetGroup(grpname);
	const char *entry;
	uint i;

	if (proc == NULL && list == NULL) return;
	if (group == NULL) return;
	group->Clear();

	for (i = 0; i != len; i++) {
		entry = (proc != NULL) ? proc(NULL, i) : list[i];

		if (entry == NULL || *entry == '\0') continue;

		group->GetItem(entry, true)->SetValue("");
	}
}

/****************************
 * OTTD specific INI stuff
 ****************************/

/** Settings-macro usage:
 * The list might look daunting at first, but is in general easy to understand.
 * We have two types of list:
 * 1. SDTG_something
 * 2. SDT_something
 * The 'G' stands for global, so this is the one you will use for a
 * SettingDescGlobVarList section meaning global variables. The other uses a
 * Base/Offset and runtime variable selection mechanism, known from the saveload
 * convention (it also has global so it should not be hard).
 * Of each type there are again two versions, the normal one and one prefixed
 * with 'COND'.
 * COND means that the setting is only valid in certain savegame versions
 * (since settings are saved to the savegame, this bookkeeping is necessary.
 * Now there are a lot of types. Easy ones are:
 * - VAR:  any number type, 'type' field specifies what number. eg int8 or uint32
 * - BOOL: a boolean number type
 * - STR:  a string or character. 'type' field specifies what string. Normal, string, or quoted
 * A bit more difficult to use are MMANY (meaning ManyOfMany) and OMANY (OneOfMany)
 * These are actually normal numbers, only bitmasked. In MMANY several bits can
 * be set, in the other only one.
 * The most complex type is INTLIST. This is basically an array of numbers. If
 * the intlist is only valid in certain savegame versions because for example
 * it has grown in size its length cannot be automatically be calculated so
 * use SDT(G)_CONDLISTO() meaning Old.
 * If nothing fits you, you can use the GENERAL macros, but it exposes the
 * internal structure somewhat so it needs a little looking. There are _NULL()
 * macros as well, these fill up space so you can add more settings there (in
 * place) and you DON'T have to increase the savegame version.
 *
 * While reading values from openttd.cfg, some values may not be converted
 * properly, for any kind of reasons.  In order to allow a process of self-cleaning
 * mechanism, a callback procedure is made available.  You will have to supply the function, which
 * will work on a string, one function per setting. And of course, enable the callback param
 * on the appropriate macro.
 */

#define NSD_GENERAL(name, def, cmd, guiflags, min, max, interval, many, str, proc, load)\
	{name, (const void*)(def), {cmd}, {guiflags}, min, max, interval, many, str, proc, load}

/* Macros for various objects to go in the configuration file.
 * This section is for global variables */
#define SDTG_GENERAL(name, sdt_cmd, sle_cmd, type, flags, guiflags, var, length, def, min, max, interval, full, str, proc, from, to)\
	{NSD_GENERAL(name, def, sdt_cmd, guiflags, min, max, interval, full, str, proc, NULL), SLEG_GENERAL(sle_cmd, var, type | flags, length, from, to)}

#define SDTG_CONDVAR(name, type, flags, guiflags, var, def, min, max, interval, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_NUMX, SL_VAR, type, flags, guiflags, var, 0, def, min, max, interval, NULL, str, proc, from, to)
#define SDTG_VAR(name, type, flags, guiflags, var, def, min, max, interval, str, proc)\
	SDTG_CONDVAR(name, type, flags, guiflags, var, def, min, max, interval, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDBOOL(name, flags, guiflags, var, def, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_BOOLX, SL_VAR, SLE_BOOL, flags, guiflags, var, 0, def, 0, 1, 0, NULL, str, proc, from, to)
#define SDTG_BOOL(name, flags, guiflags, var, def, str, proc)\
	SDTG_CONDBOOL(name, flags, guiflags, var, def, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDLIST(name, type, length, flags, guiflags, var, def, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_INTLIST, SL_ARR, type, flags, guiflags, var, length, def, 0, 0, 0, NULL, str, proc, from, to)
#define SDTG_LIST(name, type, flags, guiflags, var, def, str, proc)\
	SDTG_GENERAL(name, SDT_INTLIST, SL_ARR, type, flags, guiflags, var, lengthof(var), def, 0, 0, 0, NULL, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDSTR(name, type, length, flags, guiflags, var, def, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_STRING, SL_STR, type, flags, guiflags, var, length, def, 0, 0, 0, NULL, str, proc, from, to)
#define SDTG_STR(name, type, flags, guiflags, var, def, str, proc)\
	SDTG_GENERAL(name, SDT_STRING, SL_STR, type, flags, guiflags, var, lengthof(var), def, 0, 0, 0, NULL, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDOMANY(name, type, flags, guiflags, var, def, max, full, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_ONEOFMANY, SL_VAR, type, flags, guiflags, var, 0, def, 0, max, 0, full, str, proc, from, to)
#define SDTG_OMANY(name, type, flags, guiflags, var, def, max, full, str, proc)\
	SDTG_CONDOMANY(name, type, flags, guiflags, var, def, max, full, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDMMANY(name, type, flags, guiflags, var, def, full, str, proc, from, to)\
	SDTG_GENERAL(name, SDT_MANYOFMANY, SL_VAR, type, flags, guiflags, var, 0, def, 0, 0, 0, full, str, proc, from, to)
#define SDTG_MMANY(name, type, flags, guiflags, var, def, full, str, proc)\
	SDTG_CONDMMANY(name, type, flags, guiflags, var, def, full, str, proc, 0, SL_MAX_VERSION)

#define SDTG_CONDNULL(length, from, to)\
	{{"", NULL, {0}, {0}, 0, 0, 0, NULL, STR_NULL, NULL, NULL}, SLEG_CONDNULL(length, from, to)}

#define SDTG_END() {{NULL, NULL, {0}, {0}, 0, 0, 0, NULL, STR_NULL, NULL, NULL}, SLEG_END()}

/* Macros for various objects to go in the configuration file.
 * This section is for structures where their various members are saved */
#define SDT_GENERAL(name, sdt_cmd, sle_cmd, type, flags, guiflags, base, var, length, def, min, max, interval, full, str, proc, load, from, to)\
	{NSD_GENERAL(name, def, sdt_cmd, guiflags, min, max, interval, full, str, proc, load), SLE_GENERAL(sle_cmd, base, var, type | flags, length, from, to)}

#define SDT_CONDVAR(base, var, type, from, to, flags, guiflags, def, min, max, interval, str, proc)\
	SDT_GENERAL(#var, SDT_NUMX, SL_VAR, type, flags, guiflags, base, var, 1, def, min, max, interval, NULL, str, proc, NULL, from, to)
#define SDT_VAR(base, var, type, flags, guiflags, def, min, max, interval, str, proc)\
	SDT_CONDVAR(base, var, type, 0, SL_MAX_VERSION, flags, guiflags, def, min, max, interval, str, proc)

#define SDT_CONDBOOL(base, var, from, to, flags, guiflags, def, str, proc)\
	SDT_GENERAL(#var, SDT_BOOLX, SL_VAR, SLE_BOOL, flags, guiflags, base, var, 1, def, 0, 1, 0, NULL, str, proc, NULL, from, to)
#define SDT_BOOL(base, var, flags, guiflags, def, str, proc)\
	SDT_CONDBOOL(base, var, 0, SL_MAX_VERSION, flags, guiflags, def, str, proc)

#define SDT_CONDLIST(base, var, type, from, to, flags, guiflags, def, str, proc)\
	SDT_GENERAL(#var, SDT_INTLIST, SL_ARR, type, flags, guiflags, base, var, lengthof(((base*)8)->var), def, 0, 0, 0, NULL, str, proc, NULL, from, to)
#define SDT_LIST(base, var, type, flags, guiflags, def, str, proc)\
	SDT_CONDLIST(base, var, type, 0, SL_MAX_VERSION, flags, guiflags, def, str, proc)
#define SDT_CONDLISTO(base, var, length, type, from, to, flags, guiflags, def, str, proc, load)\
	SDT_GENERAL(#var, SDT_INTLIST, SL_ARR, type, flags, guiflags, base, var, length, def, 0, 0, 0, NULL, str, proc, load, from, to)

#define SDT_CONDSTR(base, var, type, from, to, flags, guiflags, def, str, proc)\
	SDT_GENERAL(#var, SDT_STRING, SL_STR, type, flags, guiflags, base, var, lengthof(((base*)8)->var), def, 0, 0, 0, NULL, str, proc, NULL, from, to)
#define SDT_STR(base, var, type, flags, guiflags, def, str, proc)\
	SDT_CONDSTR(base, var, type, 0, SL_MAX_VERSION, flags, guiflags, def, str, proc)
#define SDT_CONDSTRO(base, var, length, type, from, to, flags, def, str, proc)\
	SDT_GENERAL(#var, SDT_STRING, SL_STR, type, flags, 0, base, var, length, def, 0, 0, NULL, str, proc, from, to)

#define SDT_CONDCHR(base, var, from, to, flags, guiflags, def, str, proc)\
	SDT_GENERAL(#var, SDT_STRING, SL_VAR, SLE_CHAR, flags, guiflags, base, var, 1, def, 0, 0, 0, NULL, str, proc, NULL, from, to)
#define SDT_CHR(base, var, flags, guiflags, def, str, proc)\
	SDT_CONDCHR(base, var, 0, SL_MAX_VERSION, flags, guiflags, def, str, proc)

#define SDT_CONDOMANY(base, var, type, from, to, flags, guiflags, def, max, full, str, proc, load)\
	SDT_GENERAL(#var, SDT_ONEOFMANY, SL_VAR, type, flags, guiflags, base, var, 1, def, 0, max, 0, full, str, proc, load, from, to)
#define SDT_OMANY(base, var, type, flags, guiflags, def, max, full, str, proc, load)\
	SDT_CONDOMANY(base, var, type, 0, SL_MAX_VERSION, flags, guiflags, def, max, full, str, proc, load)

#define SDT_CONDMMANY(base, var, type, from, to, flags, guiflags, def, full, str, proc)\
	SDT_GENERAL(#var, SDT_MANYOFMANY, SL_VAR, type, flags, guiflags, base, var, 1, def, 0, 0, 0, full, str, proc, NULL, from, to)
#define SDT_MMANY(base, var, type, flags, guiflags, def, full, str, proc)\
	SDT_CONDMMANY(base, var, type, 0, SL_MAX_VERSION, flags, guiflags, def, full, str, proc)

#define SDT_CONDNULL(length, from, to)\
	{{"", NULL, {0}, {0}, 0, 0, 0, NULL, STR_NULL, NULL, NULL}, SLE_CONDNULL(length, from, to)}


#define SDTC_CONDVAR(var, type, from, to, flags, guiflags, def, min, max, interval, str, proc)\
	SDTG_GENERAL(#var, SDT_NUMX, SL_VAR, type, flags, guiflags, _settings_client.var, 1, def, min, max, interval, NULL, str, proc, from, to)
#define SDTC_VAR(var, type, flags, guiflags, def, min, max, interval, str, proc)\
	SDTC_CONDVAR(var, type, 0, SL_MAX_VERSION, flags, guiflags, def, min, max, interval, str, proc)

#define SDTC_CONDBOOL(var, from, to, flags, guiflags, def, str, proc)\
	SDTG_GENERAL(#var, SDT_BOOLX, SL_VAR, SLE_BOOL, flags, guiflags, _settings_client.var, 1, def, 0, 1, 0, NULL, str, proc, from, to)
#define SDTC_BOOL(var, flags, guiflags, def, str, proc)\
	SDTC_CONDBOOL(var, 0, SL_MAX_VERSION, flags, guiflags, def, str, proc)

#define SDTC_CONDLIST(var, type, length, flags, guiflags, def, str, proc, from, to)\
	SDTG_GENERAL(#var, SDT_INTLIST, SL_ARR, type, flags, guiflags, _settings_client.var, length, def, 0, 0, 0, NULL, str, proc, from, to)
#define SDTC_LIST(var, type, flags, guiflags, def, str, proc)\
	SDTG_GENERAL(#var, SDT_INTLIST, SL_ARR, type, flags, guiflags, _settings_client.var, lengthof(_settings_client.var), def, 0, 0, 0, NULL, str, proc, 0, SL_MAX_VERSION)

#define SDTC_CONDSTR(var, type, length, flags, guiflags, def, str, proc, from, to)\
	SDTG_GENERAL(#var, SDT_STRING, SL_STR, type, flags, guiflags, _settings_client.var, length, def, 0, 0, 0, NULL, str, proc, from, to)
#define SDTC_STR(var, type, flags, guiflags, def, str, proc)\
	SDTG_GENERAL(#var, SDT_STRING, SL_STR, type, flags, guiflags, _settings_client.var, lengthof(_settings_client.var), def, 0, 0, 0, NULL, str, proc, 0, SL_MAX_VERSION)

#define SDTC_CONDOMANY(var, type, from, to, flags, guiflags, def, max, full, str, proc)\
	SDTG_GENERAL(#var, SDT_ONEOFMANY, SL_VAR, type, flags, guiflags, _settings_client.var, 1, def, 0, max, 0, full, str, proc, from, to)
#define SDTC_OMANY(var, type, flags, guiflags, def, max, full, str, proc)\
	SDTC_CONDOMANY(var, type, 0, SL_MAX_VERSION, flags, guiflags, def, max, full, str, proc)

#define SDT_END() {{NULL, NULL, {0}, {0}, 0, 0, 0, NULL, STR_NULL, NULL, NULL}, SLE_END()}

/* Shortcuts for macros below. Logically if we don't save the value
 * we also don't sync it in a network game */
#define S SLF_SAVE_NO | SLF_NETWORK_NO
#define C SLF_CONFIG_NO
#define N SLF_NETWORK_NO

#define D0 SGF_0ISDISABLED
#define NC SGF_NOCOMMA
#define MS SGF_MULTISTRING
#define NO SGF_NETWORK_ONLY
#define CR SGF_CURRENCY
#define NN SGF_NO_NETWORK
#define NG SGF_NEWGAME_ONLY

/* Begin - Callback Functions for the various settings
 * virtual PositionMainToolbar function, calls the right one.*/
static bool v_PositionMainToolbar(int32 p1)
{
	if (_game_mode != GM_MENU) PositionMainToolbar(NULL);
	return true;
}

static bool PopulationInLabelActive(int32 p1)
{
	Town *t;
	FOR_ALL_TOWNS(t) UpdateTownVirtCoord(t);

	return true;
}

static bool RedrawScreen(int32 p1)
{
	MarkWholeScreenDirty();
	return true;
}

static bool InvalidateDetailsWindow(int32 p1)
{
	InvalidateWindowClasses(WC_VEHICLE_DETAILS);
	return true;
}

static bool InvalidateStationBuildWindow(int32 p1)
{
	InvalidateWindow(WC_BUILD_STATION, 0);
	return true;
}

static bool InvalidateBuildIndustryWindow(int32 p1)
{
	InvalidateWindowData(WC_BUILD_INDUSTRY, 0);
	return true;
}

static bool CloseSignalGUI(int32 p1)
{
	if (p1 == 0) {
		DeleteWindowByClass(WC_BUILD_SIGNAL);
	}
	return true;
}

static bool InvalidateTownViewWindow(int32 p1)
{
	InvalidateWindowClassesData(WC_TOWN_VIEW, p1);
	return true;
}

static bool DeleteSelectStationWindow(int32 p1)
{
	DeleteWindowById(WC_SELECT_STATION, 0);
	return true;
}

static bool UpdateConsists(int32 p1)
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		/* Update the consist of all trains so the maximum speed is set correctly. */
		if (v->type == VEH_TRAIN && (IsFrontEngine(v) || IsFreeWagon(v))) TrainConsistChanged(v, true);
	}
	return true;
}

/* Check service intervals of vehicles, p1 is value of % or day based servicing */
static bool CheckInterval(int32 p1)
{
	VehicleSettings *ptc = (_game_mode == GM_MENU) ? &_settings_newgame.vehicle : &_settings_game.vehicle;

	if (p1) {
		ptc->servint_trains   = 50;
		ptc->servint_roadveh  = 50;
		ptc->servint_aircraft = 50;
		ptc->servint_ships    = 50;
	} else {
		ptc->servint_trains   = 150;
		ptc->servint_roadveh  = 150;
		ptc->servint_aircraft = 360;
		ptc->servint_ships    = 100;
	}

	InvalidateDetailsWindow(0);

	return true;
}

static bool EngineRenewUpdate(int32 p1)
{
	DoCommandP(0, 0, _settings_client.gui.autorenew, CMD_SET_AUTOREPLACE);
	return true;
}

static bool EngineRenewMonthsUpdate(int32 p1)
{
	DoCommandP(0, 1, _settings_client.gui.autorenew_months, CMD_SET_AUTOREPLACE);
	return true;
}

static bool EngineRenewMoneyUpdate(int32 p1)
{
	DoCommandP(0, 2, _settings_client.gui.autorenew_money, CMD_SET_AUTOREPLACE);
	return true;
}

static bool TrainAccelerationModelChanged(int32 p1)
{
	Vehicle *v;

	FOR_ALL_VEHICLES(v) {
		if (v->type == VEH_TRAIN && IsFrontEngine(v)) UpdateTrainAcceleration(v);
	}

	return true;
}

static bool DragSignalsDensityChanged(int32)
{
	SetWindowDirty(FindWindowById(WC_BUILD_SIGNAL, 0));

	return true;
}

/*
 * A: competitors
 * B: competitor start time. Deprecated since savegame version 110.
 * C: town count (3 = high, 0 = very low)
 * D: industry count (4 = high, 0 = none)
 * E: inital loan (in GBP)
 * F: interest rate
 * G: running costs (0 = low, 2 = high)
 * H: construction speed of competitors (0 = very slow, 4 = very fast)
 * I: competitor intelligence. Deprecated since savegame version 110.
 * J: breakdowns (0 = off, 2 = normal)
 * K: subsidy multiplier (0 = 1.5, 3 = 4.0)
 * L: construction cost (0-2)
 * M: terrain type (0 = very flat, 3 = mountainous)
 * N: amount of water (0 = very low, 3 = high)
 * O: economy (0 = steady, 1 = fluctuating)
 * P: Train reversing (0 = end of line + stations, 1 = end of line)
 * Q: disasters
 * R: area restructuring (0 = permissive, 2 = hostile)
 * S: the difficulty level
 */
static const DifficultySettings _default_game_diff[3] = { /*
	 A, C, D,      E, F, G, H, J, K, L, M, N, O, P, Q, R, S*/
	{2, 2, 4, 300000, 2, 0, 2, 1, 2, 0, 1, 0, 0, 0, 0, 0, 0}, ///< easy
	{4, 2, 3, 150000, 3, 1, 3, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1}, ///< medium
	{7, 3, 3, 100000, 4, 1, 3, 2, 0, 2, 3, 2, 1, 1, 1, 2, 2}, ///< hard
};

void SetDifficultyLevel(int mode, DifficultySettings *gm_opt)
{
	assert(mode <= 3);

	if (mode != 3) {
		*gm_opt = _default_game_diff[mode];
	} else {
		gm_opt->diff_level = 3;
	}
}

/**
 * Checks the difficulty levels read from the configuration and
 * forces them to be correct when invalid.
 */
void CheckDifficultyLevels()
{
	if (_settings_newgame.difficulty.diff_level != 3) {
		SetDifficultyLevel(_settings_newgame.difficulty.diff_level, &_settings_newgame.difficulty);
	}
}

static bool DifficultyReset(int32 level)
{
	SetDifficultyLevel(level, (_game_mode == GM_MENU) ? &_settings_newgame.difficulty : &_settings_game.difficulty);
	return true;
}

static bool DifficultyChange(int32)
{
	if (_game_mode == GM_MENU) {
		if (_settings_newgame.difficulty.diff_level != 3) {
			ShowErrorMessage(INVALID_STRING_ID, STR_DIFFICULTY_TO_CUSTOM, 0, 0);
			_settings_newgame.difficulty.diff_level = 3;
		}
	} else {
		_settings_game.difficulty.diff_level = 3;
	}

	/* If we are a network-client, update the difficult setting (if it is open).
	 * Use this instead of just dirtying the window because we need to load in
	 * the new difficulty settings */
	if (_networking && FindWindowById(WC_GAME_OPTIONS, 0) != NULL) {
		ShowGameDifficulty();
	}

	return true;
}

static bool DifficultyNoiseChange(int32 i)
{
	if (_game_mode == GM_NORMAL) {
		UpdateAirportsNoise();
		if (_settings_game.economy.station_noise_level) {
			InvalidateWindowClassesData(WC_TOWN_VIEW, 0);
		}
	}

	return DifficultyChange(i);
}

/**
 * Check whether the road side may be changed.
 * @param p1 unused
 * @return true if the road side may be changed.
 */
static bool CheckRoadSide(int p1)
{
	extern bool RoadVehiclesAreBuilt();
	return _game_mode == GM_MENU || !RoadVehiclesAreBuilt();
}

/** Conversion callback for _gameopt_settings_game.landscape
 * It converts (or try) between old values and the new ones,
 * without losing initial setting of the user
 * @param value that was read from config file
 * @return the "hopefully" converted value
 */
static int32 ConvertLandscape(const char *value)
{
	/* try with the old values */
	return lookup_oneofmany("normal|hilly|desert|candy", value);
}

/**
 * Check for decent values been supplied by the user for the noise tolerance setting.
 * The primary idea is to avoid division by zero in game mode.
 * The secondary idea is to make it so the values will be somewhat sane and that towns will
 * not be overcrowed with airports.  It would be easy to abuse such a feature
 * So basically, 200, 400, 800 are the lowest allowed values */
static int32 CheckNoiseToleranceLevel(const char *value)
{
	GameSettings *s = (_game_mode == GM_MENU) ? &_settings_newgame : &_settings_game;
	for (uint16 i = 0; i < lengthof(s->economy.town_noise_population); i++) {
		s->economy.town_noise_population[i] = max(uint16(200 * (i + 1)), s->economy.town_noise_population[i]);
	}
	return 0;
}

static bool CheckFreeformEdges(int32 p1)
{
	if (_game_mode == GM_MENU) return true;
	if (p1 != 0) {
		Vehicle *v;
		FOR_ALL_VEHICLES(v) {
			if (v->type == VEH_SHIP && (TileX(v->tile) == 0 || TileY(v->tile) == 0)) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_EMPTY, 0, 0);
				return false;
			}
		}
		Station *st;
		FOR_ALL_STATIONS(st) {
			if (TileX(st->xy) == 0 || TileY(st->xy) == 0) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_EMPTY, 0, 0);
				return false;
			}
		}
		for (uint i = 0; i < MapSizeX(); i++) MakeVoid(TileXY(i, 0));
		for (uint i = 0; i < MapSizeY(); i++) MakeVoid(TileXY(0, i));
	} else {
		for (uint i = 0; i < MapMaxX(); i++) {
			if (TileHeight(TileXY(i, 1)) != 0) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_WATER, 0, 0);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxX(); i++) {
			if (!IsTileType(TileXY(i, MapMaxY() - 1), MP_WATER) || TileHeight(TileXY(1, MapMaxY())) != 0) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_WATER, 0, 0);
				return false;
			}
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			if (TileHeight(TileXY(1, i)) != 0) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_WATER, 0, 0);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxY(); i++) {
			if (!IsTileType(TileXY(MapMaxX() - 1, i), MP_WATER) || TileHeight(TileXY(MapMaxX(), i)) != 0) {
				ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_EDGES_NOT_WATER, 0, 0);
				return false;
			}
		}
		/* Make tiles at the border water again. */
		for (uint i = 0; i < MapMaxX(); i++) {
			SetTileHeight(TileXY(i, 0), 0);
			SetTileType(TileXY(i, 0), MP_WATER);
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			SetTileHeight(TileXY(0, i), 0);
			SetTileType(TileXY(0, i), MP_WATER);
		}
	}
	MarkWholeScreenDirty();
	return true;
}

/**
 * Changing the setting "allow multiple NewGRF sets" is not allowed
 * if there are vehicles.
 */
static bool ChangeDynamicEngines(int32 p1)
{
	if (_game_mode == GM_MENU) return true;

	const Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (IsCompanyBuildableVehicleType(v)) {
			ShowErrorMessage(INVALID_STRING_ID, STR_CONFIG_SETTING_DYNAMIC_ENGINES_EXISTING_VEHICLES, 0, 0);
			return false;
		}
	}

	/* Reset the engines, they will get new EngineIDs */
	_engine_mngr.ResetToDefaultMapping();
	ReloadNewGRFData();

	return true;
}

#ifdef ENABLE_NETWORK

static bool UpdateClientName(int32 p1)
{
	NetworkUpdateClientName();
	return true;
}

static bool UpdateServerPassword(int32 p1)
{
	if (strcmp(_settings_client.network.server_password, "*") == 0) {
		_settings_client.network.server_password[0] = '\0';
	}

	return true;
}

static bool UpdateRconPassword(int32 p1)
{
	if (strcmp(_settings_client.network.rcon_password, "*") == 0) {
		_settings_client.network.rcon_password[0] = '\0';
	}

	return true;
}

static bool UpdateClientConfigValues(int32 p1)
{
	if (_network_server) NetworkServerSendConfigUpdate();

	return true;
}

#endif /* ENABLE_NETWORK */


/* End - Callback Functions */

static const SettingDesc _music_settings[] = {
	 SDT_VAR(MusicFileSettings, playlist,   SLE_UINT8, S, 0,   0, 0,   5, 1,  STR_NULL, NULL),
	 SDT_VAR(MusicFileSettings, music_vol,  SLE_UINT8, S, 0, 127, 0, 127, 1,  STR_NULL, NULL),
	 SDT_VAR(MusicFileSettings, effect_vol, SLE_UINT8, S, 0, 127, 0, 127, 1,  STR_NULL, NULL),
	SDT_LIST(MusicFileSettings, custom_1,   SLE_UINT8, S, 0, NULL,            STR_NULL, NULL),
	SDT_LIST(MusicFileSettings, custom_2,   SLE_UINT8, S, 0, NULL,            STR_NULL, NULL),
	SDT_BOOL(MusicFileSettings, playing,               S, 0, true,            STR_NULL, NULL),
	SDT_BOOL(MusicFileSettings, shuffle,               S, 0, false,           STR_NULL, NULL),
	 SDT_END()
};

/* win32_v.cpp only settings */
#if defined(WIN32) && !defined(DEDICATED)
extern bool _force_full_redraw, _window_maximize;
extern uint _display_hz, _fullscreen_bpp;

static const SettingDescGlobVarList _win32_settings[] = {
	 SDTG_VAR("display_hz",     SLE_UINT, S, 0, _display_hz,       0, 0, 120, 0, STR_NULL, NULL),
	SDTG_BOOL("force_full_redraw",        S, 0, _force_full_redraw,false,        STR_NULL, NULL),
	 SDTG_VAR("fullscreen_bpp", SLE_UINT, S, 0, _fullscreen_bpp,   8, 8,  32, 0, STR_NULL, NULL),
	SDTG_BOOL("window_maximize",          S, 0, _window_maximize,  false,        STR_NULL, NULL),
	 SDTG_END()
};
#endif /* WIN32 */

static const SettingDescGlobVarList _misc_settings[] = {
	SDTG_MMANY("display_opt",     SLE_UINT8, S, 0, _display_opt,       (1 << DO_SHOW_TOWN_NAMES | 1 << DO_SHOW_STATION_NAMES | 1 << DO_SHOW_SIGNS | 1 << DO_FULL_ANIMATION | 1 << DO_FULL_DETAIL | 1 << DO_WAYPOINTS), "SHOW_TOWN_NAMES|SHOW_STATION_NAMES|SHOW_SIGNS|FULL_ANIMATION||FULL_DETAIL|WAYPOINTS", STR_NULL, NULL),
	 SDTG_BOOL("news_ticker_sound",          S, 0, _news_ticker_sound,     true,    STR_NULL, NULL),
	 SDTG_BOOL("fullscreen",                 S, 0, _fullscreen,           false,    STR_NULL, NULL),
	  SDTG_STR("graphicsset",      SLE_STRQ, S, 0, _ini_graphics_set,      NULL,    STR_NULL, NULL),
	  SDTG_STR("videodriver",      SLE_STRQ, S, 0, _ini_videodriver,       NULL,    STR_NULL, NULL),
	  SDTG_STR("musicdriver",      SLE_STRQ, S, 0, _ini_musicdriver,       NULL,    STR_NULL, NULL),
	  SDTG_STR("sounddriver",      SLE_STRQ, S, 0, _ini_sounddriver,       NULL,    STR_NULL, NULL),
	  SDTG_STR("blitter",          SLE_STRQ, S, 0, _ini_blitter,           NULL,    STR_NULL, NULL),
	  SDTG_STR("language",         SLE_STRB, S, 0, _dynlang.curr_file,     NULL,    STR_NULL, NULL),
#ifdef N3DS
	SDTG_CONDLIST("resolution",  SLE_INT, 2, S, 0, _cur_resolution,   "320,240",    STR_NULL, NULL, 0, SL_MAX_VERSION), // workaround for implicit lengthof() in SDTG_LIST
#else
	SDTG_CONDLIST("resolution",  SLE_INT, 2, S, 0, _cur_resolution,   "640,480",    STR_NULL, NULL, 0, SL_MAX_VERSION), // workaround for implicit lengthof() in SDTG_LIST
#endif
	  SDTG_STR("screenshot_format",SLE_STRB, S, 0, _screenshot_format_name,NULL,    STR_NULL, NULL),
	  SDTG_STR("savegame_format",  SLE_STRB, S, 0, _savegame_format,       NULL,    STR_NULL, NULL),
	 SDTG_BOOL("rightclick_emulate",         S, 0, _rightclick_emulate,   false,    STR_NULL, NULL),
#ifdef WITH_FREETYPE
	  SDTG_STR("small_font",       SLE_STRB, S, 0, _freetype.small_font,   NULL,    STR_NULL, NULL),
	  SDTG_STR("medium_font",      SLE_STRB, S, 0, _freetype.medium_font,  NULL,    STR_NULL, NULL),
	  SDTG_STR("large_font",       SLE_STRB, S, 0, _freetype.large_font,   NULL,    STR_NULL, NULL),
	  SDTG_VAR("small_size",       SLE_UINT, S, 0, _freetype.small_size,   6, 0, 72, 0, STR_NULL, NULL),
	  SDTG_VAR("medium_size",      SLE_UINT, S, 0, _freetype.medium_size, 10, 0, 72, 0, STR_NULL, NULL),
	  SDTG_VAR("large_size",       SLE_UINT, S, 0, _freetype.large_size,  16, 0, 72, 0, STR_NULL, NULL),
	 SDTG_BOOL("small_aa",                   S, 0, _freetype.small_aa,    false,    STR_NULL, NULL),
	 SDTG_BOOL("medium_aa",                  S, 0, _freetype.medium_aa,   false,    STR_NULL, NULL),
	 SDTG_BOOL("large_aa",                   S, 0, _freetype.large_aa,    false,    STR_NULL, NULL),
#endif
	  SDTG_VAR("sprite_cache_size",SLE_UINT, S, 0, _sprite_cache_size,     4, 1, 64, 0, STR_NULL, NULL),
	  SDTG_VAR("player_face",    SLE_UINT32, S, 0, _company_manager_face,0,0,0xFFFFFFFF,0, STR_NULL, NULL),
	  SDTG_VAR("transparency_options", SLE_UINT, S, 0, _transparency_opt,  0,0,0x1FF,0, STR_NULL, NULL),
	  SDTG_VAR("transparency_locks", SLE_UINT, S, 0, _transparency_lock,   0,0,0x1FF,0, STR_NULL, NULL),
	  SDTG_VAR("invisibility_options", SLE_UINT, S, 0, _invisibility_opt,  0,0, 0xFF,0, STR_NULL, NULL),
	  SDTG_STR("keyboard",         SLE_STRB, S, 0, _keyboard_opt[0],       NULL,    STR_NULL, NULL),
	  SDTG_STR("keyboard_caps",    SLE_STRB, S, 0, _keyboard_opt[1],       NULL,    STR_NULL, NULL),
	  SDTG_END()
};

static const uint GAME_DIFFICULTY_NUM = 18;
uint16 _old_diff_custom[GAME_DIFFICULTY_NUM];

static const SettingDesc _gameopt_settings[] = {
	/* In version 4 a new difficulty setting has been added to the difficulty settings,
	 * town attitude towards demolishing. Needs special handling because some dimwit thought
	 * it funny to have the GameDifficulty struct be an array while it is a struct of
	 * same-sized members
	 * XXX - To save file-space and since values are never bigger than about 10? only
	 * save the first 16 bits in the savegame. Question is why the values are still int32
	 * and why not byte for example?
	 * 'SLE_FILE_I16 | SLE_VAR_U16' in "diff_custom" is needed to get around SlArray() hack
	 * for savegames version 0 - though it is an array, it has to go through the byteswap process */
	 SDTG_GENERAL("diff_custom", SDT_INTLIST, SL_ARR, SLE_FILE_I16 | SLE_VAR_U16,    C, 0, _old_diff_custom, 17, 0, 0, 0, 0, NULL, STR_NULL, NULL, 0,  3),
	 SDTG_GENERAL("diff_custom", SDT_INTLIST, SL_ARR, SLE_UINT16,                    C, 0, _old_diff_custom, 18, 0, 0, 0, 0, NULL, STR_NULL, NULL, 4, SL_MAX_VERSION),

	      SDT_VAR(GameSettings, difficulty.diff_level,    SLE_UINT8,                     0, 0, 0, 0,  3, 0, STR_NULL, NULL),
	    SDT_OMANY(GameSettings, locale.currency,          SLE_UINT8,                     N, 0, 0, CUSTOM_CURRENCY_ID, "GBP|USD|EUR|YEN|ATS|BEF|CHF|CZK|DEM|DKK|ESP|FIM|FRF|GRD|HUF|ISK|ITL|NLG|NOK|PLN|ROL|RUR|SIT|SEK|YTL|SKK|BRL|EEK|custom", STR_NULL, NULL, NULL),
	    SDT_OMANY(GameSettings, locale.units,             SLE_UINT8,                     N, 0, 1, 2, "imperial|metric|si", STR_NULL, NULL, NULL),
	/* There are only 21 predefined town_name values (0-20), but you can have more with newgrf action F so allow these bigger values (21-255). Invalid values will fallback to english on use and (undefined string) in GUI. */
	    SDT_OMANY(GameSettings, game_creation.town_name,  SLE_UINT8,                     0, 0, 0, 255, "english|french|german|american|latin|silly|swedish|dutch|finnish|polish|slovakish|norwegian|hungarian|austrian|romanian|czech|swiss|danish|turkish|italian|catalan", STR_NULL, NULL, NULL),
	    SDT_OMANY(GameSettings, game_creation.landscape,  SLE_UINT8,                     0, 0, 0, 3, "temperate|arctic|tropic|toyland", STR_NULL, NULL, ConvertLandscape),
	      SDT_VAR(GameSettings, game_creation.snow_line,  SLE_UINT8,                     0, 0, 7 * TILE_HEIGHT, 2 * TILE_HEIGHT, 13 * TILE_HEIGHT, 0, STR_NULL, NULL),
	 SDT_CONDNULL(                                                1,  0, 22),
 SDTC_CONDOMANY(              gui.autosave,             SLE_UINT8, 23, SL_MAX_VERSION, S, 0, 1, 4, "off|monthly|quarterly|half year|yearly", STR_NULL, NULL),
	    SDT_OMANY(GameSettings, vehicle.road_side,        SLE_UINT8,                     0, 0, 1, 1, "left|right", STR_NULL, NULL, NULL),
	    SDT_END()
};

/* Some settings do not need to be synchronised when playing in multiplayer.
 * These include for example the GUI settings and will not be saved with the
 * savegame.
 * It is also a bit tricky since you would think that service_interval
 * for example doesn't need to be synched. Every client assigns the
 * service_interval value to the v->service_interval, meaning that every client
 * assigns his value. If the setting was company-based, that would mean that
 * vehicles could decide on different moments that they are heading back to a
 * service depot, causing desyncs on a massive scale. */
const SettingDesc _settings[] = {
	/***************************************************************************/
	/* Saved settings variables. */
	/* Do not ADD or REMOVE something in this "difficulty.XXX" table or before it. It breaks savegame compatability. */
	 SDT_CONDVAR(GameSettings, difficulty.max_no_competitors,        SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     2,0,MAX_COMPANIES-1,1,STR_NULL,                                  DifficultyChange),
	SDT_CONDNULL(                                                            1, 97, 109),
	 SDT_CONDVAR(GameSettings, difficulty.number_towns,              SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     2,     0,      4,  1, STR_NUM_VERY_LOW,                          DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.number_industries,         SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     4,     0,      4,  1, STR_NONE,                                  DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.max_loan,                 SLE_UINT32, 97, SL_MAX_VERSION, 0,NG|CR,300000,100000,500000,50000,STR_NULL,                               DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.initial_interest,          SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     2,     2,      4,  1, STR_NULL,                                  DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.vehicle_costs,             SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     0,     0,      2,  1, STR_6820_LOW,                              DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.competitor_speed,          SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     2,     0,      4,  1, STR_681B_VERY_SLOW,                        DifficultyChange),
	SDT_CONDNULL(                                                            1, 97, 109),
	 SDT_CONDVAR(GameSettings, difficulty.vehicle_breakdowns,        SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     1,     0,      2,  1, STR_6823_NONE,                             DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.subsidy_multiplier,        SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     2,     0,      3,  1, STR_6826_X1_5,                             DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.construction_cost,         SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     0,     0,      2,  1, STR_6820_LOW,                              DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.terrain_type,              SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     1,     0,      3,  1, STR_682A_VERY_FLAT,                        DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.quantity_sea_lakes,        SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     0,     0,      3,  1, STR_VERY_LOW,                              DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.economy,                   SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     0,     0,      1,  1, STR_682E_STEADY,                           DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.line_reverse_mode,         SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     0,     0,      1,  1, STR_6834_AT_END_OF_LINE_AND_AT_STATIONS,   DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.disasters,                 SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     0,     0,      1,  1, STR_6836_OFF,                              DifficultyChange),
	 SDT_CONDVAR(GameSettings, difficulty.town_council_tolerance,    SLE_UINT8, 97, SL_MAX_VERSION, 0, 0,     0,     0,      2,  1, STR_PERMISSIVE,                            DifficultyNoiseChange),
	 SDT_CONDVAR(GameSettings, difficulty.diff_level,                SLE_UINT8, 97, SL_MAX_VERSION, 0,NG,     0,     0,      3,  0, STR_NULL,                                  DifficultyReset),

	/* There are only 21 predefined town_name values (0-20), but you can have more with newgrf action F so allow these bigger values (21-255). Invalid values will fallback to english on use and (undefined string) in GUI. */
 SDT_CONDOMANY(GameSettings, game_creation.town_name,              SLE_UINT8, 97, SL_MAX_VERSION, 0,NN, 0, 255, "english|french|german|american|latin|silly|swedish|dutch|finnish|polish|slovakish|norwegian|hungarian|austrian|romanian|czech|swiss|danish|turkish|italian|catalan", STR_NULL, NULL, NULL),
 SDT_CONDOMANY(GameSettings, game_creation.landscape,              SLE_UINT8, 97, SL_MAX_VERSION, 0,NN, 0,   3, "temperate|arctic|tropic|toyland", STR_NULL, NULL, ConvertLandscape),
	 SDT_CONDVAR(GameSettings, game_creation.snow_line,              SLE_UINT8, 97, SL_MAX_VERSION, 0,NN, 7 * TILE_HEIGHT, 2 * TILE_HEIGHT, 13 * TILE_HEIGHT, 0, STR_NULL, NULL),
 SDT_CONDOMANY(GameSettings, vehicle.road_side,                    SLE_UINT8, 97, SL_MAX_VERSION, 0,NN, 1,   1, "left|right", STR_NULL, CheckRoadSide, NULL),

	    SDT_BOOL(GameSettings, construction.build_on_slopes,                                        0,NN,  true,                    STR_CONFIG_SETTING_BUILDONSLOPES,          NULL),
	SDT_CONDBOOL(GameSettings, construction.autoslope,                          75, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_AUTOSLOPE,              NULL),
	    SDT_BOOL(GameSettings, construction.extra_dynamite,                                         0, 0, false,                    STR_CONFIG_SETTING_EXTRADYNAMITE,          NULL),
	    SDT_BOOL(GameSettings, construction.longbridges,                                            0,NN,  true,                    STR_CONFIG_SETTING_LONGBRIDGES,            NULL),
	    SDT_BOOL(GameSettings, construction.signal_side,                                            N,NN,  true,                    STR_CONFIG_SETTING_SIGNALSIDE,             RedrawScreen),
	    SDT_BOOL(GameSettings, station.always_small_airport,                                        0,NN, false,                    STR_CONFIG_SETTING_SMALL_AIRPORTS,         NULL),
	 SDT_CONDVAR(GameSettings, economy.town_layout,                  SLE_UINT8, 59, SL_MAX_VERSION, 0,MS,TL_ORIGINAL,TL_BEGIN,NUM_TLS-1,1, STR_CONFIG_SETTING_TOWN_LAYOUT,     NULL),
	SDT_CONDBOOL(GameSettings, economy.allow_town_roads,                       113, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_ALLOW_TOWN_ROADS,       NULL),

	     SDT_VAR(GameSettings, vehicle.train_acceleration_model,     SLE_UINT8,                     0,MS,     0,     0,       1, 1, STR_CONFIG_SETTING_TRAIN_ACCELERATION_MODEL, TrainAccelerationModelChanged),
	    SDT_BOOL(GameSettings, pf.forbid_90_deg,                                                    0, 0, false,                    STR_CONFIG_SETTING_FORBID_90_DEG,          NULL),
	    SDT_BOOL(GameSettings, vehicle.mammoth_trains,                                              0,NN,  true,                    STR_CONFIG_SETTING_MAMMOTHTRAINS,          NULL),
	    SDT_BOOL(GameSettings, order.gotodepot,                                                     0, 0,  true,                    STR_CONFIG_SETTING_GOTODEPOT,              NULL),
	    SDT_BOOL(GameSettings, pf.roadveh_queue,                                                    0, 0,  true,                    STR_CONFIG_SETTING_ROADVEH_QUEUE,          NULL),

	SDT_CONDBOOL(GameSettings, pf.new_pathfinding_all,                           0,             86, 0, 0, false,                    STR_NULL,                                  NULL),
	SDT_CONDBOOL(GameSettings, pf.yapf.ship_use_yapf,                           28,             86, 0, 0, false,                    STR_NULL,                                  NULL),
	SDT_CONDBOOL(GameSettings, pf.yapf.road_use_yapf,                           28,             86, 0, 0,  true,                    STR_NULL,                                  NULL),
	SDT_CONDBOOL(GameSettings, pf.yapf.rail_use_yapf,                           28,             86, 0, 0,  true,                    STR_NULL,                                  NULL),

	 SDT_CONDVAR(GameSettings, pf.pathfinder_for_trains,             SLE_UINT8, 87, SL_MAX_VERSION, 0, MS,    2,     0,       2, 1, STR_CONFIG_SETTING_PATHFINDER_FOR_TRAINS,  NULL),
	 SDT_CONDVAR(GameSettings, pf.pathfinder_for_roadvehs,           SLE_UINT8, 87, SL_MAX_VERSION, 0, MS,    2,     0,       2, 1, STR_CONFIG_SETTING_PATHFINDER_FOR_ROADVEH, NULL),
	 SDT_CONDVAR(GameSettings, pf.pathfinder_for_ships,              SLE_UINT8, 87, SL_MAX_VERSION, 0, MS,    0,     0,       2, 1, STR_CONFIG_SETTING_PATHFINDER_FOR_SHIPS,   NULL),

	    SDT_BOOL(GameSettings, vehicle.never_expire_vehicles,                                       0,NN, false,                    STR_CONFIG_SETTING_NEVER_EXPIRE_VEHICLES,  NULL),
	     SDT_VAR(GameSettings, vehicle.max_trains,                  SLE_UINT16,                     0, 0,   500,     0,    5000, 0, STR_CONFIG_SETTING_MAX_TRAINS,             RedrawScreen),
	     SDT_VAR(GameSettings, vehicle.max_roadveh,                 SLE_UINT16,                     0, 0,   500,     0,    5000, 0, STR_CONFIG_SETTING_MAX_ROADVEH,            RedrawScreen),
	     SDT_VAR(GameSettings, vehicle.max_aircraft,                SLE_UINT16,                     0, 0,   200,     0,    5000, 0, STR_CONFIG_SETTING_MAX_AIRCRAFT,           RedrawScreen),
	     SDT_VAR(GameSettings, vehicle.max_ships,                   SLE_UINT16,                     0, 0,   300,     0,    5000, 0, STR_CONFIG_SETTING_MAX_SHIPS,              RedrawScreen),
	    SDT_BOOL(GameSettings, vehicle.servint_ispercent,                                           0,NN, false,                    STR_CONFIG_SETTING_SERVINT_ISPERCENT,      CheckInterval),
	     SDT_VAR(GameSettings, vehicle.servint_trains,              SLE_UINT16,                     0,D0,   150,     5,     800, 0, STR_CONFIG_SETTING_SERVINT_TRAINS,         InvalidateDetailsWindow),
	     SDT_VAR(GameSettings, vehicle.servint_roadveh,             SLE_UINT16,                     0,D0,   150,     5,     800, 0, STR_CONFIG_SETTING_SERVINT_ROADVEH,        InvalidateDetailsWindow),
	     SDT_VAR(GameSettings, vehicle.servint_ships,               SLE_UINT16,                     0,D0,   360,     5,     800, 0, STR_CONFIG_SETTING_SERVINT_SHIPS,          InvalidateDetailsWindow),
	     SDT_VAR(GameSettings, vehicle.servint_aircraft,            SLE_UINT16,                     0,D0,   100,     5,     800, 0, STR_CONFIG_SETTING_SERVINT_AIRCRAFT,       InvalidateDetailsWindow),
	    SDT_BOOL(GameSettings, order.no_servicing_if_no_breakdowns,                                 0, 0, false,                    STR_CONFIG_SETTING_NOSERVICE,              NULL),
	    SDT_BOOL(GameSettings, vehicle.wagon_speed_limits,                                          0,NN,  true,                    STR_CONFIG_SETTING_WAGONSPEEDLIMITS,       UpdateConsists),
	SDT_CONDBOOL(GameSettings, vehicle.disable_elrails,                         38, SL_MAX_VERSION, 0,NN, false,                    STR_CONFIG_SETTING_DISABLE_ELRAILS,        SettingsDisableElrail),
	 SDT_CONDVAR(GameSettings, vehicle.freight_trains,               SLE_UINT8, 39, SL_MAX_VERSION, 0,NN,     1,     1,     255, 1, STR_CONFIG_SETTING_FREIGHT_TRAINS,         NULL),
	SDT_CONDBOOL(GameSettings, order.timetabling,                               67, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_TIMETABLE_ALLOW,        NULL),
	 SDT_CONDVAR(GameSettings, vehicle.plane_speed,                  SLE_UINT8, 90, SL_MAX_VERSION, 0, 0,     4,     1,       4, 0, STR_CONFIG_SETTING_PLANE_SPEED,            NULL),
	SDT_CONDBOOL(GameSettings, vehicle.dynamic_engines,                         95, SL_MAX_VERSION, 0,NN, false,                    STR_CONFIG_SETTING_DYNAMIC_ENGINES,        ChangeDynamicEngines),

	    SDT_BOOL(GameSettings, station.join_stations,                                               0, 0,  true,                    STR_CONFIG_SETTING_JOINSTATIONS,           NULL),
	SDTC_CONDBOOL(             gui.sg_full_load_any,                            22,             92, 0, 0,  true,                    STR_NULL,                                  NULL),
	    SDT_BOOL(GameSettings, order.improved_load,                                                 0,NN,  true,                    STR_CONFIG_SETTING_IMPROVEDLOAD,           NULL),
	    SDT_BOOL(GameSettings, order.selectgoods,                                                   0, 0,  true,                    STR_CONFIG_SETTING_SELECTGOODS,            NULL),
	SDTC_CONDBOOL(             gui.sg_new_nonstop,                              22,             92, 0, 0, false,                    STR_NULL,                                  NULL),
	    SDT_BOOL(GameSettings, station.nonuniform_stations,                                         0,NN,  true,                    STR_CONFIG_SETTING_NONUNIFORM_STATIONS,    NULL),
	     SDT_VAR(GameSettings, station.station_spread,               SLE_UINT8,                     0, 0,    12,     4,      64, 0, STR_CONFIG_SETTING_STATION_SPREAD,         InvalidateStationBuildWindow),
	    SDT_BOOL(GameSettings, order.serviceathelipad,                                              0, 0,  true,                    STR_CONFIG_SETTING_SERVICEATHELIPAD,       NULL),
	    SDT_BOOL(GameSettings, station.modified_catchment,                                          0, 0,  true,                    STR_CONFIG_SETTING_CATCHMENT,              NULL),
	SDT_CONDBOOL(GameSettings, order.gradual_loading,                           40, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_GRADUAL_LOADING,        NULL),
	SDT_CONDBOOL(GameSettings, construction.road_stop_on_town_road,             47, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_STOP_ON_TOWN_ROAD,      NULL),
	SDT_CONDBOOL(GameSettings, construction.road_stop_on_competitor_road,      114, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_STOP_ON_COMPETITOR_ROAD,NULL),
	SDT_CONDBOOL(GameSettings, station.adjacent_stations,                       62, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_ADJACENT_STATIONS,      NULL),
	SDT_CONDBOOL(GameSettings, economy.station_noise_level,                     96, SL_MAX_VERSION, 0, 0, false,                    STR_CONFIG_SETTING_NOISE_LEVEL,            InvalidateTownViewWindow),
	SDT_CONDBOOL(GameSettings, station.distant_join_stations,                  106, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_DISTANT_JOIN_STATIONS,  DeleteSelectStationWindow),

	    SDT_BOOL(GameSettings, economy.inflation,                                                   0, 0,  true,                    STR_CONFIG_SETTING_INFLATION,              NULL),
	     SDT_VAR(GameSettings, construction.raw_industry_construction, SLE_UINT8,                   0,MS,     0,     0,       2, 0, STR_CONFIG_SETTING_RAW_INDUSTRY_CONSTRUCTION_METHOD, InvalidateBuildIndustryWindow),
	    SDT_BOOL(GameSettings, economy.multiple_industry_per_town,                                  0, 0, false,                    STR_CONFIG_SETTING_MULTIPINDTOWN,          NULL),
	    SDT_BOOL(GameSettings, economy.same_industry_close,                                         0, 0, false,                    STR_CONFIG_SETTING_SAMEINDCLOSE,           NULL),
	    SDT_BOOL(GameSettings, economy.bribe,                                                       0, 0,  true,                    STR_CONFIG_SETTING_BRIBE,                  NULL),
	SDT_CONDBOOL(GameSettings, economy.exclusive_rights,                        79, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_ALLOW_EXCLUSIVE,        NULL),
	SDT_CONDBOOL(GameSettings, economy.give_money,                              79, SL_MAX_VERSION, 0, 0,  true,                    STR_CONFIG_SETTING_ALLOW_GIVE_MONEY,       NULL),
	     SDT_VAR(GameSettings, game_creation.snow_line_height,       SLE_UINT8,                     0, 0,     7,     2,      13, 0, STR_CONFIG_SETTING_SNOWLINE_HEIGHT,        NULL),
	    SDTC_VAR(              gui.coloured_news_year,               SLE_INT32,                     0,NC,  2000,MIN_YEAR,MAX_YEAR,1,STR_CONFIG_SETTING_COLOURED_NEWS_YEAR,     NULL),
	     SDT_VAR(GameSettings, game_creation.starting_year,          SLE_INT32,                     0,NC,  1950,MIN_YEAR,MAX_YEAR,1,STR_CONFIG_SETTING_STARTING_YEAR,          NULL),
	SDT_CONDNULL(                                                            4,  0, 104),
	    SDT_BOOL(GameSettings, economy.smooth_economy,                                              0, 0,  true,                    STR_CONFIG_SETTING_SMOOTH_ECONOMY,         NULL),
	    SDT_BOOL(GameSettings, economy.allow_shares,                                                0, 0, false,                    STR_CONFIG_SETTING_ALLOW_SHARES,           NULL),
	 SDT_CONDVAR(GameSettings, economy.town_growth_rate,             SLE_UINT8, 54, SL_MAX_VERSION, 0, MS,    2,     0,       4, 0, STR_CONFIG_SETTING_TOWN_GROWTH,            NULL),
	 SDT_CONDVAR(GameSettings, economy.larger_towns,                 SLE_UINT8, 54, SL_MAX_VERSION, 0, D0,    4,     0,     255, 1, STR_CONFIG_SETTING_LARGER_TOWNS,           NULL),
	 SDT_CONDVAR(GameSettings, economy.initial_city_size,            SLE_UINT8, 56, SL_MAX_VERSION, 0, 0,     2,     1,      10, 1, STR_CONFIG_SETTING_CITY_SIZE_MULTIPLIER,   NULL),
	SDT_CONDBOOL(GameSettings, economy.mod_road_rebuild,                        77, SL_MAX_VERSION, 0, 0, false,                    STR_CONFIG_SETTING_MODIFIED_ROAD_REBUILD,  NULL),

	SDT_CONDNULL(1, 0, 106), // previously ai-new setting.
	    SDT_BOOL(GameSettings, ai.ai_in_multiplayer,                                                0, 0, true,                     STR_CONFIG_SETTING_AI_IN_MULTIPLAYER,      NULL),
	    SDT_BOOL(GameSettings, ai.ai_disable_veh_train,                                             0, 0, false,                    STR_CONFIG_SETTING_AI_BUILDS_TRAINS,       NULL),
	    SDT_BOOL(GameSettings, ai.ai_disable_veh_roadveh,                                           0, 0, false,                    STR_CONFIG_SETTING_AI_BUILDS_ROADVEH,      NULL),
	    SDT_BOOL(GameSettings, ai.ai_disable_veh_aircraft,                                          0, 0, false,                    STR_CONFIG_SETTING_AI_BUILDS_AIRCRAFT,     NULL),
	    SDT_BOOL(GameSettings, ai.ai_disable_veh_ship,                                              0, 0, false,                    STR_CONFIG_SETTING_AI_BUILDS_SHIPS,        NULL),
	 SDT_CONDVAR(GameSettings, ai.ai_max_opcode_till_suspend,       SLE_UINT32,107, SL_MAX_VERSION, 0, NG, 10000, 5000,250000,2500, STR_CONFIG_SETTING_AI_MAX_OPCODES,         NULL),

	     SDT_VAR(GameSettings, vehicle.extend_vehicle_life,          SLE_UINT8,                     0, 0,     0,     0,     100, 0, STR_NULL,                                  NULL),
	     SDT_VAR(GameSettings, economy.dist_local_authority,         SLE_UINT8,                     0, 0,    20,     5,      60, 0, STR_NULL,                                  NULL),
	     SDT_VAR(GameSettings, pf.wait_oneway_signal,                SLE_UINT8,                     0, 0,    15,     2,     255, 0, STR_NULL,                                  NULL),
	     SDT_VAR(GameSettings, pf.wait_twoway_signal,                SLE_UINT8,                     0, 0,    41,     2,     255, 0, STR_NULL,                                  NULL),
	SDT_CONDLISTO(GameSettings, economy.town_noise_population, 3,   SLE_UINT16, 96, SL_MAX_VERSION, 0,D0, "800,2000,4000",          STR_NULL,                                  NULL, CheckNoiseToleranceLevel),

	 SDT_CONDVAR(GameSettings, pf.wait_for_pbs_path,                 SLE_UINT8,100, SL_MAX_VERSION, 0, 0,    30,     2,     255, 0, STR_NULL,                                  NULL),
	SDT_CONDBOOL(GameSettings, pf.reserve_paths,                               100, SL_MAX_VERSION, 0, 0, false,                    STR_NULL,                                  NULL),
	 SDT_CONDVAR(GameSettings, pf.path_backoff_interval,             SLE_UINT8,100, SL_MAX_VERSION, 0, 0,    20,     1,     255, 0, STR_NULL,                                  NULL),

	     SDT_VAR(GameSettings, pf.opf.pf_maxlength,                          SLE_UINT16,                     0, 0,  4096,                    64,   65535, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.opf.pf_maxdepth,                            SLE_UINT8,                     0, 0,    48,                     4,     255, 0, STR_NULL,         NULL),

	     SDT_VAR(GameSettings, pf.npf.npf_max_search_nodes,                    SLE_UINT,                     0, 0, 10000,                   500,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_firstred_penalty,               SLE_UINT,                     0, 0, ( 10 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_firstred_exit_penalty,          SLE_UINT,                     0, 0, (100 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_lastred_penalty,                SLE_UINT,                     0, 0, ( 10 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_station_penalty,                SLE_UINT,                     0, 0, (  1 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_slope_penalty,                  SLE_UINT,                     0, 0, (  1 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_curve_penalty,                  SLE_UINT,                     0, 0, 1,                         0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_rail_depot_reverse_penalty,          SLE_UINT,                     0, 0, ( 50 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.npf.npf_rail_pbs_cross_penalty,              SLE_UINT,100, SL_MAX_VERSION, 0, 0, (  3 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.npf.npf_rail_pbs_signal_back_penalty,        SLE_UINT,100, SL_MAX_VERSION, 0, 0, ( 15 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_buoy_penalty,                        SLE_UINT,                     0, 0, (  2 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_water_curve_penalty,                 SLE_UINT,                     0, 0, (NPF_TILE_LENGTH / 4),     0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_road_curve_penalty,                  SLE_UINT,                     0, 0, 1,                         0,  100000, 0, STR_NULL,         NULL),
	     SDT_VAR(GameSettings, pf.npf.npf_crossing_penalty,                    SLE_UINT,                     0, 0, (  3 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.npf.npf_road_drive_through_penalty,          SLE_UINT, 47, SL_MAX_VERSION, 0, 0, (  8 * NPF_TILE_LENGTH),   0,  100000, 0, STR_NULL,         NULL),


	SDT_CONDBOOL(GameSettings, pf.yapf.disable_node_optimization,                        28, SL_MAX_VERSION, 0, 0, false,                                    STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.max_search_nodes,                       SLE_UINT, 28, SL_MAX_VERSION, 0, 0, 10000,                   500, 1000000, 0, STR_NULL,         NULL),
	SDT_CONDBOOL(GameSettings, pf.yapf.rail_firstred_twoway_eol,                         28, SL_MAX_VERSION, 0, 0,  true,                                    STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_firstred_penalty,                  SLE_UINT, 28, SL_MAX_VERSION, 0, 0,    10 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_firstred_exit_penalty,             SLE_UINT, 28, SL_MAX_VERSION, 0, 0,   100 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_lastred_penalty,                   SLE_UINT, 28, SL_MAX_VERSION, 0, 0,    10 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_lastred_exit_penalty,              SLE_UINT, 28, SL_MAX_VERSION, 0, 0,   100 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_station_penalty,                   SLE_UINT, 28, SL_MAX_VERSION, 0, 0,    10 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_slope_penalty,                     SLE_UINT, 28, SL_MAX_VERSION, 0, 0,     2 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_curve45_penalty,                   SLE_UINT, 28, SL_MAX_VERSION, 0, 0,     3 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_curve90_penalty,                   SLE_UINT, 28, SL_MAX_VERSION, 0, 0,     6 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_depot_reverse_penalty,             SLE_UINT, 28, SL_MAX_VERSION, 0, 0,    50 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_crossing_penalty,                  SLE_UINT, 28, SL_MAX_VERSION, 0, 0,     3 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_look_ahead_max_signals,            SLE_UINT, 28, SL_MAX_VERSION, 0, 0,    10,                     1,     100, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_look_ahead_signal_p0,               SLE_INT, 28, SL_MAX_VERSION, 0, 0,   500,              -1000000, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_look_ahead_signal_p1,               SLE_INT, 28, SL_MAX_VERSION, 0, 0,  -100,              -1000000, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_look_ahead_signal_p2,               SLE_INT, 28, SL_MAX_VERSION, 0, 0,     5,              -1000000, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_pbs_cross_penalty,                 SLE_UINT,100, SL_MAX_VERSION, 0, 0,     3 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_pbs_station_penalty,               SLE_UINT,100, SL_MAX_VERSION, 0, 0,     8 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_pbs_signal_back_penalty,           SLE_UINT,100, SL_MAX_VERSION, 0, 0,    15 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_doubleslip_penalty,                SLE_UINT,100, SL_MAX_VERSION, 0, 0,     1 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_longer_platform_penalty,           SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     8 * YAPF_TILE_LENGTH,  0,   20000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_longer_platform_per_tile_penalty,  SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     0 * YAPF_TILE_LENGTH,  0,   20000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_shorter_platform_penalty,          SLE_UINT, 33, SL_MAX_VERSION, 0, 0,    40 * YAPF_TILE_LENGTH,  0,   20000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.rail_shorter_platform_per_tile_penalty, SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     0 * YAPF_TILE_LENGTH,  0,   20000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.road_slope_penalty,                     SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     2 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.road_curve_penalty,                     SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     1 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.road_crossing_penalty,                  SLE_UINT, 33, SL_MAX_VERSION, 0, 0,     3 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),
	 SDT_CONDVAR(GameSettings, pf.yapf.road_stop_penalty,                      SLE_UINT, 47, SL_MAX_VERSION, 0, 0,     8 * YAPF_TILE_LENGTH,  0, 1000000, 0, STR_NULL,         NULL),

	 SDT_CONDVAR(GameSettings, game_creation.land_generator,                  SLE_UINT8, 30, SL_MAX_VERSION, 0,MS,     1,                     0,       1, 0, STR_CONFIG_SETTING_LAND_GENERATOR,        NULL),
	 SDT_CONDVAR(GameSettings, game_creation.oil_refinery_limit,              SLE_UINT8, 30, SL_MAX_VERSION, 0, 0,    32,                    12,      48, 0, STR_CONFIG_SETTING_OIL_REF_EDGE_DISTANCE, NULL),
	 SDT_CONDVAR(GameSettings, game_creation.tgen_smoothness,                 SLE_UINT8, 30, SL_MAX_VERSION, 0,MS,     1,                     0,       3, 0, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN,  NULL),
	 SDT_CONDVAR(GameSettings, game_creation.generation_seed,                SLE_UINT32, 30, SL_MAX_VERSION, 0, 0,      GENERATE_NEW_SEED, 0, UINT32_MAX, 0, STR_NULL,                                 NULL),
	 SDT_CONDVAR(GameSettings, game_creation.tree_placer,                     SLE_UINT8, 30, SL_MAX_VERSION, 0,MS,     2,                     0,       2, 0, STR_CONFIG_SETTING_TREE_PLACER,           NULL),
	     SDT_VAR(GameSettings, game_creation.heightmap_rotation,              SLE_UINT8,                     S,MS,     0,                     0,       1, 0, STR_CONFIG_SETTING_HEIGHTMAP_ROTATION,    NULL),
	     SDT_VAR(GameSettings, game_creation.se_flat_world_height,            SLE_UINT8,                     S, 0,     1,                     0,      15, 0, STR_CONFIG_SETTING_SE_FLAT_WORLD_HEIGHT,  NULL),

	     SDT_VAR(GameSettings, game_creation.map_x,                           SLE_UINT8,                     S, 0,     8,                     6,      11, 0, STR_CONFIG_SETTING_MAP_X,                 NULL),
	     SDT_VAR(GameSettings, game_creation.map_y,                           SLE_UINT8,                     S, 0,     8,                     6,      11, 0, STR_CONFIG_SETTING_MAP_Y,                 NULL),
	SDT_CONDBOOL(GameSettings, construction.freeform_edges,                             111, SL_MAX_VERSION, 0, 0,  true,                                    STR_CONFIG_SETTING_ENABLE_FREEFORM_EDGES, CheckFreeformEdges),
	 SDT_CONDVAR(GameSettings, game_creation.water_borders,                   SLE_UINT8,111, SL_MAX_VERSION, 0, 0,    15,                     0,      16, 0, STR_NULL,                                 NULL),
	 SDT_CONDVAR(GameSettings, game_creation.custom_town_number,             SLE_UINT16,115, SL_MAX_VERSION, 0, 0,     1,                     1,    5000, 0, STR_NULL,                                 NULL),

 SDT_CONDOMANY(GameSettings, locale.currency,                               SLE_UINT8, 97, SL_MAX_VERSION, N, 0, 0, CUSTOM_CURRENCY_ID, "GBP|USD|EUR|YEN|ATS|BEF|CHF|CZK|DEM|DKK|ESP|FIM|FRF|GRD|HUF|ISK|ITL|NLG|NOK|PLN|ROL|RUR|SIT|SEK|YTL|SKK|BRR|custom", STR_NULL, NULL, NULL),
 SDT_CONDOMANY(GameSettings, locale.units,                                  SLE_UINT8, 97, SL_MAX_VERSION, N, 0, 1, 2, "imperial|metric|si", STR_NULL, NULL, NULL),

	/***************************************************************************/
	/* Unsaved setting variables. */
	SDTC_OMANY(gui.autosave,                  SLE_UINT8, S,  0, 1, 4, "off|monthly|quarterly|half year|yearly", STR_NULL,                     NULL),
	SDTC_OMANY(gui.date_format_in_default_names,SLE_UINT8,S,MS, 0, 2, "long|short|iso",       STR_CONFIG_SETTING_DATE_FORMAT_IN_SAVE_NAMES,   NULL),
	 SDTC_BOOL(gui.vehicle_speed,                        S,  0,  true,                        STR_CONFIG_SETTING_VEHICLESPEED,                NULL),
	 SDTC_BOOL(gui.status_long_date,                     S,  0,  true,                        STR_CONFIG_SETTING_LONGDATE,                    NULL),
	 SDTC_BOOL(gui.show_finances,                        S,  0,  true,                        STR_CONFIG_SETTING_SHOWFINANCES,                NULL),
	 SDTC_BOOL(gui.autoscroll,                           S,  0, false,                        STR_CONFIG_SETTING_AUTOSCROLL,                  NULL),
	 SDTC_BOOL(gui.reverse_scroll,                       S,  0, false,                        STR_CONFIG_SETTING_REVERSE_SCROLLING,           NULL),
	 SDTC_BOOL(gui.smooth_scroll,                        S,  0, false,                        STR_CONFIG_SETTING_SMOOTH_SCROLLING,            NULL),
	 SDTC_BOOL(gui.left_mouse_btn_scrolling,             S,  0, false,                        STR_CONFIG_SETTING_LEFT_MOUSE_BTN_SCROLLING,    NULL),
	 SDTC_BOOL(gui.measure_tooltip,                      S,  0,  true,                        STR_CONFIG_SETTING_MEASURE_TOOLTIP,             NULL),
	  SDTC_VAR(gui.errmsg_duration,           SLE_UINT8, S,  0,     5,        0,       20, 0, STR_CONFIG_SETTING_ERRMSG_DURATION,             NULL),
	  SDTC_VAR(gui.toolbar_pos,               SLE_UINT8, S, MS,     0,        0,        2, 0, STR_CONFIG_SETTING_TOOLBAR_POS,                 v_PositionMainToolbar),
	  SDTC_VAR(gui.window_snap_radius,        SLE_UINT8, S, D0,    10,        1,       32, 0, STR_CONFIG_SETTING_SNAP_RADIUS,                 NULL),
	  SDTC_VAR(gui.window_soft_limit,         SLE_UINT8, S, D0,    20,        5,      255, 1, STR_CONFIG_SETTING_SOFT_LIMIT,                  NULL),
	 SDTC_BOOL(gui.population_in_label,                  S,  0,  true,                        STR_CONFIG_SETTING_POPULATION_IN_LABEL,         PopulationInLabelActive),
	 SDTC_BOOL(gui.link_terraform_toolbar,               S,  0, false,                        STR_CONFIG_SETTING_LINK_TERRAFORM_TOOLBAR,      NULL),
	  SDTC_VAR(gui.liveries,                  SLE_UINT8, S, MS,     2,        0,        2, 0, STR_CONFIG_SETTING_LIVERIES,                    RedrawScreen),
	 SDTC_BOOL(gui.prefer_teamchat,                      S,  0, false,                        STR_CONFIG_SETTING_PREFER_TEAMCHAT,             NULL),
	  SDTC_VAR(gui.scrollwheel_scrolling,     SLE_UINT8, S, MS,     0,        0,        2, 0, STR_CONFIG_SETTING_SCROLLWHEEL_SCROLLING,       NULL),
	  SDTC_VAR(gui.scrollwheel_multiplier,    SLE_UINT8, S,  0,     5,        1,       15, 1, STR_CONFIG_SETTING_SCROLLWHEEL_MULTIPLIER,      NULL),
	 SDTC_BOOL(gui.pause_on_newgame,                     S,  0, false,                        STR_CONFIG_SETTING_PAUSE_ON_NEW_GAME,           NULL),
	  SDTC_VAR(gui.advanced_vehicle_list,     SLE_UINT8, S, MS,     1,        0,        2, 0, STR_CONFIG_SETTING_ADVANCED_VEHICLE_LISTS,      NULL),
	 SDTC_BOOL(gui.timetable_in_ticks,                   S,  0, false,                        STR_CONFIG_SETTING_TIMETABLE_IN_TICKS,          NULL),
	 SDTC_BOOL(gui.quick_goto,                           S,  0, false,                        STR_CONFIG_SETTING_QUICKGOTO,                   NULL),
	  SDTC_VAR(gui.loading_indicators,        SLE_UINT8, S, MS,     1,        0,        2, 0, STR_CONFIG_SETTING_LOADING_INDICATORS,          RedrawScreen),
	  SDTC_VAR(gui.default_rail_type,         SLE_UINT8, S, MS,     4,        0,        6, 0, STR_CONFIG_SETTING_DEFAULT_RAIL_TYPE,           NULL),
	 SDTC_BOOL(gui.enable_signal_gui,                    S,  0,  true,                        STR_CONFIG_SETTING_ENABLE_SIGNAL_GUI,           CloseSignalGUI),
	  SDTC_VAR(gui.drag_signals_density,      SLE_UINT8, S,  0,     4,        1,       20, 0, STR_CONFIG_SETTING_DRAG_SIGNALS_DENSITY,        DragSignalsDensityChanged),
	  SDTC_VAR(gui.semaphore_build_before,    SLE_INT32, S, NC,  1975, MIN_YEAR, MAX_YEAR, 1, STR_CONFIG_SETTING_SEMAPHORE_BUILD_BEFORE_DATE, ResetSignalVariant),
	 SDTC_BOOL(gui.vehicle_income_warn,                  S,  0,  true,                        STR_CONFIG_SETTING_WARN_INCOME_LESS,            NULL),
	  SDTC_VAR(gui.order_review_system,       SLE_UINT8, S, MS,     2,        0,        2, 0, STR_CONFIG_SETTING_ORDER_REVIEW,                NULL),
	 SDTC_BOOL(gui.lost_train_warn,                      S,  0,  true,                        STR_CONFIG_SETTING_WARN_LOST_TRAIN,             NULL),
	 SDTC_BOOL(gui.autorenew,                            S,  0, false,                        STR_CONFIG_SETTING_AUTORENEW_VEHICLE,           EngineRenewUpdate),
	  SDTC_VAR(gui.autorenew_months,          SLE_INT16, S,  0,     6,      -12,       12, 0, STR_CONFIG_SETTING_AUTORENEW_MONTHS,            EngineRenewMonthsUpdate),
	  SDTC_VAR(gui.autorenew_money,            SLE_UINT, S, CR,100000,        0,  2000000, 0, STR_CONFIG_SETTING_AUTORENEW_MONEY,             EngineRenewMoneyUpdate),
	 SDTC_BOOL(gui.always_build_infrastructure,          S,  0, false,                        STR_CONFIG_SETTING_ALWAYS_BUILD_INFRASTRUCTURE, RedrawScreen),
	 SDTC_BOOL(gui.new_nonstop,                          S,  0, false,                        STR_CONFIG_SETTING_NONSTOP_BY_DEFAULT,          NULL),
	 SDTC_BOOL(gui.keep_all_autosave,                    S,  0, false,                        STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.autosave_on_exit,                     S,  0, false,                        STR_NULL,                                       NULL),
	  SDTC_VAR(gui.max_num_autosaves,         SLE_UINT8, S,  0,    16,        0,      255, 0, STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.bridge_pillars,                       S,  0,  true,                        STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.auto_euro,                            S,  0,  true,                        STR_NULL,                                       NULL),
	  SDTC_VAR(gui.news_message_timeout,      SLE_UINT8, S,  0,     2,        1,      255, 0, STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.show_track_reservation,               S,  0, false,                        STR_CONFIG_SETTING_SHOW_TRACK_RESERVATION,      RedrawScreen),
	  SDTC_VAR(gui.default_signal_type,       SLE_UINT8, S, MS,     0,        0,        2, 1, STR_CONFIG_SETTING_DEFAULT_SIGNAL_TYPE,         NULL),
	  SDTC_VAR(gui.cycle_signal_types,        SLE_UINT8, S, MS,     2,        0,        2, 1, STR_CONFIG_SETTING_CYCLE_SIGNAL_TYPES,          NULL),
	  SDTC_VAR(gui.station_numtracks,         SLE_UINT8, S,  0,     1,        1,        7, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(gui.station_platlength,        SLE_UINT8, S,  0,     5,        1,        7, 0, STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.station_dragdrop,                     S,  0,  true,                        STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.station_show_coverage,                S,  0, false,                        STR_NULL,                                       NULL),
	 SDTC_BOOL(gui.persistent_buildingtools,             S,  0, false,                        STR_CONFIG_SETTING_PERSISTENT_BUILDINGTOOLS,    NULL),
	 SDTC_BOOL(gui.expenses_layout,                      S,  0, false,                        STR_CONFIG_SETTING_EXPENSES_LAYOUT,             RedrawScreen),

	  SDTC_VAR(gui.console_backlog_timeout,  SLE_UINT16, S,  0,   100,       10,    65500, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(gui.console_backlog_length,   SLE_UINT16, S,  0,   100,       10,    65500, 0, STR_NULL,                                       NULL),
#ifdef ENABLE_NETWORK
	  SDTC_VAR(gui.network_chat_box_width,   SLE_UINT16, S,  0,   700,      200,    65535, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(gui.network_chat_box_height,   SLE_UINT8, S,  0,    25,        5,      255, 0, STR_NULL,                                       NULL),

	  SDTC_VAR(network.sync_freq,            SLE_UINT16,C|S,NO,   100,        0,      100, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.frame_freq,            SLE_UINT8,C|S,NO,     0,        0,      100, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.max_join_time,        SLE_UINT16, S, NO,   500,        0,    32000, 0, STR_NULL,                                       NULL),
	 SDTC_BOOL(network.pause_on_join,                    S, NO,  true,                        STR_NULL,                                       NULL),
	  SDTC_STR(network.server_bind_ip,         SLE_STRB, S, NO, "0.0.0.0",                    STR_NULL,                                       NULL),
	  SDTC_VAR(network.server_port,          SLE_UINT16, S, NO,NETWORK_DEFAULT_PORT,0,65535,0,STR_NULL,                                       NULL),
	 SDTC_BOOL(network.server_advertise,                 S, NO, false,                        STR_NULL,                                       NULL),
	  SDTC_VAR(network.lan_internet,          SLE_UINT8, S, NO,     0,        0,        1, 0, STR_NULL,                                       NULL),
	  SDTC_STR(network.client_name,            SLE_STRB, S,  0,  NULL,                        STR_NULL,                                       UpdateClientName),
	  SDTC_STR(network.server_password,        SLE_STRB, S, NO,  NULL,                        STR_NULL,                                       UpdateServerPassword),
	  SDTC_STR(network.rcon_password,          SLE_STRB, S, NO,  NULL,                        STR_NULL,                                       UpdateRconPassword),
	  SDTC_STR(network.default_company_pass,   SLE_STRB, S,  0,  NULL,                        STR_NULL,                                       NULL),
	  SDTC_STR(network.server_name,            SLE_STRB, S, NO,  NULL,                        STR_NULL,                                       NULL),
	  SDTC_STR(network.connect_to_ip,          SLE_STRB, S,  0,  NULL,                        STR_NULL,                                       NULL),
	  SDTC_STR(network.network_id,             SLE_STRB, S, NO,  NULL,                        STR_NULL,                                       NULL),
	 SDTC_BOOL(network.autoclean_companies,              S, NO, false,                        STR_NULL,                                       NULL),
	  SDTC_VAR(network.autoclean_unprotected, SLE_UINT8, S,D0|NO,  12,     0,         240, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.autoclean_protected,   SLE_UINT8, S,D0|NO,  36,     0,         240, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.autoclean_novehicles,  SLE_UINT8, S,D0|NO,   0,     0,         240, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.max_companies,         SLE_UINT8, S, NO,     8,     1,MAX_COMPANIES,0, STR_NULL,                                       UpdateClientConfigValues),
	  SDTC_VAR(network.max_clients,           SLE_UINT8, S, NO,    16,     2, MAX_CLIENTS, 0, STR_NULL,                                       NULL),
	  SDTC_VAR(network.max_spectators,        SLE_UINT8, S, NO,     8,     0, MAX_CLIENTS, 0, STR_NULL,                                       UpdateClientConfigValues),
	  SDTC_VAR(network.restart_game_year,     SLE_INT32, S,D0|NO|NC,0, MIN_YEAR, MAX_YEAR, 1, STR_NULL,                                       NULL),
	  SDTC_VAR(network.min_active_clients,    SLE_UINT8, S, NO,     0,     0, MAX_CLIENTS, 0, STR_NULL,                                       NULL),
	SDTC_OMANY(network.server_lang,           SLE_UINT8, S, NO,     0,    35, "ANY|ENGLISH|GERMAN|FRENCH|BRAZILIAN|BULGARIAN|CHINESE|CZECH|DANISH|DUTCH|ESPERANTO|FINNISH|HUNGARIAN|ICELANDIC|ITALIAN|JAPANESE|KOREAN|LITHUANIAN|NORWEGIAN|POLISH|PORTUGUESE|ROMANIAN|RUSSIAN|SLOVAK|SLOVENIAN|SPANISH|SWEDISH|TURKISH|UKRAINIAN|AFRIKAANS|CROATIAN|CATALAN|ESTONIAN|GALICIAN|GREEK|LATVIAN", STR_NULL, NULL),
	 SDTC_BOOL(network.reload_cfg,                       S, NO, false,                        STR_NULL,                                       NULL),
	  SDTC_STR(network.last_host,              SLE_STRB, S,  0, "0.0.0.0",                    STR_NULL,                                       NULL),
	  SDTC_VAR(network.last_port,            SLE_UINT16, S,  0,     0,     0,  UINT16_MAX, 0, STR_NULL,                                       NULL),
#endif /* ENABLE_NETWORK */

	/*
	 * Since the network code (CmdChangeSetting and friends) use the index in this array to decide
	 * which setting the server is talking about all conditional compilation of this array must be at the
	 * end. This isn't really the best solution, the settings the server can tell the client about should
	 * either use a seperate array or some other form of identifier.
	 */

#ifdef __APPLE__
	/* We might need to emulate a right mouse button on mac */
	 SDTC_VAR(gui.right_mouse_btn_emulation, SLE_UINT8, S, MS, 0, 0, 2, 0, STR_CONFIG_SETTING_RIGHT_MOUSE_BTN_EMU, NULL),
#endif

	SDT_END()
};

static const SettingDesc _currency_settings[] = {
	SDT_VAR(CurrencySpec, rate,    SLE_UINT16, S, 0, 1,      0, UINT16_MAX, 0, STR_NULL, NULL),
	SDT_CHR(CurrencySpec, separator,           S, 0, ".",                      STR_NULL, NULL),
	SDT_VAR(CurrencySpec, to_euro,  SLE_INT32, S, 0, 0, MIN_YEAR, MAX_YEAR, 0, STR_NULL, NULL),
	SDT_STR(CurrencySpec, prefix,   SLE_STRBQ, S, 0, NULL,                     STR_NULL, NULL),
	SDT_STR(CurrencySpec, suffix,   SLE_STRBQ, S, 0, " credits",               STR_NULL, NULL),
	SDT_END()
};

/* Undefine for the shortcut macros above */
#undef S
#undef C
#undef N

#undef D0
#undef NC
#undef MS
#undef NO
#undef CR

/**
 * Prepare for reading and old diff_custom by zero-ing the memory.
 */
static void PrepareOldDiffCustom()
{
	memset(_old_diff_custom, 0, sizeof(_old_diff_custom));
}

/**
 * Reading of the old diff_custom array and transforming it to the new format.
 * @param savegame is it read from the config or savegame. In the latter case
 *                 we are sure there is an array; in the former case we have
 *                 to check that.
 */
static void HandleOldDiffCustom(bool savegame)
{
	uint options_to_load = GAME_DIFFICULTY_NUM - ((savegame && CheckSavegameVersion(4)) ? 1 : 0);

	if (!savegame) {
		/* If we did read to old_diff_custom, then at least one value must be non 0. */
		bool old_diff_custom_used = false;
		for (uint i = 0; i < options_to_load && !old_diff_custom_used; i++) {
			old_diff_custom_used = (_old_diff_custom[i] != 0);
		}

		if (!old_diff_custom_used) return;
	}

	for (uint i = 0; i < options_to_load; i++) {
		const SettingDesc *sd = &_settings[i];
		/* Skip deprecated options */
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to)) continue;
		void *var = GetVariableAddress(savegame ? &_settings_game : &_settings_newgame, &sd->save);
		Write_ValidateSetting(var, sd, (int32)((i == 4 ? 1000 : 1) * _old_diff_custom[i]));
	}
}

/** tries to convert newly introduced news settings based on old ones
 * @param name pointer to the string defining name of the old news config
 * @param value pointer to the string defining value of the old news config
 * @returns true if conversion could have been made */
bool ConvertOldNewsSetting(const char *name, const char *value)
{
	if (strcasecmp(name, "openclose") == 0) {
		/* openclose has been split in "open" and "close".
		 * So the job is now to decrypt the value of the old news config
		 * and give it to the two newly introduced ones*/

		NewsDisplay display = ND_OFF; // default
		if (strcasecmp(value, "full") == 0) {
			display = ND_FULL;
		} else if (strcasecmp(value, "summarized") == 0) {
			display = ND_SUMMARY;
		}
		/* tranfert of values */
		_news_type_data[NT_INDUSTRY_OPEN].display = display;
		_news_type_data[NT_INDUSTRY_CLOSE].display = display;
		return true;
	}
	return false;
}

static void NewsDisplayLoadConfig(IniFile *ini, const char *grpname)
{
	IniGroup *group = ini->GetGroup(grpname);
	IniItem *item;

	/* If no group exists, return */
	if (group == NULL) return;

	for (item = group->item; item != NULL; item = item->next) {
		int news_item = -1;
		for (int i = 0; i < NT_END; i++) {
			if (strcasecmp(item->name, _news_type_data[i].name) == 0) {
				news_item = i;
				break;
			}
		}

		/* the config been read is not within current aceptable config */
		if (news_item == -1) {
			/* if the conversion function cannot process it, advice by a debug warning*/
			if (!ConvertOldNewsSetting(item->name, item->value)) {
				DEBUG(misc, 0, "Invalid display option: %s", item->name);
			}
			/* in all cases, there is nothing left to do */
			continue;
		}

		if (strcasecmp(item->value, "full") == 0) {
			_news_type_data[news_item].display = ND_FULL;
		} else if (strcasecmp(item->value, "off") == 0) {
			_news_type_data[news_item].display = ND_OFF;
		} else if (strcasecmp(item->value, "summarized") == 0) {
			_news_type_data[news_item].display = ND_SUMMARY;
		} else {
			DEBUG(misc, 0, "Invalid display value: %s", item->value);
			continue;
		}
	}
}

static void AILoadConfig(IniFile *ini, const char *grpname)
{
	IniGroup *group = ini->GetGroup(grpname);
	IniItem *item;

	/* Clean any configured AI */
	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		AIConfig::GetConfig(c, true)->ChangeAI(NULL);
	}

	/* If no group exists, return */
	if (group == NULL) return;

	CompanyID c = COMPANY_FIRST;
	for (item = group->item; c < MAX_COMPANIES && item != NULL; c++, item = item->next) {
		AIConfig *config = AIConfig::GetConfig(c, true);

		config->ChangeAI(item->name);
		if (!config->HasAI()) {
			if (strcmp(item->name, "none") != 0) {
				DEBUG(ai, 0, "The AI by the name '%s' was no longer found, and removed from the list.", item->name);
				continue;
			}
		}
		config->StringToSettings(item->value);
	}
}

/* Load a GRF configuration from the given group name */
static GRFConfig *GRFLoadConfig(IniFile *ini, const char *grpname, bool is_static)
{
	IniGroup *group = ini->GetGroup(grpname);
	IniItem *item;
	GRFConfig *first = NULL;
	GRFConfig **curr = &first;

	if (group == NULL) return NULL;

	for (item = group->item; item != NULL; item = item->next) {
		GRFConfig *c = CallocT<GRFConfig>(1);
		c->filename = strdup(item->name);

		/* Parse parameters */
		if (!StrEmpty(item->value)) {
			c->num_params = parse_intlist(item->value, (int*)c->param, lengthof(c->param));
			if (c->num_params == (byte)-1) {
				ShowInfoF("ini: error in array '%s'", item->name);
				c->num_params = 0;
			}
		}

		/* Check if item is valid */
		if (!FillGRFDetails(c, is_static)) {
			const char *msg;

			if (c->status == GCS_NOT_FOUND) {
				msg = "not found";
			} else if (HasBit(c->flags, GCF_UNSAFE)) {
				msg = "unsafe for static use";
			} else if (HasBit(c->flags, GCF_SYSTEM)) {
				msg = "system NewGRF";
			} else {
				msg = "unknown";
			}

			ShowInfoF("ini: ignoring invalid NewGRF '%s': %s", item->name, msg);
			ClearGRFConfig(&c);
			continue;
		}

		/* Mark file as static to avoid saving in savegame. */
		if (is_static) SetBit(c->flags, GCF_STATIC);

		/* Add item to list */
		*curr = c;
		curr = &c->next;
	}

	return first;
}

static void NewsDisplaySaveConfig(IniFile *ini, const char *grpname)
{
	IniGroup *group = ini->GetGroup(grpname);

	for (int i = 0; i < NT_END; i++) {
		const char *value;
		int v = _news_type_data[i].display;

		value = (v == ND_OFF ? "off" : (v == ND_SUMMARY ? "summarized" : "full"));

		group->GetItem(_news_type_data[i].name, true)->SetValue(value);
	}
}

static void AISaveConfig(IniFile *ini, const char *grpname)
{
	IniGroup *group = ini->GetGroup(grpname);

	if (group == NULL) return;
	group->Clear();

	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		AIConfig *config = AIConfig::GetConfig(c, true);
		const char *name;
		char value[1024];
		config->SettingsToString(value, lengthof(value));

		if (config->HasAI()) {
			name = config->GetName();
		} else {
			name = "none";
		}

		IniItem *item = new IniItem(group, name, strlen(name));
		item->SetValue(value);
	}
}

/**
 * Save the version of OpenTTD to the ini file.
 * @param ini the ini to write to
 */
static void SaveVersionInConfig(IniFile *ini)
{
	IniGroup *group = ini->GetGroup("version");

	char version[9];
	snprintf(version, lengthof(version), "%08X", _openttd_newgrf_version);

	const char *versions[][2] = {
		{ "version_string", _openttd_revision },
		{ "version_number", version }
	};

	for (uint i = 0; i < lengthof(versions); i++) {
		group->GetItem(versions[i][0], true)->SetValue(versions[i][1]);
	}
}

/* Save a GRF configuration to the given group name */
static void GRFSaveConfig(IniFile *ini, const char *grpname, const GRFConfig *list)
{
	ini->RemoveGroup(grpname);
	IniGroup *group = ini->GetGroup(grpname);
	const GRFConfig *c;

	for (c = list; c != NULL; c = c->next) {
		char params[512];
		GRFBuildParamList(params, c, lastof(params));

		group->GetItem(c->filename, true)->SetValue(params);
	}
}

/* Common handler for saving/loading variables to the configuration file */
static void HandleSettingDescs(IniFile *ini, SettingDescProc *proc, SettingDescProcList *proc_list)
{
	proc(ini, (const SettingDesc*)_misc_settings,    "misc",  NULL);
	proc(ini, (const SettingDesc*)_music_settings,   "music", &msf);
#if defined(WIN32) && !defined(DEDICATED)
	proc(ini, (const SettingDesc*)_win32_settings,   "win32", NULL);
#endif /* WIN32 */

	proc(ini, _settings,         "patches",  &_settings_newgame);
	proc(ini, _currency_settings,"currency", &_custom_currency);

#ifdef ENABLE_NETWORK
	proc_list(ini, "servers", _network_host_list, lengthof(_network_host_list), NULL);
	proc_list(ini, "bans",    _network_ban_list,  lengthof(_network_ban_list), NULL);
#endif /* ENABLE_NETWORK */
}

static IniFile *IniLoadConfig()
{
	IniFile *ini = new IniFile(_list_group_names);
	ini->LoadFromDisk(_config_file);
	return ini;
}

/** Load the values from the configuration files */
void LoadFromConfig()
{
	IniFile *ini = IniLoadConfig();
	ResetCurrencies(false); // Initialize the array of curencies, without preserving the custom one

	HandleSettingDescs(ini, ini_load_settings, ini_load_setting_list);
	_grfconfig_newgame = GRFLoadConfig(ini, "newgrf", false);
	_grfconfig_static  = GRFLoadConfig(ini, "newgrf-static", true);
	NewsDisplayLoadConfig(ini, "news_display");
	AILoadConfig(ini, "ai_players");

	PrepareOldDiffCustom();
	ini_load_settings(ini, _gameopt_settings, "gameopt",  &_settings_newgame);
	HandleOldDiffCustom(false);

	CheckDifficultyLevels();
	delete ini;
}

/** Save the values to the configuration file */
void SaveToConfig()
{
	IniFile *ini = IniLoadConfig();

	/* Remove some obsolete groups. These have all been loaded into other groups. */
	ini->RemoveGroup("patches");
	ini->RemoveGroup("yapf");
	ini->RemoveGroup("gameopt");

	HandleSettingDescs(ini, ini_save_settings, ini_save_setting_list);
	GRFSaveConfig(ini, "newgrf", _grfconfig_newgame);
	GRFSaveConfig(ini, "newgrf-static", _grfconfig_static);
	NewsDisplaySaveConfig(ini, "news_display");
	AISaveConfig(ini, "ai_players");
	SaveVersionInConfig(ini);
	ini->SaveToDisk(_config_file);
	delete ini;
}

void GetGRFPresetList(GRFPresetList *list)
{
	list->Clear();

	IniFile *ini = IniLoadConfig();
	IniGroup *group;
	for (group = ini->group; group != NULL; group = group->next) {
		if (strncmp(group->name, "preset-", 7) == 0) {
			*list->Append() = strdup(group->name + 7);
		}
	}

	delete ini;
}

GRFConfig *LoadGRFPresetFromConfig(const char *config_name)
{
	char *section = (char*)alloca(strlen(config_name) + 8);
	sprintf(section, "preset-%s", config_name);

	IniFile *ini = IniLoadConfig();
	GRFConfig *config = GRFLoadConfig(ini, section, false);
	delete ini;

	return config;
}

void SaveGRFPresetToConfig(const char *config_name, GRFConfig *config)
{
	char *section = (char*)alloca(strlen(config_name) + 8);
	sprintf(section, "preset-%s", config_name);

	IniFile *ini = IniLoadConfig();
	GRFSaveConfig(ini, section, config);
	ini->SaveToDisk(_config_file);
	delete ini;
}

void DeleteGRFPresetFromConfig(const char *config_name)
{
	char *section = (char*)alloca(strlen(config_name) + 8);
	sprintf(section, "preset-%s", config_name);

	IniFile *ini = IniLoadConfig();
	ini->RemoveGroup(section);
	ini->SaveToDisk(_config_file);
	delete ini;
}

static const SettingDesc *GetSettingDescription(uint index)
{
	if (index >= lengthof(_settings)) return NULL;
	return &_settings[index];
}

/** Network-safe changing of settings (server-only).
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the index of the setting in the SettingDesc array which identifies it
 * @param p2 the new value for the setting
 * The new value is properly clamped to its minimum/maximum when setting
 * @see _settings
 */
CommandCost CmdChangeSetting(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	const SettingDesc *sd = GetSettingDescription(p1);

	if (sd == NULL) return CMD_ERROR;
	if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to)) return CMD_ERROR;

	if ((sd->desc.flags & SGF_NETWORK_ONLY) && !_networking && _game_mode != GM_MENU) return CMD_ERROR;
	if ((sd->desc.flags & SGF_NO_NETWORK) && _networking) return CMD_ERROR;
	if ((sd->desc.flags & SGF_NEWGAME_ONLY) && _game_mode != GM_MENU) return CMD_ERROR;

	if (flags & DC_EXEC) {
		GameSettings *s = (_game_mode == GM_MENU) ? &_settings_newgame : &_settings_game;
		void *var = GetVariableAddress(s, &sd->save);

		int32 oldval = (int32)ReadValue(var, sd->save.conv);
		int32 newval = (int32)p2;

		Write_ValidateSetting(var, sd, newval);
		newval = (int32)ReadValue(var, sd->save.conv);

		if (oldval == newval) return CommandCost();

		if (sd->desc.proc != NULL && !sd->desc.proc(newval)) {
			WriteValue(var, sd->save.conv, (int64)oldval);
			return CommandCost();
		}

		if (sd->desc.flags & SGF_NO_NETWORK) {
			GamelogStartAction(GLAT_SETTING);
			GamelogSetting(sd->desc.name, oldval, newval);
			GamelogStopAction();
		}

		InvalidateWindow(WC_GAME_OPTIONS, 0);
	}

	return CommandCost();
}

/** Top function to save the new value of an element of the Settings struct
 * @param index offset in the SettingDesc array of the Settings struct which
 * identifies the setting member we want to change
 * @param object pointer to a valid settings struct that has its settings change.
 * This only affects setting-members that are not needed to be the same on all
 * clients in a network game.
 * @param value new value of the setting */
bool SetSettingValue(uint index, int32 value)
{
	const SettingDesc *sd = &_settings[index];
	/* If an item is company-based, we do not send it over the network
	 * (if any) to change. Also *hack*hack* we update the _newgame version
	 * of settings because changing a company-based setting in a game also
	 * changes its defaults. At least that is the convention we have chosen */
	if (sd->save.conv & SLF_NETWORK_NO) {
		void *var = GetVariableAddress((_game_mode == GM_MENU) ? &_settings_newgame : &_settings_game, &sd->save);
		Write_ValidateSetting(var, sd, value);

		if (_game_mode != GM_MENU) {
			void *var2 = GetVariableAddress(&_settings_newgame, &sd->save);
			Write_ValidateSetting(var2, sd, value);
		}
		if (sd->desc.proc != NULL) sd->desc.proc((int32)ReadValue(var, sd->save.conv));
		InvalidateWindow(WC_GAME_OPTIONS, 0);
		return true;
	}

	/* send non-company-based settings over the network */
	if (!_networking || (_networking && _network_server)) {
		return DoCommandP(0, index, value, CMD_CHANGE_SETTING);
	}
	return false;
}

/**
 * Set a setting value with a string.
 * @param index the settings index.
 * @param value the value to write
 * @note CANNOT BE SAVED IN THE SAVEGAME.
 */
bool SetSettingValue(uint index, const char *value)
{
	const SettingDesc *sd = &_settings[index];
	assert(sd->save.conv & SLF_NETWORK_NO);

	char *var = (char*)GetVariableAddress(NULL, &sd->save);
	ttd_strlcpy(var, value, sd->save.length);
	if (sd->desc.proc != NULL) sd->desc.proc(0);

	return true;
}

/**
 * Given a name of setting, return a setting description of it.
 * @param name  Name of the setting to return a setting description of
 * @param i     Pointer to an integer that will contain the index of the setting after the call, if it is successful.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c NULL indicates failure to obtain the description
 */
const SettingDesc *GetSettingFromName(const char *name, uint *i)
{
	const SettingDesc *sd;

	/* First check all full names */
	for (*i = 0, sd = _settings; sd->save.cmd != SL_END; sd++, (*i)++) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to)) continue;
		if (strcmp(sd->desc.name, name) == 0) return sd;
	}

	/* Then check the shortcut variant of the name. */
	for (*i = 0, sd = _settings; sd->save.cmd != SL_END; sd++, (*i)++) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to)) continue;
		const char *short_name = strchr(sd->desc.name, '.');
		if (short_name != NULL) {
			short_name++;
			if (strcmp(short_name, name) == 0) return sd;
		}
	}

	return NULL;
}

/* Those 2 functions need to be here, else we have to make some stuff non-static
 * and besides, it is also better to keep stuff like this at the same place */
void IConsoleSetSetting(const char *name, const char *value)
{
	uint index;
	const SettingDesc *sd = GetSettingFromName(name, &index);

	if (sd == NULL) {
		IConsolePrintF(CC_WARNING, "'%s' is an unknown setting.", name);
		return;
	}

	bool success;
	if (sd->desc.cmd == SDT_STRING) {
		success = SetSettingValue(index, value);
	} else {
		uint32 val;
		extern bool GetArgumentInteger(uint32 *value, const char *arg);
		success = GetArgumentInteger(&val, value);
		if (success) success = SetSettingValue(index, val);
	}

	if (!success) {
		if (_network_server) {
			IConsoleError("This command/variable is not available during network games.");
		} else {
			IConsoleError("This command/variable is only available to a network server.");
		}
	}
}

void IConsoleSetSetting(const char *name, int value)
{
	uint index;
	const SettingDesc *sd = GetSettingFromName(name, &index);
	assert(sd != NULL);
	SetSettingValue(index, value);
}

/**
 * Output value of a specific setting to the console
 * @param name  Name of the setting to output its value
 */
void IConsoleGetSetting(const char *name)
{
	char value[20];
	uint index;
	const SettingDesc *sd = GetSettingFromName(name, &index);
	const void *ptr;

	if (sd == NULL) {
		IConsolePrintF(CC_WARNING, "'%s' is an unknown setting.", name);
		return;
	}

	ptr = GetVariableAddress((_game_mode == GM_MENU) ? &_settings_newgame : &_settings_game, &sd->save);

	if (sd->desc.cmd == SDT_STRING) {
		IConsolePrintF(CC_WARNING, "Current value for '%s' is: '%s'", name, (const char *)ptr);
	} else {
		if (sd->desc.cmd == SDT_BOOLX) {
			snprintf(value, sizeof(value), (*(bool*)ptr == 1) ? "on" : "off");
		} else {
			snprintf(value, sizeof(value), "%d", (int32)ReadValue(ptr, sd->save.conv));
		}

		IConsolePrintF(CC_WARNING, "Current value for '%s' is: '%s' (min: %s%d, max: %d)",
			name, value, (sd->desc.flags & SGF_0ISDISABLED) ? "(0) " : "", sd->desc.min, sd->desc.max);
	}
}

/**
 * List all settings and their value to the console
 *
 * @param prefilter  If not \c NULL, only list settings with names that begin with \a prefilter prefix
 */
void IConsoleListSettings(const char *prefilter)
{
	IConsolePrintF(CC_WARNING, "All settings with their current value:");

	for (const SettingDesc *sd = _settings; sd->save.cmd != SL_END; sd++) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to)) continue;
		if (prefilter != NULL) {
			if (strncmp(sd->desc.name, prefilter, min(strlen(sd->desc.name), strlen(prefilter))) != 0) continue;
		}
		char value[80];
		const void *ptr = GetVariableAddress((_game_mode == GM_MENU) ? &_settings_newgame : &_settings_game, &sd->save);

		if (sd->desc.cmd == SDT_BOOLX) {
			snprintf(value, lengthof(value), (*(bool*)ptr == 1) ? "on" : "off");
		} else if (sd->desc.cmd == SDT_STRING) {
			snprintf(value, sizeof(value), "%s", (const char *)ptr);
		} else {
			snprintf(value, lengthof(value), "%d", (uint32)ReadValue(ptr, sd->save.conv));
		}
		IConsolePrintF(CC_DEFAULT, "%s = %s", sd->desc.name, value);
	}

	IConsolePrintF(CC_WARNING, "Use 'setting' command to change a value");
}

/** Save and load handler for settings
 * @param osd SettingDesc struct containing all information
 * @param object can be either NULL in which case we load global variables or
 * a pointer to a struct which is getting saved */
static void LoadSettings(const SettingDesc *osd, void *object)
{
	for (; osd->save.cmd != SL_END; osd++) {
		const SaveLoad *sld = &osd->save;
		void *ptr = GetVariableAddress(object, sld);

		if (!SlObjectMember(ptr, sld)) continue;
	}
}

/** Loadhandler for a list of global variables
 * @param sdg pointer for the global variable list SettingDescGlobVarList
 * @note this is actually a stub for LoadSettings with the
 * object pointer set to NULL */
static inline void LoadSettingsGlobList(const SettingDescGlobVarList *sdg)
{
	LoadSettings((const SettingDesc*)sdg, NULL);
}

/** Save and load handler for settings
 * @param sd SettingDesc struct containing all information
 * @param object can be either NULL in which case we load global variables or
 * a pointer to a struct which is getting saved */
static void SaveSettings(const SettingDesc *sd, void *object)
{
	/* We need to write the CH_RIFF header, but unfortunately can't call
	 * SlCalcLength() because we have a different format. So do this manually */
	const SettingDesc *i;
	size_t length = 0;
	for (i = sd; i->save.cmd != SL_END; i++) {
		const void *ptr = GetVariableAddress(object, &i->save);
		length += SlCalcObjMemberLength(ptr, &i->save);
	}
	SlSetLength(length);

	for (i = sd; i->save.cmd != SL_END; i++) {
		void *ptr = GetVariableAddress(object, &i->save);
		SlObjectMember(ptr, &i->save);
	}
}

/** Savehandler for a list of global variables
 * @note this is actually a stub for SaveSettings with the
 * object pointer set to NULL */
static inline void SaveSettingsGlobList(const SettingDescGlobVarList *sdg)
{
	SaveSettings((const SettingDesc*)sdg, NULL);
}

static void Load_OPTS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * autosave-frequency stays when joining a network-server */
	PrepareOldDiffCustom();
	LoadSettings(_gameopt_settings, &_settings_game);
	HandleOldDiffCustom(true);
}

static void Load_PATS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * signal_side stays when joining a network-server */
	LoadSettings(_settings, &_settings_game);
}

static void Save_PATS()
{
	SaveSettings(_settings, &_settings_game);
}

void CheckConfig()
{
	/*
	 * Increase old default values for pf_maxdepth and pf_maxlength
	 * to support big networks.
	 */
	if (_settings_newgame.pf.opf.pf_maxdepth == 16 && _settings_newgame.pf.opf.pf_maxlength == 512) {
		_settings_newgame.pf.opf.pf_maxdepth = 48;
		_settings_newgame.pf.opf.pf_maxlength = 4096;
	}
}

extern const ChunkHandler _setting_chunk_handlers[] = {
	{ 'OPTS', NULL,      Load_OPTS, CH_RIFF},
	{ 'PATS', Save_PATS, Load_PATS, CH_RIFF | CH_LAST},
};

static bool IsSignedVarMemType(VarType vt)
{
	switch (GetVarMemType(vt)) {
		case SLE_VAR_I8:
		case SLE_VAR_I16:
		case SLE_VAR_I32:
		case SLE_VAR_I64:
			return true;
	}
	return false;
}
