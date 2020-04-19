/* $Id$ */

/** @file roadveh_gui.cpp GUI for road vehicles. */

#include "stdafx.h"
#include "roadveh.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "vehicle_gui.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "string_func.h"

#include "table/sprites.h"
#include "table/strings.h"

void DrawRoadVehDetails(const Vehicle *v, int x, int y)
{
	uint y_offset = RoadVehHasArticPart(v) ? 15 : 0;
	StringID str;
	Money feeder_share = 0;

	SetDParam(0, v->engine_type);
	SetDParam(1, v->build_year);
	SetDParam(2, v->value);
	DrawString(x, y + y_offset, STR_9011_BUILT_VALUE, TC_FROMSTRING);

	if (RoadVehHasArticPart(v)) {
		AcceptedCargo max_cargo;
		StringID subtype_text[NUM_CARGO];
		char capacity[512];

		memset(max_cargo, 0, sizeof(max_cargo));
		memset(subtype_text, 0, sizeof(subtype_text));

		for (const Vehicle *u = v; u != NULL; u = u->Next()) {
			max_cargo[u->cargo_type] += u->cargo_cap;
			if (u->cargo_cap > 0) {
				StringID text = GetCargoSubtypeText(u);
				if (text != STR_EMPTY) subtype_text[u->cargo_type] = text;
			}
		}

		GetString(capacity, STR_ARTICULATED_RV_CAPACITY, lastof(capacity));

		bool first = true;
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (max_cargo[i] > 0) {
				char buffer[128];

				SetDParam(0, i);
				SetDParam(1, max_cargo[i]);
				GetString(buffer, STR_BARE_CARGO, lastof(buffer));

				if (!first) strecat(capacity, ", ", lastof(capacity));
				strecat(capacity, buffer, lastof(capacity));

				if (subtype_text[i] != 0) {
					GetString(buffer, subtype_text[i], lastof(buffer));
					strecat(capacity, buffer, lastof(capacity));
				}

				first = false;
			}
		}

		SetDParamStr(0, capacity);
		DrawStringTruncated(x, y + 10 + y_offset, STR_JUST_RAW_STRING, TC_BLUE, 380 - x);

		for (const Vehicle *u = v; u != NULL; u = u->Next()) {
			if (u->cargo_cap == 0) continue;

			str = STR_8812_EMPTY;
			if (!u->cargo.Empty()) {
				SetDParam(0, u->cargo_type);
				SetDParam(1, u->cargo.Count());
				SetDParam(2, u->cargo.Source());
				str = STR_8813_FROM;
				feeder_share += u->cargo.FeederShare();
			}
			DrawString(x, y + 21 + y_offset, str, TC_FROMSTRING);

			y_offset += 11;
		}

		y_offset -= 11;
	} else {
		SetDParam(0, v->cargo_type);
		SetDParam(1, v->cargo_cap);
		SetDParam(2, GetCargoSubtypeText(v));
		DrawString(x, y + 10 + y_offset, STR_9012_CAPACITY, TC_FROMSTRING);

		str = STR_8812_EMPTY;
		if (!v->cargo.Empty()) {
			SetDParam(0, v->cargo_type);
			SetDParam(1, v->cargo.Count());
			SetDParam(2, v->cargo.Source());
			str = STR_8813_FROM;
			feeder_share += v->cargo.FeederShare();
		}
		DrawString(x, y + 21 + y_offset, str, TC_FROMSTRING);
	}

	/* Draw Transfer credits text */
	SetDParam(0, feeder_share);
	DrawString(x, y + 33 + y_offset, STR_FEEDER_CARGO_VALUE, TC_FROMSTRING);
}


static inline int RoadVehLengthToPixels(int length)
{
	return (length * 28) / 8;
}

void DrawRoadVehImage(const Vehicle *v, int x, int y, VehicleID selection, int count)
{
	/* Road vehicle lengths are measured in eighths of the standard length, so
	 * count is the number of standard vehicles that should be drawn. If it is
	 * 0, we draw enough vehicles for 10 standard vehicle lengths. */
	int max_length = (count == 0) ? 80 : count * 8;

	/* Width of highlight box */
	int highlight_w = 0;

	for (int dx = 0; v != NULL && dx < max_length ; v = v->Next()) {
		int width = v->u.road.cached_veh_length;

		if (dx + width > 0 && dx <= max_length) {
			SpriteID pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);
			DrawSprite(v->GetImage(DIR_W), pal, x + 14 + RoadVehLengthToPixels(dx), y + 6);

			if (v->index == selection) {
				/* Set the highlight position */
				highlight_w = RoadVehLengthToPixels(width);
			} else if (_cursor.vehchain && highlight_w != 0) {
				highlight_w += RoadVehLengthToPixels(width);
			}
		}

		dx += width;
	}

	if (highlight_w != 0) {
		DrawFrameRect(x - 1, y - 1, x - 1 + highlight_w, y + 12, COLOUR_WHITE, FR_BORDERONLY);
	}
}

void CcBuildRoadVeh(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	const Vehicle *v;

	if (!success) return;

	v = GetVehicle(_new_vehicle_id);
	if (v->tile == _backup_orders_tile) {
		_backup_orders_tile = 0;
		RestoreVehicleOrders(v);
	}
	ShowVehicleViewWindow(v);
}
