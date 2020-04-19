/* $Id$ */

/** @file road_gui.cpp GUI for building roads. */

#include "stdafx.h"
#include "openttd.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "command_func.h"
#include "road_type.h"
#include "road_cmd.h"
#include "road_map.h"
#include "station_func.h"
#include "functions.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "tunnelbridge.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "settings_type.h"

#include "table/sprites.h"
#include "table/strings.h"

static void ShowRVStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);

static bool _remove_button_clicked;
static bool _one_way_button_clicked;

/**
 * Define the values of the RoadFlags
 * @see CmdBuildLongRoad
 */
enum RoadFlags {
	RF_NONE             = 0x00,
	RF_START_HALFROAD_Y = 0x01,    // The start tile in Y-dir should have only a half road
	RF_END_HALFROAD_Y   = 0x02,    // The end tile in Y-dir should have only a half road
	RF_DIR_Y            = 0x04,    // The direction is Y-dir
	RF_DIR_X            = RF_NONE, // Dummy; Dir X is set when RF_DIR_Y is not set
	RF_START_HALFROAD_X = 0x08,    // The start tile in X-dir should have only a half road
	RF_END_HALFROAD_X   = 0x10,    // The end tile in X-dir should have only a half road
};
DECLARE_ENUM_AS_BIT_SET(RoadFlags);

static RoadFlags _place_road_flag;

static RoadType _cur_roadtype;

static DiagDirection _road_depot_orientation;
static DiagDirection _road_station_picker_orientation;

void CcPlaySound1D(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) SndPlayTileFx(SND_1F_SPLAT, tile);
}

/**
 * Set the initial flags for the road constuction.
 * The flags are:
 * @li The direction is the X-dir
 * @li The first tile has a partitial RoadBit (true or false)
 *
 * @param tile The start tile
 */
static void PlaceRoad_X_Dir(TileIndex tile)
{
	_place_road_flag = RF_DIR_X;
	if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
	VpStartPlaceSizing(tile, VPM_FIX_Y, DDSP_PLACE_ROAD_X_DIR);
}

/**
 * Set the initial flags for the road constuction.
 * The flags are:
 * @li The direction is the Y-dir
 * @li The first tile has a partitial RoadBit (true or false)
 *
 * @param tile The start tile
 */
static void PlaceRoad_Y_Dir(TileIndex tile)
{
	_place_road_flag = RF_DIR_Y;
	if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
	VpStartPlaceSizing(tile, VPM_FIX_X, DDSP_PLACE_ROAD_Y_DIR);
}

/**
 * Set the initial flags for the road constuction.
 * The flags are:
 * @li The direction is not set.
 * @li The first tile has a partitial RoadBit (true or false)
 *
 * @param tile The start tile
 */
static void PlaceRoad_AutoRoad(TileIndex tile)
{
	_place_road_flag = RF_NONE;
	if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
	if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
	VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_AUTOROAD);
}

static void PlaceRoad_Bridge(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
}


void CcBuildRoadTunnel(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		SndPlayTileFx(SND_20_SPLAT_2, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/** Structure holding information per roadtype for several functions */
struct RoadTypeInfo {
	StringID err_build_road;        ///< Building a normal piece of road
	StringID err_remove_road;       ///< Removing a normal piece of road
	StringID err_depot;             ///< Building a depot
	StringID err_build_station[2];  ///< Building a bus or truck station
	StringID err_remove_station[2]; ///< Removing of a bus or truck station

	StringID picker_title[2];       ///< Title for the station picker for bus or truck stations
	StringID picker_tooltip[2];     ///< Tooltip for the station picker for bus or truck stations

	SpriteID cursor_nesw;           ///< Cursor for building NE and SW bits
	SpriteID cursor_nwse;           ///< Cursor for building NW and SE bits
	SpriteID cursor_autoroad;       ///< Cursor for building autoroad
};

/** What errors/cursors must be shown for several types of roads */
static const RoadTypeInfo _road_type_infos[] = {
	{
		STR_1804_CAN_T_BUILD_ROAD_HERE,
		STR_1805_CAN_T_REMOVE_ROAD_FROM,
		STR_1807_CAN_T_BUILD_ROAD_VEHICLE,
		{ STR_1808_CAN_T_BUILD_BUS_STATION,        STR_1809_CAN_T_BUILD_TRUCK_STATION },
		{ STR_CAN_T_REMOVE_BUS_STATION,            STR_CAN_T_REMOVE_TRUCK_STATION     },
		{ STR_3042_BUS_STATION_ORIENTATION,        STR_3043_TRUCK_STATION_ORIENT      },
		{ STR_3051_SELECT_BUS_STATION_ORIENTATION, STR_3052_SELECT_TRUCK_LOADING_BAY  },

		SPR_CURSOR_ROAD_NESW,
		SPR_CURSOR_ROAD_NWSE,
		SPR_CURSOR_AUTOROAD,
	},
	{
		STR_CAN_T_BUILD_TRAMWAY_HERE,
		STR_CAN_T_REMOVE_TRAMWAY_FROM,
		STR_CAN_T_BUILD_TRAM_VEHICLE,
		{ STR_CAN_T_BUILD_PASSENGER_TRAM_STATION,        STR_CAN_T_BUILD_CARGO_TRAM_STATION        },
		{ STR_CAN_T_REMOVE_PASSENGER_TRAM_STATION,       STR_CAN_T_REMOVE_CARGO_TRAM_STATION       },
		{ STR_PASSENGER_TRAM_STATION_ORIENTATION,        STR_CARGO_TRAM_STATION_ORIENT             },
		{ STR_SELECT_PASSENGER_TRAM_STATION_ORIENTATION, STR_SELECT_CARGO_TRAM_STATION_ORIENTATION },

		SPR_CURSOR_TRAMWAY_NESW,
		SPR_CURSOR_TRAMWAY_NWSE,
		SPR_CURSOR_AUTOTRAM,
	},
};

static void PlaceRoad_Tunnel(TileIndex tile)
{
	DoCommandP(tile, 0x200 | RoadTypeToRoadTypes(_cur_roadtype), 0, CMD_BUILD_TUNNEL | CMD_MSG(STR_5016_CAN_T_BUILD_TUNNEL_HERE), CcBuildRoadTunnel);
}

static void BuildRoadOutsideStation(TileIndex tile, DiagDirection direction)
{
	tile += TileOffsByDiagDir(direction);
	/* if there is a roadpiece just outside of the station entrance, build a connecting route */
	if (IsNormalRoadTile(tile)) {
		if (GetRoadBits(tile, _cur_roadtype) != ROAD_NONE) {
			DoCommandP(tile, _cur_roadtype << 4 | DiagDirToRoadBits(ReverseDiagDir(direction)), 0, CMD_BUILD_ROAD);
		}
	}
}

void CcRoadDepot(bool success, TileIndex tile, uint32 p1, uint32 p2)
{
	if (success) {
		DiagDirection dir = (DiagDirection)GB(p1, 0, 2);
		SndPlayTileFx(SND_1F_SPLAT, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
		BuildRoadOutsideStation(tile, dir);
		/* For a drive-through road stop build connecting road for other entrance */
		if (HasBit(p2, 1)) BuildRoadOutsideStation(tile, ReverseDiagDir(dir));
	}
}

static void PlaceRoad_Depot(TileIndex tile)
{
	DoCommandP(tile, _cur_roadtype << 2 | _road_depot_orientation, 0, CMD_BUILD_ROAD_DEPOT | CMD_MSG(_road_type_infos[_cur_roadtype].err_depot), CcRoadDepot);
}

static void PlaceRoadStop(TileIndex tile, uint32 p2, uint32 cmd)
{
	uint32 p1 = _road_station_picker_orientation;
	SB(p2, 16, 16, INVALID_STATION); // no station to join

	if (p1 >= DIAGDIR_END) {
		SetBit(p2, 1); // It's a drive-through stop
		p1 -= DIAGDIR_END; // Adjust picker result to actual direction
	}
	CommandContainer cmdcont = { tile, p1, p2, cmd, CcRoadDepot, "" };
	ShowSelectStationIfNeeded(cmdcont, 1, 1);
}

static void PlaceRoad_BusStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		DoCommandP(tile, 0, ROADSTOP_BUS, CMD_REMOVE_ROAD_STOP | CMD_MSG(_road_type_infos[_cur_roadtype].err_remove_station[ROADSTOP_BUS]), CcPlaySound1D);
	} else {
		PlaceRoadStop(tile, (_ctrl_pressed << 5) | RoadTypeToRoadTypes(_cur_roadtype) << 2 | ROADSTOP_BUS, CMD_BUILD_ROAD_STOP | CMD_MSG(_road_type_infos[_cur_roadtype].err_build_station[ROADSTOP_BUS]));
	}
}

static void PlaceRoad_TruckStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		DoCommandP(tile, 0, ROADSTOP_TRUCK, CMD_REMOVE_ROAD_STOP | CMD_MSG(_road_type_infos[_cur_roadtype].err_remove_station[ROADSTOP_TRUCK]), CcPlaySound1D);
	} else {
		PlaceRoadStop(tile, (_ctrl_pressed << 5) | RoadTypeToRoadTypes(_cur_roadtype) << 2 | ROADSTOP_TRUCK, CMD_BUILD_ROAD_STOP | CMD_MSG(_road_type_infos[_cur_roadtype].err_build_station[ROADSTOP_TRUCK]));
	}
}

/** Enum referring to the widgets of the build road toolbar */
enum RoadToolbarWidgets {
	RTW_CLOSEBOX = 0,
	RTW_CAPTION,
	RTW_STICKY,
	RTW_ROAD_X,
	RTW_ROAD_Y,
	RTW_AUTOROAD,
	RTW_DEMOLISH,
	RTW_DEPOT,
	RTW_BUS_STATION,
	RTW_TRUCK_STATION,
	RTW_ONE_WAY,
	RTW_BUILD_BRIDGE,
	RTW_BUILD_TUNNEL,
	RTW_REMOVE,
};

typedef void OnButtonClick(Window *w);


/** Toogles state of the Remove button of Build road toolbar
 * @param w window the button belongs to
 */
static void ToggleRoadButton_Remove(Window *w)
{
	w->ToggleWidgetLoweredState(RTW_REMOVE);
	w->InvalidateWidget(RTW_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(RTW_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/** Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove buton was changed
 */
static bool RoadToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(RTW_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = RTW_ROAD_X; i <= RTW_AUTOROAD; i++) {
		if (w->IsWidgetLowered(i)) {
			ToggleRoadButton_Remove(w);
			return true;
		}
	}

	return false;
}


/**
 * Function that handles the click on the
 *  X road placement button.
 *
 * @param w The current window
 */
static void BuildRoadClick_X_Dir(Window *w)
{
	HandlePlacePushButton(w, RTW_ROAD_X, _road_type_infos[_cur_roadtype].cursor_nwse, VHM_RECT, PlaceRoad_X_Dir);
}

/**
 * Function that handles the click on the
 *  Y road placement button.
 *
 * @param w The current window
 */
static void BuildRoadClick_Y_Dir(Window *w)
{
	HandlePlacePushButton(w, RTW_ROAD_Y, _road_type_infos[_cur_roadtype].cursor_nesw, VHM_RECT, PlaceRoad_Y_Dir);
}

/**
 * Function that handles the click on the
 *  autoroad placement button.
 *
 * @param w The current window
 */
static void BuildRoadClick_AutoRoad(Window *w)
{
	HandlePlacePushButton(w, RTW_AUTOROAD, _road_type_infos[_cur_roadtype].cursor_autoroad, VHM_RECT, PlaceRoad_AutoRoad);
}

static void BuildRoadClick_Demolish(Window *w)
{
	HandlePlacePushButton(w, RTW_DEMOLISH, ANIMCURSOR_DEMOLISH, VHM_RECT, PlaceProc_DemolishArea);
}

static void BuildRoadClick_Depot(Window *w)
{
	if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
	if (HandlePlacePushButton(w, RTW_DEPOT, SPR_CURSOR_ROAD_DEPOT, VHM_RECT, PlaceRoad_Depot)) ShowRoadDepotPicker(w);
}

static void BuildRoadClick_BusStation(Window *w)
{
	if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
	if (HandlePlacePushButton(w, RTW_BUS_STATION, SPR_CURSOR_BUS_STATION, VHM_RECT, PlaceRoad_BusStation)) ShowRVStationPicker(w, ROADSTOP_BUS);
}

static void BuildRoadClick_TruckStation(Window *w)
{
	if (_game_mode == GM_EDITOR || !CanBuildVehicleInfrastructure(VEH_ROAD)) return;
	if (HandlePlacePushButton(w, RTW_TRUCK_STATION, SPR_CURSOR_TRUCK_STATION, VHM_RECT, PlaceRoad_TruckStation)) ShowRVStationPicker(w, ROADSTOP_TRUCK);
}

/**
 * Function that handles the click on the
 *  one way road button.
 *
 * @param w The current window
 */
static void BuildRoadClick_OneWay(Window *w)
{
	if (w->IsWidgetDisabled(RTW_ONE_WAY)) return;
	w->SetDirty();
	w->ToggleWidgetLoweredState(RTW_ONE_WAY);
	SetSelectionRed(false);
}

static void BuildRoadClick_Bridge(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, VHM_RECT, PlaceRoad_Bridge);
}

static void BuildRoadClick_Tunnel(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_TUNNEL, SPR_CURSOR_ROAD_TUNNEL, VHM_SPECIAL, PlaceRoad_Tunnel);
}

static void BuildRoadClick_Remove(Window *w)
{
	if (w->IsWidgetDisabled(RTW_REMOVE)) return;

	DeleteWindowById(WC_SELECT_STATION, 0);
	ToggleRoadButton_Remove(w);
	SndPlayFx(SND_15_BEEP);
}

/** Array with the handlers of the button-clicks for the road-toolbar */
static OnButtonClick * const _build_road_button_proc[] = {
	BuildRoadClick_X_Dir,
	BuildRoadClick_Y_Dir,
	BuildRoadClick_AutoRoad,
	BuildRoadClick_Demolish,
	BuildRoadClick_Depot,
	BuildRoadClick_BusStation,
	BuildRoadClick_TruckStation,
	BuildRoadClick_OneWay,
	BuildRoadClick_Bridge,
	BuildRoadClick_Tunnel,
	BuildRoadClick_Remove
};

/** Array with the keycode of the button-clicks for the road-toolbar */
static const uint16 _road_keycodes[] = {
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7',
	'8',
	'B',
	'T',
	'R',
};

struct BuildRoadToolbarWindow : Window {
	BuildRoadToolbarWindow(const WindowDesc *desc, WindowNumber window_number) : Window(desc, window_number)
	{
		this->SetWidgetsDisabledState(true,
			RTW_REMOVE,
			RTW_ONE_WAY,
			WIDGET_LIST_END);

		this->FindWindowPlacementAndResize(desc);
		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildRoadToolbarWindow()
	{
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	/**
	 * Update the remove button lowered state of the road toolbar
	 *
	 * @param clicked_widget The widget which the client clicked just now
	 */
	void UpdateOptionWidgetStatus(RoadToolbarWidgets clicked_widget)
	{
		/* The remove and the one way button state is driven
		 * by the other buttons so they don't act on themselfs.
		 * Both are only valid if they are able to apply as options. */
		switch (clicked_widget) {
			case RTW_REMOVE:
				this->RaiseWidget(RTW_ONE_WAY);
				this->InvalidateWidget(RTW_ONE_WAY);
				break;

			case RTW_ONE_WAY:
				this->RaiseWidget(RTW_REMOVE);
				this->InvalidateWidget(RTW_REMOVE);
				break;

			case RTW_BUS_STATION:
			case RTW_TRUCK_STATION:
				this->DisableWidget(RTW_ONE_WAY);
				this->SetWidgetDisabledState(RTW_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			case RTW_ROAD_X:
			case RTW_ROAD_Y:
			case RTW_AUTOROAD:
				this->SetWidgetsDisabledState(!this->IsWidgetLowered(clicked_widget),
					RTW_REMOVE,
					RTW_ONE_WAY,
					WIDGET_LIST_END);
				break;

			default:
				/* When any other buttons than road/station, raise and
				 * disable the removal button */
				this->SetWidgetsDisabledState(true,
					RTW_REMOVE,
					RTW_ONE_WAY,
					WIDGET_LIST_END);
				this->SetWidgetsLoweredState (false,
					RTW_REMOVE,
					RTW_ONE_WAY,
					WIDGET_LIST_END);
				break;
		}
	}

	virtual void OnPaint()
	{
		this->SetWidgetsDisabledState(!CanBuildVehicleInfrastructure(VEH_ROAD),
			RTW_DEPOT,
			RTW_BUS_STATION,
			RTW_TRUCK_STATION,
			WIDGET_LIST_END);
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget)
	{
		if (widget >= RTW_ROAD_X) {
			_remove_button_clicked = false;
			_one_way_button_clicked = false;
			_build_road_button_proc[widget - RTW_ROAD_X](this);
		}
		this->UpdateOptionWidgetStatus((RoadToolbarWidgets)widget);
		if (_ctrl_pressed) RoadToolbar_CtrlChanged(this);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		for (uint i = 0; i != lengthof(_road_keycodes); i++) {
			if (keycode == _road_keycodes[i]) {
				_remove_button_clicked = false;
				_one_way_button_clicked = false;
				_build_road_button_proc[i](this);
				this->UpdateOptionWidgetStatus((RoadToolbarWidgets)(i + RTW_ROAD_X));
				if (_ctrl_pressed) RoadToolbar_CtrlChanged(this);
				state = ES_HANDLED;
				break;
			}
		}
		MarkTileDirty(_thd.pos.x, _thd.pos.y); // redraw tile selection
		return state;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		_remove_button_clicked = this->IsWidgetLowered(RTW_REMOVE);
		_one_way_button_clicked = this->IsWidgetLowered(RTW_ONE_WAY);
		_place_proc(tile);
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->SetWidgetsDisabledState(true,
			RTW_REMOVE,
			RTW_ONE_WAY,
			WIDGET_LIST_END);
		this->InvalidateWidget(RTW_REMOVE);
		this->InvalidateWidget(RTW_ONE_WAY);

		DeleteWindowById(WC_BUS_STATION, 0);
		DeleteWindowById(WC_TRUCK_STATION, 0);
		DeleteWindowById(WC_BUILD_DEPOT, 0);
		DeleteWindowById(WC_SELECT_STATION, 0);
		DeleteWindowById(WC_BUILD_BRIDGE, 0);
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		/* Here we update the end tile flags
		 * of the road placement actions.
		 * At first we reset the end halfroad
		 * bits and if needed we set them again. */
		switch (select_proc) {
			case DDSP_PLACE_ROAD_X_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_X;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;
				break;

			case DDSP_PLACE_ROAD_Y_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_Y;
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				break;

			case DDSP_PLACE_AUTOROAD:
				_place_road_flag &= ~(RF_END_HALFROAD_Y | RF_END_HALFROAD_X);
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;

				/* For autoroad we need to update the
				 * direction of the road */
				if (_thd.size.x > _thd.size.y || (_thd.size.x == _thd.size.y &&
						( (_tile_fract_coords.x < _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) < 16) ||
						(_tile_fract_coords.x > _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) > 16) ))) {
					/* Set dir = X */
					_place_road_flag &= ~RF_DIR_Y;
				} else {
					/* Set dir = Y */
					_place_road_flag |= RF_DIR_Y;
				}

				break;

			default:
				break;
		}

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_ROAD, RoadTypeToRoadTypes(_cur_roadtype));
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_PLACE_ROAD_X_DIR:
				case DDSP_PLACE_ROAD_Y_DIR:
				case DDSP_PLACE_AUTOROAD:
					/* Flag description:
					 * Use the first three bits (0x07) if dir == Y
					 * else use the last 2 bits (X dir has
					 * not the 3rd bit set) */
					_place_road_flag = (RoadFlags)((_place_road_flag & RF_DIR_Y) ? (_place_road_flag & 0x07) : (_place_road_flag >> 3));

					DoCommandP(end_tile, start_tile, _place_road_flag | (_cur_roadtype << 3) | (_one_way_button_clicked << 5),
						(_ctrl_pressed || _remove_button_clicked) ?
						CMD_REMOVE_LONG_ROAD | CMD_MSG(_road_type_infos[_cur_roadtype].err_remove_road) :
						CMD_BUILD_LONG_ROAD | CMD_MSG(_road_type_infos[_cur_roadtype].err_build_road), CcPlaySound1D);
					break;
			}
		}
	}

	virtual void OnPlacePresize(Point pt, TileIndex tile)
	{
		DoCommand(tile, 0x200 | RoadTypeToRoadTypes(_cur_roadtype), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	virtual EventState OnCTRLStateChange()
	{
		if (RoadToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}
};

/** Widget definition of the build road toolbar */
static const Widget _build_road_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    10,     0,    13, STR_00C5,                   STR_018B_CLOSE_WINDOW},             // RTW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,    11,   250,     0,    13, STR_1802_ROAD_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},   // RTW_CAPTION
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,   251,   262,     0,    13, 0x0,                        STR_STICKY_BUTTON},                 // RTW_STICKY

{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    21,    14,    35, SPR_IMG_ROAD_X_DIR,         STR_180B_BUILD_ROAD_SECTION},       // RTW_ROAD_X
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    22,    43,    14,    35, SPR_IMG_ROAD_Y_DIR,         STR_180B_BUILD_ROAD_SECTION},       // RTW_ROAD_Y
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    44,    65,    14,    35, SPR_IMG_AUTOROAD,           STR_BUILD_AUTOROAD_TIP},            // RTW_AUTOROAD
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    66,    87,    14,    35, SPR_IMG_DYNAMITE,           STR_018D_DEMOLISH_BUILDINGS_ETC},   // RTW_DEMOLISH
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    88,   109,    14,    35, SPR_IMG_ROAD_DEPOT,         STR_180C_BUILD_ROAD_VEHICLE_DEPOT}, // RTW_DEPOT
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   110,   131,    14,    35, SPR_IMG_BUS_STATION,        STR_180D_BUILD_BUS_STATION},        // RTW_BUS_STATION
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   132,   153,    14,    35, SPR_IMG_TRUCK_BAY,          STR_180E_BUILD_TRUCK_LOADING_BAY},  // RTW_TRUCK_STATION
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   154,   175,    14,    35, SPR_IMG_ROAD_ONE_WAY,       STR_TOGGLE_ONE_WAY_ROAD},           // RTW_ONE_WAY
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   176,   218,    14,    35, SPR_IMG_BRIDGE,             STR_180F_BUILD_ROAD_BRIDGE},        // RTW_BUILD_BRIDGE
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   219,   240,    14,    35, SPR_IMG_ROAD_TUNNEL,        STR_1810_BUILD_ROAD_TUNNEL},        // RTW_BUILD_TUNNEL
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   241,   262,    14,    35, SPR_IMG_REMOVE,             STR_1811_TOGGLE_BUILD_REMOVE_FOR},  // RTW_REMOVE

{   WIDGETS_END},
};

static const WindowDesc _build_road_desc(
	WDP_ALIGN_TBR, 22, 263, 36, 263, 36,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_CONSTRUCTION,
	_build_road_widgets
);

/** Widget definition of the build tram toolbar */
static const Widget _build_tramway_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    10,     0,    13, STR_00C5,                       STR_018B_CLOSE_WINDOW},                // RTW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,    11,   228,     0,    13, STR_WHITE_TRAMWAY_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},      // RTW_CAPTION
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,   229,   240,     0,    13, 0x0,                            STR_STICKY_BUTTON},                    // RTW_STICKY

{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    21,    14,    35, SPR_IMG_TRAMWAY_X_DIR,          STR_BUILD_TRAMWAY_SECTION},            // RTW_ROAD_X
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    22,    43,    14,    35, SPR_IMG_TRAMWAY_Y_DIR,          STR_BUILD_TRAMWAY_SECTION},            // RTW_ROAD_Y
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    44,    65,    14,    35, SPR_IMG_AUTOTRAM,               STR_BUILD_AUTOTRAM_TIP},               // RTW_AUTOROAD
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    66,    87,    14,    35, SPR_IMG_DYNAMITE,               STR_018D_DEMOLISH_BUILDINGS_ETC},      // RTW_DEMOLISH
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    88,   109,    14,    35, SPR_IMG_ROAD_DEPOT,             STR_BUILD_TRAM_VEHICLE_DEPOT},         // RTW_DEPOT
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   110,   131,    14,    35, SPR_IMG_BUS_STATION,            STR_BUILD_PASSENGER_TRAM_STATION},     // RTW_BUS_STATION
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   132,   153,    14,    35, SPR_IMG_TRUCK_BAY,              STR_BUILD_CARGO_TRAM_STATION},         // RTW_TRUCK_STATION
{      WWT_EMPTY,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,     0,     0,     0, 0x0,                            STR_NULL},                             // RTW_ONE_WAY
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   154,   196,    14,    35, SPR_IMG_BRIDGE,                 STR_BUILD_TRAMWAY_BRIDGE},             // RTW_BUILD_BRIDGE
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   197,   218,    14,    35, SPR_IMG_ROAD_TUNNEL,            STR_BUILD_TRAMWAY_TUNNEL},             // RTW_BUILD_TUNNEL
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   219,   240,    14,    35, SPR_IMG_REMOVE,                 STR_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS}, // RTW_REMOVE

{   WIDGETS_END},
};

static const WindowDesc _build_tramway_desc(
	WDP_ALIGN_TBR, 22, 241, 36, 241, 36,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_CONSTRUCTION,
	_build_tramway_widgets
);

void ShowBuildRoadToolbar(RoadType roadtype)
{
	if (!IsValidCompanyID(_local_company)) return;
	_cur_roadtype = roadtype;

	DeleteWindowByClass(WC_BUILD_TOOLBAR);
	AllocateWindowDescFront<BuildRoadToolbarWindow>(roadtype == ROADTYPE_ROAD ? &_build_road_desc : &_build_tramway_desc, TRANSPORT_ROAD);
}

/** Widget definition of the build road toolbar in the scenario editor */
static const Widget _build_road_scen_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    10,     0,    13, STR_00C5,                   STR_018B_CLOSE_WINDOW},            // RTW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,    11,   184,     0,    13, STR_1802_ROAD_CONSTRUCTION, STR_018C_WINDOW_TITLE_DRAG_THIS},  // RTW_CAPTION
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,   185,   196,     0,    13, 0x0,                        STR_STICKY_BUTTON},                // RTW_STICKY

{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,    21,    14,    35, SPR_IMG_ROAD_X_DIR,         STR_180B_BUILD_ROAD_SECTION},      // RTW_ROAD_X
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    22,    43,    14,    35, SPR_IMG_ROAD_Y_DIR,         STR_180B_BUILD_ROAD_SECTION},      // RTW_ROAD_Y
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    44,    65,    14,    35, SPR_IMG_AUTOROAD,           STR_BUILD_AUTOROAD_TIP},           // RTW_AUTOROAD
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    66,    87,    14,    35, SPR_IMG_DYNAMITE,           STR_018D_DEMOLISH_BUILDINGS_ETC},  // RTW_DEMOLISH
{      WWT_EMPTY,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,     0,     0,     0, 0x0,                        STR_NULL},                         // RTW_DEPOT
{      WWT_EMPTY,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,     0,     0,     0, 0x0,                        STR_NULL},                         // RTW_BUS_STATION
{      WWT_EMPTY,   RESIZE_NONE,  COLOUR_DARK_GREEN,     0,     0,     0,     0, 0x0,                        STR_NULL},                         // RTW_TRUCK_STATION
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,    88,   109,    14,    35, SPR_IMG_ROAD_ONE_WAY,       STR_TOGGLE_ONE_WAY_ROAD},          // RTW_ONE_WAY
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   110,   152,    14,    35, SPR_IMG_BRIDGE,             STR_180F_BUILD_ROAD_BRIDGE},       // RTW_BUILD_BRIDGE
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   153,   174,    14,    35, SPR_IMG_ROAD_TUNNEL,        STR_1810_BUILD_ROAD_TUNNEL},       // RTW_BUILD_TUNNEL
{     WWT_IMGBTN,   RESIZE_NONE,  COLOUR_DARK_GREEN,   175,   196,    14,    35, SPR_IMG_REMOVE,             STR_1811_TOGGLE_BUILD_REMOVE_FOR}, // RTW_REMOVE
{   WIDGETS_END},
};

static const WindowDesc _build_road_scen_desc(
	WDP_AUTO, WDP_AUTO, 197, 36, 197, 36,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_CONSTRUCTION,
	_build_road_scen_widgets
);

void ShowBuildRoadScenToolbar()
{
	_cur_roadtype = ROADTYPE_ROAD;
	AllocateWindowDescFront<BuildRoadToolbarWindow>(&_build_road_scen_desc, 0);
}

struct BuildRoadDepotWindow : public PickerWindowBase {
private:
	/** Enum referring to the widgets of the build road depot window */
	enum BuildRoadDepotWidgets {
		BRDW_CLOSEBOX = 0,
		BRDW_CAPTION,
		BRDW_BACKGROUND,
		BRDW_DEPOT_NE,
		BRDW_DEPOT_SE,
		BRDW_DEPOT_SW,
		BRDW_DEPOT_NW,
	};

public:
	BuildRoadDepotWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->LowerWidget(_road_depot_orientation + BRDW_DEPOT_NE);
		if ( _cur_roadtype == ROADTYPE_TRAM) {
			this->widget[BRDW_CAPTION].data = STR_TRAM_DEPOT_ORIENTATION;
			for (int i = BRDW_DEPOT_NE; i <= BRDW_DEPOT_NW; i++) this->widget[i].tooltips = STR_SELECT_TRAM_VEHICLE_DEPOT;
		}
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		DrawRoadDepotSprite(70, 17, DIAGDIR_NE, _cur_roadtype);
		DrawRoadDepotSprite(70, 69, DIAGDIR_SE, _cur_roadtype);
		DrawRoadDepotSprite( 2, 69, DIAGDIR_SW, _cur_roadtype);
		DrawRoadDepotSprite( 2, 17, DIAGDIR_NW, _cur_roadtype);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case BRDW_DEPOT_NW:
			case BRDW_DEPOT_NE:
			case BRDW_DEPOT_SW:
			case BRDW_DEPOT_SE:
				this->RaiseWidget(_road_depot_orientation + BRDW_DEPOT_NE);
				_road_depot_orientation = (DiagDirection)(widget - BRDW_DEPOT_NE);
				this->LowerWidget(_road_depot_orientation + BRDW_DEPOT_NE);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}
};

/** Widget definition of the build road depot window */
static const Widget _build_road_depot_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,    10,     0,    13, STR_00C5,                        STR_018B_CLOSE_WINDOW},              // BRDW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,   11,   139,     0,    13, STR_1806_ROAD_DEPOT_ORIENTATION, STR_018C_WINDOW_TITLE_DRAG_THIS},    // BRDW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,    0,   139,    14,   121, 0x0,                             STR_NULL},                           // BRDW_BACKGROUND
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,         71,   136,    17,    66, 0x0,                             STR_1813_SELECT_ROAD_VEHICLE_DEPOT}, // BRDW_DEPOT_NE
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,         71,   136,    69,   118, 0x0,                             STR_1813_SELECT_ROAD_VEHICLE_DEPOT}, // BRDW_DEPOT_SE
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,          3,    68,    69,   118, 0x0,                             STR_1813_SELECT_ROAD_VEHICLE_DEPOT}, // BRDW_DEPOT_SW
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,          3,    68,    17,    66, 0x0,                             STR_1813_SELECT_ROAD_VEHICLE_DEPOT}, // BRDW_DEPOT_NW
{   WIDGETS_END},
};

static const WindowDesc _build_road_depot_desc(
	WDP_AUTO, WDP_AUTO, 140, 122, 140, 122,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_CONSTRUCTION,
	_build_road_depot_widgets
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(&_build_road_depot_desc, parent);
}

struct BuildRoadStationWindow : public PickerWindowBase {
private:
	/** Enum referring to the widgets of the build road station window */
	enum BuildRoadStationWidgets {
		BRSW_CLOSEBOX = 0,
		BRSW_CAPTION,
		BRSW_BACKGROUND,
		BRSW_STATION_NE,
		BRSW_STATION_SE,
		BRSW_STATION_SW,
		BRSW_STATION_NW,
		BRSW_STATION_X,
		BRSW_STATION_Y,
		BRSW_LT_OFF,
		BRSW_LT_ON,
		BRSW_INFO,
	};

public:
	BuildRoadStationWindow(const WindowDesc *desc, Window *parent, RoadStopType rs) : PickerWindowBase(desc, parent)
	{
		/* Trams don't have non-drivethrough stations */
		if (_cur_roadtype == ROADTYPE_TRAM && _road_station_picker_orientation < DIAGDIR_END) {
			_road_station_picker_orientation = DIAGDIR_END;
		}
		this->SetWidgetsDisabledState(_cur_roadtype == ROADTYPE_TRAM,
			BRSW_STATION_NE,
			BRSW_STATION_SE,
			BRSW_STATION_SW,
			BRSW_STATION_NW,
			WIDGET_LIST_END);

		this->window_class = (rs == ROADSTOP_BUS) ? WC_BUS_STATION : WC_TRUCK_STATION;
		this->widget[BRSW_CAPTION].data = _road_type_infos[_cur_roadtype].picker_title[rs];
		for (uint i = BRSW_STATION_NE; i < BRSW_LT_OFF; i++) this->widget[i].tooltips = _road_type_infos[_cur_roadtype].picker_tooltip[rs];

		this->LowerWidget(_road_station_picker_orientation + BRSW_STATION_NE);
		this->LowerWidget(_settings_client.gui.station_show_coverage + BRSW_LT_OFF);
		this->FindWindowPlacementAndResize(desc);
	}

	virtual ~BuildRoadStationWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		if (_settings_client.gui.station_show_coverage) {
			int rad = _settings_game.station.modified_catchment ? CA_TRUCK /* = CA_BUS */ : CA_UNMODIFIED;
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		StationType st = (this->window_class == WC_BUS_STATION) ? STATION_BUS : STATION_TRUCK;

		StationPickerDrawSprite(103, 35, st, INVALID_RAILTYPE, ROADTYPE_ROAD, 0);
		StationPickerDrawSprite(103, 85, st, INVALID_RAILTYPE, ROADTYPE_ROAD, 1);
		StationPickerDrawSprite( 35, 85, st, INVALID_RAILTYPE, ROADTYPE_ROAD, 2);
		StationPickerDrawSprite( 35, 35, st, INVALID_RAILTYPE, ROADTYPE_ROAD, 3);

		StationPickerDrawSprite(171, 35, st, INVALID_RAILTYPE, _cur_roadtype, 4);
		StationPickerDrawSprite(171, 85, st, INVALID_RAILTYPE, _cur_roadtype, 5);

		int text_end = DrawStationCoverageAreaText(2, 146,
			(this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY,
			3, false);
		text_end = DrawStationCoverageAreaText(2, text_end + 4,
			(this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY,
			3, true) + 4;
		if (text_end > this->widget[BRSW_BACKGROUND].bottom) {
			this->SetDirty();
			ResizeWindowForWidget(this, BRSW_BACKGROUND, 0, text_end - this->widget[BRSW_BACKGROUND].bottom);
			this->SetDirty();
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case BRSW_STATION_NE:
			case BRSW_STATION_SE:
			case BRSW_STATION_SW:
			case BRSW_STATION_NW:
			case BRSW_STATION_X:
			case BRSW_STATION_Y:
				this->RaiseWidget(_road_station_picker_orientation + BRSW_STATION_NE);
				_road_station_picker_orientation = (DiagDirection)(widget - BRSW_STATION_NE);
				this->LowerWidget(_road_station_picker_orientation + BRSW_STATION_NE);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;

			case BRSW_LT_OFF:
			case BRSW_LT_ON:
				this->RaiseWidget(_settings_client.gui.station_show_coverage + BRSW_LT_OFF);
				_settings_client.gui.station_show_coverage = (widget != BRSW_LT_OFF);
				this->LowerWidget(_settings_client.gui.station_show_coverage + BRSW_LT_OFF);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}

	virtual void OnTick()
	{
		CheckRedrawStationCoverage(this);
	}
};

/** Widget definition of the build raod station window */
static const Widget _rv_station_picker_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_DARK_GREEN,   0,    10,     0,    13, STR_00C5,                         STR_018B_CLOSE_WINDOW},             // BRSW_CLOSEBOX
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_DARK_GREEN,  11,   206,     0,    13, STR_NULL,                         STR_018C_WINDOW_TITLE_DRAG_THIS},   // BRSW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,   0,   206,    14,   176, 0x0,                              STR_NULL},                          // BRSW_BACKGROUND

{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,        71,   136,    17,    66, 0x0,                              STR_NULL},                          // BRSW_STATION_NE
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,        71,   136,    69,   118, 0x0,                              STR_NULL},                          // BRSW_STATION_SE
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,         3,    68,    69,   118, 0x0,                              STR_NULL},                          // BRSW_STATION_SW
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,         3,    68,    17,    66, 0x0,                              STR_NULL},                          // BRSW_STATION_NW
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,       139,   204,    17,    66, 0x0,                              STR_NULL},                          // BRSW_STATION_X
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,       139,   204,    69,   118, 0x0,                              STR_NULL},                          // BRSW_STATION_Y

{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,        10,    69,   133,   144, STR_02DB_OFF,                     STR_3065_DON_T_HIGHLIGHT_COVERAGE}, // BRSW_LT_OFF
{    WWT_TEXTBTN,   RESIZE_NONE,  COLOUR_GREY,        70,   129,   133,   144, STR_02DA_ON,                      STR_3064_HIGHLIGHT_COVERAGE_AREA},  // BRSW_LT_ON
{      WWT_LABEL,   RESIZE_NONE,  COLOUR_DARK_GREEN,   0,   139,   120,   133, STR_3066_COVERAGE_AREA_HIGHLIGHT, STR_NULL},                          // BRSW_INFO
{   WIDGETS_END},
};

static const WindowDesc _rv_station_picker_desc(
	WDP_AUTO, WDP_AUTO, 207, 177, 207, 177,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_CONSTRUCTION,
	_rv_station_picker_widgets
);

static void ShowRVStationPicker(Window *parent, RoadStopType rs)
{
	new BuildRoadStationWindow(&_rv_station_picker_desc, parent, rs);
}

void InitializeRoadGui()
{
	_road_depot_orientation = DIAGDIR_NW;
	_road_station_picker_orientation = DIAGDIR_NW;
}
