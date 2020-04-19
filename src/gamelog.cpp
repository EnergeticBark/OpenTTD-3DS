/* $Id$ */

/** @file gamelog.cpp Definition of functions used for logging of important changes in the game */

#include "stdafx.h"
#include "openttd.h"
#include "saveload/saveload.h"
#include "core/alloc_func.hpp"
#include "variables.h"
#include "string_func.h"
#include "settings_type.h"
#include "gamelog.h"
#include "gamelog_internal.h"
#include "console_func.h"
#include "debug.h"
#include "rev.h"

#include <stdarg.h>

extern const uint16 SAVEGAME_VERSION;  ///< current savegame version

extern SavegameType _savegame_type; ///< type of savegame we are loading

extern uint32 _ttdp_version;     ///< version of TTDP savegame (if applicable)
extern uint16 _sl_version;       ///< the major savegame version identifier
extern byte   _sl_minor_version; ///< the minor savegame version, DO NOT USE!


static GamelogActionType _gamelog_action_type = GLAT_NONE; ///< action to record if anything changes

LoggedAction *_gamelog_action = NULL;        ///< first logged action
uint _gamelog_actions         = 0;           ///< number of actions
static LoggedAction *_current_action = NULL; ///< current action we are logging, NULL when there is no action active


/** Stores information about new action, but doesn't allocate it
 * Action is allocated only when there is at least one change
 * @param at type of action
 */
void GamelogStartAction(GamelogActionType at)
{
	assert(_gamelog_action_type == GLAT_NONE); // do not allow starting new action without stopping the previous first
	_gamelog_action_type = at;
}

/** Stops logging of any changes
 */
void GamelogStopAction()
{
	assert(_gamelog_action_type != GLAT_NONE); // nobody should try to stop if there is no action in progress

	bool print = _current_action != NULL;

	_current_action = NULL;
	_gamelog_action_type = GLAT_NONE;

	if (print) GamelogPrintDebug(5);
}

/** Resets and frees all memory allocated - used before loading or starting a new game
 */
void GamelogReset()
{
	assert(_gamelog_action_type == GLAT_NONE);

	for (uint i = 0; i < _gamelog_actions; i++) {
		const LoggedAction *la = &_gamelog_action[i];
		for (uint j = 0; j < la->changes; j++) {
			const LoggedChange *lc = &la->change[j];
			if (lc->ct == GLCT_SETTING) free(lc->setting.name);
		}
		free(la->change);
	}

	free(_gamelog_action);

	_gamelog_action  = NULL;
	_gamelog_actions = 0;
	_current_action  = NULL;
}

enum {
	GAMELOG_BUF_LEN = 1024 ///< length of buffer for one line of text
};

static int _dbgofs = 0; ///< offset in current output buffer

static void AddDebugText(char *buf, const char *s, ...)
{
	if (GAMELOG_BUF_LEN <= _dbgofs) return;

	va_list va;

	va_start(va, s);
	_dbgofs += vsnprintf(buf + _dbgofs, GAMELOG_BUF_LEN - _dbgofs, s, va);
	va_end(va);
}


/** Prints GRF filename if found
 * @param grfid GRF which filename to print
 */
static void PrintGrfFilename(char *buf, uint grfid)
{
	const GRFConfig *gc = FindGRFConfig(grfid);

	if (gc == NULL) return;

	AddDebugText(buf, ", filename: %s", gc->filename);
}

/** Prints GRF ID, checksum and filename if found
 * @param grfid GRF ID
 * @param md5sum array of md5sum to print
 */
static void PrintGrfInfo(char *buf, uint grfid, const uint8 *md5sum)
{
	char txt[40];

	md5sumToString(txt, lastof(txt), md5sum);

	AddDebugText(buf, "GRF ID %08X, checksum %s", BSWAP32(grfid), txt);

	PrintGrfFilename(buf, grfid);

	return;
}


/** Text messages for various logged actions */
static const char *la_text[] = {
	"new game started",
	"game loaded",
	"GRF config changed",
	"cheat was used",
	"settings changed",
	"GRF bug triggered",
	"emergency savegame",
};

assert_compile(lengthof(la_text) == GLAT_END);


/** Prints active gamelog */
void GamelogPrint(GamelogPrintProc *proc)
{
	char buf[GAMELOG_BUF_LEN];

	proc("---- gamelog start ----");

	const LoggedAction *laend = &_gamelog_action[_gamelog_actions];

	for (const LoggedAction *la = _gamelog_action; la != laend; la++) {
		assert((uint)la->at < GLAT_END);

		snprintf(buf, GAMELOG_BUF_LEN, "Tick %u: %s", (uint)la->tick, la_text[(uint)la->at]);
		proc(buf);

		const LoggedChange *lcend = &la->change[la->changes];

		for (const LoggedChange *lc = la->change; lc != lcend; lc++) {
			_dbgofs = 0;
			AddDebugText(buf, "     ");

			switch (lc->ct) {
				default: NOT_REACHED();
				case GLCT_MODE:
					AddDebugText(buf, "New game mode: %u landscape: %u",
						(uint)lc->mode.mode, (uint)lc->mode.landscape);
					break;

				case GLCT_REVISION:
					AddDebugText(buf, "Revision text changed to %s, savegame version %u, ",
						lc->revision.text, lc->revision.slver);

					switch (lc->revision.modified) {
						case 0: AddDebugText(buf, "not "); break;
						case 1: AddDebugText(buf, "maybe "); break;
						default: break;
					}

					AddDebugText(buf, "modified, _openttd_newgrf_version = 0x%08x", lc->revision.newgrf);
					break;

				case GLCT_OLDVER:
					AddDebugText(buf, "Conversion from ");
					switch (lc->oldver.type) {
						default: NOT_REACHED();
						case SGT_OTTD:
							AddDebugText(buf, "OTTD savegame without gamelog: version %u, %u",
								GB(lc->oldver.version, 8, 16), GB(lc->oldver.version, 0, 8));
							break;

						case SGT_TTO:
							AddDebugText(buf, "TTO savegame");
							break;

						case SGT_TTD:
							AddDebugText(buf, "TTD savegame");
							break;

						case SGT_TTDP1:
						case SGT_TTDP2:
							AddDebugText(buf, "TTDP savegame, %s format",
								lc->oldver.type == SGT_TTDP1 ? "old" : "new");
							if (lc->oldver.version != 0) {
								AddDebugText(buf, ", TTDP version %u.%u.%u.%u",
									GB(lc->oldver.version, 24, 8), GB(lc->oldver.version, 20, 4),
									GB(lc->oldver.version, 16, 4), GB(lc->oldver.version, 0, 16));
							}
							break;
					}
					break;

				case GLCT_SETTING:
					AddDebugText(buf, "Setting changed: %s : %d -> %d", lc->setting.name, lc->setting.oldval, lc->setting.newval);
					break;

				case GLCT_GRFADD:
					AddDebugText(buf, "Added NewGRF: ");
					PrintGrfInfo(buf, lc->grfadd.grfid, lc->grfadd.md5sum);
					break;

				case GLCT_GRFREM:
					AddDebugText(buf, "Removed NewGRF: %08X", BSWAP32(lc->grfrem.grfid));
					PrintGrfFilename(buf, lc->grfrem.grfid);
					break;

				case GLCT_GRFCOMPAT:
					AddDebugText(buf, "Compatible NewGRF loaded: ");
					PrintGrfInfo(buf, lc->grfcompat.grfid, lc->grfcompat.md5sum);
					break;

				case GLCT_GRFPARAM:
					AddDebugText(buf, "GRF parameter changed: %08X", BSWAP32(lc->grfparam.grfid));
					PrintGrfFilename(buf, lc->grfparam.grfid);
					break;

				case GLCT_GRFMOVE:
					AddDebugText(buf, "GRF order changed: %08X moved %d places %s",
						BSWAP32(lc->grfmove.grfid), abs(lc->grfmove.offset), lc->grfmove.offset >= 0 ? "down" : "up" );
					PrintGrfFilename(buf, lc->grfmove.grfid);
					break;

				case GLCT_GRFBUG:
					switch (lc->grfbug.bug) {
						default: NOT_REACHED();
						case GBUG_VEH_LENGTH:
							AddDebugText(buf, "Rail vehicle changes length outside a depot: GRF ID %08X, internal ID 0x%X", BSWAP32(lc->grfbug.grfid), (uint)lc->grfbug.data);
							PrintGrfFilename(buf, lc->grfbug.grfid);
							break;
					}

				case GLCT_EMERGENCY:
					break;
			}

			proc(buf);
		}
	}

	proc("---- gamelog end ----");
}


static void GamelogPrintConsoleProc(const char *s)
{
	IConsolePrint(CC_WARNING, s);
}

void GamelogPrintConsole()
{
	GamelogPrint(&GamelogPrintConsoleProc);
}

static int _gamelog_print_level = 0; ///< gamelog debug level we need to print stuff

static void GamelogPrintDebugProc(const char *s)
{
	DEBUG(gamelog, _gamelog_print_level, s);
}


/** Prints gamelog to debug output. Code is executed even when
 * there will be no output. It is called very seldom, so it
 * doesn't matter that much. At least it gives more uniform code...
 * @param level debug level we need to print stuff
 */
void GamelogPrintDebug(int level)
{
	_gamelog_print_level = level;
	GamelogPrint(&GamelogPrintDebugProc);
}


/** Allocates new LoggedChange and new LoggedAction if needed.
 * If there is no action active, NULL is returned.
 * @param ct type of change
 * @return new LoggedChange, or NULL if there is no action active
 */
static LoggedChange *GamelogChange(GamelogChangeType ct)
{
	if (_current_action == NULL) {
		if (_gamelog_action_type == GLAT_NONE) return NULL;

		_gamelog_action  = ReallocT(_gamelog_action, _gamelog_actions + 1);
		_current_action  = &_gamelog_action[_gamelog_actions++];

		_current_action->at      = _gamelog_action_type;
		_current_action->tick    = _tick_counter;
		_current_action->change  = NULL;
		_current_action->changes = 0;
	}

	_current_action->change = ReallocT(_current_action->change, _current_action->changes + 1);

	LoggedChange *lc = &_current_action->change[_current_action->changes++];
	lc->ct = ct;

	return lc;
}


/** Logs a emergency savegame
 */
void GamelogEmergency()
{
	assert(_gamelog_action_type == GLAT_EMERGENCY);
	GamelogChange(GLCT_EMERGENCY);
}

/** Finds out if current game is a loaded emergency savegame.
 */
bool GamelogTestEmergency()
{
	const LoggedChange *emergency = NULL;

	const LoggedAction *laend = &_gamelog_action[_gamelog_actions];
	for (const LoggedAction *la = _gamelog_action; la != laend; la++) {
		const LoggedChange *lcend = &la->change[la->changes];
		for (const LoggedChange *lc = la->change; lc != lcend; lc++) {
			if (lc->ct == GLCT_EMERGENCY) emergency = lc;
		}
	}

	return (emergency != NULL);
}

/** Logs a change in game revision
 * @param revision new revision string
 */
void GamelogRevision()
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD);

	LoggedChange *lc = GamelogChange(GLCT_REVISION);
	if (lc == NULL) return;

	memset(lc->revision.text, 0, sizeof(lc->revision.text));
	strecpy(lc->revision.text, _openttd_revision, lastof(lc->revision.text));
	lc->revision.slver = SAVEGAME_VERSION;
	lc->revision.modified = _openttd_revision_modified;
	lc->revision.newgrf = _openttd_newgrf_version;
}

/** Logs a change in game mode (scenario editor or game)
 */
void GamelogMode()
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_CHEAT);

	LoggedChange *lc = GamelogChange(GLCT_MODE);
	if (lc == NULL) return;

	lc->mode.mode      = _game_mode;
	lc->mode.landscape = _settings_game.game_creation.landscape;
}

/** Logs loading from savegame without gamelog
 */
void GamelogOldver()
{
	assert(_gamelog_action_type == GLAT_LOAD);

	LoggedChange *lc = GamelogChange(GLCT_OLDVER);
	if (lc == NULL) return;

	lc->oldver.type = _savegame_type;
	lc->oldver.version = (_savegame_type == SGT_OTTD ? ((uint32)_sl_version << 8 | _sl_minor_version) : _ttdp_version);
}

/** Logs change in game settings. Only non-networksafe settings are logged
 * @param name setting name
 * @param oldval old setting value
 * @param newval new setting value
 */
void GamelogSetting(const char *name, int32 oldval, int32 newval)
{
	assert(_gamelog_action_type == GLAT_SETTING);

	LoggedChange *lc = GamelogChange(GLCT_SETTING);
	if (lc == NULL) return;

	lc->setting.name = strdup(name);
	lc->setting.oldval = oldval;
	lc->setting.newval = newval;
}


/** Finds out if current revision is different than last revision stored in the savegame.
 * Appends GLCT_REVISION when the revision string changed
 */
void GamelogTestRevision()
{
	const LoggedChange *rev = NULL;

	const LoggedAction *laend = &_gamelog_action[_gamelog_actions];
	for (const LoggedAction *la = _gamelog_action; la != laend; la++) {
		const LoggedChange *lcend = &la->change[la->changes];
		for (const LoggedChange *lc = la->change; lc != lcend; lc++) {
			if (lc->ct == GLCT_REVISION) rev = lc;
		}
	}

	if (rev == NULL || strcmp(rev->revision.text, _openttd_revision) != 0 ||
			rev->revision.modified != _openttd_revision_modified ||
			rev->revision.newgrf != _openttd_newgrf_version) {
		GamelogRevision();
	}
}

/** Finds last stored game mode or landscape.
 * Any change is logged
 */
void GamelogTestMode()
{
	const LoggedChange *mode = NULL;

	const LoggedAction *laend = &_gamelog_action[_gamelog_actions];
	for (const LoggedAction *la = _gamelog_action; la != laend; la++) {
		const LoggedChange *lcend = &la->change[la->changes];
		for (const LoggedChange *lc = la->change; lc != lcend; lc++) {
			if (lc->ct == GLCT_MODE) mode = lc;
		}
	}

	if (mode == NULL || mode->mode.mode != _game_mode || mode->mode.landscape != _settings_game.game_creation.landscape) GamelogMode();
}


/** Logs triggered GRF bug.
 * @param grfid ID of problematic GRF
 * @param bug type of bug, @see enum GRFBugs
 * @param data additional data
 */
static void GamelogGRFBug(uint32 grfid, byte bug, uint64 data)
{
	assert(_gamelog_action_type == GLAT_GRFBUG);

	LoggedChange *lc = GamelogChange(GLCT_GRFBUG);
	if (lc == NULL) return;

	lc->grfbug.data  = data;
	lc->grfbug.grfid = grfid;
	lc->grfbug.bug   = bug;
}

/** Logs GRF bug - rail vehicle has different length after reversing.
 * Ensures this is logged only once for each GRF and engine type
 * This check takes some time, but it is called pretty seldom, so it
 * doesn't matter that much (ideally it shouldn't be called at all).
 * @param engine engine to log
 * @return true iff a unique record was done
 */
bool GamelogGRFBugReverse(uint32 grfid, uint16 internal_id)
{
	const LoggedAction *laend = &_gamelog_action[_gamelog_actions];
	for (const LoggedAction *la = _gamelog_action; la != laend; la++) {
		const LoggedChange *lcend = &la->change[la->changes];
		for (const LoggedChange *lc = la->change; lc != lcend; lc++) {
			if (lc->ct == GLCT_GRFBUG && lc->grfbug.grfid == grfid &&
					lc->grfbug.bug == GBUG_VEH_LENGTH && lc->grfbug.data == internal_id) {
				return false;
			}
		}
	}

	GamelogStartAction(GLAT_GRFBUG);
	GamelogGRFBug(grfid, GBUG_VEH_LENGTH, internal_id);
	GamelogStopAction();

	return true;
}


/** Decides if GRF should be logged
 * @param g grf to determine
 * @return true iff GRF is not static and is loaded
 */
static inline bool IsLoggableGrfConfig(const GRFConfig *g)
{
	return !HasBit(g->flags, GCF_STATIC) && g->status != GCS_NOT_FOUND;
}

/** Logs removal of a GRF
 * @param grfid ID of removed GRF
 */
void GamelogGRFRemove(uint32 grfid)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFREM);
	if (lc == NULL) return;

	lc->grfrem.grfid = grfid;
}

/** Logs adding of a GRF
 * @param newg added GRF
 */
void GamelogGRFAdd(const GRFConfig *newg)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_GRF);

	if (!IsLoggableGrfConfig(newg)) return;

	LoggedChange *lc = GamelogChange(GLCT_GRFADD);
	if (lc == NULL) return;

	memcpy(&lc->grfadd, newg, sizeof(GRFIdentifier));
}

/** Logs loading compatible GRF
 * (the same ID, but different MD5 hash)
 * @param newg new (updated) GRF
 */
void GamelogGRFCompatible(const GRFIdentifier *newg)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFCOMPAT);
	if (lc == NULL) return;

	memcpy(&lc->grfcompat, newg, sizeof(GRFIdentifier));
}

/** Logs changing GRF order
 * @param grfid GRF that is moved
 * @param offset how far it is moved, positive = moved down
 */
static void GamelogGRFMove(uint32 grfid, int32 offset)
{
	assert(_gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFMOVE);
	if (lc == NULL) return;

	lc->grfmove.grfid  = grfid;
	lc->grfmove.offset = offset;
}

/** Logs change in GRF parameters.
 * Details about parameters changed are not stored
 * @param grfid ID of GRF to store
 */
static void GamelogGRFParameters(uint32 grfid)
{
	assert(_gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFPARAM);
	if (lc == NULL) return;

	lc->grfparam.grfid = grfid;
}

/** Logs adding of list of GRFs.
 * Useful when old savegame is loaded or when new game is started
 * @param newg head of GRF linked list
 */
void GamelogGRFAddList(const GRFConfig *newg)
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD);

	for (; newg != NULL; newg = newg->next) {
		GamelogGRFAdd(newg);
	}
}

/** List of GRFs using array of pointers instead of linked list */
struct GRFList {
	uint n;
	const GRFConfig *grf[VARARRAY_SIZE];
};

/** Generates GRFList
 * @param grfc head of GRF linked list
 */
static GRFList *GenerateGRFList(const GRFConfig *grfc)
{
	uint n = 0;
	for (const GRFConfig *g = grfc; g != NULL; g = g->next) {
		if (IsLoggableGrfConfig(g)) n++;
	}

	GRFList *list = (GRFList*)MallocT<byte>(sizeof(GRFList) + n * sizeof(GRFConfig*));

	list->n = 0;
	for (const GRFConfig *g = grfc; g != NULL; g = g->next) {
		if (IsLoggableGrfConfig(g)) list->grf[list->n++] = g;
	}

	return list;
}

/** Compares two NewGRF lists and logs any change
 * @param oldc original GRF list
 * @param newc new GRF list
 */
void GamelogGRFUpdate(const GRFConfig *oldc, const GRFConfig *newc)
{
	GRFList *ol = GenerateGRFList(oldc);
	GRFList *nl = GenerateGRFList(newc);

	uint o = 0, n = 0;

	while (o < ol->n && n < nl->n) {
		const GRFConfig *og = ol->grf[o];
		const GRFConfig *ng = nl->grf[n];

		if (og->grfid != ng->grfid) {
			uint oi, ni;
			for (oi = 0; oi < ol->n; oi++) {
				if (ol->grf[oi]->grfid == nl->grf[n]->grfid) break;
			}
			if (oi < o) {
				/* GRF was moved, this change has been logged already */
				n++;
				continue;
			}
			if (oi == ol->n) {
				/* GRF couldn't be found in the OLD list, GRF was ADDED */
				GamelogGRFAdd(nl->grf[n++]);
				continue;
			}
			for (ni = 0; ni < nl->n; ni++) {
				if (nl->grf[ni]->grfid == ol->grf[o]->grfid) break;
			}
			if (ni < n) {
				/* GRF was moved, this change has been logged already */
				o++;
				continue;
			}
			if (ni == nl->n) {
				/* GRF couldn't be found in the NEW list, GRF was REMOVED */
				GamelogGRFRemove(ol->grf[o++]->grfid);
				continue;
			}

			/* o < oi < ol->n
			 * n < ni < nl->n */
			assert(ni > n && ni < nl->n);
			assert(oi > o && oi < ol->n);

			ni -= n; // number of GRFs it was moved downwards
			oi -= o; // number of GRFs it was moved upwards

			if (ni >= oi) { // prefer the one that is moved further
				/* GRF was moved down */
				GamelogGRFMove(ol->grf[o++]->grfid, ni);
			} else {
				GamelogGRFMove(nl->grf[n++]->grfid, -(int)oi);
			}
		} else {
			if (memcmp(og->md5sum, ng->md5sum, sizeof(og->md5sum)) != 0) {
				/* md5sum changed, probably loading 'compatible' GRF */
				GamelogGRFCompatible(nl->grf[n]);
			}

			if (og->num_params != ng->num_params || memcmp(og->param, ng->param, og->num_params * sizeof(og->param[0])) != 0) {
				GamelogGRFParameters(ol->grf[o]->grfid);
			}

			o++;
			n++;
		}
	}

	while (o < ol->n) GamelogGRFRemove(ol->grf[o++]->grfid); // remaining GRFs were removed ...
	while (n < nl->n) GamelogGRFAdd   (nl->grf[n++]);    // ... or added

	free(ol);
	free(nl);
}

/**
 * Get the MD5 checksum of the original NewGRF that was loaded.
 * @param grfid the GRF ID to search for
 * @param md5sum the MD5 checksum to write to.
 */
void GamelogGetOriginalGRFMD5Checksum(uint32 grfid, byte *md5sum)
{
	const LoggedAction *la = &_gamelog_action[_gamelog_actions - 1];
	/* There should always be a "start game" action */
	assert(_gamelog_actions > 0);

	do {
		const LoggedChange *lc = &la->change[la->changes - 1];
		/* There should always be at least one change per action */
		assert(la->changes > 0);

		do {
			if (lc->ct == GLCT_GRFADD && lc->grfadd.grfid == grfid) {
				memcpy(md5sum, lc->grfadd.md5sum, sizeof(lc->grfadd.md5sum));
				return;
			}
		} while (lc-- != la->change);
	} while (la-- != _gamelog_action);

	NOT_REACHED();
}
