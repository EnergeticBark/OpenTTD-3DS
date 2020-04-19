/* $Id$ */

/** @file vehicle_gui.cpp The base GUI for all vehicles. */

#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "company_func.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "vehicle_gui_base.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "station_map.h"
#include "roadveh.h"
#include "depot_base.h"
#include "group_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "autoreplace_gui.h"
#include "string_func.h"
#include "widgets/dropdown_func.h"
#include "timetable.h"
#include "vehiclelist.h"
#include "settings_type.h"
#include "articulated_vehicles.h"

#include "table/sprites.h"
#include "table/strings.h"

Sorting _sorting;

static GUIVehicleList::SortFunction VehicleNumberSorter;
static GUIVehicleList::SortFunction VehicleNameSorter;
static GUIVehicleList::SortFunction VehicleAgeSorter;
static GUIVehicleList::SortFunction VehicleProfitThisYearSorter;
static GUIVehicleList::SortFunction VehicleProfitLastYearSorter;
static GUIVehicleList::SortFunction VehicleCargoSorter;
static GUIVehicleList::SortFunction VehicleReliabilitySorter;
static GUIVehicleList::SortFunction VehicleMaxSpeedSorter;
static GUIVehicleList::SortFunction VehicleModelSorter;
static GUIVehicleList::SortFunction VehicleValueSorter;
static GUIVehicleList::SortFunction VehicleLengthSorter;
static GUIVehicleList::SortFunction VehicleTimeToLiveSorter;

GUIVehicleList::SortFunction * const BaseVehicleListWindow::vehicle_sorter_funcs[] = {
	&VehicleNumberSorter,
	&VehicleNameSorter,
	&VehicleAgeSorter,
	&VehicleProfitThisYearSorter,
	&VehicleProfitLastYearSorter,
	&VehicleCargoSorter,
	&VehicleReliabilitySorter,
	&VehicleMaxSpeedSorter,
	&VehicleModelSorter,
	&VehicleValueSorter,
	&VehicleLengthSorter,
	&VehicleTimeToLiveSorter,
};

const StringID BaseVehicleListWindow::vehicle_sorter_names[] = {
	STR_SORT_BY_NUMBER,
	STR_SORT_BY_DROPDOWN_NAME,
	STR_SORT_BY_AGE,
	STR_SORT_BY_PROFIT_THIS_YEAR,
	STR_SORT_BY_PROFIT_LAST_YEAR,
	STR_SORT_BY_TOTAL_CAPACITY_PER_CARGOTYPE,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_MODEL,
	STR_SORT_BY_VALUE,
	STR_SORT_BY_LENGTH,
	STR_SORT_BY_LIFE_TIME,
	INVALID_STRING_ID
};

void BaseVehicleListWindow::BuildVehicleList(Owner owner, uint16 index, uint16 window_type)
{
	if (!this->vehicles.NeedRebuild()) return;

	DEBUG(misc, 3, "Building vehicle list for company %d at station %d", owner, index);

	GenerateVehicleSortList(&this->vehicles, this->vehicle_type, owner, index, window_type);

	this->vehicles.RebuildDone();
}

/* cached values for VehicleNameSorter to spare many GetString() calls */
static const Vehicle *_last_vehicle[2] = { NULL, NULL };

void BaseVehicleListWindow::SortVehicleList()
{
	if (this->vehicles.Sort()) return;

	/* invalidate cached values for name sorter - vehicle names could change */
	_last_vehicle[0] = _last_vehicle[1] = NULL;
}

void DepotSortList(VehicleList *list)
{
	if (list->Length() < 2) return;
	QSortT(list->Begin(), list->Length(), &VehicleNumberSorter);
}

/** draw the vehicle profit button in the vehicle list window. */
void DrawVehicleProfitButton(const Vehicle *v, int x, int y)
{
	SpriteID pal;

	/* draw profit-based coloured icons */
	if (v->age <= DAYS_IN_YEAR * 2) {
		pal = PALETTE_TO_GREY;
	} else if (v->GetDisplayProfitLastYear() < 0) {
		pal = PALETTE_TO_RED;
	} else if (v->GetDisplayProfitLastYear() < 10000) {
		pal = PALETTE_TO_YELLOW;
	} else {
		pal = PALETTE_TO_GREEN;
	}
	DrawSprite(SPR_BLOT, pal, x, y);
}

struct RefitOption {
	CargoID cargo;
	byte subtype;
	uint16 value;
	EngineID engine;
};

struct RefitList {
	uint num_lines;
	RefitOption *items;
};

static RefitList *BuildRefitList(const Vehicle *v)
{
	uint max_lines = 256;
	RefitOption *refit = CallocT<RefitOption>(max_lines);
	RefitList *list = CallocT<RefitList>(1);
	Vehicle *u = (Vehicle*)v;
	uint num_lines = 0;
	uint i;

	do {
		uint32 cmask = EngInfo(u->engine_type)->refit_mask;
		byte callbackmask = EngInfo(u->engine_type)->callbackmask;

		/* Skip this engine if it has no capacity */
		if (u->cargo_cap == 0) continue;

		/* Loop through all cargos in the refit mask */
		for (CargoID cid = 0; cid < NUM_CARGO && num_lines < max_lines; cid++) {
			/* Skip cargo type if it's not listed */
			if (!HasBit(cmask, cid)) continue;

			/* Check the vehicle's callback mask for cargo suffixes */
			if (HasBit(callbackmask, CBM_VEHICLE_CARGO_SUFFIX)) {
				/* Make a note of the original cargo type. It has to be
				 * changed to test the cargo & subtype... */
				CargoID temp_cargo = u->cargo_type;
				byte temp_subtype  = u->cargo_subtype;
				byte refit_cyc;

				u->cargo_type = cid;

				for (refit_cyc = 0; refit_cyc < 16 && num_lines < max_lines; refit_cyc++) {
					bool duplicate = false;
					uint16 callback;

					u->cargo_subtype = refit_cyc;
					callback = GetVehicleCallback(CBID_VEHICLE_CARGO_SUFFIX, 0, 0, u->engine_type, u);

					if (callback == 0xFF) callback = CALLBACK_FAILED;
					if (refit_cyc != 0 && callback == CALLBACK_FAILED) break;

					/* Check if this cargo and subtype combination are listed */
					for (i = 0; i < num_lines && !duplicate; i++) {
						if (refit[i].cargo == cid && refit[i].value == callback) duplicate = true;
					}

					if (duplicate) continue;

					refit[num_lines].cargo   = cid;
					refit[num_lines].subtype = refit_cyc;
					refit[num_lines].value   = callback;
					refit[num_lines].engine  = u->engine_type;
					num_lines++;
				}

				/* Reset the vehicle's cargo type */
				u->cargo_type    = temp_cargo;
				u->cargo_subtype = temp_subtype;
			} else {
				/* No cargo suffix callback -- use no subtype */
				bool duplicate = false;

				for (i = 0; i < num_lines && !duplicate; i++) {
					if (refit[i].cargo == cid && refit[i].value == CALLBACK_FAILED) duplicate = true;
				}

				if (!duplicate) {
					refit[num_lines].cargo   = cid;
					refit[num_lines].subtype = 0;
					refit[num_lines].value   = CALLBACK_FAILED;
					refit[num_lines].engine  = INVALID_ENGINE;
					num_lines++;
				}
			}
		}
	} while ((v->type == VEH_TRAIN || v->type == VEH_ROAD) && (u = u->Next()) != NULL && num_lines < max_lines);

	list->num_lines = num_lines;
	list->items = refit;

	return list;
}

/** Draw the list of available refit options for a consist.
 * Draw the list and highlight the selected refit option (if any)
 * @param *list first vehicle in consist to get the refit-options of
 * @param sel selected refit cargo-type in the window
 * @param pos position of the selected item in caller widow
 * @param rows number of rows(capacity) in caller window
 * @param delta step height in caller window
 * @return the refit option that is hightlighted, NULL if none
 */
static RefitOption *DrawVehicleRefitWindow(const RefitList *list, int sel, uint pos, uint rows, uint delta)
{
	RefitOption *refit = list->items;
	RefitOption *selected = NULL;
	uint num_lines = list->num_lines;
	uint y = 31;
	uint i;

	/* Draw the list, and find the selected cargo (by its position in list) */
	for (i = 0; i < num_lines; i++) {
		TextColour colour = TC_BLACK;
		if (sel == 0) {
			selected = &refit[i];
			colour = TC_WHITE;
		}

		if (i >= pos && i < pos + rows) {
			/* Draw the cargo name */
			int last_x = DrawString(2, y, GetCargo(refit[i].cargo)->name, colour);

			/* If the callback succeeded, draw the cargo suffix */
			if (refit[i].value != CALLBACK_FAILED) {
				DrawString(last_x + 1, y, GetGRFStringID(GetEngineGRFID(refit[i].engine), 0xD000 + refit[i].value), colour);
			}
			y += delta;
		}

		sel--;
	}

	return selected;
}

struct RefitWindow : public Window {
	int sel;
	RefitOption *cargo;
	RefitList *list;
	uint length;
	VehicleOrderID order;

	RefitWindow(const WindowDesc *desc, const Vehicle *v, VehicleOrderID order) : Window(desc, v->index)
	{
		this->owner = v->owner;
		this->vscroll.cap = 8;
		this->resize.step_height = 14;

		this->order = order;
		this->sel  = -1;
		this->list = BuildRefitList(v);
		if (v->type == VEH_TRAIN) this->length = CountVehiclesInChain(v);
		SetVScrollCount(this, this->list->num_lines);

		switch (v->type) {
			case VEH_TRAIN:
				this->widget[3].tooltips = STR_RAIL_SELECT_TYPE_OF_CARGO_FOR;
				this->widget[6].data     = STR_RAIL_REFIT_VEHICLE;
				this->widget[6].tooltips = STR_RAIL_REFIT_TO_CARRY_HIGHLIGHTED;
				break;

			case VEH_ROAD:
				this->widget[3].tooltips = STR_ROAD_SELECT_TYPE_OF_CARGO_FOR;
				this->widget[6].data     = STR_REFIT_ROAD_VEHICLE;
				this->widget[6].tooltips = STR_REFIT_ROAD_VEHICLE_TO_CARRY_HIGHLIGHTED;
				break;

			case VEH_SHIP:
				this->widget[3].tooltips = STR_983D_SELECT_TYPE_OF_CARGO_FOR;
				this->widget[6].data     = STR_983C_REFIT_SHIP;
				this->widget[6].tooltips = STR_983E_REFIT_SHIP_TO_CARRY_HIGHLIGHTED;
				break;

			case VEH_AIRCRAFT:
				this->widget[3].tooltips = STR_A03E_SELECT_TYPE_OF_CARGO_FOR;
				this->widget[6].data     = STR_A03D_REFIT_AIRCRAFT;
				this->widget[6].tooltips = STR_A03F_REFIT_AIRCRAFT_TO_CARRY;
				break;

			default: NOT_REACHED();
		}

		this->FindWindowPlacementAndResize(desc);
	}

	~RefitWindow()
	{
		free(this->list->items);
		free(this->list);
	}

	virtual void OnPaint()
	{
		Vehicle *v = GetVehicle(this->window_number);

		if (v->type == VEH_TRAIN) {
			uint length = CountVehiclesInChain(v);

			if (length != this->length) {
				/* Consist length has changed, so rebuild the refit list */
				free(this->list->items);
				free(this->list);
				this->list = BuildRefitList(v);
				this->length = length;
			}
		}

		SetVScrollCount(this, this->list->num_lines);

		SetDParam(0, v->index);
		this->DrawWidgets();

		this->cargo = DrawVehicleRefitWindow(this->list, this->sel, this->vscroll.pos, this->vscroll.cap, this->resize.step_height);

		if (this->cargo != NULL) {
			CommandCost cost;

			cost = DoCommand(v->tile, v->index, this->cargo->cargo | this->cargo->subtype << 8,
							 DC_QUERY_COST, GetCmdRefitVeh(v->type));

			if (CmdSucceeded(cost)) {
				SetDParam(0, this->cargo->cargo);
				SetDParam(1, _returned_refit_capacity);
				SetDParam(2, cost.GetCost());
				DrawString(2, this->widget[5].top + 1, STR_9840_NEW_CAPACITY_COST_OF_REFIT, TC_FROMSTRING);
			}
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case 3: { // listbox
				int y = pt.y - this->widget[3].top;
				if (y >= 0) {
					this->sel = (y / (int)this->resize.step_height) + this->vscroll.pos;
					this->SetDirty();
				}
				break;
			}

			case 6: // refit button
				if (this->cargo != NULL) {
					const Vehicle *v = GetVehicle(this->window_number);

					if (this->order == INVALID_VEH_ORDER_ID) {
						int command = 0;

						switch (v->type) {
							default: NOT_REACHED();
							case VEH_TRAIN:    command = CMD_REFIT_RAIL_VEHICLE | CMD_MSG(STR_RAIL_CAN_T_REFIT_VEHICLE);  break;
							case VEH_ROAD:     command = CMD_REFIT_ROAD_VEH     | CMD_MSG(STR_REFIT_ROAD_VEHICLE_CAN_T);  break;
							case VEH_SHIP:     command = CMD_REFIT_SHIP         | CMD_MSG(STR_9841_CAN_T_REFIT_SHIP);     break;
							case VEH_AIRCRAFT: command = CMD_REFIT_AIRCRAFT     | CMD_MSG(STR_A042_CAN_T_REFIT_AIRCRAFT); break;
						}
						if (DoCommandP(v->tile, v->index, this->cargo->cargo | this->cargo->subtype << 8, command)) delete this;
					} else {
						if (DoCommandP(v->tile, v->index, this->cargo->cargo | this->cargo->subtype << 8 | this->order << 16, CMD_ORDER_REFIT)) delete this;
					}
				}
				break;
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		this->vscroll.cap += delta.y / (int)this->resize.step_height;
		this->widget[3].data = (this->vscroll.cap << 8) + 1;
	}
};


static const Widget _vehicle_refit_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                            STR_018B_CLOSE_WINDOW},
	{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   239,     0,    13, STR_983B_REFIT,                      STR_018C_WINDOW_TITLE_DRAG_THIS},
	{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,     0,   239,    14,    27, STR_983F_SELECT_CARGO_TYPE_TO_CARRY, STR_983D_SELECT_TYPE_OF_CARGO_FOR},
	{     WWT_MATRIX, RESIZE_BOTTOM,  COLOUR_GREY,     0,   227,    28,   139, 0x801,                               STR_EMPTY},
	{  WWT_SCROLLBAR, RESIZE_BOTTOM,  COLOUR_GREY,   228,   239,    28,   139, 0x0,                                 STR_0190_SCROLL_BAR_SCROLLS_LIST},
	{      WWT_PANEL,     RESIZE_TB,  COLOUR_GREY,     0,   239,   140,   161, 0x0,                                 STR_NULL},
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,     0,   227,   162,   173, 0x0,                                 STR_NULL},
	{  WWT_RESIZEBOX,     RESIZE_TB,  COLOUR_GREY,   228,   239,   162,   173, 0x0,                                 STR_RESIZE_BUTTON},
	{   WIDGETS_END},
};

static const WindowDesc _vehicle_refit_desc(
	WDP_AUTO, WDP_AUTO, 240, 174, 240, 174,
	WC_VEHICLE_REFIT, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE | WDF_CONSTRUCTION,
	_vehicle_refit_widgets
);

/** Show the refit window for a vehicle
 * @param *v The vehicle to show the refit window for
 * @param order of the vehicle ( ? )
 */
void ShowVehicleRefitWindow(const Vehicle *v, VehicleOrderID order, Window *parent)
{
	DeleteWindowById(WC_VEHICLE_REFIT, v->index);
	RefitWindow *w = new RefitWindow(&_vehicle_refit_desc, v, order);
	w->parent = parent;
}

/** Display additional text from NewGRF in the purchase information window */
uint ShowAdditionalText(int x, int y, uint w, EngineID engine)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_ADDITIONAL_TEXT, 0, 0, engine, NULL);
	if (callback == CALLBACK_FAILED) return 0;

	/* STR_02BD is used to start the string with {BLACK} */
	SetDParam(0, GetGRFStringID(GetEngineGRFID(engine), 0xD000 + callback));
	PrepareTextRefStackUsage(0);
	uint result = DrawStringMultiLine(x, y, STR_02BD, w);
	StopTextRefStackUsage();
	return result;
}

/** Display list of cargo types of the engine, for the purchase information window */
uint ShowRefitOptionsList(int x, int y, uint w, EngineID engine)
{
	/* List of cargo types of this engine */
	uint32 cmask = GetUnionOfArticulatedRefitMasks(engine, GetEngine(engine)->type, false);
	/* List of cargo types available in this climate */
	uint32 lmask = _cargo_mask;
	char string[512];
	char *b = string;

	/* Draw nothing if the engine is not refittable */
	if (CountBits(cmask) <= 1) return 0;

	b = InlineString(b, STR_PURCHASE_INFO_REFITTABLE_TO);

	if (cmask == lmask) {
		/* Engine can be refitted to all types in this climate */
		b = InlineString(b, STR_PURCHASE_INFO_ALL_TYPES);
	} else {
		/* Check if we are able to refit to more cargo types and unable to. If
		 * so, invert the cargo types to list those that we can't refit to. */
		if (CountBits(cmask ^ lmask) < CountBits(cmask)) {
			cmask ^= lmask;
			b = InlineString(b, STR_PURCHASE_INFO_ALL_BUT);
		}

		bool first = true;

		/* Add each cargo type to the list */
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (!HasBit(cmask, cid)) continue;

			if (b >= lastof(string) - (2 + 2 * 4)) break; // ", " and two calls to Utf8Encode()

			if (!first) b = strecpy(b, ", ", lastof(string));
			first = false;

			b = InlineString(b, GetCargo(cid)->name);
		}
	}

	/* Terminate and display the completed string */
	*b = '\0';

	/* Make sure we detect any buffer overflow */
	assert(b < endof(string));

	SetDParamStr(0, string);
	return DrawStringMultiLine(x, y, STR_JUST_RAW_STRING, w);
}

/** Get the cargo subtype text from NewGRF for the vehicle details window. */
StringID GetCargoSubtypeText(const Vehicle *v)
{
	if (HasBit(EngInfo(v->engine_type)->callbackmask, CBM_VEHICLE_CARGO_SUFFIX)) {
		uint16 cb = GetVehicleCallback(CBID_VEHICLE_CARGO_SUFFIX, 0, 0, v->engine_type, v);
		if (cb != CALLBACK_FAILED) {
			return GetGRFStringID(GetEngineGRFID(v->engine_type), 0xD000 + cb);
		}
	}
	return STR_EMPTY;
}

/** Sort vehicles by their number */
static int CDECL VehicleNumberSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	return (*a)->unitnumber - (*b)->unitnumber;
}

/** Sort vehicles by their name */
static int CDECL VehicleNameSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	static char last_name[2][64];

	if (*a != _last_vehicle[0]) {
		_last_vehicle[0] = *a;
		SetDParam(0, (*a)->index);
		GetString(last_name[0], STR_VEHICLE_NAME, lastof(last_name[0]));
	}

	if (*b != _last_vehicle[1]) {
		_last_vehicle[1] = *b;
		SetDParam(0, (*b)->index);
		GetString(last_name[1], STR_VEHICLE_NAME, lastof(last_name[1]));
	}

	int r = strcmp(last_name[0], last_name[1]);
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their age */
static int CDECL VehicleAgeSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = (*a)->age - (*b)->age;
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by this year profit */
static int CDECL VehicleProfitThisYearSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = ClampToI32((*a)->GetDisplayProfitThisYear() - (*b)->GetDisplayProfitThisYear());
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by last year profit */
static int CDECL VehicleProfitLastYearSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = ClampToI32((*a)->GetDisplayProfitLastYear() - (*b)->GetDisplayProfitLastYear());
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their cargo */
static int CDECL VehicleCargoSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	const Vehicle *v;
	AcceptedCargo diff;
	memset(diff, 0, sizeof(diff));

	/* Append the cargo of the connected weagons */
	for (v = *a; v != NULL; v = v->Next()) diff[v->cargo_type] += v->cargo_cap;
	for (v = *b; v != NULL; v = v->Next()) diff[v->cargo_type] -= v->cargo_cap;

	int r = 0;
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		r = diff[i];
		if (r != 0) break;
	}

	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their reliability */
static int CDECL VehicleReliabilitySorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = (*a)->reliability - (*b)->reliability;
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their max speed */
static int CDECL VehicleMaxSpeedSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = 0;
	if ((*a)->type == VEH_TRAIN && (*b)->type == VEH_TRAIN) {
		r = (*a)->u.rail.cached_max_speed - (*b)->u.rail.cached_max_speed;
	} else {
		r = (*a)->max_speed - (*b)->max_speed;
	}
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by model */
static int CDECL VehicleModelSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = (*a)->engine_type - (*b)->engine_type;
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehciles by their value */
static int CDECL VehicleValueSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	const Vehicle *u;
	Money diff = 0;

	for (u = *a; u != NULL; u = u->Next()) diff += u->value;
	for (u = *b; u != NULL; u = u->Next()) diff -= u->value;

	int r = ClampToI32(diff);
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their length */
static int CDECL VehicleLengthSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = 0;
	switch ((*a)->type) {
		case VEH_TRAIN:
			r = (*a)->u.rail.cached_total_length - (*b)->u.rail.cached_total_length;
			break;

		case VEH_ROAD: {
			const Vehicle *u;
			for (u = *a; u != NULL; u = u->Next()) r += u->u.road.cached_veh_length;
			for (u = *b; u != NULL; u = u->Next()) r -= u->u.road.cached_veh_length;
		} break;

		default: NOT_REACHED();
	}
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

/** Sort vehicles by the time they can still live */
static int CDECL VehicleTimeToLiveSorter(const Vehicle * const *a, const Vehicle * const *b)
{
	int r = ClampToI32(((*a)->max_age - (*a)->age) - ((*b)->max_age - (*b)->age));
	return (r != 0) ? r : VehicleNumberSorter(a, b);
}

void InitializeGUI()
{
	MemSetT(&_sorting, 0);
}

/**
 * Assign a vehicle window a new vehicle
 * @param window_class WindowClass to search for
 * @param from_index the old vehicle ID
 * @param to_index the new vehicle ID
 */
static inline void ChangeVehicleWindow(WindowClass window_class, VehicleID from_index, VehicleID to_index)
{
	Window *w = FindWindowById(window_class, from_index);
	if (w != NULL) {
		w->window_number = to_index;
		if (w->viewport != NULL) w->viewport->follow_vehicle = to_index;
		if (to_index != INVALID_VEHICLE) InvalidateThisWindowData(w, 0);
	}
}

/**
 * Report a change in vehicle IDs (due to autoreplace) to affected vehicle windows.
 * @param from_index the old vehicle ID
 * @param to_index the new vehicle ID
 */
void ChangeVehicleViewWindow(VehicleID from_index, VehicleID to_index)
{
	ChangeVehicleWindow(WC_VEHICLE_VIEW,      from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_ORDERS,    from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_REFIT,     from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_DETAILS,   from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_TIMETABLE, from_index, to_index);
}

enum VehicleListWindowWidgets {
	VLW_WIDGET_CLOSEBOX = 0,
	VLW_WIDGET_CAPTION,
	VLW_WIDGET_STICKY,
	VLW_WIDGET_SORT_ORDER,
	VLW_WIDGET_SORT_BY_PULLDOWN,
	VLW_WIDGET_EMPTY_TOP_RIGHT,
	VLW_WIDGET_LIST,
	VLW_WIDGET_SCROLLBAR,
	VLW_WIDGET_OTHER_COMPANY_FILLER,
	VLW_WIDGET_AVAILABLE_VEHICLES,
	VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN,
	VLW_WIDGET_STOP_ALL,
	VLW_WIDGET_START_ALL,
	VLW_WIDGET_EMPTY_BOTTOM_RIGHT,
	VLW_WIDGET_RESIZE,
};

static const Widget _vehicle_list_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,             STR_018B_CLOSE_WINDOW},
	{    WWT_CAPTION,  RESIZE_RIGHT,  COLOUR_GREY,    11,   247,     0,    13, 0x0,                  STR_018C_WINDOW_TITLE_DRAG_THIS},
	{  WWT_STICKYBOX,     RESIZE_LR,  COLOUR_GREY,   248,   259,     0,    13, 0x0,                  STR_STICKY_BUTTON},
	{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_GREY,     0,    80,    14,    25, STR_SORT_BY,          STR_SORT_ORDER_TIP},
	{   WWT_DROPDOWN,   RESIZE_NONE,  COLOUR_GREY,    81,   247,    14,    25, 0x0,                  STR_SORT_CRITERIA_TIP},
	{      WWT_PANEL,  RESIZE_RIGHT,  COLOUR_GREY,   248,   259,    14,    25, 0x0,                  STR_NULL},
	{     WWT_MATRIX,     RESIZE_RB,  COLOUR_GREY,     0,   247,    26,   181, 0x0,                  STR_NULL},
	{  WWT_SCROLLBAR,    RESIZE_LRB,  COLOUR_GREY,   248,   259,    26,   181, 0x0,                  STR_0190_SCROLL_BAR_SCROLLS_LIST},
	/* Widget to be shown for other companies hiding the following 6 widgets */
	{      WWT_PANEL,    RESIZE_RTB,  COLOUR_GREY,     0,   247,   182,   193, 0x0,                  STR_NULL},

	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,     0,   105,   182,   193, 0x0,                  STR_AVAILABLE_ENGINES_TIP},
	{   WWT_DROPDOWN,     RESIZE_TB,  COLOUR_GREY,   106,   223,   182,   193, STR_MANAGE_LIST,      STR_MANAGE_LIST_TIP},

	{ WWT_PUSHIMGBTN,     RESIZE_TB,  COLOUR_GREY,   224,   235,   182,   193, SPR_FLAG_VEH_STOPPED, STR_MASS_STOP_LIST_TIP},
	{ WWT_PUSHIMGBTN,     RESIZE_TB,  COLOUR_GREY,   236,   247,   182,   193, SPR_FLAG_VEH_RUNNING, STR_MASS_START_LIST_TIP},
	{      WWT_PANEL,    RESIZE_RTB,  COLOUR_GREY,   248,   247,   182,   193, 0x0,                  STR_NULL},
	{  WWT_RESIZEBOX,   RESIZE_LRTB,  COLOUR_GREY,   248,   259,   182,   193, 0x0,                  STR_RESIZE_BUTTON},
	{   WIDGETS_END},
};

static void DrawSmallOrderList(const Vehicle *v, int x, int y)
{
	const Order *order;
	int sel, i = 0;

	sel = v->cur_order_index;

	FOR_VEHICLE_ORDERS(v, order) {
		if (sel == 0) DrawString(x - 6, y, STR_SMALL_RIGHT_ARROW, TC_BLACK);
		sel--;

		if (order->IsType(OT_GOTO_STATION)) {
			if (v->type == VEH_SHIP && GetStation(order->GetDestination())->IsBuoy()) continue;

			SetDParam(0, order->GetDestination());
			DrawString(x, y, STR_A036, TC_FROMSTRING);

			y += 6;
			if (++i == 4) break;
		}
	}
}

static void DrawVehicleImage(const Vehicle *v, int x, int y, VehicleID selection, int count, int skip)
{
	switch (v->type) {
		case VEH_TRAIN:    DrawTrainImage(v, x, y, selection, count, skip); break;
		case VEH_ROAD:     DrawRoadVehImage(v, x, y, selection, count);     break;
		case VEH_SHIP:     DrawShipImage(v, x, y, selection);               break;
		case VEH_AIRCRAFT: DrawAircraftImage(v, x, y, selection);           break;
		default: NOT_REACHED();
	}
}

/**
 * Draw all the vehicle list items.
 * @param x the position from where to draw the items.
 * @param selected_vehicle the vehicle that is to be selected
 */
void BaseVehicleListWindow::DrawVehicleListItems(int x, VehicleID selected_vehicle)
{
	int y = PLY_WND_PRC__OFFSET_TOP_WIDGET;
	uint max = min(this->vscroll.pos + this->vscroll.cap, this->vehicles.Length());
	for (uint i = this->vscroll.pos; i < max; ++i) {
		const Vehicle *v = this->vehicles[i];
		StringID str;

		SetDParam(0, v->GetDisplayProfitThisYear());
		SetDParam(1, v->GetDisplayProfitLastYear());

		DrawVehicleImage(v, x + 19, y + 6, selected_vehicle, this->widget[VLW_WIDGET_LIST].right - this->widget[VLW_WIDGET_LIST].left - 20, 0);
		DrawString(x + 19, y + this->resize.step_height - 8, STR_0198_PROFIT_THIS_YEAR_LAST_YEAR, TC_FROMSTRING);

		if (v->name != NULL) {
			/* The vehicle got a name so we will print it */
			SetDParam(0, v->index);
			DrawString(x + 19, y, STR_01AB, TC_FROMSTRING);
		} else if (v->group_id != DEFAULT_GROUP) {
			/* The vehicle has no name, but is member of a group, so print group name */
			SetDParam(0, v->group_id);
			DrawString(x + 19, y, STR_GROUP_TINY_NAME, TC_BLACK);
		}

		if (this->resize.step_height == PLY_WND_PRC__SIZE_OF_ROW_BIG) DrawSmallOrderList(v, x + 138, y);

		if (v->IsInDepot()) {
			str = STR_021F;
		} else {
			str = (v->age > v->max_age - DAYS_IN_LEAP_YEAR) ? STR_00E3 : STR_00E2;
		}

		SetDParam(0, v->unitnumber);
		DrawString(x, y + 2, str, TC_FROMSTRING);

		DrawVehicleProfitButton(v, x, y + 13);

		y += this->resize.step_height;
	}
}

/**
 * Window for the (old) vehicle listing.
 *
 * bitmask for w->window_number
 * 0-7 CompanyID (owner)
 * 8-10 window type (use flags in vehicle_gui.h)
 * 11-15 vehicle type (using VEH_, but can be compressed to fewer bytes if needed)
 * 16-31 StationID or OrderID depending on window type (bit 8-10)
 */
struct VehicleListWindow : public BaseVehicleListWindow {

	VehicleListWindow(const WindowDesc *desc, WindowNumber window_number) : BaseVehicleListWindow(desc, window_number)
	{
		uint16 window_type = this->window_number & VLW_MASK;
		CompanyID company = (CompanyID)GB(this->window_number, 0, 8);

		this->vehicle_type = (VehicleType)GB(this->window_number, 11, 5);
		this->owner = company;

		/* Hide the widgets that we will not use in this window
		 * Some windows contains actions only fit for the owner */
		if (company == _local_company) {
			this->HideWidget(VLW_WIDGET_OTHER_COMPANY_FILLER);
			this->SetWidgetDisabledState(VLW_WIDGET_AVAILABLE_VEHICLES, window_type != VLW_STANDARD);
		} else {
			this->SetWidgetsHiddenState(true,
				VLW_WIDGET_AVAILABLE_VEHICLES,
				VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN,
				VLW_WIDGET_STOP_ALL,
				VLW_WIDGET_START_ALL,
				VLW_WIDGET_EMPTY_BOTTOM_RIGHT,
				WIDGET_LIST_END);
		}

		/* Set up the window widgets */
		switch (this->vehicle_type) {
			case VEH_TRAIN:
				this->widget[VLW_WIDGET_LIST].tooltips          = STR_883D_TRAINS_CLICK_ON_TRAIN_FOR;
				this->widget[VLW_WIDGET_AVAILABLE_VEHICLES].data = STR_AVAILABLE_TRAINS;
				break;

			case VEH_ROAD:
				this->widget[VLW_WIDGET_LIST].tooltips          = STR_901A_ROAD_VEHICLES_CLICK_ON;
				this->widget[VLW_WIDGET_AVAILABLE_VEHICLES].data = STR_AVAILABLE_ROAD_VEHICLES;
				break;

			case VEH_SHIP:
				this->widget[VLW_WIDGET_LIST].tooltips          = STR_9823_SHIPS_CLICK_ON_SHIP_FOR;
				this->widget[VLW_WIDGET_AVAILABLE_VEHICLES].data = STR_AVAILABLE_SHIPS;
				break;

			case VEH_AIRCRAFT:
				this->widget[VLW_WIDGET_LIST].tooltips          = STR_A01F_AIRCRAFT_CLICK_ON_AIRCRAFT;
				this->widget[VLW_WIDGET_AVAILABLE_VEHICLES].data = STR_AVAILABLE_AIRCRAFT;
				break;

			default: NOT_REACHED();
		}

		switch (window_type) {
			case VLW_SHARED_ORDERS:
				this->widget[VLW_WIDGET_CAPTION].data  = STR_VEH_WITH_SHARED_ORDERS_LIST;
				break;

			case VLW_STANDARD: // Company Name - standard widget setup
				switch (this->vehicle_type) {
					case VEH_TRAIN:    this->widget[VLW_WIDGET_CAPTION].data = STR_881B_TRAINS;        break;
					case VEH_ROAD:     this->widget[VLW_WIDGET_CAPTION].data = STR_9001_ROAD_VEHICLES; break;
					case VEH_SHIP:     this->widget[VLW_WIDGET_CAPTION].data = STR_9805_SHIPS;         break;
					case VEH_AIRCRAFT: this->widget[VLW_WIDGET_CAPTION].data = STR_A009_AIRCRAFT;      break;
					default: NOT_REACHED(); break;
				}
				break;

			case VLW_WAYPOINT_LIST:
				this->widget[VLW_WIDGET_CAPTION].data = STR_WAYPOINT_VIEWPORT_LIST;
				break;

			case VLW_STATION_LIST: // Station Name
				switch (this->vehicle_type) {
					case VEH_TRAIN:    this->widget[VLW_WIDGET_CAPTION].data = STR_SCHEDULED_TRAINS;        break;
					case VEH_ROAD:     this->widget[VLW_WIDGET_CAPTION].data = STR_SCHEDULED_ROAD_VEHICLES; break;
					case VEH_SHIP:     this->widget[VLW_WIDGET_CAPTION].data = STR_SCHEDULED_SHIPS;         break;
					case VEH_AIRCRAFT: this->widget[VLW_WIDGET_CAPTION].data = STR_SCHEDULED_AIRCRAFT;      break;
					default: NOT_REACHED(); break;
				}
				break;

			case VLW_DEPOT_LIST:
				switch (this->vehicle_type) {
					case VEH_TRAIN:    this->widget[VLW_WIDGET_CAPTION].data = STR_VEHICLE_LIST_TRAIN_DEPOT;    break;
					case VEH_ROAD:     this->widget[VLW_WIDGET_CAPTION].data = STR_VEHICLE_LIST_ROADVEH_DEPOT;  break;
					case VEH_SHIP:     this->widget[VLW_WIDGET_CAPTION].data = STR_VEHICLE_LIST_SHIP_DEPOT;     break;
					case VEH_AIRCRAFT: this->widget[VLW_WIDGET_CAPTION].data = STR_VEHICLE_LIST_AIRCRAFT_DEPOT; break;
					default: NOT_REACHED(); break;
				}
				break;
			default: NOT_REACHED(); break;
		}

		switch (this->vehicle_type) {
			case VEH_TRAIN:
				this->resize.step_width = 1;
				/* Fallthrough */
			case VEH_ROAD:
				this->vscroll.cap = 6;
				this->resize.step_height = PLY_WND_PRC__SIZE_OF_ROW_SMALL;
				break;
			case VEH_SHIP:
			case VEH_AIRCRAFT:
				this->vscroll.cap = 4;
				this->resize.step_height = PLY_WND_PRC__SIZE_OF_ROW_BIG;
				break;
			default: NOT_REACHED();
		}


		this->widget[VLW_WIDGET_LIST].data = (this->vscroll.cap << 8) + 1;

		/* Set up sorting. Make the window-specific _sorting variable
		 * point to the correct global _sorting struct so we are freed
		 * from having conditionals during window operation */
		switch (this->vehicle_type) {
			case VEH_TRAIN:    this->sorting = &_sorting.train; break;
			case VEH_ROAD:     this->sorting = &_sorting.roadveh; break;
			case VEH_SHIP:     this->sorting = &_sorting.ship; break;
			case VEH_AIRCRAFT: this->sorting = &_sorting.aircraft; break;
			default: NOT_REACHED(); break;
		}

		this->vehicles.SetListing(*this->sorting);
		this->vehicles.ForceRebuild();
		this->vehicles.NeedResort();

		this->FindWindowPlacementAndResize(desc);
		if (this->vehicle_type == VEH_TRAIN) ResizeWindow(this, 65, 0);
	}

	~VehicleListWindow()
	{
		*this->sorting = this->vehicles.GetListing();
	}

	virtual void OnPaint()
	{
		int x = 2;
		const Owner owner = this->owner;
		const uint16 window_type = this->window_number & VLW_MASK;
		const uint16 index = GB(this->window_number, 16, 16);

		this->BuildVehicleList(owner, index, window_type);
		this->SortVehicleList();
		SetVScrollCount(this, this->vehicles.Length());

		if (this->vehicles.Length() == 0) HideDropDownMenu(this);

		/* draw the widgets */
		switch (window_type) {
			case VLW_SHARED_ORDERS: // Shared Orders
				if (this->vehicles.Length() == 0) {
					/* We can't open this window without vehicles using this order
					 * and we should close the window when deleting the order      */
					NOT_REACHED();
				}
				SetDParam(0, this->vscroll.count);
				break;

			case VLW_STANDARD: // Company Name
				SetDParam(0, owner);
				SetDParam(1, this->vscroll.count);
				break;

			case VLW_WAYPOINT_LIST:
				SetDParam(0, index);
				SetDParam(1, this->vscroll.count);
				break;

			case VLW_STATION_LIST: // Station Name
				SetDParam(0, index);
				SetDParam(1, this->vscroll.count);
				break;

			case VLW_DEPOT_LIST:
				switch (this->vehicle_type) {
					case VEH_TRAIN:    SetDParam(0, STR_8800_TRAIN_DEPOT);        break;
					case VEH_ROAD:     SetDParam(0, STR_9003_ROAD_VEHICLE_DEPOT); break;
					case VEH_SHIP:     SetDParam(0, STR_9803_SHIP_DEPOT);         break;
					case VEH_AIRCRAFT: SetDParam(0, STR_A002_AIRCRAFT_HANGAR);    break;
					default: NOT_REACHED(); break;
				}
				if (this->vehicle_type == VEH_AIRCRAFT) {
					SetDParam(1, index); // Airport name
				} else {
					SetDParam(1, GetDepot(index)->town_index);
				}
				SetDParam(2, this->vscroll.count);
				break;
			default: NOT_REACHED(); break;
		}

		this->SetWidgetsDisabledState(this->vehicles.Length() == 0,
			VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN,
			VLW_WIDGET_STOP_ALL,
			VLW_WIDGET_START_ALL,
			WIDGET_LIST_END);

		this->DrawWidgets();

		/* draw sorting criteria string */
		DrawString(85, 15, this->vehicle_sorter_names[this->vehicles.SortType()], TC_BLACK);
		/* draw arrow pointing up/down for ascending/descending sorting */
		this->DrawSortButtonState(VLW_WIDGET_SORT_ORDER, this->vehicles.IsDescSortOrder() ? SBS_DOWN : SBS_UP);

		this->DrawVehicleListItems(x,  INVALID_VEHICLE);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case VLW_WIDGET_SORT_ORDER: // Flip sorting method ascending/descending
				this->vehicles.ToggleSortOrder();
				this->SetDirty();
				break;
			case VLW_WIDGET_SORT_BY_PULLDOWN:// Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->vehicle_sorter_names, this->vehicles.SortType(), VLW_WIDGET_SORT_BY_PULLDOWN, 0, (this->vehicle_type == VEH_TRAIN || this->vehicle_type == VEH_ROAD) ? 0 : (1 << 10));
				return;
			case VLW_WIDGET_LIST: { // Matrix to show vehicles
				uint32 id_v = (pt.y - PLY_WND_PRC__OFFSET_TOP_WIDGET) / this->resize.step_height;
				const Vehicle *v;

				if (id_v >= this->vscroll.cap) return; // click out of bounds

				id_v += this->vscroll.pos;

				if (id_v >= this->vehicles.Length()) return; // click out of list bound

				v = this->vehicles[id_v];

				ShowVehicleViewWindow(v);
			} break;

			case VLW_WIDGET_AVAILABLE_VEHICLES:
				ShowBuildVehicleWindow(INVALID_TILE, this->vehicle_type);
				break;

			case VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN: {
				static StringID action_str[] = {
					STR_REPLACE_VEHICLES,
					STR_SEND_FOR_SERVICING,
					STR_NULL,
					INVALID_STRING_ID
				};

				static const StringID depot_name[] = {
					STR_SEND_TRAIN_TO_DEPOT,
					STR_SEND_ROAD_VEHICLE_TO_DEPOT,
					STR_SEND_SHIP_TO_DEPOT,
					STR_SEND_AIRCRAFT_TO_HANGAR
				};

				/* XXX - Substite string since the dropdown cannot handle dynamic strings */
				action_str[2] = depot_name[this->vehicle_type];
				ShowDropDownMenu(this, action_str, 0, VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN, 0, (this->window_number & VLW_MASK) == VLW_STANDARD ? 0 : 1);
				break;
			}

			case VLW_WIDGET_STOP_ALL:
			case VLW_WIDGET_START_ALL:
				DoCommandP(0, GB(this->window_number, 16, 16), (this->window_number & VLW_MASK) | (1 << 6) | (widget == VLW_WIDGET_START_ALL ? (1 << 5) : 0) | this->vehicle_type, CMD_MASS_START_STOP);
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case VLW_WIDGET_SORT_BY_PULLDOWN:
				this->vehicles.SetSortType(index);
				break;
			case VLW_WIDGET_MANAGE_VEHICLES_DROPDOWN:
				assert(this->vehicles.Length() != 0);

				switch (index) {
					case 0: // Replace window
						ShowReplaceGroupVehicleWindow(DEFAULT_GROUP, this->vehicle_type);
						break;
					case 1: // Send for servicing
						DoCommandP(0, GB(this->window_number, 16, 16) /* StationID or OrderID (depending on VLW) */,
							(this->window_number & VLW_MASK) | DEPOT_MASS_SEND | DEPOT_SERVICE,
							GetCmdSendToDepot(this->vehicle_type));
						break;
					case 2: // Send to Depots
						DoCommandP(0, GB(this->window_number, 16, 16) /* StationID or OrderID (depending on VLW) */,
							(this->window_number & VLW_MASK) | DEPOT_MASS_SEND,
							GetCmdSendToDepot(this->vehicle_type));
						break;

					default: NOT_REACHED();
				}
				break;
			default: NOT_REACHED();
		}
		this->SetDirty();
	}

	virtual void OnTick()
	{
		if (_pause_game != 0) return;
		if (this->vehicles.NeedResort()) {
			StationID station = ((this->window_number & VLW_MASK) == VLW_STATION_LIST) ? GB(this->window_number, 16, 16) : INVALID_STATION;

			DEBUG(misc, 3, "Periodic resort %d list company %d at station %d", this->vehicle_type, this->owner, station);
			this->SetDirty();
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		this->vscroll.cap += delta.y / (int)this->resize.step_height;
		this->widget[VLW_WIDGET_LIST].data = (this->vscroll.cap << 8) + 1;
	}

	virtual void OnInvalidateData(int data)
	{
		if (HasBit(data, 15) && (this->window_number & VLW_MASK) == VLW_SHARED_ORDERS) {
			SB(this->window_number, 16, 16, GB(data, 16, 16));
			this->vehicles.ForceRebuild();
			return;
		}

		if (data == 0) {
			this->vehicles.ForceRebuild();
		} else {
			this->vehicles.ForceResort();
		}
	}
};

static WindowDesc _vehicle_list_desc(
	WDP_AUTO, WDP_AUTO, 260, 194, 260, 246,
	WC_INVALID, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_vehicle_list_widgets
);

static void ShowVehicleListWindowLocal(CompanyID company, uint16 VLW_flag, VehicleType vehicle_type, uint16 unique_number)
{
	if (!IsValidCompanyID(company)) return;

	_vehicle_list_desc.cls = GetWindowClassForVehicleType(vehicle_type);
	WindowNumber num = (unique_number << 16) | (vehicle_type << 11) | VLW_flag | company;
	AllocateWindowDescFront<VehicleListWindow>(&_vehicle_list_desc, num);
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type)
{
	/* If _settings_client.gui.advanced_vehicle_list > 1, display the Advanced list
	 * if _settings_client.gui.advanced_vehicle_list == 1, display Advanced list only for local company
	 * if _ctrl_pressed, do the opposite action (Advanced list x Normal list)
	 */

	if ((_settings_client.gui.advanced_vehicle_list > (uint)(company != _local_company)) != _ctrl_pressed) {
		ShowCompanyGroup(company, vehicle_type);
	} else {
		ShowVehicleListWindowLocal(company, VLW_STANDARD, vehicle_type, 0);
	}
}

void ShowVehicleListWindow(const Waypoint *wp)
{
	if (wp == NULL) return;
	ShowVehicleListWindowLocal(wp->owner, VLW_WAYPOINT_LIST, VEH_TRAIN, wp->index);
}

void ShowVehicleListWindow(const Vehicle *v)
{
	ShowVehicleListWindowLocal(v->owner, VLW_SHARED_ORDERS, v->type, v->FirstShared()->index);
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, StationID station)
{
	ShowVehicleListWindowLocal(company, VLW_STATION_LIST, vehicle_type, station);
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, TileIndex depot_tile)
{
	uint16 depot_airport_index;

	if (vehicle_type == VEH_AIRCRAFT) {
		depot_airport_index = GetStationIndex(depot_tile);
	} else {
		Depot *depot = GetDepotByTile(depot_tile);
		if (depot == NULL) return; // no depot to show
		depot_airport_index = depot->index;
	}
	ShowVehicleListWindowLocal(company, VLW_DEPOT_LIST, vehicle_type, depot_airport_index);
}


/* Unified vehicle GUI - Vehicle Details Window */

/** Constants of vehicle details widget indices */
enum VehicleDetailsWindowWidgets {
	VLD_WIDGET_CLOSEBOX = 0,
	VLD_WIDGET_CAPTION,
	VLD_WIDGET_RENAME_VEHICLE,
	VLD_WIDGET_STICKY,
	VLD_WIDGET_TOP_DETAILS,
	VLD_WIDGET_INCREASE_SERVICING_INTERVAL,
	VLD_WIDGET_DECREASE_SERVICING_INTERVAL,
	VLD_WIDGET_BOTTOM_RIGHT,
	VLD_WIDGET_MIDDLE_DETAILS,
	VLD_WIDGET_SCROLLBAR,
	VLD_WIDGET_DETAILS_CARGO_CARRIED,
	VLD_WIDGET_DETAILS_TRAIN_VEHICLES,
	VLD_WIDGET_DETAILS_CAPACITY_OF_EACH,
	VLD_WIDGET_DETAILS_TOTAL_CARGO,
	VLD_WIDGET_RESIZE,
};

/** Vehicle details widgets. */
static const Widget _vehicle_details_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,   0,  10,   0,  13, STR_00C5,             STR_018B_CLOSE_WINDOW},                  // VLD_WIDGET_CLOSEBOX
	{    WWT_CAPTION,  RESIZE_RIGHT,  COLOUR_GREY,  11, 352,   0,  13, 0x0,                  STR_018C_WINDOW_TITLE_DRAG_THIS},        // VLD_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,     RESIZE_LR,  COLOUR_GREY, 353, 392,   0,  13, STR_01AA_NAME,        STR_NULL /* filled in later */},         // VLD_WIDGET_RENAME_VEHICLE
	{  WWT_STICKYBOX,     RESIZE_LR,  COLOUR_GREY, 393, 404,   0,  13, STR_NULL,             STR_STICKY_BUTTON},                      // VLD_WIDGET_STICKY
	{      WWT_PANEL,  RESIZE_RIGHT,  COLOUR_GREY,   0, 404,  14,  55, 0x0,                  STR_NULL},                               // VLD_WIDGET_TOP_DETAILS
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,   0,  10, 101, 106, STR_0188,             STR_884D_INCREASE_SERVICING_INTERVAL},   // VLD_WIDGET_INCREASE_SERVICING_INTERVAL
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,   0,  10, 107, 112, STR_0189,             STR_884E_DECREASE_SERVICING_INTERVAL},   // VLD_WIDGET_DECREASE_SERVICING_INTERVAL
	{      WWT_PANEL,    RESIZE_RTB,  COLOUR_GREY,  11, 404, 101, 112, 0x0,                  STR_NULL},                               // VLD_WIDGET_BOTTOM_RIGHT
	{     WWT_MATRIX,     RESIZE_RB,  COLOUR_GREY,   0, 392,  56, 100, 0x701,                STR_NULL},                               // VLD_WIDGET_MIDDLE_DETAILS
	{  WWT_SCROLLBAR,    RESIZE_LRB,  COLOUR_GREY, 393, 404,  56, 100, 0x0,                  STR_0190_SCROLL_BAR_SCROLLS_LIST},       // VLD_WIDGET_SCROLLBAR
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,   0,  95, 113, 124, STR_013C_CARGO,       STR_884F_SHOW_DETAILS_OF_CARGO_CARRIED}, // VLD_WIDGET_DETAILS_CARGO_CARRIED
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY,  96, 194, 113, 124, STR_013D_INFORMATION, STR_8850_SHOW_DETAILS_OF_TRAIN_VEHICLES},// VLD_WIDGET_DETAILS_TRAIN_VEHICLES
	{ WWT_PUSHTXTBTN,     RESIZE_TB,  COLOUR_GREY, 195, 293, 113, 124, STR_013E_CAPACITIES,  STR_8851_SHOW_CAPACITIES_OF_EACH},       // VLD_WIDGET_DETAILS_CAPACITY_OF_EACH
	{ WWT_PUSHTXTBTN,    RESIZE_RTB,  COLOUR_GREY, 294, 392, 113, 124, STR_TOTAL_CARGO,      STR_SHOW_TOTAL_CARGO},                   // VLD_WIDGET_DETAILS_TOTAL_CARGO
	{  WWT_RESIZEBOX,   RESIZE_LRTB,  COLOUR_GREY, 393, 404, 113, 124, 0x0,                  STR_RESIZE_BUTTON},                      // VLD_RESIZE
	{   WIDGETS_END},
};


/** Command indices for the _vehicle_command_translation_table. */
enum VehicleStringTranslation {
	VST_VEHICLE_AGE_RUNNING_COST_YR,
	VST_VEHICLE_MAX_SPEED,
	VST_VEHICLE_PROFIT_THIS_YEAR_LAST_YEAR,
	VST_VEHICLE_RELIABILITY_BREAKDOWNS,
};

/** Command codes for the shared buttons indexed by VehicleCommandTranslation and vehicle type. */
static const StringID _vehicle_translation_table[][4] = {
	{ // VST_VEHICLE_AGE_RUNNING_COST_YR
		STR_885D_AGE_RUNNING_COST_YR,
		STR_900D_AGE_RUNNING_COST_YR,
		STR_9812_AGE_RUNNING_COST_YR,
		STR_A00D_AGE_RUNNING_COST_YR,
	},
	{ // VST_VEHICLE_MAX_SPEED
		STR_NULL,
		STR_900E_MAX_SPEED,
		STR_9813_MAX_SPEED,
		STR_A00E_MAX_SPEED,
	},
	{ // VST_VEHICLE_PROFIT_THIS_YEAR_LAST_YEAR
		STR_885F_PROFIT_THIS_YEAR_LAST_YEAR,
		STR_900F_PROFIT_THIS_YEAR_LAST_YEAR,
		STR_9814_PROFIT_THIS_YEAR_LAST_YEAR,
		STR_A00F_PROFIT_THIS_YEAR_LAST_YEAR,
	},
	{ // VST_VEHICLE_RELIABILITY_BREAKDOWNS
		STR_8860_RELIABILITY_BREAKDOWNS,
		STR_9010_RELIABILITY_BREAKDOWNS,
		STR_9815_RELIABILITY_BREAKDOWNS,
		STR_A010_RELIABILITY_BREAKDOWNS,
	},
};


extern int GetTrainDetailsWndVScroll(VehicleID veh_id, byte det_tab);
extern void DrawTrainDetails(const Vehicle *v, int x, int y, int vscroll_pos, uint16 vscroll_cap, byte det_tab);
extern void DrawRoadVehDetails(const Vehicle *v, int x, int y);
extern void DrawShipDetails(const Vehicle *v, int x, int y);
extern void DrawAircraftDetails(const Vehicle *v, int x, int y);

struct VehicleDetailsWindow : Window {
	int tab;

	/** Initialize a newly created vehicle details window */
	VehicleDetailsWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		const Vehicle *v = GetVehicle(this->window_number);

		switch (v->type) {
			case VEH_TRAIN:
				ResizeWindow(this, 0, 39);

				this->vscroll.cap = 6;
				this->height += 12;
				this->resize.step_height = 14;
				this->resize.height = this->height - 14 * 2; // Minimum of 4 wagons in the display

				this->widget[VLD_WIDGET_RENAME_VEHICLE].tooltips = STR_8867_NAME_TRAIN;
				this->widget[VLD_WIDGET_CAPTION].data = STR_8802_DETAILS;
				break;

			case VEH_ROAD: {
				this->widget[VLD_WIDGET_CAPTION].data = STR_900C_DETAILS;
				this->widget[VLD_WIDGET_RENAME_VEHICLE].tooltips = STR_902E_NAME_ROAD_VEHICLE;

				if (!RoadVehHasArticPart(v)) break;

				/* Draw the text under the vehicle instead of next to it, minus the
				 * height already allocated for the cargo of the first vehicle. */
				uint height_extension = 15 - 11;

				/* Add space for the cargo amount for each part. */
				for (const Vehicle *u = v; u != NULL; u = u->Next()) {
					if (u->cargo_cap != 0) height_extension += 11;
				}

				ResizeWindow(this, 0, height_extension);
			} break;

			case VEH_SHIP:
				this->widget[VLD_WIDGET_RENAME_VEHICLE].tooltips = STR_982F_NAME_SHIP;
				this->widget[VLD_WIDGET_CAPTION].data = STR_9811_DETAILS;
				break;

			case VEH_AIRCRAFT:
				ResizeWindow(this, 0, 11);
				this->widget[VLD_WIDGET_RENAME_VEHICLE].tooltips = STR_A032_NAME_AIRCRAFT;
				this->widget[VLD_WIDGET_CAPTION].data = STR_A00C_DETAILS;
				break;
			default: NOT_REACHED();
		}

		if (v->type != VEH_TRAIN) {
			this->vscroll.cap = 1;
			this->widget[VLD_WIDGET_MIDDLE_DETAILS].right += 12;
		}

		this->widget[VLD_WIDGET_MIDDLE_DETAILS].data = (this->vscroll.cap << 8) + 1;
		this->owner = v->owner;

		this->tab = 0;

		this->FindWindowPlacementAndResize(desc);
	}

	/** Checks whether service interval is enabled for the vehicle. */
	static bool IsVehicleServiceIntervalEnabled(const VehicleType vehicle_type)
	{
		switch (vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:    return _settings_game.vehicle.servint_trains   != 0; break;
			case VEH_ROAD:     return _settings_game.vehicle.servint_roadveh  != 0; break;
			case VEH_SHIP:     return _settings_game.vehicle.servint_ships    != 0; break;
			case VEH_AIRCRAFT: return _settings_game.vehicle.servint_aircraft != 0; break;
		}
		return false; // kill a compiler warning
	}

	/**
	 * Draw the details for the given vehicle at the position (x, y) of the Details windows
	 *
	 * @param v current vehicle
	 * @param x The x coordinate
	 * @param y The y coordinate
	 * @param vscroll_pos (train only)
	 * @param vscroll_cap (train only)
	 * @param det_tab (train only)
	 */
	static void DrawVehicleDetails(const Vehicle *v, int x, int y, int vscroll_pos, uint vscroll_cap, byte det_tab)
	{
		switch (v->type) {
			case VEH_TRAIN:    DrawTrainDetails(v, x, y, vscroll_pos, vscroll_cap, det_tab);  break;
			case VEH_ROAD:     DrawRoadVehDetails(v, x, y);  break;
			case VEH_SHIP:     DrawShipDetails(v, x, y);     break;
			case VEH_AIRCRAFT: DrawAircraftDetails(v, x, y); break;
			default: NOT_REACHED();
		}
	}

	/** Repaint vehicle details window. */
	virtual void OnPaint()
	{
		const Vehicle *v = GetVehicle(this->window_number);
		byte det_tab = this->tab;

		this->SetWidgetDisabledState(VLD_WIDGET_RENAME_VEHICLE, v->owner != _local_company);

		if (v->type == VEH_TRAIN) {
			this->DisableWidget(det_tab + VLD_WIDGET_DETAILS_CARGO_CARRIED);
			SetVScrollCount(this, GetTrainDetailsWndVScroll(v->index, det_tab));
		}

		this->SetWidgetsHiddenState(v->type != VEH_TRAIN,
			VLD_WIDGET_SCROLLBAR,
			VLD_WIDGET_DETAILS_CARGO_CARRIED,
			VLD_WIDGET_DETAILS_TRAIN_VEHICLES,
			VLD_WIDGET_DETAILS_CAPACITY_OF_EACH,
			VLD_WIDGET_DETAILS_TOTAL_CARGO,
			VLD_WIDGET_RESIZE,
			WIDGET_LIST_END);

		/* Disable service-scroller when interval is set to disabled */
		this->SetWidgetsDisabledState(!IsVehicleServiceIntervalEnabled(v->type),
			VLD_WIDGET_INCREASE_SERVICING_INTERVAL,
			VLD_WIDGET_DECREASE_SERVICING_INTERVAL,
			WIDGET_LIST_END);


		SetDParam(0, v->index);
		this->DrawWidgets();

		/* Draw running cost */
		SetDParam(1, v->age / DAYS_IN_LEAP_YEAR);
		SetDParam(0, (v->age + DAYS_IN_YEAR < v->max_age) ? STR_AGE : STR_AGE_RED);
		SetDParam(2, v->max_age / DAYS_IN_LEAP_YEAR);
		SetDParam(3, v->GetDisplayRunningCost());
		DrawString(2, 15, _vehicle_translation_table[VST_VEHICLE_AGE_RUNNING_COST_YR][v->type], TC_FROMSTRING);

		/* Draw max speed */
		switch (v->type) {
			case VEH_TRAIN:
				SetDParam(2, v->GetDisplayMaxSpeed());
				SetDParam(1, v->u.rail.cached_power);
				SetDParam(0, v->u.rail.cached_weight);
				SetDParam(3, v->u.rail.cached_max_te / 1000);
				DrawString(2, 25, (_settings_game.vehicle.train_acceleration_model != TAM_ORIGINAL && v->u.rail.railtype != RAILTYPE_MAGLEV) ?
					STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE :
					STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED, TC_FROMSTRING);
				break;

			case VEH_ROAD:
			case VEH_SHIP:
			case VEH_AIRCRAFT:
				SetDParam(0, v->GetDisplayMaxSpeed());
				DrawString(2, 25, _vehicle_translation_table[VST_VEHICLE_MAX_SPEED][v->type], TC_FROMSTRING);
				break;

			default: NOT_REACHED();
		}

		/* Draw profit */
		SetDParam(0, v->GetDisplayProfitThisYear());
		SetDParam(1, v->GetDisplayProfitLastYear());
		DrawString(2, 35, _vehicle_translation_table[VST_VEHICLE_PROFIT_THIS_YEAR_LAST_YEAR][v->type], TC_FROMSTRING);

		/* Draw breakdown & reliability */
		SetDParam(0, v->reliability * 100 >> 16);
		SetDParam(1, v->breakdowns_since_last_service);
		DrawString(2, 45, _vehicle_translation_table[VST_VEHICLE_RELIABILITY_BREAKDOWNS][v->type], TC_FROMSTRING);

		/* Draw service interval text */
		SetDParam(0, v->service_interval);
		SetDParam(1, v->date_of_last_service);
		DrawString(13, this->height - (v->type != VEH_TRAIN ? 11 : 23), _settings_game.vehicle.servint_ispercent ? STR_SERVICING_INTERVAL_PERCENT : STR_883C_SERVICING_INTERVAL_DAYS, TC_FROMSTRING);

		switch (v->type) {
			case VEH_TRAIN:
				DrawVehicleDetails(v, 2, 57, this->vscroll.pos, this->vscroll.cap, det_tab);
				break;

			case VEH_ROAD:
			case VEH_SHIP:
			case VEH_AIRCRAFT:
				DrawVehicleImage(v, 3, 57, INVALID_VEHICLE, 0, 0);
				DrawVehicleDetails(v, 75, 57, this->vscroll.pos, this->vscroll.cap, det_tab);
				break;

			default: NOT_REACHED();
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		/** Message strings for renaming vehicles indexed by vehicle type. */
		static const StringID _name_vehicle_title[] = {
			STR_8865_NAME_TRAIN,
			STR_902C_NAME_ROAD_VEHICLE,
			STR_9831_NAME_SHIP,
			STR_A030_NAME_AIRCRAFT
		};

		switch (widget) {
			case VLD_WIDGET_RENAME_VEHICLE: {// rename
				const Vehicle *v = GetVehicle(this->window_number);
				SetDParam(0, v->index);
				ShowQueryString(STR_VEHICLE_NAME, _name_vehicle_title[v->type], MAX_LENGTH_VEHICLE_NAME_BYTES, MAX_LENGTH_VEHICLE_NAME_PIXELS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT);
			} break;

			case VLD_WIDGET_INCREASE_SERVICING_INTERVAL:   // increase int
			case VLD_WIDGET_DECREASE_SERVICING_INTERVAL: { // decrease int
				int mod = _ctrl_pressed ? 5 : 10;
				const Vehicle *v = GetVehicle(this->window_number);

				mod = (widget == VLD_WIDGET_DECREASE_SERVICING_INTERVAL) ? -mod : mod;
				mod = GetServiceIntervalClamped(mod + v->service_interval);
				if (mod == v->service_interval) return;

				DoCommandP(v->tile, v->index, mod, CMD_CHANGE_SERVICE_INT | CMD_MSG(STR_018A_CAN_T_CHANGE_SERVICING));
			} break;

			case VLD_WIDGET_DETAILS_CARGO_CARRIED:
			case VLD_WIDGET_DETAILS_TRAIN_VEHICLES:
			case VLD_WIDGET_DETAILS_CAPACITY_OF_EACH:
			case VLD_WIDGET_DETAILS_TOTAL_CARGO:
				this->SetWidgetsDisabledState(false,
					VLD_WIDGET_DETAILS_CARGO_CARRIED,
					VLD_WIDGET_DETAILS_TRAIN_VEHICLES,
					VLD_WIDGET_DETAILS_CAPACITY_OF_EACH,
					VLD_WIDGET_DETAILS_TOTAL_CARGO,
					widget,
					WIDGET_LIST_END);

				this->tab = widget - VLD_WIDGET_DETAILS_CARGO_CARRIED;
				this->SetDirty();
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		/** Message strings for error while renaming indexed by vehicle type. */
		static const StringID _name_vehicle_error[] = {
			STR_8866_CAN_T_NAME_TRAIN,
			STR_902D_CAN_T_NAME_ROAD_VEHICLE,
			STR_9832_CAN_T_NAME_SHIP,
			STR_A031_CAN_T_NAME_AIRCRAFT
		};

		if (str == NULL) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_VEHICLE | CMD_MSG(_name_vehicle_error[GetVehicle(this->window_number)->type]), NULL, str);
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		if (delta.x != 0) ResizeButtons(this, VLD_WIDGET_DETAILS_CARGO_CARRIED, VLD_WIDGET_DETAILS_TOTAL_CARGO);
		if (delta.y == 0) return;

		this->vscroll.cap += delta.y / 14;
		this->widget[VLD_WIDGET_MIDDLE_DETAILS].data = (this->vscroll.cap << 8) + 1;
	}
};

/** Vehicle details window descriptor. */
static const WindowDesc _vehicle_details_desc(
	WDP_AUTO, WDP_AUTO, 405, 113, 405, 113,
	WC_VEHICLE_DETAILS, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_vehicle_details_widgets
);

/** Shows the vehicle details window of the given vehicle. */
static void ShowVehicleDetailsWindow(const Vehicle *v)
{
	DeleteWindowById(WC_VEHICLE_ORDERS, v->index, false);
	DeleteWindowById(WC_VEHICLE_TIMETABLE, v->index, false);
	AllocateWindowDescFront<VehicleDetailsWindow>(&_vehicle_details_desc, v->index);
}


/* Unified vehicle GUI - Vehicle View Window */

/** Vehicle view widgets. */
static const Widget _vehicle_view_widgets[] = {
	{   WWT_CLOSEBOX,  RESIZE_NONE,  COLOUR_GREY,   0,  10,   0,  13, STR_00C5,                 STR_018B_CLOSE_WINDOW },           // VVW_WIDGET_CLOSEBOX
	{    WWT_CAPTION, RESIZE_RIGHT,  COLOUR_GREY,  11, 237,   0,  13, 0x0 /* filled later */,   STR_018C_WINDOW_TITLE_DRAG_THIS }, // VVW_WIDGET_CAPTION
	{  WWT_STICKYBOX,    RESIZE_LR,  COLOUR_GREY, 238, 249,   0,  13, 0x0,                      STR_STICKY_BUTTON },               // VVW_WIDGET_STICKY
	{      WWT_PANEL,    RESIZE_RB,  COLOUR_GREY,   0, 231,  14, 103, 0x0,                      STR_NULL },                        // VVW_WIDGET_PANEL
	{      WWT_INSET,    RESIZE_RB,  COLOUR_GREY,   2, 229,  16, 101, 0x0,                      STR_NULL },                        // VVW_WIDGET_VIEWPORT
	{    WWT_PUSHBTN,   RESIZE_RTB,  COLOUR_GREY,   0, 237, 104, 115, 0x0,                      0x0 /* filled later */ },          // VVW_WIDGET_START_STOP_VEH
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  14,  31, SPR_CENTRE_VIEW_VEHICLE,  0x0 /* filled later */ },          // VVW_WIDGET_CENTER_MAIN_VIEH
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  32,  49, 0x0 /* filled later */,   0x0 /* filled later */ },          // VVW_WIDGET_GOTO_DEPOT
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  50,  67, SPR_REFIT_VEHICLE,        0x0 /* filled later */ },          // VVW_WIDGET_REFIT_VEH
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  68,  85, SPR_SHOW_ORDERS,          0x0 /* filled later */ },          // VVW_WIDGET_SHOW_ORDERS
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  86, 103, SPR_SHOW_VEHICLE_DETAILS, 0x0 /* filled later */ },          // VVW_WIDGET_SHOW_DETAILS
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  32,  49, 0x0 /* filled later */,   0x0 /* filled later */ },          // VVW_WIDGET_CLONE_VEH
	{      WWT_PANEL,   RESIZE_LRB,  COLOUR_GREY, 232, 249, 104, 103, 0x0,                      STR_NULL },                        // VVW_WIDGET_EMPTY_BOTTOM_RIGHT
	{  WWT_RESIZEBOX,  RESIZE_LRTB,  COLOUR_GREY, 238, 249, 104, 115, 0x0,                      STR_NULL },                        // VVW_WIDGET_RESIZE
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  50,  67, SPR_FORCE_VEHICLE_TURN,   STR_9020_FORCE_VEHICLE_TO_TURN_AROUND }, // VVW_WIDGET_TURN_AROUND
	{ WWT_PUSHIMGBTN,    RESIZE_LR,  COLOUR_GREY, 232, 249,  50,  67, SPR_IGNORE_SIGNALS,       STR_884A_FORCE_TRAIN_TO_PROCEED },       // VVW_WIDGET_FORCE_PROCEED
{   WIDGETS_END},
};


/** Vehicle view window descriptor for all vehicles but trains. */
static const WindowDesc _vehicle_view_desc(
	WDP_AUTO, WDP_AUTO, 250, 116, 250, 116,
	WC_VEHICLE_VIEW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_vehicle_view_widgets
);

/** Vehicle view window descriptor for trains. Only minimum_height and
 *  default_height are different for train view.
 */
static const WindowDesc _train_view_desc(
	WDP_AUTO, WDP_AUTO, 250, 134, 250, 134,
	WC_VEHICLE_VIEW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_vehicle_view_widgets
);


/* Just to make sure, nobody has changed the vehicle type constants, as we are
	 using them for array indexing in a number of places here. */
assert_compile(VEH_TRAIN == 0);
assert_compile(VEH_ROAD == 1);
assert_compile(VEH_SHIP == 2);
assert_compile(VEH_AIRCRAFT == 3);

/** Zoom levels for vehicle views indexed by vehicle type. */
static const ZoomLevel _vehicle_view_zoom_levels[] = {
	ZOOM_LVL_TRAIN,
	ZOOM_LVL_ROADVEH,
	ZOOM_LVL_SHIP,
	ZOOM_LVL_AIRCRAFT,
};

/* Constants for geometry of vehicle view viewport */
static const int VV_VIEWPORT_X = 3;
static const int VV_VIEWPORT_Y = 17;
static const int VV_INITIAL_VIEWPORT_WIDTH = 226;
static const int VV_INITIAL_VIEWPORT_HEIGHT = 84;
static const int VV_INITIAL_VIEWPORT_HEIGHT_TRAIN = 102;

/** Command indices for the _vehicle_command_translation_table. */
enum VehicleCommandTranslation {
	VCT_CMD_START_STOP = 0,
	VCT_CMD_GOTO_DEPOT,
	VCT_CMD_CLONE_VEH,
	VCT_CMD_TURN_AROUND,
};

/** Command codes for the shared buttons indexed by VehicleCommandTranslation and vehicle type. */
static const uint32 _vehicle_command_translation_table[][4] = {
	{ // VCT_CMD_START_STOP
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_883B_CAN_T_STOP_START_TRAIN),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_9015_CAN_T_STOP_START_ROAD_VEHICLE),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_9818_CAN_T_STOP_START_SHIP),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_A016_CAN_T_STOP_START_AIRCRAFT)
	},
	{ // VCT_CMD_GOTO_DEPOT
		/* TrainGotoDepot has a nice randomizer in the pathfinder, which causes desyncs... */
		CMD_SEND_TRAIN_TO_DEPOT | CMD_NO_TEST_IF_IN_NETWORK | CMD_MSG(STR_8830_CAN_T_SEND_TRAIN_TO_DEPOT),
		CMD_SEND_ROADVEH_TO_DEPOT | CMD_MSG(STR_9018_CAN_T_SEND_VEHICLE_TO_DEPOT),
		CMD_SEND_SHIP_TO_DEPOT | CMD_MSG(STR_9819_CAN_T_SEND_SHIP_TO_DEPOT),
		CMD_SEND_AIRCRAFT_TO_HANGAR | CMD_MSG(STR_A012_CAN_T_SEND_AIRCRAFT_TO)
	},
	{ // VCT_CMD_CLONE_VEH
		CMD_CLONE_VEHICLE | CMD_MSG(STR_882B_CAN_T_BUILD_RAILROAD_VEHICLE),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_9009_CAN_T_BUILD_ROAD_VEHICLE),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_980D_CAN_T_BUILD_SHIP),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_A008_CAN_T_BUILD_AIRCRAFT)
	},
	{ // VCT_CMD_TURN_AROUND
		CMD_REVERSE_TRAIN_DIRECTION | CMD_MSG(STR_8869_CAN_T_REVERSE_DIRECTION),
		CMD_TURN_ROADVEH | CMD_MSG(STR_9033_CAN_T_MAKE_VEHICLE_TURN),
		0xffffffff, // invalid for ships
		0xffffffff  // invalid for aircrafts
	},
};

/** Checks whether the vehicle may be refitted at the moment.*/
static bool IsVehicleRefitable(const Vehicle *v)
{
	if (!v->IsStoppedInDepot()) return false;

	do {
		if (IsEngineRefittable(v->engine_type)) return true;
	} while ((v->type == VEH_TRAIN || v->type == VEH_ROAD) && (v = v->Next()) != NULL);

	return false;
}

struct VehicleViewWindow : Window {
	VehicleViewWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		const Vehicle *v = GetVehicle(this->window_number);

		this->owner = v->owner;
		InitializeWindowViewport(this, VV_VIEWPORT_X, VV_VIEWPORT_Y, VV_INITIAL_VIEWPORT_WIDTH,
												 (v->type == VEH_TRAIN) ? VV_INITIAL_VIEWPORT_HEIGHT_TRAIN : VV_INITIAL_VIEWPORT_HEIGHT,
												 this->window_number | (1 << 31), _vehicle_view_zoom_levels[v->type]);

		/*
		 * fill in data and tooltip codes for the widgets and
		 * move some of the buttons for trains
		 */
		switch (v->type) {
			case VEH_TRAIN:
				this->widget[VVW_WIDGET_CAPTION].data = STR_882E;

				this->widget[VVW_WIDGET_START_STOP_VEH].tooltips = STR_8846_CURRENT_TRAIN_ACTION_CLICK;

				this->widget[VVW_WIDGET_CENTER_MAIN_VIEH].tooltips = STR_8848_CENTER_MAIN_VIEW_ON_TRAIN;

				this->widget[VVW_WIDGET_GOTO_DEPOT].data = SPR_SEND_TRAIN_TODEPOT;
				this->widget[VVW_WIDGET_GOTO_DEPOT].tooltips = STR_8849_SEND_TRAIN_TO_DEPOT;

				this->widget[VVW_WIDGET_REFIT_VEH].tooltips = STR_RAIL_REFIT_VEHICLE_TO_CARRY;

				this->widget[VVW_WIDGET_SHOW_ORDERS].tooltips = STR_8847_SHOW_TRAIN_S_ORDERS;

				this->widget[VVW_WIDGET_SHOW_DETAILS].tooltips = STR_884C_SHOW_TRAIN_DETAILS;

				this->widget[VVW_WIDGET_CLONE_VEH].data = SPR_CLONE_TRAIN;
				this->widget[VVW_WIDGET_CLONE_VEH].tooltips = STR_CLONE_TRAIN_INFO;

				this->widget[VVW_WIDGET_TURN_AROUND].tooltips = STR_884B_REVERSE_DIRECTION_OF_TRAIN;


				/* due to more buttons we must modify the layout a bit for trains */
				this->widget[VVW_WIDGET_PANEL].bottom = 121;
				this->widget[VVW_WIDGET_VIEWPORT].bottom = 119;

				this->widget[VVW_WIDGET_START_STOP_VEH].top = 122;
				this->widget[VVW_WIDGET_START_STOP_VEH].bottom = 133;

				this->widget[VVW_WIDGET_REFIT_VEH].top = 68;
				this->widget[VVW_WIDGET_REFIT_VEH].bottom = 85;

				this->widget[VVW_WIDGET_SHOW_ORDERS].top = 86;
				this->widget[VVW_WIDGET_SHOW_ORDERS].bottom = 103;

				this->widget[VVW_WIDGET_SHOW_DETAILS].top = 104;
				this->widget[VVW_WIDGET_SHOW_DETAILS].bottom = 121;

				this->widget[VVW_WIDGET_EMPTY_BOTTOM_RIGHT].top = 122;
				this->widget[VVW_WIDGET_EMPTY_BOTTOM_RIGHT].bottom = 121;

				this->widget[VVW_WIDGET_RESIZE].top = 122;
				this->widget[VVW_WIDGET_RESIZE].bottom = 133;

				this->widget[VVW_WIDGET_TURN_AROUND].top = 68;
				this->widget[VVW_WIDGET_TURN_AROUND].bottom = 85;
				break;

			case VEH_ROAD:
				this->widget[VVW_WIDGET_CAPTION].data = STR_9002;

				this->widget[VVW_WIDGET_START_STOP_VEH].tooltips = STR_901C_CURRENT_VEHICLE_ACTION;

				this->widget[VVW_WIDGET_CENTER_MAIN_VIEH].tooltips = STR_901E_CENTER_MAIN_VIEW_ON_VEHICLE;

				this->widget[VVW_WIDGET_GOTO_DEPOT].data = SPR_SEND_ROADVEH_TODEPOT;
				this->widget[VVW_WIDGET_GOTO_DEPOT].tooltips = STR_901F_SEND_VEHICLE_TO_DEPOT;

				this->widget[VVW_WIDGET_REFIT_VEH].tooltips = STR_REFIT_ROAD_VEHICLE_TO_CARRY;

				this->widget[VVW_WIDGET_SHOW_ORDERS].tooltips = STR_901D_SHOW_VEHICLE_S_ORDERS;

				this->widget[VVW_WIDGET_SHOW_DETAILS].tooltips = STR_9021_SHOW_ROAD_VEHICLE_DETAILS;

				this->widget[VVW_WIDGET_CLONE_VEH].data = SPR_CLONE_ROADVEH;
				this->widget[VVW_WIDGET_CLONE_VEH].tooltips = STR_CLONE_ROAD_VEHICLE_INFO;

				this->SetWidgetHiddenState(VVW_WIDGET_FORCE_PROCEED, true);
				break;

			case VEH_SHIP:
				this->widget[VVW_WIDGET_CAPTION].data = STR_980F;

				this->widget[VVW_WIDGET_START_STOP_VEH].tooltips = STR_9827_CURRENT_SHIP_ACTION_CLICK;

				this->widget[VVW_WIDGET_CENTER_MAIN_VIEH].tooltips = STR_9829_CENTER_MAIN_VIEW_ON_SHIP;

				this->widget[VVW_WIDGET_GOTO_DEPOT].data = SPR_SEND_SHIP_TODEPOT;
				this->widget[VVW_WIDGET_GOTO_DEPOT].tooltips = STR_982A_SEND_SHIP_TO_DEPOT;

				this->widget[VVW_WIDGET_REFIT_VEH].tooltips = STR_983A_REFIT_CARGO_SHIP_TO_CARRY;

				this->widget[VVW_WIDGET_SHOW_ORDERS].tooltips = STR_9828_SHOW_SHIP_S_ORDERS;

				this->widget[VVW_WIDGET_SHOW_DETAILS].tooltips = STR_982B_SHOW_SHIP_DETAILS;

				this->widget[VVW_WIDGET_CLONE_VEH].data = SPR_CLONE_SHIP;
				this->widget[VVW_WIDGET_CLONE_VEH].tooltips = STR_CLONE_SHIP_INFO;

				this->SetWidgetsHiddenState(true,
																		VVW_WIDGET_TURN_AROUND,
																		VVW_WIDGET_FORCE_PROCEED,
																		WIDGET_LIST_END);
				break;

			case VEH_AIRCRAFT:
				this->widget[VVW_WIDGET_CAPTION].data = STR_A00A;

				this->widget[VVW_WIDGET_START_STOP_VEH].tooltips = STR_A027_CURRENT_AIRCRAFT_ACTION;

				this->widget[VVW_WIDGET_CENTER_MAIN_VIEH].tooltips = STR_A029_CENTER_MAIN_VIEW_ON_AIRCRAFT;

				this->widget[VVW_WIDGET_GOTO_DEPOT].data = SPR_SEND_AIRCRAFT_TODEPOT;
				this->widget[VVW_WIDGET_GOTO_DEPOT].tooltips = STR_A02A_SEND_AIRCRAFT_TO_HANGAR;

				this->widget[VVW_WIDGET_REFIT_VEH].tooltips = STR_A03B_REFIT_AIRCRAFT_TO_CARRY;

				this->widget[VVW_WIDGET_SHOW_ORDERS].tooltips = STR_A028_SHOW_AIRCRAFT_S_ORDERS;

				this->widget[VVW_WIDGET_SHOW_DETAILS].tooltips = STR_A02B_SHOW_AIRCRAFT_DETAILS;

				this->widget[VVW_WIDGET_CLONE_VEH].data = SPR_CLONE_AIRCRAFT;
				this->widget[VVW_WIDGET_CLONE_VEH].tooltips = STR_CLONE_AIRCRAFT_INFO;

				this->SetWidgetsHiddenState(true,
																		VVW_WIDGET_TURN_AROUND,
																		VVW_WIDGET_FORCE_PROCEED,
																		WIDGET_LIST_END);
				break;

				default: NOT_REACHED();
		}

		this->FindWindowPlacementAndResize(desc);
	}

	~VehicleViewWindow()
	{
		DeleteWindowById(WC_VEHICLE_ORDERS, this->window_number, false);
		DeleteWindowById(WC_VEHICLE_REFIT, this->window_number, false);
		DeleteWindowById(WC_VEHICLE_DETAILS, this->window_number, false);
		DeleteWindowById(WC_VEHICLE_TIMETABLE, this->window_number, false);
	}

	virtual void OnPaint()
	{
		/** Message strings for heading to depot indexed by vehicle type. */
		static const StringID _heading_for_depot_strings[] = {
			STR_HEADING_FOR_TRAIN_DEPOT,
			STR_HEADING_FOR_ROAD_DEPOT,
			STR_HEADING_FOR_SHIP_DEPOT,
			STR_HEADING_FOR_HANGAR,
		};

		/** Message strings for heading to depot and servicing indexed by vehicle type. */
		static const StringID _heading_for_depot_service_strings[] = {
			STR_HEADING_FOR_TRAIN_DEPOT_SERVICE,
			STR_HEADING_FOR_ROAD_DEPOT_SERVICE,
			STR_HEADING_FOR_SHIP_DEPOT_SERVICE,
			STR_HEADING_FOR_HANGAR_SERVICE,
		};

		const Vehicle *v = GetVehicle(this->window_number);
		StringID str;
		bool is_localcompany = v->owner == _local_company;
		bool refitable_and_stopped_in_depot = IsVehicleRefitable(v);

		this->SetWidgetDisabledState(VVW_WIDGET_GOTO_DEPOT, !is_localcompany);
		this->SetWidgetDisabledState(VVW_WIDGET_REFIT_VEH,
																!refitable_and_stopped_in_depot || !is_localcompany);
		this->SetWidgetDisabledState(VVW_WIDGET_CLONE_VEH, !is_localcompany);

		if (v->type == VEH_TRAIN) {
			this->SetWidgetDisabledState(VVW_WIDGET_FORCE_PROCEED, !is_localcompany);
			this->SetWidgetDisabledState(VVW_WIDGET_TURN_AROUND, !is_localcompany);
		}

		/* draw widgets & caption */
		SetDParam(0, v->index);
		this->DrawWidgets();

		if (v->vehstatus & VS_CRASHED) {
			str = STR_8863_CRASHED;
		} else if (v->type != VEH_AIRCRAFT && v->breakdown_ctr == 1) { // check for aircraft necessary?
			str = STR_885C_BROKEN_DOWN;
		} else if (v->vehstatus & VS_STOPPED) {
			if (v->type == VEH_TRAIN) {
				if (v->cur_speed == 0) {
					if (v->u.rail.cached_power == 0) {
						str = STR_TRAIN_NO_POWER;
					} else {
						str = STR_8861_STOPPED;
					}
				} else {
					SetDParam(0, v->GetDisplaySpeed());
					str = STR_TRAIN_STOPPING + _settings_client.gui.vehicle_speed;
				}
			} else { // no train
				str = STR_8861_STOPPED;
			}
		} else if (v->type == VEH_TRAIN && HasBit(v->u.rail.flags, VRF_TRAIN_STUCK)) {
			str = STR_TRAIN_STUCK;
		} else { // vehicle is in a "normal" state, show current order
			switch (v->current_order.GetType()) {
				case OT_GOTO_STATION: {
					SetDParam(0, v->current_order.GetDestination());
					SetDParam(1, v->GetDisplaySpeed());
					str = STR_HEADING_FOR_STATION + _settings_client.gui.vehicle_speed;
				} break;

				case OT_GOTO_DEPOT: {
					if (v->type == VEH_AIRCRAFT) {
						/* Aircrafts always go to a station, even if you say depot */
						SetDParam(0, v->current_order.GetDestination());
						SetDParam(1, v->GetDisplaySpeed());
					} else {
						Depot *depot = GetDepot(v->current_order.GetDestination());
						SetDParam(0, depot->town_index);
						SetDParam(1, v->GetDisplaySpeed());
					}
					if (v->current_order.GetDepotActionType() & ODATFB_HALT) {
						str = _heading_for_depot_strings[v->type] + _settings_client.gui.vehicle_speed;
					} else {
						str = _heading_for_depot_service_strings[v->type] + _settings_client.gui.vehicle_speed;
					}
				} break;

				case OT_LOADING:
					str = STR_882F_LOADING_UNLOADING;
					break;

				case OT_GOTO_WAYPOINT: {
					assert(v->type == VEH_TRAIN);
					SetDParam(0, v->current_order.GetDestination());
					str = STR_HEADING_FOR_WAYPOINT + _settings_client.gui.vehicle_speed;
					SetDParam(1, v->GetDisplaySpeed());
					break;
				}

				case OT_LEAVESTATION:
					if (v->type != VEH_AIRCRAFT) {
						str = STR_LEAVING;
						break;
					}
					/* fall-through if aircraft. Does this even happen? */

				default:
					if (v->GetNumOrders() == 0) {
						str = STR_NO_ORDERS + _settings_client.gui.vehicle_speed;
						SetDParam(0, v->GetDisplaySpeed());
					} else {
						str = STR_EMPTY;
					}
					break;
			}
		}

		/* draw the flag plus orders */
		DrawSprite(v->vehstatus & VS_STOPPED ? SPR_FLAG_VEH_STOPPED : SPR_FLAG_VEH_RUNNING, PAL_NONE, 2, this->widget[VVW_WIDGET_START_STOP_VEH].top + 1);
		DrawStringCenteredTruncated(this->widget[VVW_WIDGET_START_STOP_VEH].left + 8, this->widget[VVW_WIDGET_START_STOP_VEH].right, this->widget[VVW_WIDGET_START_STOP_VEH].top + 1, str, TC_FROMSTRING);
		this->DrawViewport();
	}

	virtual void OnClick(Point pt, int widget)
	{
		const Vehicle *v = GetVehicle(this->window_number);

		switch (widget) {
			case VVW_WIDGET_START_STOP_VEH: // start stop
				DoCommandP(v->tile, v->index, 0,
										_vehicle_command_translation_table[VCT_CMD_START_STOP][v->type]);
				break;
			case VVW_WIDGET_CENTER_MAIN_VIEH: {// center main view
				const Window *mainwindow = FindWindowById(WC_MAIN_WINDOW, 0);
				/* code to allow the main window to 'follow' the vehicle if the ctrl key is pressed */
				if (_ctrl_pressed && mainwindow->viewport->zoom == ZOOM_LVL_NORMAL) {
					mainwindow->viewport->follow_vehicle = v->index;
				} else {
					ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos);
				}
			} break;

			case VVW_WIDGET_GOTO_DEPOT: // goto hangar
				DoCommandP(v->tile, v->index, _ctrl_pressed ? DEPOT_SERVICE : 0,
					_vehicle_command_translation_table[VCT_CMD_GOTO_DEPOT][v->type]);
				break;
			case VVW_WIDGET_REFIT_VEH: // refit
				ShowVehicleRefitWindow(v, INVALID_VEH_ORDER_ID, this);
				break;
			case VVW_WIDGET_SHOW_ORDERS: // show orders
				if (_ctrl_pressed) {
					ShowTimetableWindow(v);
				} else {
					ShowOrdersWindow(v);
				}
				break;
			case VVW_WIDGET_SHOW_DETAILS: // show details
				ShowVehicleDetailsWindow(v);
				break;
			case VVW_WIDGET_CLONE_VEH: // clone vehicle
				DoCommandP(v->tile, v->index, _ctrl_pressed ? 1 : 0,
										_vehicle_command_translation_table[VCT_CMD_CLONE_VEH][v->type],
										CcCloneVehicle);
				break;
			case VVW_WIDGET_TURN_AROUND: // turn around
				assert(v->type == VEH_TRAIN || v->type == VEH_ROAD);
				DoCommandP(v->tile, v->index, 0,
										_vehicle_command_translation_table[VCT_CMD_TURN_AROUND][v->type]);
				break;
			case VVW_WIDGET_FORCE_PROCEED: // force proceed
				assert(v->type == VEH_TRAIN);
				DoCommandP(v->tile, v->index, 0, CMD_FORCE_TRAIN_PROCEED | CMD_MSG(STR_8862_CAN_T_MAKE_TRAIN_PASS_SIGNAL));
				break;
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		this->viewport->width          += delta.x;
		this->viewport->height         += delta.y;
		this->viewport->virtual_width  += delta.x;
		this->viewport->virtual_height += delta.y;
	}

	virtual void OnTick()
	{
		const Vehicle *v = GetVehicle(this->window_number);
		bool veh_stopped = v->IsStoppedInDepot();

		/* Widget VVW_WIDGET_GOTO_DEPOT must be hidden if the vehicle is already
		 * stopped in depot.
		 * Widget VVW_WIDGET_CLONE_VEH should then be shown, since cloning is
		 * allowed only while in depot and stopped.
		 * This sytem allows to have two buttons, on top of each other.
		 * The same system applies to widget VVW_WIDGET_REFIT_VEH and VVW_WIDGET_TURN_AROUND.*/
		if (veh_stopped != this->IsWidgetHidden(VVW_WIDGET_GOTO_DEPOT) || veh_stopped == this->IsWidgetHidden(VVW_WIDGET_CLONE_VEH)) {
			this->SetWidgetHiddenState( VVW_WIDGET_GOTO_DEPOT, veh_stopped);  // send to depot
			this->SetWidgetHiddenState(VVW_WIDGET_CLONE_VEH, !veh_stopped); // clone
			if (v->type == VEH_ROAD || v->type == VEH_TRAIN) {
				this->SetWidgetHiddenState( VVW_WIDGET_REFIT_VEH, !veh_stopped); // refit
				this->SetWidgetHiddenState(VVW_WIDGET_TURN_AROUND, veh_stopped);  // force turn around
			}
			this->SetDirty();
		}
	}
};


/** Shows the vehicle view window of the given vehicle. */
void ShowVehicleViewWindow(const Vehicle *v)
{
	AllocateWindowDescFront<VehicleViewWindow>((v->type == VEH_TRAIN) ? &_train_view_desc : &_vehicle_view_desc, v->index);
}

void StopGlobalFollowVehicle(const Vehicle *v)
{
	Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
	if (w != NULL && w->viewport->follow_vehicle == v->index) {
		ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos, true); // lock the main view on the vehicle's last position
		w->viewport->follow_vehicle = INVALID_VEHICLE;
	}
}
