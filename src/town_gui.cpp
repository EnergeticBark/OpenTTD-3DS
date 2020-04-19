/* $Id$ */

/** @file town_gui.cpp GUI for towns. */

#include "stdafx.h"
#include "openttd.h"
#include "town.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "company_func.h"
#include "company_base.h"
#include "company_gui.h"
#include "network/network.h"
#include "variables.h"
#include "strings_func.h"
#include "sound_func.h"
#include "economy_func.h"
#include "tilehighlight_func.h"
#include "sortlist_type.h"
#include "road_cmd.h"
#include "landscape_type.h"
#include "landscape.h"
#include "cargotype.h"
#include "tile_map.h"

#include "table/sprites.h"
#include "table/strings.h"

typedef GUIList<const Town*> GUITownList;

static const Widget _town_authority_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_BROWN,     0,    10,     0,    13, STR_00C5,                 STR_018B_CLOSE_WINDOW},              // TWA_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_BROWN,    11,   316,     0,    13, STR_2022_LOCAL_AUTHORITY, STR_018C_WINDOW_TITLE_DRAG_THIS},    // TWA_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_BROWN,     0,   316,    14,   105, 0x0,                      STR_NULL},                           // TWA_RATING_INFO
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_BROWN,     0,   304,   106,   157, 0x0,                      STR_2043_LIST_OF_THINGS_TO_DO_AT},   // TWA_COMMAND_LIST
{  WWT_SCROLLBAR,   RESIZE_NONE,  COLOUR_BROWN,   305,   316,   106,   157, 0x0,                      STR_0190_SCROLL_BAR_SCROLLS_LIST},   // TWA_SCROLLBAR
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_BROWN,     0,   316,   158,   209, 0x0,                      STR_NULL},                           // TWA_ACTION_INFO
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,     0,   316,   210,   221, STR_2042_DO_IT,           STR_2044_CARRY_OUT_THE_HIGHLIGHTED}, // TWA_EXECUTE
{   WIDGETS_END},
};

extern const byte _town_action_costs[8];

struct TownAuthorityWindow : Window {
private:
	Town *town;
	int sel_index;

	enum TownAuthorityWidget {
		TWA_CLOSEBOX = 0,
		TWA_CAPTION,
		TWA_RATING_INFO,
		TWA_COMMAND_LIST,
		TWA_SCROLLBAR,
		TWA_ACTION_INFO,
		TWA_EXECUTE,
	};

	/**
	 * Get the position of the Nth set bit.
	 *
	 * If there is no Nth bit set return -1
	 *
	 * @param bits The value to search in
	 * @param n The Nth set bit from which we want to know the position
	 * @return The position of the Nth set bit
	 */
	static int GetNthSetBit(uint32 bits, int n)
	{
		if (n >= 0) {
			uint i;
			FOR_EACH_SET_BIT(i, bits) {
				n--;
				if (n < 0) return i;
			}
		}
		return -1;
	}

public:
	TownAuthorityWindow(const WindowDesc *desc, WindowNumber window_number) :
			Window(desc, window_number), sel_index(-1)
	{
		this->town = GetTown(this->window_number);
		this->vscroll.cap = 5;

		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		int numact;
		uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);

		SetVScrollCount(this, numact + 1);

		if (this->sel_index != -1 && !HasBit(buttons, this->sel_index)) {
			this->sel_index = -1;
		}

		this->SetWidgetDisabledState(6, this->sel_index == -1);

		SetDParam(0, this->window_number);
		this->DrawWidgets();

		int y = this->widget[TWA_RATING_INFO].top + 1;

		DrawString(2, y, STR_2023_TRANSPORT_COMPANY_RATINGS, TC_FROMSTRING);
		y += 10;

		/* Draw list of companies */
		const Company *c;
		FOR_ALL_COMPANIES(c) {
			if ((HasBit(this->town->have_ratings, c->index) || this->town->exclusivity == c->index)) {
				DrawCompanyIcon(c->index, 2, y);

				SetDParam(0, c->index);
				SetDParam(1, c->index);

				int r = this->town->ratings[c->index];
				StringID str;
				(str = STR_3035_APPALLING, r <= RATING_APPALLING) || // Apalling
				(str++,                    r <= RATING_VERYPOOR)  || // Very Poor
				(str++,                    r <= RATING_POOR)      || // Poor
				(str++,                    r <= RATING_MEDIOCRE)  || // Mediocore
				(str++,                    r <= RATING_GOOD)      || // Good
				(str++,                    r <= RATING_VERYGOOD)  || // Very Good
				(str++,                    r <= RATING_EXCELLENT) || // Excellent
				(str++,                    true);                    // Outstanding

				SetDParam(2, str);
				if (this->town->exclusivity == c->index) { // red icon for company with exclusive rights
					DrawSprite(SPR_BLOT, PALETTE_TO_RED, 18, y);
				}

				DrawString(28, y, STR_2024, TC_FROMSTRING);
				y += 10;
			}
		}

		if (y > this->widget[TWA_RATING_INFO].bottom) {
			/* If the company list is too big to fit, mark ourself dirty and draw again. */
			ResizeWindowForWidget(this, TWA_RATING_INFO, 0, y - this->widget[TWA_RATING_INFO].bottom);
			this->SetDirty();
			return;
		}

		y = this->widget[TWA_COMMAND_LIST].top + 1;
		int pos = this->vscroll.pos;

		if (--pos < 0) {
			DrawString(2, y, STR_2045_ACTIONS_AVAILABLE, TC_FROMSTRING);
			y += 10;
		}

		for (int i = 0; buttons; i++, buttons >>= 1) {
			if (pos <= -5) break; ///< Draw only the 5 fitting lines

			if ((buttons & 1) && --pos < 0) {
				DrawString(3, y, STR_2046_SMALL_ADVERTISING_CAMPAIGN + i, TC_ORANGE);
				y += 10;
			}
		}

		if (this->sel_index != -1) {
			SetDParam(1, (_price.build_industry >> 8) * _town_action_costs[this->sel_index]);
			SetDParam(0, STR_2046_SMALL_ADVERTISING_CAMPAIGN + this->sel_index);
			DrawStringMultiLine(2, this->widget[TWA_ACTION_INFO].top + 1, STR_204D_INITIATE_A_SMALL_LOCAL + this->sel_index, 313);
		}
	}

	virtual void OnDoubleClick(Point pt, int widget) { HandleClick(pt, widget, true); }
	virtual void OnClick(Point pt, int widget) { HandleClick(pt, widget, false); }

	void HandleClick(Point pt, int widget, bool double_click)
	{
		switch (widget) {
			case TWA_COMMAND_LIST: {
				int y = (pt.y - this->widget[TWA_COMMAND_LIST].top - 1) / 10;

				if (!IsInsideMM(y, 0, 5)) return;

				y = GetNthSetBit(GetMaskOfTownActions(NULL, _local_company, this->town), y + this->vscroll.pos - 1);
				if (y >= 0) {
					this->sel_index = y;
					this->SetDirty();
				}
				/* Fall through to clicking in case we are double-clicked */
				if (!double_click || y < 0) break;
			}

			case TWA_EXECUTE:
				DoCommandP(this->town->xy, this->window_number, this->sel_index, CMD_DO_TOWN_ACTION | CMD_MSG(STR_00B4_CAN_T_DO_THIS));
				break;
		}
	}

	virtual void OnHundredthTick()
	{
		this->SetDirty();
	}
};

static const WindowDesc _town_authority_desc(
	WDP_AUTO, WDP_AUTO, 317, 222, 317, 222,
	WC_TOWN_AUTHORITY, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_town_authority_widgets
);

static void ShowTownAuthorityWindow(uint town)
{
	AllocateWindowDescFront<TownAuthorityWindow>(&_town_authority_desc, town);
}

struct TownViewWindow : Window {
private:
	Town *town;

	enum TownViewWidget {
		TVW_CAPTION = 1,
		TVW_STICKY,
		TVW_VIEWPORTPANEL,
		TVW_INFOPANEL = 5,
		TVW_CENTERVIEW,
		TVW_SHOWAUTORITY,
		TVW_CHANGENAME,
		TVW_EXPAND,
		TVW_DELETE,
	};

public:
	enum {
		TVW_HEIGHT_NORMAL = 150,
	};

	TownViewWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		this->town = GetTown(this->window_number);
		bool ingame = _game_mode != GM_EDITOR;

		this->flags4 |= WF_DISABLE_VP_SCROLL;
		InitializeWindowViewport(this, 3, 17, 254, 86, this->town->xy, ZOOM_LVL_TOWN);

		if (this->town->larger_town) this->widget[TVW_CAPTION].data = STR_CITY;
		this->SetWidgetHiddenState(TVW_DELETE, ingame);  // hide delete button on game mode
		this->SetWidgetHiddenState(TVW_EXPAND, ingame);  // hide expand button on game mode
		this->SetWidgetHiddenState(TVW_SHOWAUTORITY, !ingame); // hide autority button on editor mode

		if (ingame) {
			/* resize caption bar */
			this->widget[TVW_CAPTION].right = this->widget[TVW_STICKY].left -1;
			/* move the rename from top on scenario to bottom in game */
			this->widget[TVW_CHANGENAME].top = this->widget[TVW_EXPAND].top;
			this->widget[TVW_CHANGENAME].bottom = this->widget[TVW_EXPAND].bottom;
			this->widget[TVW_CHANGENAME].right = this->widget[TVW_STICKY].right;
		}

		this->ResizeWindowAsNeeded();

		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		/* disable renaming town in network games if you are not the server */
		this->SetWidgetDisabledState(TVW_CHANGENAME, _networking && !_network_server);

		SetDParam(0, this->town->index);
		this->DrawWidgets();

		uint y = 107;

		SetDParam(0, this->town->population);
		SetDParam(1, this->town->num_houses);
		DrawString(2, y, STR_2006_POPULATION, TC_FROMSTRING);

		SetDParam(0, this->town->act_pass);
		SetDParam(1, this->town->max_pass);
		DrawString(2, y += 10, STR_200D_PASSENGERS_LAST_MONTH_MAX, TC_FROMSTRING);

		SetDParam(0, this->town->act_mail);
		SetDParam(1, this->town->max_mail);
		DrawString(2, y += 10, STR_200E_MAIL_LAST_MONTH_MAX, TC_FROMSTRING);

		uint cargo_needed_for_growth = 0;
		switch (_settings_game.game_creation.landscape) {
			case LT_ARCTIC:
				if (TilePixelHeight(this->town->xy) >= LowestSnowLine()) cargo_needed_for_growth = 1;
				break;

			case LT_TROPIC:
				if (GetTropicZone(this->town->xy) == TROPICZONE_DESERT) cargo_needed_for_growth = 2;
				break;

			default: break;
		}

		if (cargo_needed_for_growth > 0) {
			DrawString(2, y += 10, STR_CARGO_FOR_TOWNGROWTH, TC_FROMSTRING);

			CargoID first_food_cargo = CT_INVALID;
			StringID food_name = STR_001E_FOOD;
			CargoID first_water_cargo = CT_INVALID;
			StringID water_name = STR_0021_WATER;
			for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
				const CargoSpec *cs = GetCargo(cid);
				if (first_food_cargo == CT_INVALID && cs->town_effect == TE_FOOD) {
					first_food_cargo = cid;
					food_name = cs->name;
				}
				if (first_water_cargo == CT_INVALID && cs->town_effect == TE_WATER) {
					first_water_cargo = cid;
					water_name = cs->name;
				}
			}

			if (first_food_cargo != CT_INVALID && this->town->act_food > 0) {
				SetDParam(0, first_food_cargo);
				SetDParam(1, this->town->act_food);
				DrawString(2, y += 10, STR_CARGO_FOR_TOWNGROWTH_LAST_MONTH, TC_FROMSTRING);
			} else {
				SetDParam(0, food_name);
				DrawString(2, y += 10, STR_CARGO_FOR_TOWNGROWTH_REQUIRED, TC_FROMSTRING);
			}

			if (cargo_needed_for_growth > 1) {
				if (first_water_cargo != CT_INVALID && this->town->act_water > 0) {
					SetDParam(0, first_water_cargo);
					SetDParam(1, this->town->act_water);
					DrawString(2, y += 10, STR_CARGO_FOR_TOWNGROWTH_LAST_MONTH, TC_FROMSTRING);
				} else {
					SetDParam(0, water_name);
					DrawString(2, y += 10, STR_CARGO_FOR_TOWNGROWTH_REQUIRED, TC_FROMSTRING);
				}
			}
		}

		this->DrawViewport();

		/* only show the town noise, if the noise option is activated. */
		if (_settings_game.economy.station_noise_level) {
			SetDParam(0, this->town->noise_reached);
			SetDParam(1, this->town->MaxTownNoise());
			DrawString(2, y += 10, STR_NOISE_IN_TOWN, TC_FROMSTRING);
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case TVW_CENTERVIEW: // scroll to location
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(this->town->xy);
				} else {
					ScrollMainWindowToTile(this->town->xy);
				}
				break;

			case TVW_SHOWAUTORITY: // town authority
				ShowTownAuthorityWindow(this->window_number);
				break;

			case TVW_CHANGENAME: // rename
				SetDParam(0, this->window_number);
				ShowQueryString(STR_TOWN, STR_2007_RENAME_TOWN, MAX_LENGTH_TOWN_NAME_BYTES, MAX_LENGTH_TOWN_NAME_PIXELS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT);
				break;

			case TVW_EXPAND: // expand town - only available on Scenario editor
				ExpandTown(this->town);
				break;

			case TVW_DELETE: // delete town - only available on Scenario editor
				delete this->town;
				break;
		}
	}

	void ResizeWindowAsNeeded()
	{
		int aimed_height = TVW_HEIGHT_NORMAL;

		switch (_settings_game.game_creation.landscape) {
			case LT_ARCTIC:
				if (TilePixelHeight(this->town->xy) >= LowestSnowLine()) aimed_height += 20;
				break;

			case LT_TROPIC:
				if (GetTropicZone(this->town->xy) == TROPICZONE_DESERT) aimed_height += 30;
				break;

			default: break;
		}

		if (_settings_game.economy.station_noise_level) aimed_height += 10;

		if (this->height != aimed_height) ResizeWindowForWidget(this, TVW_INFOPANEL, 0, aimed_height - this->height);
	}

	virtual void OnInvalidateData(int data = 0)
	{
		/* Called when setting station noise have changed, in order to resize the window */
		this->SetDirty(); // refresh display for current size. This will allow to avoid glitches when downgrading
		this->ResizeWindowAsNeeded();
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_TOWN | CMD_MSG(STR_2008_CAN_T_RENAME_TOWN), NULL, str);
	}
};


static const Widget _town_view_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_BROWN,     0,    10,     0,    13, STR_00C5,                 STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_BROWN,    11,   172,     0,    13, STR_2005,                 STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_BROWN,   248,   259,     0,    13, 0x0,                      STR_STICKY_BUTTON},
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_BROWN,     0,   259,    14,   105, 0x0,                      STR_NULL},
{      WWT_INSET,   RESIZE_NONE,  COLOUR_BROWN,     2,   257,    16,   103, 0x0,                      STR_NULL},
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_BROWN,     0,   259,   106,   137, 0x0,                      STR_NULL},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,     0,    85,   138,   149, STR_00E4_LOCATION,        STR_200B_CENTER_THE_MAIN_VIEW_ON},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,    86,   171,   138,   149, STR_2020_LOCAL_AUTHORITY, STR_2021_SHOW_INFORMATION_ON_LOCAL},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,   172,   247,     0,    13, STR_0130_RENAME,          STR_200C_CHANGE_TOWN_NAME},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,    86,   171,   138,   149, STR_023C_EXPAND,          STR_023B_INCREASE_SIZE_OF_TOWN},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,   172,   259,   138,   149, STR_0290_DELETE,          STR_0291_DELETE_THIS_TOWN_COMPLETELY},
{   WIDGETS_END},
};

static const WindowDesc _town_view_desc(
	WDP_AUTO, WDP_AUTO, 260, TownViewWindow::TVW_HEIGHT_NORMAL, 260, TownViewWindow::TVW_HEIGHT_NORMAL,
	WC_TOWN_VIEW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON,
	_town_view_widgets
);

void ShowTownViewWindow(TownID town)
{
	AllocateWindowDescFront<TownViewWindow>(&_town_view_desc, town);
}

static const Widget _town_directory_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_BROWN,     0,    10,     0,    13, STR_00C5,               STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_BROWN,    11,   195,     0,    13, STR_2000_TOWNS,         STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_BROWN,   196,   207,     0,    13, 0x0,                    STR_STICKY_BUTTON},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,     0,    98,    14,    25, STR_SORT_BY_NAME,       STR_SORT_ORDER_TIP},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_BROWN,    99,   195,    14,    25, STR_SORT_BY_POPULATION, STR_SORT_ORDER_TIP},
{      WWT_PANEL, RESIZE_BOTTOM,  COLOUR_BROWN,     0,   195,    26,   189, 0x0,                    STR_200A_TOWN_NAMES_CLICK_ON_NAME},
{  WWT_SCROLLBAR, RESIZE_BOTTOM,  COLOUR_BROWN,   196,   207,    14,   189, 0x0,                    STR_0190_SCROLL_BAR_SCROLLS_LIST},
{      WWT_PANEL,     RESIZE_TB,  COLOUR_BROWN,     0,   195,   190,   201, 0x0,                    STR_NULL},
{  WWT_RESIZEBOX,     RESIZE_TB,  COLOUR_BROWN,   196,   207,   190,   201, 0x0,                    STR_RESIZE_BUTTON},
{   WIDGETS_END},
};


struct TownDirectoryWindow : public Window {
private:
	enum TownDirectoryWidget {
		TDW_SORTNAME = 3,
		TDW_SORTPOPULATION,
		TDW_CENTERTOWN,
	};

	/* Runtime saved values */
	static Listing last_sorting;
	static const Town *last_town;

	/* Constants for sorting towns */
	static GUITownList::SortFunction * const sorter_funcs[];

	GUITownList towns;

	void BuildTownList()
	{
		if (!this->towns.NeedRebuild()) return;

		this->towns.Clear();

		const Town *t;
		FOR_ALL_TOWNS(t) {
			*this->towns.Append() = t;
		}

		this->towns.Compact();
		this->towns.RebuildDone();
	}

	void SortTownList()
	{
		last_town = NULL;
		this->towns.Sort();
	}

	/** Sort by town name */
	static int CDECL TownNameSorter(const Town * const *a, const Town * const *b)
	{
		static char buf_cache[64];
		const Town *ta = *a;
		const Town *tb = *b;
		char buf[64];

		SetDParam(0, ta->index);
		GetString(buf, STR_TOWN, lastof(buf));

		/* If 'b' is the same town as in the last round, use the cached value
		 * We do this to speed stuff up ('b' is called with the same value a lot of
		 * times after eachother) */
		if (tb != last_town) {
			last_town = tb;
			SetDParam(0, tb->index);
			GetString(buf_cache, STR_TOWN, lastof(buf_cache));
		}

		return strcmp(buf, buf_cache);
	}

	/** Sort by population */
	static int CDECL TownPopulationSorter(const Town * const *a, const Town * const *b)
	{
		return (*a)->population - (*b)->population;
	}

public:
	TownDirectoryWindow(const WindowDesc *desc) : Window(desc, 0)
	{
		this->vscroll.cap = 16;
		this->resize.step_height = 10;
		this->resize.height = this->height - 10 * 6; // minimum of 10 items in the list, each item 10 high

		this->towns.SetListing(this->last_sorting);
		this->towns.SetSortFuncs(this->sorter_funcs);
		this->towns.ForceRebuild();

		this->FindWindowPlacementAndResize(desc);
	}

	~TownDirectoryWindow()
	{
		this->last_sorting = this->towns.GetListing();
	}

	virtual void OnPaint()
	{
		this->BuildTownList();
		this->SortTownList();

		SetVScrollCount(this, this->towns.Length());

		this->DrawWidgets();
		this->DrawSortButtonState(this->towns.SortType() == 0 ? TDW_SORTNAME : TDW_SORTPOPULATION, this->towns.IsDescSortOrder() ? SBS_DOWN : SBS_UP);

		{
			int n = 0;
			uint16 i = this->vscroll.pos;
			int y = 28;

			while (i < this->towns.Length()) {
				const Town *t = this->towns[i];

				assert(t->xy != INVALID_TILE);

				SetDParam(0, t->index);
				SetDParam(1, t->population);
				DrawString(2, y, STR_2057, TC_FROMSTRING);

				y += 10;
				i++;
				if (++n == this->vscroll.cap) break; // max number of towns in 1 window
			}

			SetDParam(0, GetWorldPopulation());
			DrawString(3, this->height - 12 + 2, STR_TOWN_POPULATION, TC_FROMSTRING);
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case TDW_SORTNAME: // Sort by Name ascending/descending
				if (this->towns.SortType() == 0) {
					this->towns.ToggleSortOrder();
				} else {
					this->towns.SetSortType(0);
				}
				this->SetDirty();
				break;

			case TDW_SORTPOPULATION: // Sort by Population ascending/descending
				if (this->towns.SortType() == 1) {
					this->towns.ToggleSortOrder();
				} else {
					this->towns.SetSortType(1);
				}
				this->SetDirty();
				break;

			case TDW_CENTERTOWN: { // Click on Town Matrix
				uint16 id_v = (pt.y - 28) / 10;

				if (id_v >= this->vscroll.cap) return; // click out of bounds

				id_v += this->vscroll.pos;

				if (id_v >= this->towns.Length()) return; // click out of town bounds

				const Town *t = this->towns[id_v];
				assert(t->xy != INVALID_TILE);
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(t->xy);
				} else {
					ScrollMainWindowToTile(t->xy);
				}
				break;
			}
		}
	}

	virtual void OnHundredthTick()
	{
		this->SetDirty();
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		this->vscroll.cap += delta.y / 10;
	}

	virtual void OnInvalidateData(int data)
	{
		if (data == 0) {
			this->towns.ForceRebuild();
		} else {
			this->towns.ForceResort();
		}
	}
};

Listing TownDirectoryWindow::last_sorting = {false, 0};
const Town *TownDirectoryWindow::last_town = NULL;

/* Available town directory sorting functions */
GUITownList::SortFunction * const TownDirectoryWindow::sorter_funcs[] = {
	&TownNameSorter,
	&TownPopulationSorter,
};

static const WindowDesc _town_directory_desc(
	WDP_AUTO, WDP_AUTO, 208, 202, 208, 202,
	WC_TOWN_DIRECTORY, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_town_directory_widgets
);

void ShowTownDirectory()
{
	if (BringWindowToFrontById(WC_TOWN_DIRECTORY, 0)) return;
	new TownDirectoryWindow(&_town_directory_desc);
}

void CcBuildTown(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_1F_SPLAT, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	}
}

static const Widget _found_town_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,    10,     0,    13, STR_00C5,                 STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,   11,   147,     0,    13, STR_0233_TOWN_GENERATION, STR_018C_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,  148,   159,     0,    13, 0x0,                      STR_STICKY_BUTTON},
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,   159,    14,   161, 0x0,                      STR_NULL},

{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,   157,    16,    27, STR_0234_NEW_TOWN,        STR_0235_CONSTRUCT_NEW_TOWN},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,   157,    29,    40, STR_023D_RANDOM_TOWN,     STR_023E_BUILD_TOWN_IN_RANDOM_LOCATION},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,   157,    42,    53, STR_MANY_RANDOM_TOWNS,    STR_RANDOM_TOWNS_TIP},

{      WWT_LABEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,   147,    54,    67, STR_02A5_TOWN_SIZE,          STR_NULL},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,    79,    68,    79, STR_02A1_SMALL,              STR_02A4_SELECT_TOWN_SIZE},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,         80,   157,    68,    79, STR_02A2_MEDIUM,             STR_02A4_SELECT_TOWN_SIZE},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,    79,    81,    92, STR_02A3_LARGE,              STR_02A4_SELECT_TOWN_SIZE},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,         80,   157,    81,    92, STR_SELECT_TOWN_SIZE_RANDOM, STR_02A4_SELECT_TOWN_SIZE},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,   157,    96,   107, STR_FOUND_TOWN_CITY,         STR_FOUND_TOWN_CITY_TOOLTIP},

{      WWT_LABEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,   147,   108,   121, STR_TOWN_ROAD_LAYOUT,           STR_NULL},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,    79,   122,   133, STR_SELECT_LAYOUT_ORIGINAL,     STR_SELECT_TOWN_ROAD_LAYOUT},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,         80,   157,   122,   133, STR_SELECT_LAYOUT_BETTER_ROADS, STR_SELECT_TOWN_ROAD_LAYOUT},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,    79,   135,   146, STR_SELECT_LAYOUT_2X2_GRID,     STR_SELECT_TOWN_ROAD_LAYOUT},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,         80,   157,   135,   146, STR_SELECT_LAYOUT_3X3_GRID,     STR_SELECT_TOWN_ROAD_LAYOUT},
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,          2,   157,   148,   159, STR_SELECT_LAYOUT_RANDOM,       STR_SELECT_TOWN_ROAD_LAYOUT},

{   WIDGETS_END},
};

struct FoundTownWindow : Window
{
private:
	enum TownScenarioEditorWidget {
		TSEW_NEWTOWN = 4,
		TSEW_RANDOMTOWN,
		TSEW_MANYRANDOMTOWNS,
		TSEW_TOWNSIZE,
		TSEW_SIZE_SMALL,
		TSEW_SIZE_MEDIUM,
		TSEW_SIZE_LARGE,
		TSEW_SIZE_RANDOM,
		TSEW_CITY,
		TSEW_TOWNLAYOUT,
		TSEW_LAYOUT_ORIGINAL,
		TSEW_LAYOUT_BETTER,
		TSEW_LAYOUT_GRID2,
		TSEW_LAYOUT_GRID3,
		TSEW_LAYOUT_RANDOM,
	};

	static TownSize town_size;
	static bool city;
	static TownLayout town_layout;

public:
	FoundTownWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		this->FindWindowPlacementAndResize(desc);
		town_layout = _settings_game.economy.town_layout;
		city = false;
		this->UpdateButtons();
	}

	void UpdateButtons()
	{
		for (int i = TSEW_SIZE_SMALL; i <= TSEW_SIZE_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == TSEW_SIZE_SMALL + town_size);
		}

		this->SetWidgetLoweredState(TSEW_CITY, city);

		for (int i = TSEW_LAYOUT_ORIGINAL; i <= TSEW_LAYOUT_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == TSEW_LAYOUT_ORIGINAL + town_layout);
		}

		this->SetDirty();
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case TSEW_NEWTOWN:
				HandlePlacePushButton(this, TSEW_NEWTOWN, SPR_CURSOR_TOWN, VHM_RECT, PlaceProc_Town);
				break;

			case TSEW_RANDOMTOWN: {
				this->HandleButtonClick(TSEW_RANDOMTOWN);
				_generating_world = true;
				UpdateNearestTownForRoadTiles(true);
				const Town *t = CreateRandomTown(20, town_size, city, town_layout);
				UpdateNearestTownForRoadTiles(false);
				_generating_world = false;

				if (t == NULL) {
					ShowErrorMessage(STR_NO_SPACE_FOR_TOWN, STR_CANNOT_GENERATE_TOWN, 0, 0);
				} else {
					ScrollMainWindowToTile(t->xy);
				}
			} break;

			case TSEW_MANYRANDOMTOWNS:
				this->HandleButtonClick(TSEW_MANYRANDOMTOWNS);

				_generating_world = true;
				UpdateNearestTownForRoadTiles(true);
				if (!GenerateTowns(town_layout)) {
					ShowErrorMessage(STR_NO_SPACE_FOR_TOWN, STR_CANNOT_GENERATE_TOWN, 0, 0);
				}
				UpdateNearestTownForRoadTiles(false);
				_generating_world = false;
				break;

			case TSEW_SIZE_SMALL: case TSEW_SIZE_MEDIUM: case TSEW_SIZE_LARGE: case TSEW_SIZE_RANDOM:
				town_size = (TownSize)(widget - TSEW_SIZE_SMALL);
				this->UpdateButtons();
				break;

			case TSEW_CITY:
				city ^= true;
				this->SetWidgetLoweredState(TSEW_CITY, city);
				this->SetDirty();
				break;

			case TSEW_LAYOUT_ORIGINAL: case TSEW_LAYOUT_BETTER: case TSEW_LAYOUT_GRID2:
			case TSEW_LAYOUT_GRID3: case TSEW_LAYOUT_RANDOM:
				town_layout = (TownLayout)(widget - TSEW_LAYOUT_ORIGINAL);
				this->UpdateButtons();
				break;
		}
	}

	virtual void OnTimeout()
	{
		this->RaiseWidget(TSEW_RANDOMTOWN);
		this->RaiseWidget(TSEW_MANYRANDOMTOWNS);
		this->SetDirty();
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		_place_proc(tile);
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->UpdateButtons();
	}

	static void PlaceProc_Town(TileIndex tile)
	{
		uint32 townnameparts;
		if (!GenerateTownName(&townnameparts)) {
			ShowErrorMessage(STR_023A_TOO_MANY_TOWNS, STR_0236_CAN_T_BUILD_TOWN_HERE, 0, 0);
			return;
		}

		DoCommandP(tile, town_size | city << 2 | town_layout << 3, townnameparts, CMD_BUILD_TOWN | CMD_MSG(STR_0236_CAN_T_BUILD_TOWN_HERE), CcBuildTown);
	}
};

TownSize FoundTownWindow::town_size = TS_MEDIUM; // select medium-sized towns per default;
bool FoundTownWindow::city;
TownLayout FoundTownWindow::town_layout;

static const WindowDesc _found_town_desc(
	WDP_AUTO, WDP_AUTO, 160, 162, 160, 162,
	WC_FOUND_TOWN, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_CONSTRUCTION,
	_found_town_widgets
);

void ShowBuildTownWindow()
{
	if (_game_mode != GM_EDITOR && !IsValidCompanyID(_local_company)) return;
	AllocateWindowDescFront<FoundTownWindow>(&_found_town_desc, 0);
}
