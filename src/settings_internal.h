/* $Id$ */

/** @file settings_internal.h Functions and types used internally for the settings configurations. */

#ifndef SETTINGS_INTERNAL_H
#define SETTINGS_INTERNAL_H

#include "saveload/saveload.h"
#include "settings_type.h"

/** Convention/Type of settings. This is then further specified if necessary
 * with the SLE_ (SLE_VAR/SLE_FILE) enums in saveload.h
 * @see VarTypes
 * @see SettingDescBase */
enum SettingDescTypeLong {
	/* 4 bytes allocated a maximum of 16 types for GenericType */
	SDT_BEGIN       = 0,
	SDT_NUMX        = 0, ///< any number-type
	SDT_BOOLX       = 1, ///< a boolean number
	SDT_ONEOFMANY   = 2, ///< bitmasked number where only ONE bit may be set
	SDT_MANYOFMANY  = 3, ///< bitmasked number where MULTIPLE bits may be set
	SDT_INTLIST     = 4, ///< list of integers seperated by a comma ','
	SDT_STRING      = 5, ///< string with a pre-allocated buffer
	SDT_END,
	/* 10 more possible primitives */
};

template <> struct EnumPropsT<SettingDescTypeLong> : MakeEnumPropsT<SettingDescTypeLong, byte, SDT_BEGIN, SDT_END, SDT_END> {};
typedef TinyEnumT<SettingDescTypeLong> SettingDescType;


enum SettingGuiFlagLong {
	/* 8 bytes allocated for a maximum of 8 flags
	 * Flags directing saving/loading of a variable */
	SGF_NONE = 0,
	SGF_0ISDISABLED  = 1 << 0, ///< a value of zero means the feature is disabled
	SGF_NOCOMMA      = 1 << 1, ///< number without any thousand seperators (no formatting)
	SGF_MULTISTRING  = 1 << 2, ///< the value represents a limited number of string-options (internally integer)
	SGF_NETWORK_ONLY = 1 << 3, ///< this setting only applies to network games
	SGF_CURRENCY     = 1 << 4, ///< the number represents money, so when reading value multiply by exchange rate
	SGF_NO_NETWORK   = 1 << 5, ///< this setting does not apply to network games; it may not be changed during the game
	SGF_NEWGAME_ONLY = 1 << 6, ///< this setting cannot be changed in inside a game
	SGF_END          = 1 << 7,
};

DECLARE_ENUM_AS_BIT_SET(SettingGuiFlagLong);
template <> struct EnumPropsT<SettingGuiFlagLong> : MakeEnumPropsT<SettingGuiFlagLong, byte, SGF_NONE, SGF_END, SGF_END> {};
typedef TinyEnumT<SettingGuiFlagLong> SettingGuiFlag;


typedef bool OnChange(int32 var);           ///< callback prototype on data modification
typedef int32 OnConvert(const char *value); ///< callback prototype for convertion error

struct SettingDescBase {
	const char *name;       ///< name of the setting. Used in configuration file and for console
	const void *def;        ///< default value given when none is present
	SettingDescType cmd;    ///< various flags for the variable
	SettingGuiFlag flags;   ///< handles how a setting would show up in the GUI (text/currency, etc.)
	int32 min, max;         ///< minimum and maximum values
	int32 interval;         ///< the interval to use between settings in the 'settings' window. If interval is '0' the interval is dynamically determined
	const char *many;       ///< ONE/MANY_OF_MANY: string of possible values for this type
	StringID str;           ///< (translated) string with descriptive text; gui and console
	OnChange *proc;         ///< callback procedure for when the value is changed
	OnConvert *proc_cnvt;   ///< callback procedure when loading value mechanism fails
};

struct SettingDesc {
	SettingDescBase desc;   ///< Settings structure (going to configuration file)
	SaveLoad save;          ///< Internal structure (going to savegame, parts to config)
};

/* NOTE: The only difference between SettingDesc and SettingDescGlob is
 * that one uses global variables as a source and the other offsets
 * in a struct which are bound to a certain variable during runtime.
 * The only way to differentiate between these two is to check if an object
 * has been passed to the function or not. If not, then it is a global variable
 * and save->variable has its address, otherwise save->variable only holds the
 * offset in a certain struct */
typedef SettingDesc SettingDescGlobVarList;

const SettingDesc *GetSettingFromName(const char *name, uint *i);
bool SetSettingValue(uint index, int32 value);
bool SetSettingValue(uint index, const char *value);

#endif /* SETTINGS_H */
