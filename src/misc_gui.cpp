/* $Id$ */

/** @file misc_gui.cpp GUIs for a number of misc windows. */

#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "landscape.h"
#include "newgrf_text.h"
#include "saveload/saveload.h"
#include "tile_map.h"
#include "gui.h"
#include "station_gui.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "station_func.h"
#include "command_func.h"
#include "company_func.h"
#include "town.h"
#include "network/network.h"
#include "network/network_content.h"
#include "variables.h"
#include "company_base.h"
#include "texteff.hpp"
#include "cargotype.h"
#include "company_manager_face.h"
#include "strings_func.h"
#include "fileio_func.h"
#include "fios.h"
#include "zoom_func.h"
#include "window_func.h"
#include "string_func.h"
#include "newgrf_cargo.h"
#include "tilehighlight_func.h"
#include "querystring_gui.h"

#include "table/strings.h"

/* Variables to display file lists */
SaveLoadDialogMode _saveload_mode;


static bool _fios_path_changed;
static bool _savegame_sort_dirty;
int _caret_timer;

static const Widget _land_info_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                       STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   299,     0,    13, STR_01A3_LAND_AREA_INFORMATION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL, RESIZE_BOTTOM,  COLOUR_GREY,     0,   299,    14,    99, 0x0,                            STR_NULL},
{    WIDGETS_END},
};

static const WindowDesc _land_info_desc(
	WDP_AUTO, WDP_AUTO, 300, 100, 300, 100,
	WC_LAND_INFO, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_land_info_widgets
);

class LandInfoWindow : public Window {
	enum {
		LAND_INFO_CENTERED_LINES   = 12,                       ///< Up to 12 centered lines
		LAND_INFO_MULTICENTER_LINE = LAND_INFO_CENTERED_LINES, ///< One multicenter line
		LAND_INFO_LINE_END,

		LAND_INFO_LINE_BUFF_SIZE = 512,
	};

public:
	char landinfo_data[LAND_INFO_LINE_END][LAND_INFO_LINE_BUFF_SIZE];

	virtual void OnPaint()
	{
		this->DrawWidgets();

		uint y = 21;
		for (uint i = 0; i < LAND_INFO_CENTERED_LINES; i++) {
			if (StrEmpty(this->landinfo_data[i])) break;

			DoDrawStringCentered(150, y, this->landinfo_data[i], i == 0 ? TC_LIGHT_BLUE : TC_FROMSTRING);
			y += i == 0 ? 16 : 12;
		}

		y += 6;

		if (!StrEmpty(this->landinfo_data[LAND_INFO_MULTICENTER_LINE])) {
			SetDParamStr(0, this->landinfo_data[LAND_INFO_MULTICENTER_LINE]);
			DrawStringMultiCenter(150, y, STR_JUST_RAW_STRING, this->width - 4);
		}
	}

	LandInfoWindow(TileIndex tile) : Window(&_land_info_desc) {
		Company *c = GetCompany(IsValidCompanyID(_local_company) ? _local_company : COMPANY_FIRST);
		Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);

		Money old_money = c->money;
		c->money = INT64_MAX;
		CommandCost costclear = DoCommand(tile, 0, 0, DC_NONE, CMD_LANDSCAPE_CLEAR);
		c->money = old_money;

		/* Because build_date is not set yet in every TileDesc, we make sure it is empty */
		TileDesc td;
		AcceptedCargo ac;

		td.build_date = INVALID_DATE;

		/* Most tiles have only one owner, but
		 *  - drivethrough roadstops can be build on town owned roads (up to 2 owners) and
		 *  - roads can have up to four owners (railroad, road, tram, 3rd-roadtype "highway").
		 */
		td.owner_type[0] = STR_01A7_OWNER; // At least one owner is displayed, though it might be "N/A".
		td.owner_type[1] = STR_NULL;       // STR_NULL results in skipping the owner
		td.owner_type[2] = STR_NULL;
		td.owner_type[3] = STR_NULL;
		td.owner[0] = OWNER_NONE;
		td.owner[1] = OWNER_NONE;
		td.owner[2] = OWNER_NONE;
		td.owner[3] = OWNER_NONE;

		td.station_class = STR_NULL;
		td.station_name = STR_NULL;

		td.grf = NULL;

		GetAcceptedCargo(tile, ac);
		GetTileDesc(tile, &td);

		uint line_nr = 0;

		/* Tiletype */
		SetDParam(0, td.dparam[0]);
		GetString(this->landinfo_data[line_nr], td.str, lastof(this->landinfo_data[line_nr]));
		line_nr++;

		/* Up to four owners */
		for (uint i = 0; i < 4; i++) {
			if (td.owner_type[i] == STR_NULL) continue;

			SetDParam(0, STR_01A6_N_A);
			if (td.owner[i] != OWNER_NONE && td.owner[i] != OWNER_WATER) GetNameOfOwner(td.owner[i], tile);
			GetString(this->landinfo_data[line_nr], td.owner_type[i], lastof(this->landinfo_data[line_nr]));
			line_nr++;
		}

		/* Cost to clear/revenue when cleared */
		StringID str = STR_01A4_COST_TO_CLEAR_N_A;
		if (CmdSucceeded(costclear)) {
			Money cost = costclear.GetCost();
			if (cost < 0) {
				cost = -cost; // Negate negative cost to a positive revenue
				str = STR_REVENUE_WHEN_CLEARED;
			} else {
				str = STR_01A5_COST_TO_CLEAR;
			}
			SetDParam(0, cost);
		}
		GetString(this->landinfo_data[line_nr], str, lastof(this->landinfo_data[line_nr]));
		line_nr++;

		/* Location */
		char tmp[16];
		snprintf(tmp, lengthof(tmp), "0x%.4X", tile);
		SetDParam(0, TileX(tile));
		SetDParam(1, TileY(tile));
		SetDParam(2, TileHeight(tile));
		SetDParamStr(3, tmp);
		GetString(this->landinfo_data[line_nr], STR_LANDINFO_COORDS, lastof(this->landinfo_data[line_nr]));
		line_nr++;

		/* Local authority */
		SetDParam(0, STR_01A9_NONE);
		if (t != NULL && t->IsValid()) {
			SetDParam(0, STR_TOWN);
			SetDParam(1, t->index);
		}
		GetString(this->landinfo_data[line_nr], STR_01A8_LOCAL_AUTHORITY, lastof(this->landinfo_data[line_nr]));
		line_nr++;

		/* Build date */
		if (td.build_date != INVALID_DATE) {
			SetDParam(0, td.build_date);
			GetString(this->landinfo_data[line_nr], STR_BUILD_DATE, lastof(this->landinfo_data[line_nr]));
			line_nr++;
		}

		/* Station class */
		if (td.station_class != STR_NULL) {
			SetDParam(0, td.station_class);
			GetString(this->landinfo_data[line_nr], STR_TILEDESC_STATION_CLASS, lastof(this->landinfo_data[line_nr]));
			line_nr++;
		}

		/* Station type name */
		if (td.station_name != STR_NULL) {
			SetDParam(0, td.station_name);
			GetString(this->landinfo_data[line_nr], STR_TILEDESC_STATION_TYPE, lastof(this->landinfo_data[line_nr]));
			line_nr++;
		}

		/* NewGRF name */
		if (td.grf != NULL) {
			SetDParamStr(0, td.grf);
			GetString(this->landinfo_data[line_nr], STR_TILEDESC_NEWGRF_NAME, lastof(this->landinfo_data[line_nr]));
			line_nr++;
		}

		assert(line_nr < LAND_INFO_CENTERED_LINES);

		/* Mark last line empty */
		this->landinfo_data[line_nr][0] = '\0';

		/* Cargo acceptance is displayed in a extra multiline */
		char *strp = GetString(this->landinfo_data[LAND_INFO_MULTICENTER_LINE], STR_01CE_CARGO_ACCEPTED, lastof(this->landinfo_data[LAND_INFO_MULTICENTER_LINE]));
		bool found = false;

		for (CargoID i = 0; i < NUM_CARGO; ++i) {
			if (ac[i] > 0) {
				/* Add a comma between each item. */
				if (found) {
					*strp++ = ',';
					*strp++ = ' ';
				}
				found = true;

				/* If the accepted value is less than 8, show it in 1/8:ths */
				if (ac[i] < 8) {
					SetDParam(0, ac[i]);
					SetDParam(1, GetCargo(i)->name);
					strp = GetString(strp, STR_01D1_8, lastof(this->landinfo_data[LAND_INFO_MULTICENTER_LINE]));
				} else {
					strp = GetString(strp, GetCargo(i)->name, lastof(this->landinfo_data[LAND_INFO_MULTICENTER_LINE]));
				}
			}
		}
		if (!found) this->landinfo_data[LAND_INFO_MULTICENTER_LINE][0] = '\0';

		if (found) line_nr += 2;

		if (line_nr > 6) ResizeWindow(this, 0, 12 * (line_nr - 6));

		this->FindWindowPlacementAndResize(&_land_info_desc);

#if defined(_DEBUG)
#	define LANDINFOD_LEVEL 0
#else
#	define LANDINFOD_LEVEL 1
#endif
		DEBUG(misc, LANDINFOD_LEVEL, "TILE: %#x (%i,%i)", tile, TileX(tile), TileY(tile));
		DEBUG(misc, LANDINFOD_LEVEL, "type_height  = %#x", _m[tile].type_height);
		DEBUG(misc, LANDINFOD_LEVEL, "m1           = %#x", _m[tile].m1);
		DEBUG(misc, LANDINFOD_LEVEL, "m2           = %#x", _m[tile].m2);
		DEBUG(misc, LANDINFOD_LEVEL, "m3           = %#x", _m[tile].m3);
		DEBUG(misc, LANDINFOD_LEVEL, "m4           = %#x", _m[tile].m4);
		DEBUG(misc, LANDINFOD_LEVEL, "m5           = %#x", _m[tile].m5);
		DEBUG(misc, LANDINFOD_LEVEL, "m6           = %#x", _m[tile].m6);
		DEBUG(misc, LANDINFOD_LEVEL, "m7           = %#x", _me[tile].m7);
#undef LANDINFOD_LEVEL
	}
};

static void Place_LandInfo(TileIndex tile)
{
	DeleteWindowById(WC_LAND_INFO, 0);
	new LandInfoWindow(tile);
}

void PlaceLandBlockInfo()
{
	if (_cursor.sprite == SPR_CURSOR_QUERY) {
		ResetObjectToPlace();
	} else {
		_place_proc = Place_LandInfo;
		SetObjectToPlace(SPR_CURSOR_QUERY, PAL_NONE, VHM_RECT, WC_MAIN_TOOLBAR, 0);
	}
}

static const Widget _about_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,         STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   419,     0,    13, STR_015B_OPENTTD, STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   419,    14,   271, 0x0,              STR_NULL},
{      WWT_FRAME,   RESIZE_NONE,  COLOUR_GREY,     5,   414,    40,   245, STR_NULL,         STR_NULL},
{    WIDGETS_END},
};

static const WindowDesc _about_desc(
	WDP_CENTER, WDP_CENTER, 420, 272, 420, 272,
	WC_GAME_OPTIONS, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_about_widgets
);

struct AboutWindow : public Window {
	int scroll_height;
	uint16 counter;

	AboutWindow() : Window(&_about_desc)
	{
		this->counter = 5;
		this->scroll_height = this->height - 40;
		this->FindWindowPlacementAndResize(&_about_desc);
	}

	virtual void OnPaint()
	{
		static const char *credits[] = {
			/*************************************************************************
			 *                      maximum length of string which fits in window   -^*/
			"Original design by Chris Sawyer",
			"Original graphics by Simon Foster",
			"",
			"The OpenTTD team (in alphabetical order):",
			"  Jean-Francois Claeys (Belugas) - GUI, newindustries and more",
			"  Bjarni Corfitzen (Bjarni) - MacOSX port, coder and vehicles",
			"  Matthijs Kooijman (blathijs) - Pathfinder-guru, pool rework",
			"  Victor Fischer (Celestar) - Programming everywhere you need him to",
			"  Christoph Elsenhans (frosch) - General coding",
			"  Lo\xC3\xAF""c Guilloux (glx) - Windows Expert",
			"  Michael Lutz (michi_cc) - Path based signals",
			"  Owen Rudge (orudge) - Forum host, OS/2 port",
			"  Peter Nelson (peter1138) - Spiritual descendant from newGRF gods",
			"  Remko Bijker (Rubidium) - Lead coder and way more",
			"  Zdenek Sojka (SmatZ) - Bug finder and fixer",
			"  Thijs Marinussen (Yexo) - AI Framework",
			"",
			"Inactive Developers:",
			"  Tam\xC3\xA1s Farag\xC3\xB3 (Darkvater) - Ex-Lead coder",
			"  Jaroslav Mazanec (KUDr) - YAPG (Yet Another Pathfinder God) ;)",
			"  Jonathan Coome (Maedhros) - High priest of the NewGRF Temple",
			"  Attila B\xC3\xA1n (MiHaMiX) - WebTranslator, Nightlies, Wiki and bugtracker host",
			"  Christoph Mallon (Tron) - Programmer, code correctness police",
			"",
			"Retired Developers:",
			"  Ludvig Strigeus (ludde) - OpenTTD author, main coder (0.1 - 0.3.3)",
			"  Serge Paquet (vurlix) - Assistant project manager, coder (0.1 - 0.3.3)",
			"  Dominik Scherer (dominik81) - Lead programmer, GUI expert (0.3.0 - 0.3.6)",
			"  Benedikt Brüggemeier (skidd13) - Bug fixer and code reworker",
			"  Patric Stout (TrueLight) - Programmer, webhoster (0.3 - pre0.7)",
			"",
			"Special thanks go out to:",
			"  Josef Drexler - For his great work on TTDPatch",
			"  Marcin Grzegorczyk - For his documentation of TTD internals",
			"  Petr Baudis (pasky) - Many patches, newGRF support",
			"  Stefan Meißner (sign_de) - For his work on the console",
			"  Simon Sasburg (HackyKid) - Many bugfixes he has blessed us with",
			"  Cian Duffy (MYOB) - BeOS port / manual writing",
			"  Christian Rosentreter (tokai) - MorphOS / AmigaOS port",
			"  Richard Kempton (richK) - additional airports, initial TGP implementation",
			"",
			"  Alberto Demichelis - Squirrel scripting language © 2003-2008",
			"  Michael Blunck - Pre-Signals and Semaphores © 2003",
			"  George - Canal/Lock graphics © 2003-2004",
			"  David Dallaston - Tram tracks",
			"  Marcin Grzegorczyk - Foundations for Tracks on Slopes",
			"  All Translators - Who made OpenTTD a truly international game",
			"  Bug Reporters - Without whom OpenTTD would still be full of bugs!",
			"",
			"",
			"And last but not least:",
			"  Chris Sawyer - For an amazing game!"
		};

		this->DrawWidgets();

		/* Show original copyright and revision version */
		DrawStringCentered(210, 17, STR_00B6_ORIGINAL_COPYRIGHT, TC_FROMSTRING);
		DrawStringCentered(210, 17 + 10, STR_00B7_VERSION, TC_FROMSTRING);

		int y = this->scroll_height;

		/* Show all scrolling credits */
		for (uint i = 0; i < lengthof(credits); i++) {
			if (y >= 50 && y < (this->height - 40)) {
				DoDrawString(credits[i], 10, y, TC_BLACK);
			}
			y += 10;
		}

		/* If the last text has scrolled start a new from the start */
		if (y < 50) this->scroll_height = this->height - 40;

		DoDrawStringCentered(210, this->height - 25, "Website: http://www.openttd.org", TC_BLACK);
		DrawStringCentered(210, this->height - 15, STR_00BA_COPYRIGHT_OPENTTD, TC_FROMSTRING);
	}

	virtual void OnTick()
	{
		if (--this->counter == 0) {
			this->counter = 5;
			this->scroll_height--;
			this->SetDirty();
		}
	}
};

void ShowAboutWindow()
{
	DeleteWindowById(WC_GAME_OPTIONS, 0);
	new AboutWindow();
}

static const Widget _errmsg_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,    COLOUR_RED,     0,    10,     0,    13, STR_00C5,         STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,    COLOUR_RED,    11,   239,     0,    13, STR_00B2_MESSAGE, STR_NULL},
{      WWT_PANEL,   RESIZE_BOTTOM,  COLOUR_RED,     0,   239,    14,    45, 0x0,              STR_NULL},
{    WIDGETS_END},
};

static const Widget _errmsg_face_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,    COLOUR_RED,     0,    10,     0,    13, STR_00C5,              STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,    COLOUR_RED,    11,   333,     0,    13, STR_00B3_MESSAGE_FROM, STR_NULL},
{      WWT_PANEL,   RESIZE_BOTTOM,  COLOUR_RED,     0,   333,    14,   136, 0x0,                   STR_NULL},
{   WIDGETS_END},
};

struct ErrmsgWindow : public Window {
private:
	uint duration;
	uint64 decode_params[20];
	StringID message_1;
	StringID message_2;
	bool show_company_manager_face;

	int y[2];

public:
	ErrmsgWindow(Point pt, int width, int height, StringID msg1, StringID msg2, const Widget *widget, bool show_company_manager_face) :
			Window(pt.x, pt.y, width, height, WC_ERRMSG, widget),
			show_company_manager_face(show_company_manager_face)
	{
		this->duration = _settings_client.gui.errmsg_duration;
		CopyOutDParam(this->decode_params, 0, lengthof(this->decode_params));
		this->message_1 = msg1;
		this->message_2 = msg2;
		this->desc_flags = WDF_STD_BTN | WDF_DEF_WIDGET;

		SwitchToErrorRefStack();
		RewindTextRefStack();

		assert(msg2 != INVALID_STRING_ID);

		int h2 = 3 + GetStringHeight(msg2, width - 2); // msg2 is printed first
		int h1 = (msg1 == INVALID_STRING_ID) ? 0 : 3 + GetStringHeight(msg1, width - 2);

		SwitchToNormalRefStack();

		int h = 15 + h1 + h2;
		height = max<int>(height, h);

		if (msg1 == INVALID_STRING_ID) {
			/* only 1 line will be printed */
			y[1] = (height - 15) / 2 + 15 - 5;
		} else {
			int over = (height - h) / 4;

			y[1] = 15 + h2 / 2 + 1 - 5 + over;
			y[0] = height - 3 - h1 / 2 - 5 - over;
		}

		this->FindWindowPlacementAndResize(width, height);
	}

	virtual void OnPaint()
	{
		CopyInDParam(0, this->decode_params, lengthof(this->decode_params));
		this->DrawWidgets();
		CopyInDParam(0, this->decode_params, lengthof(this->decode_params));

		/* If the error message comes from a NewGRF, we must use the text ref. stack reserved for error messages.
		 * If the message doesn't come from a NewGRF, it won't use the TTDP-style text ref. stack, so we won't hurt anything
		 */
		SwitchToErrorRefStack();
		RewindTextRefStack();

		if (this->show_company_manager_face) {
			const Company *c = GetCompany((CompanyID)GetDParamX(this->decode_params, 2));
			DrawCompanyManagerFace(c->face, c->colour, 2, 16);
		}

		DrawStringMultiCenter(this->width - 120, y[1], this->message_2, this->width - 2);
		if (this->message_1 != INVALID_STRING_ID) DrawStringMultiCenter(this->width - 120, y[0], this->message_1, this->width - 2);

		/* Switch back to the normal text ref. stack for NewGRF texts */
		SwitchToNormalRefStack();
	}

	virtual void OnMouseLoop()
	{
		if (_right_button_down) delete this;
	}

	virtual void OnHundredthTick()
	{
		if (--this->duration == 0) delete this;
	}

	~ErrmsgWindow()
	{
		SetRedErrorSquare(INVALID_TILE);
		extern StringID _switch_mode_errorstr;
		_switch_mode_errorstr = INVALID_STRING_ID;
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		if (keycode != WKC_SPACE) return ES_NOT_HANDLED;
		delete this;
		return ES_HANDLED;
	}
};

void ShowErrorMessage(StringID msg_1, StringID msg_2, int x, int y)
{
	DeleteWindowById(WC_ERRMSG, 0);

	if (!_settings_client.gui.errmsg_duration) return;

	if (msg_2 == STR_NULL) msg_2 = STR_EMPTY;

	Point pt;
	const ViewPort *vp;

	if (msg_1 != STR_013B_OWNED_BY || GetDParam(2) >= 8) {
		if ((x | y) != 0) {
			pt = RemapCoords2(x, y);
			vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;

			/* move x pos to opposite corner */
			pt.x = UnScaleByZoom(pt.x - vp->virtual_left, vp->zoom) + vp->left;
			pt.x = (pt.x < (_screen.width >> 1)) ? _screen.width - 260 : 20;

			/* move y pos to opposite corner */
			pt.y = UnScaleByZoom(pt.y - vp->virtual_top, vp->zoom) + vp->top;
			pt.y = (pt.y < (_screen.height >> 1)) ? _screen.height - 80 : 100;

		} else {
			pt.x = (_screen.width - 240) >> 1;
			pt.y = (_screen.height - 46) >> 1;
		}
		new ErrmsgWindow(pt, 240, 46, msg_1, msg_2, _errmsg_widgets, false);
	} else {
		if ((x | y) != 0) {
			pt = RemapCoords2(x, y);
			vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;
			pt.x = Clamp(UnScaleByZoom(pt.x - vp->virtual_left, vp->zoom) + vp->left - (334 / 2),  0, _screen.width  - 334);
			pt.y = Clamp(UnScaleByZoom(pt.y - vp->virtual_top,  vp->zoom) + vp->top  - (137 / 2), 22, _screen.height - 137);
		} else {
			pt.x = (_screen.width  - 334) >> 1;
			pt.y = (_screen.height - 137) >> 1;
		}
		new ErrmsgWindow(pt, 334, 137, msg_1, msg_2, _errmsg_face_widgets, true);
	}
}

void ShowEstimatedCostOrIncome(Money cost, int x, int y)
{
	StringID msg = STR_0805_ESTIMATED_COST;

	if (cost < 0) {
		cost = -cost;
		msg = STR_0807_ESTIMATED_INCOME;
	}
	SetDParam(0, cost);
	ShowErrorMessage(INVALID_STRING_ID, msg, x, y);
}

void ShowCostOrIncomeAnimation(int x, int y, int z, Money cost)
{
	Point pt = RemapCoords(x, y, z);
	StringID msg = STR_0801_COST;

	if (cost < 0) {
		cost = -cost;
		msg = STR_0803_INCOME;
	}
	SetDParam(0, cost);
	AddTextEffect(msg, pt.x, pt.y, 0x250, TE_RISING);
}

void ShowFeederIncomeAnimation(int x, int y, int z, Money cost)
{
	Point pt = RemapCoords(x, y, z);

	SetDParam(0, cost);
	AddTextEffect(STR_FEEDER, pt.x, pt.y, 0x250, TE_RISING);
}

TextEffectID ShowFillingPercent(int x, int y, int z, uint8 percent, StringID string)
{
	Point pt = RemapCoords(x, y, z);

	assert(string != STR_NULL);

	SetDParam(0, percent);
	return AddTextEffect(string, pt.x, pt.y, 0xFFFF, TE_STATIC);
}

void UpdateFillingPercent(TextEffectID te_id, uint8 percent, StringID string)
{
	assert(string != STR_NULL);

	SetDParam(0, percent);
	UpdateTextEffect(te_id, string);
}

void HideFillingPercent(TextEffectID *te_id)
{
	if (*te_id == INVALID_TE_ID) return;

	RemoveTextEffect(*te_id);
	*te_id = INVALID_TE_ID;
}

static const Widget _tooltips_widgets[] = {
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   199,     0,    31, 0x0, STR_NULL},
{   WIDGETS_END},
};

struct TooltipsWindow : public Window
{
	StringID string_id;
	byte paramcount;
	uint64 params[5];
	bool use_left_mouse_button;

	TooltipsWindow(int x, int y, int width, int height, const Widget *widget,
								 StringID str, uint paramcount, const uint64 params[], bool use_left_mouse_button) :
			Window(x, y, width, height, WC_TOOLTIPS, widget)
	{
		this->string_id = str;
		assert(sizeof(this->params[0]) == sizeof(params[0]));
		assert(paramcount <= lengthof(this->params));
		memcpy(this->params, params, sizeof(this->params[0]) * paramcount);
		this->paramcount = paramcount;
		this->use_left_mouse_button = use_left_mouse_button;

		this->flags4 &= ~WF_WHITE_BORDER_MASK; // remove white-border from tooltip
		this->widget[0].right = width;
		this->widget[0].bottom = height;

		FindWindowPlacementAndResize(width, height);
	}

	virtual void OnPaint()
	{
		GfxFillRect(0, 0, this->width - 1, this->height - 1, 0);
		GfxFillRect(1, 1, this->width - 2, this->height - 2, 0x44);

		for (uint arg = 0; arg < this->paramcount; arg++) {
			SetDParam(arg, this->params[arg]);
		}
		DrawStringMultiCenter((this->width >> 1), (this->height >> 1) - 5, this->string_id, this->width - 2);
	}

	virtual void OnMouseLoop()
	{
		/* We can show tooltips while dragging tools. These are shown as long as
		 * we are dragging the tool. Normal tooltips work with rmb */
		if (this->use_left_mouse_button ? !_left_button_down : !_right_button_down) delete this;
	}
};

/** Shows a tooltip
 * @param str String to be displayed
 * @param paramcount number of params to deal with
 * @param params (optional) up to 5 pieces of additional information that may be added to a tooltip
 * @param use_left_mouse_button close the tooltip when the left (true) or right (false) mousebutton is released
 */
void GuiShowTooltips(StringID str, uint paramcount, const uint64 params[], bool use_left_mouse_button)
{
	DeleteWindowById(WC_TOOLTIPS, 0);

	if (str == STR_NULL) return;

	for (uint i = 0; i != paramcount; i++) SetDParam(i, params[i]);
	char buffer[512];
	GetString(buffer, str, lastof(buffer));

	Dimension br = GetStringBoundingBox(buffer);
	br.width += 6; br.height += 4; // increase slightly to have some space around the box

	/* Cut tooltip length to 200 pixels max, wrap to new line if longer */
	if (br.width > 200) {
		br.height += ((br.width - 4) / 176) * 10;
		br.width = 200;
	}

	/* Correctly position the tooltip position, watch out for window and cursor size
	 * Clamp value to below main toolbar and above statusbar. If tooltip would
	 * go below window, flip it so it is shown above the cursor */
	int y = Clamp(_cursor.pos.y + _cursor.size.y + _cursor.offs.y + 5, 22, _screen.height - 12);
	if (y + br.height > _screen.height - 12) y = _cursor.pos.y + _cursor.offs.y - br.height - 5;
	int x = Clamp(_cursor.pos.x - (br.width >> 1), 0, _screen.width - br.width);

	new TooltipsWindow(x, y, br.width, br.height, _tooltips_widgets, str, paramcount, params, use_left_mouse_button);
}


static int DrawStationCoverageText(const AcceptedCargo cargo,
	int str_x, int str_y, StationCoverageType sct, bool supplies)
{
	bool first = true;

	char string[512];
	char *b = InlineString(string, supplies ? STR_SUPPLIES : STR_000D_ACCEPTS);

	for (CargoID i = 0; i < NUM_CARGO; i++) {
		if (b >= lastof(string) - (1 + 2 * 4)) break; // ',' or ' ' and two calls to Utf8Encode()
		switch (sct) {
			case SCT_PASSENGERS_ONLY: if (!IsCargoInClass(i, CC_PASSENGERS)) continue; break;
			case SCT_NON_PASSENGERS_ONLY: if (IsCargoInClass(i, CC_PASSENGERS)) continue; break;
			case SCT_ALL: break;
			default: NOT_REACHED();
		}
		if (cargo[i] >= (supplies ? 1U : 8U)) {
			if (first) {
				first = false;
			} else {
				/* Add a comma if this is not the first item */
				*b++ = ',';
				*b++ = ' ';
			}
			b = InlineString(b, GetCargo(i)->name);
		}
	}

	/* If first is still true then no cargo is accepted */
	if (first) b = InlineString(b, STR_00D0_NOTHING);

	*b = '\0';

	/* Make sure we detect any buffer overflow */
	assert(b < endof(string));

	SetDParamStr(0, string);
	return DrawStringMultiLine(str_x, str_y, STR_JUST_RAW_STRING, 144);
}

/**
 * Calculates and draws the accepted or supplied cargo around the selected tile(s)
 * @param sx x position where the string is to be drawn
 * @param sy y position where the string is to be drawn
 * @param sct which type of cargo is to be displayed (passengers/non-passengers)
 * @param rad radius around selected tile(s) to be searched
 * @param supplies if supplied cargos should be drawn, else accepted cargos
 * @return Returns the y value below the string that was drawn
 */
int DrawStationCoverageAreaText(int sx, int sy, StationCoverageType sct, int rad, bool supplies)
{
	TileIndex tile = TileVirtXY(_thd.pos.x, _thd.pos.y);
	AcceptedCargo cargo;
	if (tile < MapSize()) {
		if (supplies) {
			GetProductionAroundTiles(cargo, tile, _thd.size.x / TILE_SIZE, _thd.size.y / TILE_SIZE , rad);
		} else {
			GetAcceptanceAroundTiles(cargo, tile, _thd.size.x / TILE_SIZE, _thd.size.y / TILE_SIZE , rad);
		}
		return sy + DrawStationCoverageText(cargo, sx, sy, sct, supplies);
	}

	return sy;
}

void CheckRedrawStationCoverage(const Window *w)
{
	if (_thd.dirty & 1) {
		_thd.dirty &= ~1;
		SetWindowDirty(w);
	}
}

/* Delete a character at the caret position in a text buf.
 * If backspace is set, delete the character before the caret,
 * else delete the character after it. */
static void DelChar(Textbuf *tb, bool backspace)
{
	WChar c;
	char *s = tb->buf + tb->caretpos;

	if (backspace) s = Utf8PrevChar(s);

	uint16 len = (uint16)Utf8Decode(&c, s);
	uint width = GetCharacterWidth(FS_NORMAL, c);

	tb->width  -= width;
	if (backspace) {
		tb->caretpos   -= len;
		tb->caretxoffs -= width;
	}

	/* Move the remaining characters over the marker */
	memmove(s, s + len, tb->size - (s - tb->buf) - len);
	tb->size -= len;
}

/**
 * Delete a character from a textbuffer, either with 'Delete' or 'Backspace'
 * The character is delete from the position the caret is at
 * @param tb Textbuf type to be changed
 * @param delmode Type of deletion, either WKC_BACKSPACE or WKC_DELETE
 * @return Return true on successful change of Textbuf, or false otherwise
 */
bool DeleteTextBufferChar(Textbuf *tb, int delmode)
{
	if (delmode == WKC_BACKSPACE && tb->caretpos != 0) {
		DelChar(tb, true);
		return true;
	} else if (delmode == WKC_DELETE && tb->caretpos < tb->size - 1) {
		DelChar(tb, false);
		return true;
	}

	return false;
}

/**
 * Delete every character in the textbuffer
 * @param tb Textbuf buffer to be emptied
 */
void DeleteTextBufferAll(Textbuf *tb)
{
	memset(tb->buf, 0, tb->maxsize);
	tb->size = 1;
	tb->width = tb->caretpos = tb->caretxoffs = 0;
}

/**
 * Insert a character to a textbuffer. If maxwidth of the Textbuf is zero,
 * we don't care about the visual-length but only about the physical
 * length of the string
 * @param tb Textbuf type to be changed
 * @param key Character to be inserted
 * @return Return true on successful change of Textbuf, or false otherwise
 */
bool InsertTextBufferChar(Textbuf *tb, WChar key)
{
	const byte charwidth = GetCharacterWidth(FS_NORMAL, key);
	uint16 len = (uint16)Utf8CharLen(key);
	if (tb->size + len <= tb->maxsize && (tb->maxwidth == 0 || tb->width + charwidth <= tb->maxwidth)) {
		memmove(tb->buf + tb->caretpos + len, tb->buf + tb->caretpos, tb->size - tb->caretpos);
		Utf8Encode(tb->buf + tb->caretpos, key);
		tb->size  += len;
		tb->width += charwidth;

		tb->caretpos   += len;
		tb->caretxoffs += charwidth;
		return true;
	}
	return false;
}

/**
 * Handle text navigation with arrow keys left/right.
 * This defines where the caret will blink and the next characer interaction will occur
 * @param tb Textbuf type where navigation occurs
 * @param navmode Direction in which navigation occurs WKC_LEFT, WKC_RIGHT, WKC_END, WKC_HOME
 * @return Return true on successful change of Textbuf, or false otherwise
 */
bool MoveTextBufferPos(Textbuf *tb, int navmode)
{
	switch (navmode) {
		case WKC_LEFT:
			if (tb->caretpos != 0) {
				WChar c;
				const char *s = Utf8PrevChar(tb->buf + tb->caretpos);
				Utf8Decode(&c, s);
				tb->caretpos    = s - tb->buf; // -= (tb->buf + tb->caretpos - s)
				tb->caretxoffs -= GetCharacterWidth(FS_NORMAL, c);

				return true;
			}
			break;

		case WKC_RIGHT:
			if (tb->caretpos < tb->size - 1) {
				WChar c;

				tb->caretpos   += (uint16)Utf8Decode(&c, tb->buf + tb->caretpos);
				tb->caretxoffs += GetCharacterWidth(FS_NORMAL, c);

				return true;
			}
			break;

		case WKC_HOME:
			tb->caretpos = 0;
			tb->caretxoffs = 0;
			return true;

		case WKC_END:
			tb->caretpos = tb->size - 1;
			tb->caretxoffs = tb->width;
			return true;

		default:
			break;
	}

	return false;
}

/**
 * Initialize the textbuffer by supplying it the buffer to write into
 * and the maximum length of this buffer
 * @param tb Textbuf type which is getting initialized
 * @param buf the buffer that will be holding the data for input
 * @param maxsize maximum size in bytes, including terminating '\0'
 * @param maxwidth maximum length in pixels of this buffer. If reached, buffer
 * cannot grow, even if maxsize would allow because there is space. Width
 * of zero '0' means the buffer is only restricted by maxsize */
void InitializeTextBuffer(Textbuf *tb, char *buf, uint16 maxsize, uint16 maxwidth)
{
	assert(maxsize != 0);

	tb->buf      = buf;
	tb->maxsize  = maxsize;
	tb->maxwidth = maxwidth;
	tb->caret    = true;
	UpdateTextBufferSize(tb);
}

/**
 * Update Textbuf type with its actual physical character and screenlength
 * Get the count of characters in the string as well as the width in pixels.
 * Useful when copying in a larger amount of text at once
 * @param tb Textbuf type which length is calculated
 */
void UpdateTextBufferSize(Textbuf *tb)
{
	const char *buf = tb->buf;

	tb->width = 0;
	tb->size = 1; // terminating zero

	WChar c;
	while ((c = Utf8Consume(&buf)) != '\0') {
		tb->width += GetCharacterWidth(FS_NORMAL, c);
		tb->size += Utf8CharLen(c);
	}

	assert(tb->size <= tb->maxsize);

	tb->caretpos = tb->size - 1;
	tb->caretxoffs = tb->width;
}

bool HandleCaret(Textbuf *tb)
{
	/* caret changed? */
	bool b = !!(_caret_timer & 0x20);

	if (b != tb->caret) {
		tb->caret = b;
		return true;
	}
	return false;
}

bool QueryString::HasEditBoxFocus(const Window *w, int wid) const
{
	return ((w->window_class == WC_OSK &&
			_focused_window == w->parent &&
			w->parent->focused_widget &&
			w->parent->focused_widget->type == WWT_EDITBOX) ||
			w->IsWidgetGloballyFocused(wid));
}

HandleEditBoxResult QueryString::HandleEditBoxKey(Window *w, int wid, uint16 key, uint16 keycode, Window::EventState &state)
{
	if (!QueryString::HasEditBoxFocus(w, wid)) return HEBR_NOT_FOCUSED;

	state = Window::ES_HANDLED;

	switch (keycode) {
		case WKC_ESC: return HEBR_CANCEL;

		case WKC_RETURN: case WKC_NUM_ENTER: return HEBR_CONFIRM;

		case (WKC_CTRL | 'V'):
			if (InsertTextBufferClipboard(&this->text)) w->InvalidateWidget(wid);
			break;

		case (WKC_CTRL | 'U'):
			DeleteTextBufferAll(&this->text);
			w->InvalidateWidget(wid);
			break;

		case WKC_BACKSPACE: case WKC_DELETE:
			if (DeleteTextBufferChar(&this->text, keycode)) w->InvalidateWidget(wid);
			break;

		case WKC_LEFT: case WKC_RIGHT: case WKC_END: case WKC_HOME:
			if (MoveTextBufferPos(&this->text, keycode)) w->InvalidateWidget(wid);
			break;

		default:
			if (IsValidChar(key, this->afilter)) {
				if (InsertTextBufferChar(&this->text, key)) w->InvalidateWidget(wid);
			} else {
				state = Window::ES_NOT_HANDLED;
			}
	}

	return HEBR_EDITING;
}

void QueryString::HandleEditBox(Window *w, int wid)
{
	if (HasEditBoxFocus(w, wid) && HandleCaret(&this->text)) {
		w->InvalidateWidget(wid);
		/* When we're not the OSK, notify 'our' OSK to redraw the widget,
		 * so the caret changes appropriately. */
		if (w->window_class != WC_OSK) {
			Window *w_osk = FindWindowById(WC_OSK, 0);
			if (w_osk != NULL && w_osk->parent == w) w_osk->OnInvalidateData();
		}
	}
}

void QueryString::DrawEditBox(Window *w, int wid)
{
	const Widget *wi = &w->widget[wid];

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	GfxFillRect(wi->left + 1, wi->top + 1, wi->right - 1, wi->bottom - 1, 215);

	DrawPixelInfo dpi;
	int delta;

	/* Limit the drawing of the string inside the widget boundaries */
	if (!FillDrawPixelInfo(&dpi,
			wi->left + 4,
			wi->top + 1,
			wi->right - wi->left - 4,
			wi->bottom - wi->top - 1)) {
		return;
	}

	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &dpi;

	/* We will take the current widget length as maximum width, with a small
	 * space reserved at the end for the caret to show */
	const Textbuf *tb = &this->text;

	delta = (wi->right - wi->left) - tb->width - 10;
	if (delta > 0) delta = 0;

	if (tb->caretxoffs + delta < 0) delta = -tb->caretxoffs;

	DoDrawString(tb->buf, delta, 0, TC_YELLOW);
	if (HasEditBoxFocus(w, wid) && tb->caret) DoDrawString("_", tb->caretxoffs + delta, 0, TC_WHITE);

	_cur_dpi = old_dpi;
}

HandleEditBoxResult QueryStringBaseWindow::HandleEditBoxKey(int wid, uint16 key, uint16 keycode, EventState &state)
{
	return this->QueryString::HandleEditBoxKey(this, wid, key, keycode, state);
}

void QueryStringBaseWindow::HandleEditBox(int wid)
{
	this->QueryString::HandleEditBox(this, wid);
}

void QueryStringBaseWindow::DrawEditBox(int wid)
{
	this->QueryString::DrawEditBox(this, wid);
}

void QueryStringBaseWindow::OnOpenOSKWindow(int wid)
{
	ShowOnScreenKeyboard(this, wid, 0, 0);
}

enum QueryStringWidgets {
	QUERY_STR_WIDGET_TEXT = 3,
	QUERY_STR_WIDGET_DEFAULT,
	QUERY_STR_WIDGET_CANCEL,
	QUERY_STR_WIDGET_OK
};


struct QueryStringWindow : public QueryStringBaseWindow
{
	QueryStringWindow(uint16 size, const WindowDesc *desc, Window *parent) : QueryStringBaseWindow(size, desc)
	{
		this->parent = parent;
		this->SetFocusedWidget(QUERY_STR_WIDGET_TEXT);

		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		SetDParam(0, this->caption);
		this->DrawWidgets();

		this->DrawEditBox(QUERY_STR_WIDGET_TEXT);
	}

	void OnOk()
	{
		if (this->orig == NULL || strcmp(this->text.buf, this->orig) != 0) {
			/* If the parent is NULL, the editbox is handled by general function
			 * HandleOnEditText */
			if (this->parent != NULL) {
				this->parent->OnQueryTextFinished(this->text.buf);
			} else {
				HandleOnEditText(this->text.buf);
			}
			this->handled = true;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case QUERY_STR_WIDGET_DEFAULT:
				this->text.buf[0] = '\0';
				/* Fallthrough */
			case QUERY_STR_WIDGET_OK:
				this->OnOk();
				/* Fallthrough */
			case QUERY_STR_WIDGET_CANCEL:
				delete this;
				break;
		}
	}

	virtual void OnMouseLoop()
	{
		this->HandleEditBox(QUERY_STR_WIDGET_TEXT);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		switch (this->HandleEditBoxKey(QUERY_STR_WIDGET_TEXT, key, keycode, state)) {
			default: NOT_REACHED();
			case HEBR_EDITING: {
				Window *osk = FindWindowById(WC_OSK, 0);
				if (osk != NULL && osk->parent == this) osk->OnInvalidateData();
			} break;
			case HEBR_CONFIRM: this->OnOk();
			/* FALL THROUGH */
			case HEBR_CANCEL: delete this; break; // close window, abandon changes
			case HEBR_NOT_FOCUSED: break;
		}
		return state;
	}

	virtual void OnOpenOSKWindow(int wid)
	{
		ShowOnScreenKeyboard(this, wid, QUERY_STR_WIDGET_CANCEL, QUERY_STR_WIDGET_OK);
	}

	~QueryStringWindow()
	{
		if (!this->handled && this->parent != NULL) {
			Window *parent = this->parent;
			this->parent = NULL; // so parent doesn't try to delete us again
			parent->OnQueryTextFinished(NULL);
		}
	}
};

static const Widget _query_string_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,        STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   259,     0,    13, STR_012D,        STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   259,    14,    29, 0x0,             STR_NULL},
{    WWT_EDITBOX,   RESIZE_NONE,  COLOUR_GREY,     2,   257,    16,    27, 0x0,             STR_NULL}, // QUERY_STR_WIDGET_TEXT
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,     0,    86,    30,    41, STR_DEFAULT,     STR_NULL}, // QUERY_STR_WIDGET_DEFAULT
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,    87,   172,    30,    41, STR_012E_CANCEL, STR_NULL}, // QUERY_STR_WIDGET_CANCEL
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,   173,   259,    30,    41, STR_012F_OK,     STR_NULL}, // QUERY_STR_WIDGET_OK
{   WIDGETS_END},
};

static const WindowDesc _query_string_desc(
	190, 219, 260, 42, 260, 42,
	WC_QUERY_STRING, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_query_string_widgets
);

/** Show a query popup window with a textbox in it.
 * @param str StringID for the text shown in the textbox
 * @param caption StringID of text shown in caption of querywindow
 * @param maxsize maximum size in bytes (including terminating '\0')
 * @param maxwidth maximum width in pixels allowed
 * @param parent pointer to a Window that will handle the events (ok/cancel) of this
 *        window. If NULL, results are handled by global function HandleOnEditText
 * @param afilter filters out unwanted character input
 * @param flags various flags, @see QueryStringFlags
 */
void ShowQueryString(StringID str, StringID caption, uint maxsize, uint maxwidth, Window *parent, CharSetFilter afilter, QueryStringFlags flags)
{
	DeleteWindowById(WC_QUERY_STRING, 0);

	QueryStringWindow *w = new QueryStringWindow(maxsize, &_query_string_desc, parent);

	GetString(w->edit_str_buf, str, &w->edit_str_buf[maxsize - 1]);
	w->edit_str_buf[maxsize - 1] = '\0';

	if ((flags & QSF_ACCEPT_UNCHANGED) == 0) w->orig = strdup(w->edit_str_buf);

	if ((flags & QSF_ENABLE_DEFAULT) == 0) {
		/* without the "Default" button, make "Cancel" and "OK" buttons wider */
		w->SetWidgetHiddenState(QUERY_STR_WIDGET_DEFAULT, true);
		w->widget[QUERY_STR_WIDGET_CANCEL].left  = 0;
		w->widget[QUERY_STR_WIDGET_CANCEL].right = w->width / 2 - 1;
		w->widget[QUERY_STR_WIDGET_OK].left      = w->width / 2;
		w->widget[QUERY_STR_WIDGET_OK].right     = w->width - 1;
	}

	w->LowerWidget(QUERY_STR_WIDGET_TEXT);
	w->caption = caption;
	w->afilter = afilter;
	InitializeTextBuffer(&w->text, w->edit_str_buf, maxsize, maxwidth);
}


enum QueryWidgets {
	QUERY_WIDGET_CAPTION = 1,
	QUERY_WIDGET_NO = 3,
	QUERY_WIDGET_YES
};

/**
 * Window used for asking the user a YES/NO question.
 */
struct QueryWindow : public Window {
	QueryCallbackProc *proc; ///< callback function executed on closing of popup. Window* points to parent, bool is true if 'yes' clicked, false otherwise
	uint64 params[10];       ///< local copy of _decode_parameters
	StringID message;        ///< message shown for query window

	QueryWindow(const WindowDesc *desc, StringID caption, StringID message, Window *parent, QueryCallbackProc *callback) : Window(desc)
	{
		if (parent == NULL) parent = FindWindowById(WC_MAIN_WINDOW, 0);
		this->parent = parent;
		this->left = parent->left + (parent->width / 2) - (this->width / 2);
		this->top = parent->top + (parent->height / 2) - (this->height / 2);

		/* Create a backup of the variadic arguments to strings because it will be
		 * overridden pretty often. We will copy these back for drawing */
		CopyOutDParam(this->params, 0, lengthof(this->params));
		this->widget[QUERY_WIDGET_CAPTION].data = caption;
		this->message    = message;
		this->proc       = callback;

		this->FindWindowPlacementAndResize(desc);
	}

	~QueryWindow()
	{
		if (this->proc != NULL) this->proc(this->parent, false);
	}

	virtual void OnPaint()
	{
		CopyInDParam(0, this->params, lengthof(this->params));
		this->DrawWidgets();
		CopyInDParam(0, this->params, lengthof(this->params));

		DrawStringMultiCenter(this->width / 2, (this->height / 2) - 10, this->message, this->width - 2);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case QUERY_WIDGET_YES: {
				/* in the Generate New World window, clicking 'Yes' causes
				 * DeleteNonVitalWindows() to be called - we shouldn't be in a window then */
				QueryCallbackProc *proc = this->proc;
				Window *parent = this->parent;
				/* Prevent the destructor calling the callback function */
				this->proc = NULL;
				delete this;
				if (proc != NULL) {
					proc(parent, true);
					proc = NULL;
				}
			} break;
			case QUERY_WIDGET_NO:
				delete this;
				break;
		}
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		/* ESC closes the window, Enter confirms the action */
		switch (keycode) {
			case WKC_RETURN:
			case WKC_NUM_ENTER:
				if (this->proc != NULL) {
					this->proc(this->parent, true);
					this->proc = NULL;
				}
				/* Fallthrough */
			case WKC_ESC:
				delete this;
				return ES_HANDLED;
		}
		return ES_NOT_HANDLED;
	}
};


static const Widget _query_widgets[] = {
{  WWT_CLOSEBOX, RESIZE_NONE,  COLOUR_RED,      0,  10,   0,  13, STR_00C5,        STR_018B_CLOSE_WINDOW},
{   WWT_CAPTION, RESIZE_NONE,  COLOUR_RED,     11, 209,   0,  13, STR_NULL,        STR_NULL},
{     WWT_PANEL, RESIZE_NONE,  COLOUR_RED,      0, 209,  14,  81, 0x0, /*OVERRIDE*/STR_NULL},
{WWT_PUSHTXTBTN, RESIZE_NONE,  COLOUR_YELLOW,  20,  90,  62,  73, STR_00C9_NO,     STR_NULL},
{WWT_PUSHTXTBTN, RESIZE_NONE,  COLOUR_YELLOW, 120, 190,  62,  73, STR_00C8_YES,    STR_NULL},
{   WIDGETS_END },
};

static const WindowDesc _query_desc(
	WDP_CENTER, WDP_CENTER, 210, 82, 210, 82,
	WC_CONFIRM_POPUP_QUERY, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_UNCLICK_BUTTONS | WDF_DEF_WIDGET | WDF_MODAL,
	_query_widgets
);

/** Show a modal confirmation window with standard 'yes' and 'no' buttons
 * The window is aligned to the centre of its parent.
 * NOTE: You cannot use BindCString as parameter for this window!
 * @param caption string shown as window caption
 * @param message string that will be shown for the window
 * @param parent pointer to parent window, if this pointer is NULL the parent becomes
 * the main window WC_MAIN_WINDOW
 * @param callback callback function pointer to set in the window descriptor
 */
void ShowQuery(StringID caption, StringID message, Window *parent, QueryCallbackProc *callback)
{
	new QueryWindow(&_query_desc, caption, message, parent, callback);
}


static const Widget _load_dialog_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                 STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,  RESIZE_RIGHT,  COLOUR_GREY,    11,   256,     0,    13, STR_NULL,                 STR_018C_WINDOW_TITLE_DRAG_THIS},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_GREY,     0,   127,    14,    25, STR_SORT_BY_NAME,         STR_SORT_ORDER_TIP},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_GREY,   128,   256,    14,    25, STR_SORT_BY_DATE,         STR_SORT_ORDER_TIP},
{      WWT_PANEL,  RESIZE_RIGHT,  COLOUR_GREY,     0,   256,    26,    47, 0x0,                      STR_NULL},
{      WWT_PANEL,     RESIZE_RB,  COLOUR_GREY,     0,   256,    48,   153, 0x0,                      STR_NULL},
{ WWT_PUSHIMGBTN,     RESIZE_LR,  COLOUR_GREY,   245,   256,    48,    59, SPR_HOUSE_ICON,           STR_SAVELOAD_HOME_BUTTON},
{      WWT_INSET,     RESIZE_RB,  COLOUR_GREY,     2,   243,    50,   139, 0x0,                      STR_400A_LIST_OF_DRIVES_DIRECTORIES},
{  WWT_SCROLLBAR,    RESIZE_LRB,  COLOUR_GREY,   245,   256,    60,   141, 0x0,                      STR_0190_SCROLL_BAR_SCROLLS_LIST},
{ WWT_PUSHTXTBTN,    RESIZE_RTB,  COLOUR_GREY,     0,   243,   142,   153, STR_CONTENT_INTRO_BUTTON, STR_CONTENT_INTRO_BUTTON_TIP},
{  WWT_RESIZEBOX,   RESIZE_LRTB,  COLOUR_GREY,   245,   256,   142,   153, 0x0,                      STR_RESIZE_BUTTON},
{   WIDGETS_END},
};

static const Widget _save_dialog_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,         STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,  RESIZE_RIGHT,  COLOUR_GREY,    11,   256,     0,    13, STR_NULL,         STR_018C_WINDOW_TITLE_DRAG_THIS},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_GREY,     0,   127,    14,    25, STR_SORT_BY_NAME, STR_SORT_ORDER_TIP},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_GREY,   128,   256,    14,    25, STR_SORT_BY_DATE, STR_SORT_ORDER_TIP},
{      WWT_PANEL,  RESIZE_RIGHT,  COLOUR_GREY,     0,   256,    26,    47, 0x0,              STR_NULL},
{      WWT_PANEL,     RESIZE_RB,  COLOUR_GREY,     0,   256,    48,   167, 0x0,              STR_NULL},
{ WWT_PUSHIMGBTN,     RESIZE_LR,  COLOUR_GREY,   245,   256,    48,    59, SPR_HOUSE_ICON,   STR_SAVELOAD_HOME_BUTTON},
{      WWT_INSET,     RESIZE_RB,  COLOUR_GREY,     2,   243,    50,   150, 0x0,              STR_400A_LIST_OF_DRIVES_DIRECTORIES},
{  WWT_SCROLLBAR,    RESIZE_LRB,  COLOUR_GREY,   245,   256,    60,   151, 0x0,              STR_0190_SCROLL_BAR_SCROLLS_LIST},
{      WWT_PANEL,    RESIZE_RTB,  COLOUR_GREY,     0,   256,   152,     0, 0x0,              STR_NULL},
{    WWT_EDITBOX,    RESIZE_RTB,  COLOUR_GREY,     2,   254,   154,   165, STR_SAVE_OSKTITLE, STR_400B_CURRENTLY_SELECTED_NAME},
{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,     0,   127,   168,   179, STR_4003_DELETE,  STR_400C_DELETE_THE_CURRENTLY_SELECTED},
{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,   128,   244,   168,   179, STR_4002_SAVE,    STR_400D_SAVE_THE_CURRENT_GAME_USING},
{  WWT_RESIZEBOX,   RESIZE_LRTB,  COLOUR_GREY,   245,   256,   168,   179, 0x0,              STR_RESIZE_BUTTON},
{   WIDGETS_END},
};

/* Colours for fios types */
const TextColour _fios_colours[] = {
	TC_LIGHT_BLUE, TC_DARK_GREEN,  TC_DARK_GREEN, TC_ORANGE, TC_LIGHT_BROWN,
	TC_ORANGE,     TC_LIGHT_BROWN, TC_ORANGE,     TC_ORANGE, TC_YELLOW
};

void BuildFileList()
{
	_fios_path_changed = true;
	FiosFreeSavegameList();

	switch (_saveload_mode) {
		case SLD_NEW_GAME:
		case SLD_LOAD_SCENARIO:
		case SLD_SAVE_SCENARIO:
			FiosGetScenarioList(_saveload_mode); break;
		case SLD_LOAD_HEIGHTMAP:
			FiosGetHeightmapList(_saveload_mode); break;

		default: FiosGetSavegameList(_saveload_mode); break;
	}
}

static void DrawFiosTexts(uint maxw)
{
	static const char *path = NULL;
	static StringID str = STR_4006_UNABLE_TO_READ_DRIVE;
	static uint64 tot = 0;

	if (_fios_path_changed) {
		str = FiosGetDescText(&path, &tot);
		_fios_path_changed = false;
	}

	if (str != STR_4006_UNABLE_TO_READ_DRIVE) SetDParam(0, tot);
	DrawString(2, 37, str, TC_FROMSTRING);
	DoDrawStringTruncated(path, 2, 27, TC_BLACK, maxw);
}

static void MakeSortedSaveGameList()
{
	uint sort_start = 0;
	uint sort_end = 0;

	/* Directories are always above the files (FIOS_TYPE_DIR)
	 * Drives (A:\ (windows only) are always under the files (FIOS_TYPE_DRIVE)
	 * Only sort savegames/scenarios, not directories
	 */
	for (const FiosItem *item = _fios_items.Begin(); item != _fios_items.End(); item++) {
		switch (item->type) {
			case FIOS_TYPE_DIR:    sort_start++; break;
			case FIOS_TYPE_PARENT: sort_start++; break;
			case FIOS_TYPE_DRIVE:  sort_end++;   break;
			default: break;
		}
	}

	uint s_amount = _fios_items.Length() - sort_start - sort_end;
	if (s_amount > 0) {
		qsort(_fios_items.Get(sort_start), s_amount, sizeof(FiosItem), compare_FiosItems);
	}
}

extern void StartupEngines();

struct SaveLoadWindow : public QueryStringBaseWindow {
private:
	enum SaveLoadWindowWidgets {
		SLWW_CLOSE = 0,
		SLWW_WINDOWTITLE,
		SLWW_SORT_BYNAME,
		SLWW_SORT_BYDATE,
		SLWW_HOME_BUTTON = 6,
		SLWW_DRIVES_DIRECTORIES_LIST,
		SLWW_CONTENT_DOWNLOAD = 9, ///< only available for play scenario/heightmap (content download)
		SLWW_SAVE_OSK_TITLE,       ///< only available for save operations
		SLWW_DELETE_SELECTION,     ///< same in here
		SLWW_SAVE_GAME,            ///< not to mention in here too
	};

	FiosItem o_dir;
public:

	void GenerateFileName()
	{
		GenerateDefaultSaveName(this->edit_str_buf, &this->edit_str_buf[this->edit_str_size - 1]);
	}

	SaveLoadWindow(const WindowDesc *desc, SaveLoadDialogMode mode) : QueryStringBaseWindow(64, desc)
	{
		static const StringID saveload_captions[] = {
			STR_4001_LOAD_GAME,
			STR_0298_LOAD_SCENARIO,
			STR_4000_SAVE_GAME,
			STR_0299_SAVE_SCENARIO,
			STR_LOAD_HEIGHTMAP,
		};

		this->vscroll.cap = 10;
		this->resize.step_width = 2;
		this->resize.step_height = 10;

		SetObjectToPlace(SPR_CURSOR_ZZZ, PAL_NONE, VHM_NONE, WC_MAIN_WINDOW, 0);

		/* Use an array to define what will be the current file type being handled
		 * by current file mode */
		switch (mode) {
			case SLD_LOAD_GAME:
				this->HideWidget(SLWW_CONTENT_DOWNLOAD);
				this->widget[SLWW_DRIVES_DIRECTORIES_LIST].bottom += this->widget[SLWW_CONTENT_DOWNLOAD].bottom - this->widget[SLWW_CONTENT_DOWNLOAD].top;
				break;

			case SLD_LOAD_SCENARIO:
			case SLD_LOAD_HEIGHTMAP:
				this->vscroll.cap--;

			case SLD_SAVE_GAME:     this->GenerateFileName(); break;
			case SLD_SAVE_SCENARIO: strcpy(this->edit_str_buf, "UNNAMED"); break;
			default:                break;
		}

		assert((uint)mode < lengthof(saveload_captions));

		this->widget[SLWW_WINDOWTITLE].data = saveload_captions[mode];
		this->LowerWidget(SLWW_DRIVES_DIRECTORIES_LIST);

		this->afilter = CS_ALPHANUMERAL;
		InitializeTextBuffer(&this->text, this->edit_str_buf, this->edit_str_size, 240);

		/* pause is only used in single-player, non-editor mode, non-menu mode. It
		 * will be unpaused in the WE_DESTROY event handler. */
		if (_game_mode != GM_MENU && !_networking && _game_mode != GM_EDITOR) {
			if (_pause_game >= 0) DoCommandP(0, 1, 0, CMD_PAUSE);
		}

		BuildFileList();

		ResetObjectToPlace();

		o_dir.type = FIOS_TYPE_DIRECT;
		switch (_saveload_mode) {
			case SLD_SAVE_GAME:
			case SLD_LOAD_GAME:
				FioGetDirectory(o_dir.name, lengthof(o_dir.name), SAVE_DIR);
				break;

			case SLD_SAVE_SCENARIO:
			case SLD_LOAD_SCENARIO:
				FioGetDirectory(o_dir.name, lengthof(o_dir.name), SCENARIO_DIR);
				break;

			case SLD_LOAD_HEIGHTMAP:
				FioGetDirectory(o_dir.name, lengthof(o_dir.name), HEIGHTMAP_DIR);
				break;

			default:
				strecpy(o_dir.name, _personal_dir, lastof(o_dir.name));
		}

		/* Focus the edit box by default in the save windows */
		if (_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO) {
			this->SetFocusedWidget(SLWW_SAVE_OSK_TITLE);
		}

		this->FindWindowPlacementAndResize(desc);
	}

	virtual ~SaveLoadWindow()
	{
		/* pause is only used in single-player, non-editor mode, non menu mode */
		if (!_networking && _game_mode != GM_EDITOR && _game_mode != GM_MENU) {
			if (_pause_game >= 0) DoCommandP(0, 0, 0, CMD_PAUSE);
		}
		FiosFreeSavegameList();
	}

	virtual void OnPaint()
	{
		int y;

		SetVScrollCount(this, _fios_items.Length());
		this->DrawWidgets();
		DrawFiosTexts(this->width);

		if (_savegame_sort_dirty) {
			_savegame_sort_dirty = false;
			MakeSortedSaveGameList();
		}

		const Widget *widg = &this->widget[SLWW_DRIVES_DIRECTORIES_LIST];
		GfxFillRect(widg->left + 1, widg->top + 1, widg->right, widg->bottom, 0xD7);
		this->DrawSortButtonState(_savegame_sort_order & SORT_BY_NAME ? SLWW_SORT_BYNAME : SLWW_SORT_BYDATE, _savegame_sort_order & SORT_DESCENDING ? SBS_DOWN : SBS_UP);

		y = widg->top + 1;
		for (uint pos = this->vscroll.pos; pos < _fios_items.Length(); pos++) {
			const FiosItem *item = _fios_items.Get(pos);

			DoDrawStringTruncated(item->title, 4, y, _fios_colours[item->type], this->width - 18);
			y += 10;
			if (y >= this->vscroll.cap * 10 + widg->top + 1) break;
		}

		if (_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO) {
			this->DrawEditBox(SLWW_SAVE_OSK_TITLE);
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case SLWW_SORT_BYNAME: // Sort save names by name
				_savegame_sort_order = (_savegame_sort_order == SORT_BY_NAME) ?
					SORT_BY_NAME | SORT_DESCENDING : SORT_BY_NAME;
				_savegame_sort_dirty = true;
				this->SetDirty();
				break;

			case SLWW_SORT_BYDATE: // Sort save names by date
				_savegame_sort_order = (_savegame_sort_order == SORT_BY_DATE) ?
					SORT_BY_DATE | SORT_DESCENDING : SORT_BY_DATE;
				_savegame_sort_dirty = true;
				this->SetDirty();
				break;

			case SLWW_HOME_BUTTON: // OpenTTD 'button', jumps to OpenTTD directory
				FiosBrowseTo(&o_dir);
				this->SetDirty();
				BuildFileList();
				break;

			case SLWW_DRIVES_DIRECTORIES_LIST: { // Click the listbox
				int y = (pt.y - this->widget[widget].top - 1) / 10;

				if (y < 0 || (y += this->vscroll.pos) >= this->vscroll.count) return;

				const FiosItem *file = _fios_items.Get(y);

				const char *name = FiosBrowseTo(file);
				if (name != NULL) {
					if (_saveload_mode == SLD_LOAD_GAME || _saveload_mode == SLD_LOAD_SCENARIO) {
						_switch_mode = (_game_mode == GM_EDITOR) ? SM_LOAD_SCENARIO : SM_LOAD;

						SetFiosType(file->type);
						strecpy(_file_to_saveload.name, name, lastof(_file_to_saveload.name));
						strecpy(_file_to_saveload.title, file->title, lastof(_file_to_saveload.title));

						delete this;
					} else if (_saveload_mode == SLD_LOAD_HEIGHTMAP) {
						SetFiosType(file->type);
						strecpy(_file_to_saveload.name, name, lastof(_file_to_saveload.name));
						strecpy(_file_to_saveload.title, file->title, lastof(_file_to_saveload.title));

						delete this;
						ShowHeightmapLoad();
					} else {
						/* SLD_SAVE_GAME, SLD_SAVE_SCENARIO copy clicked name to editbox */
						ttd_strlcpy(this->text.buf, file->title, this->text.maxsize);
						UpdateTextBufferSize(&this->text);
						this->InvalidateWidget(SLWW_SAVE_OSK_TITLE);
					}
				} else {
					/* Changed directory, need repaint. */
					this->SetDirty();
					BuildFileList();
				}
				break;
			}

			case SLWW_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(INVALID_STRING_ID, STR_NETWORK_ERR_NOTAVAILABLE, 0, 0);
				} else {
#if defined(ENABLE_NETWORK)
					switch (_saveload_mode) {
						default: NOT_REACHED();
						case SLD_LOAD_SCENARIO:  ShowNetworkContentListWindow(NULL, CONTENT_TYPE_SCENARIO);  break;
						case SLD_LOAD_HEIGHTMAP: ShowNetworkContentListWindow(NULL, CONTENT_TYPE_HEIGHTMAP); break;
					}
#endif
				}
				break;

			case SLWW_DELETE_SELECTION: case SLWW_SAVE_GAME: // Delete, Save game
				break;
		}
	}

	virtual void OnMouseLoop()
	{
		if (_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO) {
			this->HandleEditBox(SLWW_SAVE_OSK_TITLE);
		}
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		if (keycode == WKC_ESC) {
			delete this;
			return ES_HANDLED;
		}

		EventState state = ES_NOT_HANDLED;
		if ((_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO) &&
				this->HandleEditBoxKey(SLWW_SAVE_OSK_TITLE, key, keycode, state) == HEBR_CONFIRM) {
			this->HandleButtonClick(SLWW_SAVE_GAME);
		}

		return state;
	}

	virtual void OnTimeout()
	{
		/* This test protects against using widgets 11 and 12 which are only available
		 * in those two saveload mode */
		if (!(_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO)) return;

		if (this->IsWidgetLowered(SLWW_DELETE_SELECTION)) { // Delete button clicked
			if (!FiosDelete(this->text.buf)) {
				ShowErrorMessage(INVALID_STRING_ID, STR_4008_UNABLE_TO_DELETE_FILE, 0, 0);
			} else {
				BuildFileList();
				/* Reset file name to current date on successful delete */
				if (_saveload_mode == SLD_SAVE_GAME) GenerateFileName();
			}

			UpdateTextBufferSize(&this->text);
			this->SetDirty();
		} else if (this->IsWidgetLowered(SLWW_SAVE_GAME)) { // Save button clicked
			_switch_mode = SM_SAVE;
			FiosMakeSavegameName(_file_to_saveload.name, this->text.buf, sizeof(_file_to_saveload.name));

			/* In the editor set up the vehicle engines correctly (date might have changed) */
			if (_game_mode == GM_EDITOR) StartupEngines();
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		/* Widget 2 and 3 have to go with halve speed, make it so obiwan */
		uint diff = delta.x / 2;
		this->widget[SLWW_SORT_BYNAME].right += diff;
		this->widget[SLWW_SORT_BYDATE].left  += diff;
		this->widget[SLWW_SORT_BYDATE].right += delta.x;

		/* Same for widget 11 and 12 in save-dialog */
		if (_saveload_mode == SLD_SAVE_GAME || _saveload_mode == SLD_SAVE_SCENARIO) {
			this->widget[SLWW_DELETE_SELECTION].right += diff;
			this->widget[SLWW_SAVE_GAME].left  += diff;
			this->widget[SLWW_SAVE_GAME].right += delta.x;
		}

		this->vscroll.cap += delta.y / 10;
	}

	virtual void OnInvalidateData(int data)
	{
		BuildFileList();
	}
};

static const WindowDesc _load_dialog_desc(
	WDP_CENTER, WDP_CENTER, 257, 154, 257, 294,
	WC_SAVELOAD, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_STD_BTN | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_load_dialog_widgets
);

static const WindowDesc _save_dialog_desc(
	WDP_CENTER, WDP_CENTER, 257, 180, 257, 320,
	WC_SAVELOAD, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_STD_BTN | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_save_dialog_widgets
);

/** These values are used to convert the file/operations mode into a corresponding file type.
 * So each entry, as expressed by the related comment, is based on the enum   */
static const FileType _file_modetotype[] = {
	FT_SAVEGAME,  ///< used for SLD_LOAD_GAME
	FT_SCENARIO,  ///< used for SLD_LOAD_SCENARIO
	FT_SAVEGAME,  ///< used for SLD_SAVE_GAME
	FT_SCENARIO,  ///< used for SLD_SAVE_SCENARIO
	FT_HEIGHTMAP, ///< used for SLD_LOAD_HEIGHTMAP
	FT_SAVEGAME,  ///< SLD_NEW_GAME
};

void ShowSaveLoadDialog(SaveLoadDialogMode mode)
{
	DeleteWindowById(WC_SAVELOAD, 0);

	const WindowDesc *sld;
	switch (mode) {
		case SLD_SAVE_GAME:
		case SLD_SAVE_SCENARIO:
			sld = &_save_dialog_desc; break;
		default:
			sld = &_load_dialog_desc; break;
	}

	_saveload_mode = mode;
	_file_to_saveload.filetype = _file_modetotype[mode];

	new SaveLoadWindow(sld, mode);
}

void RedrawAutosave()
{
	SetWindowDirty(FindWindowById(WC_STATUS_BAR, 0));
}

void SetFiosType(const byte fiostype)
{
	switch (fiostype) {
		case FIOS_TYPE_FILE:
		case FIOS_TYPE_SCENARIO:
			_file_to_saveload.mode = SL_LOAD;
			break;

		case FIOS_TYPE_OLDFILE:
		case FIOS_TYPE_OLD_SCENARIO:
			_file_to_saveload.mode = SL_OLD_LOAD;
			break;

#ifdef WITH_PNG
		case FIOS_TYPE_PNG:
			_file_to_saveload.mode = SL_PNG;
			break;
#endif /* WITH_PNG */

		case FIOS_TYPE_BMP:
			_file_to_saveload.mode = SL_BMP;
			break;

		default:
			_file_to_saveload.mode = SL_INVALID;
			break;
	}
}
