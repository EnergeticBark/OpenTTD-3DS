/* $Id$ */

/** @file bridge_gui.cpp Graphical user interface for bridge construction */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "command_func.h"
#include "economy_func.h"
#include "bridge.h"
#include "strings_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "map_func.h"
#include "gfx_func.h"
#include "tunnelbridge.h"
#include "sortlist_type.h"
#include "widgets/dropdown_func.h"

#include "table/strings.h"

/** The type of the last built rail bridge */
static BridgeType _last_railbridge_type = 0;
/** The type of the last built road bridge */
static BridgeType _last_roadbridge_type = 0;

/**
 * Carriage for the data we need if we want to build a bridge
 */
struct BuildBridgeData {
	BridgeType index;
	const BridgeSpec *spec;
	Money cost;
};

typedef GUIList<BuildBridgeData> GUIBridgeList;

/**
 * Callback executed after a build Bridge CMD has been called
 *
 * @param scucess True if the build succeded
 * @param tile The tile where the command has been executed
 * @param p1 not used
 * @param p2 not used
 */
void CcBuildBridge(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) SndPlayTileFx(SND_27_BLACKSMITH_ANVIL, tile);
}

/* Names of the build bridge selection window */
enum BuildBridgeSelectionWidgets {
	BBSW_CLOSEBOX = 0,
	BBSW_CAPTION,
	BBSW_DROPDOWN_ORDER,
	BBSW_DROPDOWN_CRITERIA,
	BBSW_BRIDGE_LIST,
	BBSW_SCROLLBAR,
	BBSW_RESIZEBOX
};

class BuildBridgeWindow : public Window {
private:
	/* Runtime saved values */
	static uint16 last_size;
	static Listing last_sorting;

	/* Constants for sorting the bridges */
	static const StringID sorter_names[];
	static GUIBridgeList::SortFunction * const sorter_funcs[];

	/* Internal variables */
	TileIndex start_tile;
	TileIndex end_tile;
	uint32 type;
	GUIBridgeList *bridges;

	/** Sort the bridges by their index */
	static int CDECL BridgeIndexSorter(const BuildBridgeData *a, const BuildBridgeData *b)
	{
		return a->index - b->index;
	}

	/** Sort the bridges by their price */
	static int CDECL BridgePriceSorter(const BuildBridgeData *a, const BuildBridgeData *b)
	{
		return a->cost - b->cost;
	}

	/** Sort the bridges by their maximum speed */
	static int CDECL BridgeSpeedSorter(const BuildBridgeData *a, const BuildBridgeData *b)
	{
		return a->spec->speed - b->spec->speed;
	}

	void BuildBridge(uint8 i)
	{
		switch ((TransportType)(this->type >> 15)) {
			case TRANSPORT_RAIL: _last_railbridge_type = this->bridges->Get(i)->index; break;
			case TRANSPORT_ROAD: _last_roadbridge_type = this->bridges->Get(i)->index; break;
			default: break;
		}
		DoCommandP(this->end_tile, this->start_tile, this->type | this->bridges->Get(i)->index,
					CMD_BUILD_BRIDGE | CMD_MSG(STR_5015_CAN_T_BUILD_BRIDGE_HERE), CcBuildBridge);
	}

	/** Sort the builable bridges */
	void SortBridgeList()
	{
		this->bridges->Sort();

		/* Display the current sort variant */
		this->widget[BBSW_DROPDOWN_CRITERIA].data = this->sorter_names[this->bridges->SortType()];

		/* Set the modified widgets dirty */
		this->InvalidateWidget(BBSW_DROPDOWN_CRITERIA);
		this->InvalidateWidget(BBSW_BRIDGE_LIST);
	}

public:
	BuildBridgeWindow(const WindowDesc *desc, TileIndex start, TileIndex end, uint32 br_type, GUIBridgeList *bl) : Window(desc),
		start_tile(start),
		end_tile(end),
		type(br_type),
		bridges(bl)
	{
		this->parent = FindWindowById(WC_BUILD_TOOLBAR, GB(this->type, 15, 2));
		this->bridges->SetListing(this->last_sorting);
		this->bridges->SetSortFuncs(this->sorter_funcs);
		this->bridges->NeedResort();
		this->SortBridgeList();

		/* Change the data, or the caption of the gui. Set it to road or rail, accordingly */
		this->widget[BBSW_CAPTION].data = (GB(this->type, 15, 2) == TRANSPORT_ROAD) ? STR_1803_SELECT_ROAD_BRIDGE : STR_100D_SELECT_RAIL_BRIDGE;

		this->resize.step_height = 22;
		this->vscroll.count = bl->Length();

		if (this->last_size <= 4) {
			this->vscroll.cap = 4;
		} else {
			/* Resize the bridge selection window if we used a bigger one the last time */
			this->vscroll.cap = min(this->last_size, this->vscroll.count);
			ResizeWindow(this, 0, (this->vscroll.cap - 4) * this->resize.step_height);
		}

		this->FindWindowPlacementAndResize(desc);
	}

	~BuildBridgeWindow()
	{
		this->last_sorting = this->bridges->GetListing();

		delete bridges;
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		this->DrawSortButtonState(BBSW_DROPDOWN_ORDER, this->bridges->IsDescSortOrder() ? SBS_DOWN : SBS_UP);

		uint y = this->widget[BBSW_BRIDGE_LIST].top + 2;

		for (int i = this->vscroll.pos; (i < (this->vscroll.cap + this->vscroll.pos)) && (i < (int)this->bridges->Length()); i++) {
			const BridgeSpec *b = this->bridges->Get(i)->spec;

			SetDParam(2, this->bridges->Get(i)->cost);
			SetDParam(1, b->speed);
			SetDParam(0, b->material);

			DrawSprite(b->sprite, b->pal, 3, y);
			DrawString(44, y, STR_500D, TC_FROMSTRING);
			y += this->resize.step_height;

		}
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		const uint8 i = keycode - '1';
		if (i < 9 && i < this->bridges->Length()) {
			/* Build the requested bridge */
			this->BuildBridge(i);
			delete this;
			return ES_HANDLED;
		}
		return ES_NOT_HANDLED;
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			default: break;
			case BBSW_BRIDGE_LIST: {
				uint i = ((int)pt.y - this->widget[BBSW_BRIDGE_LIST].top) / this->resize.step_height;
				if (i < this->vscroll.cap) {
					i += this->vscroll.pos;
					if (i < this->bridges->Length()) {
						this->BuildBridge(i);
						delete this;
					}
				}
			} break;

			case BBSW_DROPDOWN_ORDER:
				this->bridges->ToggleSortOrder();
				this->SetDirty();
				break;

			case BBSW_DROPDOWN_CRITERIA:
				ShowDropDownMenu(this, this->sorter_names, this->bridges->SortType(), BBSW_DROPDOWN_CRITERIA, 0, 0);
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (widget == BBSW_DROPDOWN_CRITERIA && this->bridges->SortType() != index) {
			this->bridges->SetSortType(index);

			this->SortBridgeList();
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		this->vscroll.cap += delta.y / (int)this->resize.step_height;
		this->widget[BBSW_BRIDGE_LIST].data = (this->vscroll.cap << 8) + 1;
		SetVScrollCount(this, this->bridges->Length());

		this->last_size = max(this->vscroll.cap, this->last_size);
	}
};

/* Set the default size of the Build Bridge Window */
uint16 BuildBridgeWindow::last_size = 4;
/* Set the default sorting for the bridges */
Listing BuildBridgeWindow::last_sorting = {false, 0};

/* Availible bridge sorting functions */
GUIBridgeList::SortFunction * const BuildBridgeWindow::sorter_funcs[] = {
	&BridgeIndexSorter,
	&BridgePriceSorter,
	&BridgeSpeedSorter
};

/* Names of the sorting functions */
const StringID BuildBridgeWindow::sorter_names[] = {
	STR_SORT_BY_NUMBER,
	STR_ENGINE_SORT_COST,
	STR_SORT_BY_MAX_SPEED,
	INVALID_STRING_ID
};

/* Widget definition for the rail bridge selection window */
static const Widget _build_bridge_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,   0,  10,   0,  13, STR_00C5,                    STR_018B_CLOSE_WINDOW},            // BBSW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,  11, 199,   0,  13, STR_100D_SELECT_RAIL_BRIDGE, STR_018C_WINDOW_TITLE_DRAG_THIS},  // BBSW_CAPTION

{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   0,  80,  14,  25, STR_SORT_BY,                 STR_SORT_ORDER_TIP},               // BBSW_DROPDOWN_ORDER
{   WWT_DROPDOWN,   RESIZE_NONE,  COLOUR_DARK_GREEN,  81, 199,  14,  25, 0x0,                         STR_SORT_CRITERIA_TIP},            // BBSW_DROPDOWN_CRITERIA

{     WWT_MATRIX, RESIZE_BOTTOM,  COLOUR_DARK_GREEN,   0, 187,  26, 113, 0x401,                       STR_101F_BRIDGE_SELECTION_CLICK},  // BBSW_BRIDGE_LIST
{  WWT_SCROLLBAR, RESIZE_BOTTOM,  COLOUR_DARK_GREEN, 188, 199,  26, 101, 0x0,                         STR_0190_SCROLL_BAR_SCROLLS_LIST}, // BBSW_SCROLLBAR
{  WWT_RESIZEBOX,     RESIZE_TB,  COLOUR_DARK_GREEN, 188, 199, 102, 113, 0x0,                         STR_RESIZE_BUTTON},                // BBSW_RESIZEBOX
{   WIDGETS_END},
};

/* Window definition for the rail bridge selection window */
static const WindowDesc _build_bridge_desc(
	WDP_AUTO, WDP_AUTO, 200, 114, 200, 114,
	WC_BUILD_BRIDGE, WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_RESIZABLE | WDF_CONSTRUCTION,
	_build_bridge_widgets
);

/**
 * Prepare the data for the build a bridge window.
 *  If we can't build a bridge under the given conditions
 *  show an error message.
 *
 * @parma start The start tile of the bridge
 * @param end The end tile of the bridge
 * @param transport_type The transport type
 * @param road_rail_type The road/rail type
 */
void ShowBuildBridgeWindow(TileIndex start, TileIndex end, TransportType transport_type, byte road_rail_type)
{
	DeleteWindowById(WC_BUILD_BRIDGE, 0);

	/* Data type for the bridge.
	 * Bit 16,15 = transport type,
	 *     14..8 = road/rail types,
	 *      7..0 = type of bridge */
	uint32 type = (transport_type << 15) | (road_rail_type << 8);

	/* The bridge length without ramps. */
	const uint bridge_len = GetTunnelBridgeLength(start, end);

	/* If Ctrl is being pressed, check wether the last bridge built is available
	 * If so, return this bridge type. Otherwise continue normally.
	 * We store bridge types for each transport type, so we have to check for
	 * the transport type beforehand.
	 */
	BridgeType last_bridge_type = 0;
	switch (transport_type) {
		case TRANSPORT_ROAD: last_bridge_type = _last_roadbridge_type; break;
		case TRANSPORT_RAIL: last_bridge_type = _last_railbridge_type; break;
		default: break; // water ways and air routes don't have bridge types
	}
	if (_ctrl_pressed && CheckBridge_Stuff(last_bridge_type, bridge_len)) {
		DoCommandP(end, start, type | last_bridge_type, CMD_BUILD_BRIDGE | CMD_MSG(STR_5015_CAN_T_BUILD_BRIDGE_HERE), CcBuildBridge);
		return;
	}

	/* only query bridge building possibility once, result is the same for all bridges!
	 * returns CMD_ERROR on failure, and price on success */
	StringID errmsg = INVALID_STRING_ID;
	CommandCost ret = DoCommand(end, start, type, DC_AUTO | DC_QUERY_COST, CMD_BUILD_BRIDGE);

	GUIBridgeList *bl = NULL;
	if (CmdFailed(ret)) {
		errmsg = _error_message;
	} else {
		/* check which bridges can be built */
		const uint tot_bridgedata_len = CalcBridgeLenCostFactor(bridge_len + 2);

		bl = new GUIBridgeList();

		/* loop for all bridgetypes */
		for (BridgeType brd_type = 0; brd_type != MAX_BRIDGES; brd_type++) {
			if (CheckBridge_Stuff(brd_type, bridge_len)) {
				/* bridge is accepted, add to list */
				BuildBridgeData *item = bl->Append();
				item->index = brd_type;
				item->spec = GetBridgeSpec(brd_type);
				/* Add to terraforming & bulldozing costs the cost of the
				 * bridge itself (not computed with DC_QUERY_COST) */
				item->cost = ret.GetCost() + (((int64)tot_bridgedata_len * _price.build_bridge * item->spec->price) >> 8);
			}
		}
	}

	if (bl != NULL && bl->Length() != 0) {
		new BuildBridgeWindow(&_build_bridge_desc, start, end, type, bl);
	} else {
		if (bl != NULL) delete bl;
		ShowErrorMessage(errmsg, STR_5015_CAN_T_BUILD_BRIDGE_HERE, TileX(end) * TILE_SIZE, TileY(end) * TILE_SIZE);
	}
}
