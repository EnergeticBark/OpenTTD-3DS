/* $Id$ */

/** @file engine_gui.cpp GUI to show engine related information. */

#include "stdafx.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "engine_func.h"
#include "engine_base.h"
#include "command_func.h"
#include "news_type.h"
#include "newgrf_engine.h"
#include "strings_func.h"
#include "engine_gui.h"
#include "articulated_vehicles.h"
#include "rail.h"

#include "table/strings.h"
#include "table/sprites.h"

StringID GetEngineCategoryName(EngineID engine)
{
	switch (GetEngine(engine)->type) {
		default: NOT_REACHED();
		case VEH_ROAD:              return STR_8103_ROAD_VEHICLE;
		case VEH_AIRCRAFT:          return STR_8104_AIRCRAFT;
		case VEH_SHIP:              return STR_8105_SHIP;
		case VEH_TRAIN:
			return GetRailTypeInfo(RailVehInfo(engine)->railtype)->strings.new_loco;
	}
}

static const Widget _engine_preview_widgets[] = {
{   WWT_CLOSEBOX,  RESIZE_NONE,  COLOUR_LIGHT_BLUE,    0,   10,    0,   13, STR_00C5,                                  STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,  RESIZE_NONE,  COLOUR_LIGHT_BLUE,   11,  299,    0,   13, STR_8100_MESSAGE_FROM_VEHICLE_MANUFACTURE, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,  RESIZE_NONE,  COLOUR_LIGHT_BLUE,    0,  299,   14,  191, 0x0,                                       STR_NULL},
{ WWT_PUSHTXTBTN,  RESIZE_NONE,  COLOUR_LIGHT_BLUE,   85,  144,  172,  183, STR_00C9_NO,                               STR_NULL},
{ WWT_PUSHTXTBTN,  RESIZE_NONE,  COLOUR_LIGHT_BLUE,  155,  214,  172,  183, STR_00C8_YES,                              STR_NULL},
{   WIDGETS_END},
};

typedef void DrawEngineProc(int x, int y, EngineID engine, SpriteID pal);
typedef void DrawEngineInfoProc(EngineID, int x, int y, int maxw);

struct DrawEngineInfo {
	DrawEngineProc *engine_proc;
	DrawEngineInfoProc *info_proc;
};

static void DrawTrainEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawRoadVehEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawShipEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawAircraftEngineInfo(EngineID engine, int x, int y, int maxw);

static const DrawEngineInfo _draw_engine_list[4] = {
	{ DrawTrainEngine,    DrawTrainEngineInfo    },
	{ DrawRoadVehEngine,  DrawRoadVehEngineInfo  },
	{ DrawShipEngine,     DrawShipEngineInfo     },
	{ DrawAircraftEngine, DrawAircraftEngineInfo },
};

struct EnginePreviewWindow : Window {
	EnginePreviewWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		EngineID engine = this->window_number;
		SetDParam(0, GetEngineCategoryName(engine));
		DrawStringMultiCenter(150, 44, STR_8101_WE_HAVE_JUST_DESIGNED_A, 296);

		SetDParam(0, engine);
		DrawStringCentered(this->width >> 1, 80, STR_ENGINE_NAME, TC_BLACK);

		const DrawEngineInfo *dei = &_draw_engine_list[GetEngine(engine)->type];

		int width = this->width;
		dei->engine_proc(width >> 1, 100, engine, 0);
		dei->info_proc(engine, width >> 1, 130, width - 52);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case 4:
				DoCommandP(0, this->window_number, 0, CMD_WANT_ENGINE_PREVIEW);
				/* Fallthrough */
			case 3:
				delete this;
				break;
		}
	}
};

static const WindowDesc _engine_preview_desc(
	WDP_CENTER, WDP_CENTER, 300, 192, 300, 192,
	WC_ENGINE_PREVIEW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_CONSTRUCTION,
	_engine_preview_widgets
);


void ShowEnginePreviewWindow(EngineID engine)
{
	AllocateWindowDescFront<EnginePreviewWindow>(&_engine_preview_desc, engine);
}

uint GetTotalCapacityOfArticulatedParts(EngineID engine, VehicleType type)
{
	uint total = 0;

	uint16 *cap = GetCapacityOfArticulatedParts(engine, type);
	for (uint c = 0; c < NUM_CARGO; c++) {
		total += cap[c];
	}

	return total;
}

static void DrawTrainEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const Engine *e = GetEngine(engine);

	SetDParam(0, e->GetCost());
	SetDParam(2, e->GetDisplayMaxSpeed());
	SetDParam(3, e->GetPower());
	SetDParam(1, e->GetDisplayWeight());

	SetDParam(4, e->GetRunningCost());

	uint capacity = GetTotalCapacityOfArticulatedParts(engine, VEH_TRAIN);
	if (capacity != 0) {
		SetDParam(5, e->GetDefaultCargoType());
		SetDParam(6, capacity);
	} else {
		SetDParam(5, CT_INVALID);
	}
	DrawStringMultiCenter(x, y, STR_VEHICLE_INFO_COST_WEIGHT_SPEED_POWER, maxw);
}

static void DrawAircraftEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const Engine *e = GetEngine(engine);
	CargoID cargo = e->GetDefaultCargoType();

	if (cargo == CT_INVALID || cargo == CT_PASSENGERS) {
		SetDParam(0, e->GetCost());
		SetDParam(1, e->GetDisplayMaxSpeed());
		SetDParam(2, e->GetDisplayDefaultCapacity());
		SetDParam(3, e->u.air.mail_capacity);
		SetDParam(4, e->GetRunningCost());

		DrawStringMultiCenter(x, y, STR_A02E_COST_MAX_SPEED_CAPACITY, maxw);
	} else {
		SetDParam(0, e->GetCost());
		SetDParam(1, e->GetDisplayMaxSpeed());
		SetDParam(2, cargo);
		SetDParam(3, e->GetDisplayDefaultCapacity());
		SetDParam(4, e->GetRunningCost());

		DrawStringMultiCenter(x, y, STR_982E_COST_MAX_SPEED_CAPACITY, maxw);
	}
}

static void DrawRoadVehEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const Engine *e = GetEngine(engine);

	SetDParam(0, e->GetCost());
	SetDParam(1, e->GetDisplayMaxSpeed());
	SetDParam(2, e->GetRunningCost());
	uint capacity = GetTotalCapacityOfArticulatedParts(engine, VEH_ROAD);
	if (capacity != 0) {
		SetDParam(3, e->GetDefaultCargoType());
		SetDParam(4, capacity);
	} else {
		SetDParam(3, CT_INVALID);
	}

	DrawStringMultiCenter(x, y, STR_902A_COST_SPEED_RUNNING_COST, maxw);
}

static void DrawShipEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const Engine *e = GetEngine(engine);

	SetDParam(0, e->GetCost());
	SetDParam(1, e->GetDisplayMaxSpeed());
	SetDParam(2, e->GetDefaultCargoType());
	SetDParam(3, e->GetDisplayDefaultCapacity());
	SetDParam(4, e->GetRunningCost());
	DrawStringMultiCenter(x, y, STR_982E_COST_MAX_SPEED_CAPACITY, maxw);
}

void DrawNewsNewVehicleAvail(Window *w, const NewsItem *ni)
{
	EngineID engine = ni->data_a;
	const DrawEngineInfo *dei = &_draw_engine_list[GetEngine(engine)->type];

	SetDParam(0, GetEngineCategoryName(engine));
	DrawStringMultiCenter(w->width >> 1, 20, STR_NEW_VEHICLE_NOW_AVAILABLE, w->width - 2);

	GfxFillRect(25, 56, w->width - 25, w->height - 2, 10);

	SetDParam(0, engine);
	DrawStringMultiCenter(w->width >> 1, 57, STR_NEW_VEHICLE_TYPE, w->width - 2);

	dei->engine_proc(w->width >> 1, 88, engine, 0);
	GfxFillRect(25, 56, w->width - 56, 112, PALETTE_TO_STRUCT_GREY, FILLRECT_RECOLOUR);
	dei->info_proc(engine, w->width >> 1, 129, w->width - 52);
}


/** Sort all items using qsort() and given 'CompareItems' function
 * @param el list to be sorted
 * @param compare function for evaluation of the quicksort
 */
void EngList_Sort(GUIEngineList *el, EngList_SortTypeFunction compare)
{
	uint size = el->Length();
	/* out-of-bounds access at the next line for size == 0 (even with operator[] at some systems)
	 * generally, do not sort if there are less than 2 items */
	if (size < 2) return;
	qsort(el->Begin(), size, sizeof(*el->Begin()), compare); // MorphOS doesn't know vector::at(int) ...
}

/** Sort selected range of items (on indices @ <begin, begin+num_items-1>)
 * @param el list to be sorted
 * @param compare function for evaluation of the quicksort
 * @param begin start of sorting
 * @param num_items count of items to be sorted
 */
void EngList_SortPartial(GUIEngineList *el, EngList_SortTypeFunction compare, uint begin, uint num_items)
{
	if (num_items < 2) return;
	assert(begin < el->Length());
	assert(begin + num_items <= el->Length());
	qsort(el->Get(begin), num_items, sizeof(*el->Begin()), compare);
}

