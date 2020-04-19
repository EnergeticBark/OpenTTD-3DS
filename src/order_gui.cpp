/* $Id$ */

/** @file order_gui.cpp GUI related to orders. */

#include "stdafx.h"
#include "station_map.h"
#include "window_gui.h"
#include "command_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "depot_base.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "timetable.h"
#include "cargotype.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "newgrf_cargo.h"
#include "widgets/dropdown_func.h"
#include "textbuf_gui.h"
#include "string_func.h"
#include "tilehighlight_func.h"
#include "network/network.h"
#include "settings_type.h"

#include "table/sprites.h"
#include "table/strings.h"

enum OrderWindowWidgets {
	ORDER_WIDGET_CLOSEBOX = 0,
	ORDER_WIDGET_CAPTION,
	ORDER_WIDGET_TIMETABLE_VIEW,
	ORDER_WIDGET_STICKY,
	ORDER_WIDGET_ORDER_LIST,
	ORDER_WIDGET_SCROLLBAR,
	ORDER_WIDGET_SKIP,
	ORDER_WIDGET_DELETE,
	ORDER_WIDGET_NON_STOP_DROPDOWN,
	ORDER_WIDGET_NON_STOP,
	ORDER_WIDGET_GOTO_DROPDOWN,
	ORDER_WIDGET_GOTO,
	ORDER_WIDGET_FULL_LOAD_DROPDOWN,
	ORDER_WIDGET_FULL_LOAD,
	ORDER_WIDGET_UNLOAD_DROPDOWN,
	ORDER_WIDGET_UNLOAD,
	ORDER_WIDGET_REFIT,
	ORDER_WIDGET_SERVICE_DROPDOWN,
	ORDER_WIDGET_SERVICE,
	ORDER_WIDGET_COND_VARIABLE,
	ORDER_WIDGET_COND_COMPARATOR,
	ORDER_WIDGET_COND_VALUE,
	ORDER_WIDGET_RESIZE_BAR,
	ORDER_WIDGET_SHARED_ORDER_LIST,
	ORDER_WIDGET_RESIZE,
};

/** Order load types that could be given to station orders. */
static const StringID _station_load_types[][5] = {
	{
		STR_EMPTY,
		INVALID_STRING_ID,
		STR_ORDER_FULL_LOAD,
		STR_ORDER_FULL_LOAD_ANY,
		STR_ORDER_NO_LOAD,
	}, {
		STR_ORDER_UNLOAD,
		INVALID_STRING_ID,
		STR_ORDER_UNLOAD_FULL_LOAD,
		STR_ORDER_UNLOAD_FULL_LOAD_ANY,
		STR_ORDER_UNLOAD_NO_LOAD,
	}, {
		STR_ORDER_TRANSFER,
		INVALID_STRING_ID,
		STR_ORDER_TRANSFER_FULL_LOAD,
		STR_ORDER_TRANSFER_FULL_LOAD_ANY,
		STR_ORDER_TRANSFER_NO_LOAD,
	}, {
		/* Unload and transfer do not work together. */
		INVALID_STRING_ID,
		INVALID_STRING_ID,
		INVALID_STRING_ID,
		INVALID_STRING_ID,
	}, {
		STR_ORDER_NO_UNLOAD,
		INVALID_STRING_ID,
		STR_ORDER_NO_UNLOAD_FULL_LOAD,
		STR_ORDER_NO_UNLOAD_FULL_LOAD_ANY,
		INVALID_STRING_ID,
	}
};

static const StringID _order_non_stop_drowdown[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_NON_STOP_TO,
	STR_ORDER_GO_VIA,
	STR_ORDER_GO_NON_STOP_VIA,
	INVALID_STRING_ID
};

static const StringID _order_full_load_drowdown[] = {
	STR_ORDER_DROP_LOAD_IF_POSSIBLE,
	STR_EMPTY,
	STR_ORDER_DROP_FULL_LOAD_ALL,
	STR_ORDER_DROP_FULL_LOAD_ANY,
	STR_ORDER_DROP_NO_LOADING,
	INVALID_STRING_ID
};

static const StringID _order_unload_drowdown[] = {
	STR_ORDER_DROP_UNLOAD_IF_ACCEPTED,
	STR_ORDER_DROP_UNLOAD,
	STR_ORDER_DROP_TRANSFER,
	STR_EMPTY,
	STR_ORDER_DROP_NO_UNLOADING,
	INVALID_STRING_ID
};

static const StringID _order_goto_dropdown[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_TO_NEAREST_DEPOT,
	STR_ORDER_CONDITIONAL,
	INVALID_STRING_ID
};

static const StringID _order_goto_dropdown_aircraft[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_TO_NEAREST_HANGAR,
	STR_ORDER_CONDITIONAL,
	INVALID_STRING_ID
};

static const StringID _order_conditional_variable[] = {
	STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE,
	STR_ORDER_CONDITIONAL_RELIABILITY,
	STR_ORDER_CONDITIONAL_MAX_SPEED,
	STR_ORDER_CONDITIONAL_AGE,
	STR_ORDER_CONDITIONAL_REQUIRES_SERVICE,
	STR_ORDER_CONDITIONAL_UNCONDITIONALLY,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_NOT_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_LESS_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_LESS_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_MORE_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_MORE_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_IS_TRUE,
	STR_ORDER_CONDITIONAL_COMPARATOR_IS_FALSE,
	INVALID_STRING_ID,
};

extern uint ConvertSpeedToDisplaySpeed(uint speed);
extern uint ConvertDisplaySpeedToSpeed(uint speed);

static const StringID _order_depot_action_dropdown[] = {
	STR_ORDER_DROP_GO_ALWAYS_DEPOT,
	STR_ORDER_DROP_SERVICE_DEPOT,
	STR_ORDER_DROP_HALT_DEPOT,
	INVALID_STRING_ID
};

static int DepotActionStringIndex(const Order *order)
{
	if (order->GetDepotActionType() & ODATFB_HALT) {
		return DA_STOP;
	} else if (order->GetDepotOrderType() & ODTFB_SERVICE) {
		return DA_SERVICE;
	} else {
		return DA_ALWAYS_GO;
	}
}

void DrawOrderString(const Vehicle *v, const Order *order, int order_index, int y, bool selected, bool timetable, int width)
{
	StringID str = (v->cur_order_index == order_index) ? STR_8805 : STR_8804;
	SetDParam(6, STR_EMPTY);

	switch (order->GetType()) {
		case OT_DUMMY:
			SetDParam(1, STR_INVALID_ORDER);
			SetDParam(2, order->GetDestination());
			break;

		case OT_GOTO_STATION: {
			OrderLoadFlags load = order->GetLoadType();
			OrderUnloadFlags unload = order->GetUnloadType();

			SetDParam(1, STR_GO_TO_STATION);
			SetDParam(2, STR_ORDER_GO_TO + ((v->type == VEH_TRAIN || v->type == VEH_ROAD) ? order->GetNonStopType() : 0));
			SetDParam(3, order->GetDestination());

			if (timetable) {
				SetDParam(4, STR_EMPTY);

				if (order->wait_time > 0) {
					SetDParam(6, STR_TIMETABLE_STAY_FOR);
					SetTimetableParams(7, 8, order->wait_time);
				}
			} else {
				SetDParam(4, (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) ? STR_EMPTY : _station_load_types[unload][load]);
			}
		} break;

		case OT_GOTO_DEPOT:
			if (v->type == VEH_AIRCRAFT) {
				if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
					SetDParam(1, STR_GO_TO_NEAREST_DEPOT);
					SetDParam(3, STR_ORDER_NEAREST_HANGAR);
				} else {
					SetDParam(1, STR_GO_TO_HANGAR);
					SetDParam(3, order->GetDestination());
				}
				SetDParam(4, STR_EMPTY);
			} else {
				if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
					SetDParam(1, STR_GO_TO_NEAREST_DEPOT);
					SetDParam(3, STR_ORDER_NEAREST_DEPOT);
				} else {
					SetDParam(1, STR_GO_TO_DEPOT);
					SetDParam(3, GetDepot(order->GetDestination())->town_index);
				}

				switch (v->type) {
					case VEH_TRAIN: SetDParam(4, STR_ORDER_TRAIN_DEPOT); break;
					case VEH_ROAD:  SetDParam(4, STR_ORDER_ROAD_DEPOT); break;
					case VEH_SHIP:  SetDParam(4, STR_ORDER_SHIP_DEPOT); break;
					default: NOT_REACHED();
				}
			}

			if (order->GetDepotOrderType() & ODTFB_SERVICE) {
				SetDParam(2, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_ORDER_SERVICE_NON_STOP_AT : STR_ORDER_SERVICE_AT);
			} else {
				SetDParam(2, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_ORDER_GO_NON_STOP_TO : STR_ORDER_GO_TO);
			}

			if (!timetable && (order->GetDepotActionType() & ODATFB_HALT)) {
				SetDParam(6, STR_STOP_ORDER);
			}

			if (!timetable && order->IsRefit()) {
				SetDParam(6, (order->GetDepotActionType() & ODATFB_HALT) ? STR_REFIT_STOP_ORDER : STR_REFIT_ORDER);
				SetDParam(7, GetCargo(order->GetRefitCargo())->name);
			}
			break;

		case OT_GOTO_WAYPOINT:
			SetDParam(1, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_GO_NON_STOP_TO_WAYPOINT : STR_GO_TO_WAYPOINT);
			SetDParam(2, order->GetDestination());
			break;

		case OT_CONDITIONAL:
			SetDParam(2, order->GetConditionSkipToOrder() + 1);
			if (order->GetConditionVariable() == OCV_UNCONDITIONALLY) {
				SetDParam(1, STR_CONDITIONAL_UNCONDITIONAL);
			} else {
				OrderConditionComparator occ = order->GetConditionComparator();
				SetDParam(1, (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) ? STR_CONDITIONAL_TRUE_FALSE : STR_CONDITIONAL_NUM);
				SetDParam(3, STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + order->GetConditionVariable());
				SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + occ);

				uint value = order->GetConditionValue();
				if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value);
				SetDParam(5, value);
			}

			if (timetable && order->wait_time > 0) {
				SetDParam(6, STR_TIMETABLE_AND_TRAVEL_FOR);
				SetTimetableParams(7, 8, order->wait_time);
			} else {
				SetDParam(6, STR_EMPTY);
			}
			break;

		default: NOT_REACHED();
	}

	SetDParam(0, order_index + 1);
	DrawStringTruncated(2, y, str, selected ? TC_WHITE : TC_BLACK, width);
}


static Order GetOrderCmdFromTile(const Vehicle *v, TileIndex tile)
{
	Order order;
	order.next  = NULL;
	order.index = 0;

	/* check depot first */
	if (_settings_game.order.gotodepot) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				if (v->type == VEH_TRAIN && IsTileOwner(tile, _local_company)) {
					if (IsRailDepot(tile)) {
						order.MakeGoToDepot(GetDepotByTile(tile)->index, ODTFB_PART_OF_ORDERS);
						if (_ctrl_pressed) order.SetDepotOrderType((OrderDepotTypeFlags)(order.GetDepotOrderType() ^ ODTFB_SERVICE));
						if (_settings_client.gui.new_nonstop) order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
						return order;
					}
				}
				break;

			case MP_ROAD:
				if (IsRoadDepot(tile) && v->type == VEH_ROAD && IsTileOwner(tile, _local_company)) {
					order.MakeGoToDepot(GetDepotByTile(tile)->index, ODTFB_PART_OF_ORDERS);
					if (_ctrl_pressed) order.SetDepotOrderType((OrderDepotTypeFlags)(order.GetDepotOrderType() ^ ODTFB_SERVICE));
					if (_settings_client.gui.new_nonstop) order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
					return order;
				}
				break;

			case MP_STATION:
				if (v->type != VEH_AIRCRAFT) break;
				if (IsHangar(tile) && IsTileOwner(tile, _local_company)) {
					order.MakeGoToDepot(GetStationIndex(tile), ODTFB_PART_OF_ORDERS);
					if (_ctrl_pressed) order.SetDepotOrderType((OrderDepotTypeFlags)(order.GetDepotOrderType() ^ ODTFB_SERVICE));
					return order;
				}
				break;

			case MP_WATER:
				if (v->type != VEH_SHIP) break;
				if (IsShipDepot(tile) && IsTileOwner(tile, _local_company)) {
					TileIndex tile2 = GetOtherShipDepotTile(tile);

					order.MakeGoToDepot(GetDepotByTile(tile < tile2 ? tile : tile2)->index, ODTFB_PART_OF_ORDERS);
					if (_ctrl_pressed) order.SetDepotOrderType((OrderDepotTypeFlags)(order.GetDepotOrderType() ^ ODTFB_SERVICE));
					return order;
				}

			default:
				break;
		}
	}

	/* check waypoint */
	if (IsRailWaypointTile(tile) &&
			v->type == VEH_TRAIN &&
			IsTileOwner(tile, _local_company)) {
		order.MakeGoToWaypoint(GetWaypointByTile(tile)->index);
		if (_settings_client.gui.new_nonstop != _ctrl_pressed) order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
		return order;
	}

	if (IsTileType(tile, MP_STATION)) {
		StationID st_index = GetStationIndex(tile);
		const Station *st = GetStation(st_index);

		if (st->owner == _local_company || st->owner == OWNER_NONE) {
			byte facil;
			(facil = FACIL_DOCK, v->type == VEH_SHIP) ||
			(facil = FACIL_TRAIN, v->type == VEH_TRAIN) ||
			(facil = FACIL_AIRPORT, v->type == VEH_AIRCRAFT) ||
			(facil = FACIL_BUS_STOP, v->type == VEH_ROAD && IsCargoInClass(v->cargo_type, CC_PASSENGERS)) ||
			(facil = FACIL_TRUCK_STOP, 1);
			if (st->facilities & facil) {
				order.MakeGoToStation(st_index);
				if (_ctrl_pressed) order.SetLoadType(OLF_FULL_LOAD_ANY);
				if (_settings_client.gui.new_nonstop && (v->type == VEH_TRAIN || v->type == VEH_ROAD)) order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
				return order;
			}
		}
	}

	/* not found */
	order.Free();
	return order;
}

struct OrdersWindow : public Window {
private:
	/** Under what reason are we using the PlaceObject functionality? */
	enum OrderPlaceObjectState {
		OPOS_GOTO,
		OPOS_CONDITIONAL,
	};

	int selected_order;
	OrderPlaceObjectState goto_type;
	const Vehicle *vehicle;

	/**
	 * Return the memorised selected order.
	 * @return the memorised order if it is a vaild one
	 *  else return the number of orders
	 */
	int OrderGetSel()
	{
		int num = this->selected_order;
		return (num >= 0 && num < vehicle->GetNumOrders()) ? num : vehicle->GetNumOrders();
	}

	/**
	 * Calculate the selected order.
	 * The calculation is based on the relative (to the window) y click position and
	 *  the position of the scrollbar.
	 *
	 * @param y Y-value of the click relative to the window origin
	 * @param v current vehicle
	 * @return the new selected order if the order is valid else return that
	 *  an invalid one has been selected.
	 */
	int GetOrderFromPt(int y)
	{
		/*
		 * Calculation description:
		 * 15 = 14 (w->widget[ORDER_WIDGET_ORDER_LIST].top) + 1 (frame-line)
		 * 10 = order text hight
		 */
		int sel = (y - this->widget[ORDER_WIDGET_ORDER_LIST].top - 1) / 10;

		if ((uint)sel >= this->vscroll.cap) return INVALID_ORDER;

		sel += this->vscroll.pos;

		return (sel <= vehicle->GetNumOrders() && sel >= 0) ? sel : INVALID_ORDER;
	}

	bool HandleOrderVehClick(const Vehicle *u)
	{
		if (u->type != this->vehicle->type) return false;

		if (!u->IsPrimaryVehicle()) {
			u = u->First();
			if (!u->IsPrimaryVehicle()) return false;
		}

		/* v is vehicle getting orders. Only copy/clone orders if vehicle doesn't have any orders yet
		 * obviously if you press CTRL on a non-empty orders vehicle you know what you are doing */
		if (this->vehicle->GetNumOrders() != 0 && _ctrl_pressed == 0) return false;

		if (DoCommandP(this->vehicle->tile, this->vehicle->index | (u->index << 16), _ctrl_pressed ? CO_SHARE : CO_COPY,
			_ctrl_pressed ? CMD_CLONE_ORDER | CMD_MSG(STR_CANT_SHARE_ORDER_LIST) : CMD_CLONE_ORDER | CMD_MSG(STR_CANT_COPY_ORDER_LIST))) {
			this->selected_order = -1;
			ResetObjectToPlace();
		}

		return true;
	}

	/**
	 * Handle the click on the goto button.
	 *
	 * @param w current window
	 */
	static void OrderClick_Goto(OrdersWindow *w, int i)
	{
		w->InvalidateWidget(ORDER_WIDGET_GOTO);
		w->ToggleWidgetLoweredState(ORDER_WIDGET_GOTO);
		if (w->IsWidgetLowered(ORDER_WIDGET_GOTO)) {
			_place_clicked_vehicle = NULL;
			SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, VHM_RECT, w);
			w->goto_type = OPOS_GOTO;
		} else {
			ResetObjectToPlace();
		}
	}

	/**
	 * Handle the click on the full load button.
	 *
	 * @param w current window
	 * @param load_type the way to load.
	 */
	static void OrderClick_FullLoad(OrdersWindow *w, int load_type)
	{
		VehicleOrderID sel_ord = w->OrderGetSel();
		const Order *order = GetVehicleOrder(w->vehicle, sel_ord);

		if (order == NULL || order->GetLoadType() == load_type) return;

		if (load_type < 0) {
			load_type = order->GetLoadType() == OLF_LOAD_IF_POSSIBLE ? OLF_FULL_LOAD_ANY : OLF_LOAD_IF_POSSIBLE;
		}
		DoCommandP(w->vehicle->tile, w->vehicle->index + (sel_ord << 16), MOF_LOAD | (load_type << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
	}

	/**
	 * Handle the click on the service.
	 *
	 * @param w current window
	 */
	static void OrderClick_Service(OrdersWindow *w, int i)
	{
		VehicleOrderID sel_ord = w->OrderGetSel();

		if (i < 0) {
			const Order *order = GetVehicleOrder(w->vehicle, sel_ord);
			if (order == NULL) return;
			i = (order->GetDepotOrderType() & ODTFB_SERVICE) ? DA_ALWAYS_GO : DA_SERVICE;
		}
		DoCommandP(w->vehicle->tile, w->vehicle->index + (sel_ord << 16), MOF_DEPOT_ACTION | (i << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
	}

	/**
	 * Handle the click on the service in nearest depot button.
	 *
	 * @param w current window
	 */
	static void OrderClick_NearestDepot(OrdersWindow *w, int i)
	{
		Order order;
		order.next = NULL;
		order.index = 0;
		order.MakeGoToDepot(0, ODTFB_PART_OF_ORDERS);
		order.SetDepotActionType(ODATFB_NEAREST_DEPOT);

		DoCommandP(w->vehicle->tile, w->vehicle->index + (w->OrderGetSel() << 16), order.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_8833_CAN_T_INSERT_NEW_ORDER));
	}

	/**
	 * Handle the click on the conditional order button.
	 *
	 * @param w current window
	 */
	static void OrderClick_Conditional(OrdersWindow *w, int i)
	{
		w->InvalidateWidget(ORDER_WIDGET_GOTO);
		w->LowerWidget(ORDER_WIDGET_GOTO);
		SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, VHM_RECT, w);
		w->goto_type = OPOS_CONDITIONAL;
	}

	/**
	 * Handle the click on the unload button.
	 *
	 * @param w current window
	 */
	static void OrderClick_Unload(OrdersWindow *w, int unload_type)
	{
		VehicleOrderID sel_ord = w->OrderGetSel();
		const Order *order = GetVehicleOrder(w->vehicle, sel_ord);

		if (order == NULL || order->GetUnloadType() == unload_type) return;

		if (unload_type < 0) {
			unload_type = order->GetUnloadType() == OUF_UNLOAD_IF_POSSIBLE ? OUFB_UNLOAD : OUF_UNLOAD_IF_POSSIBLE;
		}

		DoCommandP(w->vehicle->tile, w->vehicle->index + (sel_ord << 16), MOF_UNLOAD | (unload_type << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
	}

	/**
	 * Handle the click on the nonstop button.
	 *
	 * @param w current window
	 * @param non_stop what non-stop type to use; -1 to use the 'next' one.
	 */
	static void OrderClick_Nonstop(OrdersWindow *w, int non_stop)
	{
		VehicleOrderID sel_ord = w->OrderGetSel();
		const Order *order = GetVehicleOrder(w->vehicle, sel_ord);

		if (order == NULL || order->GetNonStopType() == non_stop) return;

		/* Keypress if negative, so 'toggle' to the next */
		if (non_stop < 0) {
			non_stop = order->GetNonStopType() ^ ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
		}

		w->InvalidateWidget(ORDER_WIDGET_NON_STOP);
		DoCommandP(w->vehicle->tile, w->vehicle->index + (sel_ord << 16), MOF_NON_STOP | non_stop << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
	}

	/**
	 * Handle the click on the skip button.
	 * If ctrl is pressed skip to selected order.
	 *  Else skip to current order + 1
	 *
	 * @param w current window
	 */
	static void OrderClick_Skip(OrdersWindow *w, int i)
	{
		/* Don't skip when there's nothing to skip */
		if (_ctrl_pressed && w->vehicle->cur_order_index == w->OrderGetSel()) return;
		if (w->vehicle->GetNumOrders() <= 1) return;

		DoCommandP(w->vehicle->tile, w->vehicle->index, _ctrl_pressed ? w->OrderGetSel() : ((w->vehicle->cur_order_index + 1) % w->vehicle->GetNumOrders()),
				CMD_SKIP_TO_ORDER | CMD_MSG(_ctrl_pressed ? STR_CAN_T_SKIP_TO_ORDER : STR_CAN_T_SKIP_ORDER));
	}

	/**
	 * Handle the click on the delete button.
	 *
	 * @param w current window
	 */
	static void OrderClick_Delete(OrdersWindow *w, int i)
	{
		/* When networking, move one order lower */
		int selected = w->selected_order + (int)_networking;

		if (DoCommandP(w->vehicle->tile, w->vehicle->index, w->OrderGetSel(), CMD_DELETE_ORDER | CMD_MSG(STR_8834_CAN_T_DELETE_THIS_ORDER))) {
			w->selected_order = selected >= w->vehicle->GetNumOrders() ? -1 : selected;
		}
	}

	/**
	 * Handle the click on the refit button.
	 * If ctrl is pressed cancel refitting.
	 *  Else show the refit window.
	 *
	 * @param w current window
	 */
	static void OrderClick_Refit(OrdersWindow *w, int i)
	{
		if (_ctrl_pressed) {
			/* Cancel refitting */
			DoCommandP(w->vehicle->tile, w->vehicle->index, (w->OrderGetSel() << 16) | (CT_NO_REFIT << 8) | CT_NO_REFIT, CMD_ORDER_REFIT);
		} else {
			ShowVehicleRefitWindow(w->vehicle, w->OrderGetSel(), w);
		}
	}
	typedef void Handler(OrdersWindow*, int);
	struct KeyToEvent {
		uint16 keycode;
		Handler *proc;
	};

public:
	OrdersWindow(const WindowDesc *desc, const Vehicle *v) : Window(desc, v->index)
	{
		this->owner = v->owner;
		this->vscroll.cap = 6;
		this->resize.step_height = 10;
		this->selected_order = -1;
		this->vehicle = v;

		if (_settings_client.gui.quick_goto && v->owner == _local_company) {
			/* If there are less than 2 station, make Go To active. */
			int station_orders = 0;
			const Order *order;
			FOR_VEHICLE_ORDERS(v, order) {
				if (order->IsType(OT_GOTO_STATION)) station_orders++;
			}

			if (station_orders < 2) OrderClick_Goto(this, 0);
		}

		if (_settings_game.order.timetabling) {
			this->widget[ORDER_WIDGET_CAPTION].right -= 61;
		} else {
			this->HideWidget(ORDER_WIDGET_TIMETABLE_VIEW);
		}
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnInvalidateData(int data)
	{
		switch (data) {
			case 0:
				/* Autoreplace replaced the vehicle */
				this->vehicle = GetVehicle(this->window_number);
				break;

			case -1:
				/* Removed / replaced all orders (after deleting / sharing) */
				if (this->selected_order == -1) break;

				this->DeleteChildWindows();
				HideDropDownMenu(this);
				this->selected_order = -1;
				break;

			default: {
				/* Moving an order. If one of these is INVALID_VEH_ORDER_ID, then
				 * the order is being created / removed */
				if (this->selected_order == -1) break;

				VehicleOrderID from = GB(data, 0, 8);
				VehicleOrderID to   = GB(data, 8, 8);

				if (from == to) break; // no need to change anything

				if (from != this->selected_order) {
					/* Moving from preceeding order? */
					this->selected_order -= (int)(from <= this->selected_order);
					/* Moving to   preceeding order? */
					this->selected_order += (int)(to   <= this->selected_order);
					break;
				}

				/* Now we are modifying the selected order */
				if (to == INVALID_VEH_ORDER_ID) {
					/* Deleting selected order */
					this->DeleteChildWindows();
					HideDropDownMenu(this);
					this->selected_order = -1;
					break;
				}

				/* Moving selected order */
				this->selected_order = to;
			} break;
		}
	}

	virtual void OnPaint()
	{
		bool shared_orders = this->vehicle->IsOrderListShared();

		SetVScrollCount(this, this->vehicle->GetNumOrders() + 1);

		int sel = OrderGetSel();
		const Order *order = GetVehicleOrder(this->vehicle, sel);

		if (this->vehicle->owner == _local_company) {
			/* Set the strings for the dropdown boxes. */
			this->widget[ORDER_WIDGET_COND_VARIABLE].data   = _order_conditional_variable[order == NULL ? 0 : order->GetConditionVariable()];
			this->widget[ORDER_WIDGET_COND_COMPARATOR].data = _order_conditional_condition[order == NULL ? 0 : order->GetConditionComparator()];

			/* skip */
			this->SetWidgetDisabledState(ORDER_WIDGET_SKIP, this->vehicle->GetNumOrders() <= 1);

			/* delete */
			this->SetWidgetDisabledState(ORDER_WIDGET_DELETE,
					(uint)this->vehicle->GetNumOrders() + ((shared_orders || this->vehicle->GetNumOrders() != 0) ? 1 : 0) <= (uint)this->selected_order);

			/* non-stop only for trains */
			this->SetWidgetDisabledState(ORDER_WIDGET_NON_STOP,  (this->vehicle->type != VEH_TRAIN && this->vehicle->type != VEH_ROAD) || order == NULL);
			this->SetWidgetDisabledState(ORDER_WIDGET_NON_STOP_DROPDOWN, this->IsWidgetDisabled(ORDER_WIDGET_NON_STOP));
			this->SetWidgetDisabledState(ORDER_WIDGET_FULL_LOAD, order == NULL || (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // full load
			this->SetWidgetDisabledState(ORDER_WIDGET_FULL_LOAD_DROPDOWN, this->IsWidgetDisabled(ORDER_WIDGET_FULL_LOAD));
			this->SetWidgetDisabledState(ORDER_WIDGET_UNLOAD,    order == NULL || (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // unload
			this->SetWidgetDisabledState(ORDER_WIDGET_UNLOAD_DROPDOWN, this->IsWidgetDisabled(ORDER_WIDGET_UNLOAD));
			/* Disable list of vehicles with the same shared orders if there is no list */
			this->SetWidgetDisabledState(ORDER_WIDGET_SHARED_ORDER_LIST, !shared_orders);
			this->SetWidgetDisabledState(ORDER_WIDGET_REFIT,     order == NULL); // Refit
			this->SetWidgetDisabledState(ORDER_WIDGET_SERVICE,   order == NULL); // Service
			this->SetWidgetDisabledState(ORDER_WIDGET_SERVICE_DROPDOWN,   order == NULL); // Service
			this->HideWidget(ORDER_WIDGET_REFIT); // Refit
			this->HideWidget(ORDER_WIDGET_SERVICE); // Service
			this->HideWidget(ORDER_WIDGET_SERVICE_DROPDOWN); // Service

			this->HideWidget(ORDER_WIDGET_COND_VARIABLE);
			this->HideWidget(ORDER_WIDGET_COND_COMPARATOR);
			this->HideWidget(ORDER_WIDGET_COND_VALUE);
		}

		this->ShowWidget(ORDER_WIDGET_NON_STOP_DROPDOWN);
		this->ShowWidget(ORDER_WIDGET_NON_STOP);
		this->ShowWidget(ORDER_WIDGET_UNLOAD_DROPDOWN);
		this->ShowWidget(ORDER_WIDGET_UNLOAD);
		this->ShowWidget(ORDER_WIDGET_FULL_LOAD_DROPDOWN);
		this->ShowWidget(ORDER_WIDGET_FULL_LOAD);

		this->RaiseWidget(ORDER_WIDGET_NON_STOP);
		this->RaiseWidget(ORDER_WIDGET_FULL_LOAD);
		this->RaiseWidget(ORDER_WIDGET_UNLOAD);
		this->RaiseWidget(ORDER_WIDGET_SERVICE);

		if (order != NULL) {
			this->SetWidgetLoweredState(ORDER_WIDGET_NON_STOP, order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
			switch (order->GetType()) {
				case OT_GOTO_STATION:
					if (!GetStation(order->GetDestination())->IsBuoy()) {
						this->SetWidgetLoweredState(ORDER_WIDGET_FULL_LOAD, order->GetLoadType() == OLF_FULL_LOAD_ANY);
						this->SetWidgetLoweredState(ORDER_WIDGET_UNLOAD, order->GetUnloadType() == OUFB_UNLOAD);
						break;
					}
					/* Fall-through */

				case OT_GOTO_WAYPOINT:
					this->DisableWidget(ORDER_WIDGET_FULL_LOAD_DROPDOWN);
					this->DisableWidget(ORDER_WIDGET_FULL_LOAD);
					this->DisableWidget(ORDER_WIDGET_UNLOAD_DROPDOWN);
					this->DisableWidget(ORDER_WIDGET_UNLOAD);
					break;

				case OT_GOTO_DEPOT:
					/* Remove unload and replace it with refit */
					this->HideWidget(ORDER_WIDGET_UNLOAD_DROPDOWN);
					this->HideWidget(ORDER_WIDGET_UNLOAD);
					this->HideWidget(ORDER_WIDGET_FULL_LOAD_DROPDOWN);
					this->HideWidget(ORDER_WIDGET_FULL_LOAD);
					this->ShowWidget(ORDER_WIDGET_REFIT);
					this->ShowWidget(ORDER_WIDGET_SERVICE_DROPDOWN);
					this->ShowWidget(ORDER_WIDGET_SERVICE);
					this->SetWidgetLoweredState(ORDER_WIDGET_SERVICE, order->GetDepotOrderType() & ODTFB_SERVICE);
					break;

				case OT_CONDITIONAL: {
					this->HideWidget(ORDER_WIDGET_NON_STOP_DROPDOWN);
					this->HideWidget(ORDER_WIDGET_NON_STOP);
					this->HideWidget(ORDER_WIDGET_UNLOAD);
					this->HideWidget(ORDER_WIDGET_UNLOAD_DROPDOWN);
					this->HideWidget(ORDER_WIDGET_FULL_LOAD);
					this->HideWidget(ORDER_WIDGET_FULL_LOAD_DROPDOWN);
					this->ShowWidget(ORDER_WIDGET_COND_VARIABLE);
					this->ShowWidget(ORDER_WIDGET_COND_COMPARATOR);
					this->ShowWidget(ORDER_WIDGET_COND_VALUE);

					OrderConditionVariable ocv = order->GetConditionVariable();
					this->SetWidgetDisabledState(ORDER_WIDGET_COND_COMPARATOR, ocv == OCV_UNCONDITIONALLY);
					this->SetWidgetDisabledState(ORDER_WIDGET_COND_VALUE, ocv == OCV_REQUIRES_SERVICE || ocv == OCV_UNCONDITIONALLY);

					uint value = order->GetConditionValue();
					if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value);
					SetDParam(1, value);
				} break;

				default: // every other orders
					this->DisableWidget(ORDER_WIDGET_NON_STOP_DROPDOWN);
					this->DisableWidget(ORDER_WIDGET_NON_STOP);
					this->DisableWidget(ORDER_WIDGET_FULL_LOAD_DROPDOWN);
					this->DisableWidget(ORDER_WIDGET_FULL_LOAD);
					this->DisableWidget(ORDER_WIDGET_UNLOAD_DROPDOWN);
					this->DisableWidget(ORDER_WIDGET_UNLOAD);
			}
		}

		SetDParam(0, this->vehicle->index);
		this->DrawWidgets();

		int y = 15;

		int i = this->vscroll.pos;
		order = GetVehicleOrder(this->vehicle, i);
		StringID str;
		while (order != NULL) {
			/* Don't draw anything if it extends past the end of the window. */
			if (i - this->vscroll.pos >= this->vscroll.cap) break;

			DrawOrderString(this->vehicle, order, i, y, i == this->selected_order, false, this->widget[ORDER_WIDGET_ORDER_LIST].right - 4);
			y += 10;

			i++;
			order = order->next;
		}

		if (i - this->vscroll.pos < this->vscroll.cap) {
			str = shared_orders ? STR_END_OF_SHARED_ORDERS : STR_882A_END_OF_ORDERS;
			DrawString(2, y, str, (i == this->selected_order) ? TC_WHITE : TC_BLACK);
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case ORDER_WIDGET_ORDER_LIST: {
				ResetObjectToPlace();

				int sel = this->GetOrderFromPt(pt.y);

				if (_ctrl_pressed && sel < this->vehicle->GetNumOrders()) {
					const Order *ord = GetVehicleOrder(this->vehicle, sel);
					TileIndex xy = INVALID_TILE;

					switch (ord->GetType()) {
						case OT_GOTO_STATION:  xy = GetStation(ord->GetDestination())->xy ; break;
						case OT_GOTO_WAYPOINT: xy = GetWaypoint(ord->GetDestination())->xy; break;
						case OT_GOTO_DEPOT:
							if ((ord->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) break;
							xy = (this->vehicle->type == VEH_AIRCRAFT) ?  GetStation(ord->GetDestination())->xy : GetDepot(ord->GetDestination())->xy;
							break;
						default:
							break;
					}

					if (xy != INVALID_TILE) ScrollMainWindowToTile(xy);
					return;
				}

				/* This order won't be selected any more, close all child windows and dropdowns */
				this->DeleteChildWindows();
				HideDropDownMenu(this);

				if (sel == INVALID_ORDER || sel == this->selected_order) {
					/* Deselect clicked order */
					this->selected_order = -1;
				} else {
					/* Select clicked order */
					this->selected_order = sel;

					if (this->vehicle->owner == _local_company) {
						/* Activate drag and drop */
						SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, VHM_DRAG, this);
					}
				}

				this->SetDirty();
			} break;

			case ORDER_WIDGET_SKIP:
				OrderClick_Skip(this, 0);
				break;

			case ORDER_WIDGET_DELETE:
				OrderClick_Delete(this, 0);
				break;

			case ORDER_WIDGET_NON_STOP:
				OrderClick_Nonstop(this, -1);
				break;

			case ORDER_WIDGET_NON_STOP_DROPDOWN: {
				const Order *o = GetVehicleOrder(this->vehicle, this->OrderGetSel());
				ShowDropDownMenu(this, _order_non_stop_drowdown, o->GetNonStopType(), ORDER_WIDGET_NON_STOP_DROPDOWN, 0, o->IsType(OT_GOTO_STATION) ? 0 : (o->IsType(OT_GOTO_WAYPOINT) ? 3 : 12));
			} break;

			case ORDER_WIDGET_GOTO:
				OrderClick_Goto(this, 0);
				break;

			case ORDER_WIDGET_GOTO_DROPDOWN:
				ShowDropDownMenu(this, this->vehicle->type == VEH_AIRCRAFT ? _order_goto_dropdown_aircraft : _order_goto_dropdown, 0, ORDER_WIDGET_GOTO_DROPDOWN, 0, 0);
				break;

			case ORDER_WIDGET_FULL_LOAD:
				OrderClick_FullLoad(this, -1);
				break;

			case ORDER_WIDGET_FULL_LOAD_DROPDOWN:
				ShowDropDownMenu(this, _order_full_load_drowdown, GetVehicleOrder(this->vehicle, this->OrderGetSel())->GetLoadType(), ORDER_WIDGET_FULL_LOAD_DROPDOWN, 0, 2);
				break;

			case ORDER_WIDGET_UNLOAD:
				OrderClick_Unload(this, -1);
				break;

			case ORDER_WIDGET_UNLOAD_DROPDOWN:
				ShowDropDownMenu(this, _order_unload_drowdown, GetVehicleOrder(this->vehicle, this->OrderGetSel())->GetUnloadType(), ORDER_WIDGET_UNLOAD_DROPDOWN, 0, 8);
				break;

			case ORDER_WIDGET_REFIT:
				OrderClick_Refit(this, 0);
				break;

			case ORDER_WIDGET_SERVICE:
				OrderClick_Service(this, -1);
				break;

			case ORDER_WIDGET_SERVICE_DROPDOWN:
				ShowDropDownMenu(this, _order_depot_action_dropdown, DepotActionStringIndex(GetVehicleOrder(this->vehicle, this->OrderGetSel())), ORDER_WIDGET_SERVICE_DROPDOWN, 0, 0);
				break;

			case ORDER_WIDGET_TIMETABLE_VIEW:
				ShowTimetableWindow(this->vehicle);
				break;

			case ORDER_WIDGET_COND_VARIABLE:
				ShowDropDownMenu(this, _order_conditional_variable, GetVehicleOrder(this->vehicle, this->OrderGetSel())->GetConditionVariable(), ORDER_WIDGET_COND_VARIABLE, 0, 0);
				break;

			case ORDER_WIDGET_COND_COMPARATOR: {
				const Order *o = GetVehicleOrder(this->vehicle, this->OrderGetSel());
				ShowDropDownMenu(this, _order_conditional_condition, o->GetConditionComparator(), ORDER_WIDGET_COND_COMPARATOR, 0, (o->GetConditionVariable() == OCV_REQUIRES_SERVICE) ? 0x3F : 0xC0);
			} break;

			case ORDER_WIDGET_COND_VALUE: {
				const Order *order = GetVehicleOrder(this->vehicle, this->OrderGetSel());
				uint value = order->GetConditionValue();
				if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value);
				SetDParam(0, value);
				ShowQueryString(STR_CONFIG_SETTING_INT32, STR_ORDER_CONDITIONAL_VALUE_CAPT, 5, 100, this, CS_NUMERAL, QSF_NONE);
			} break;

			case ORDER_WIDGET_SHARED_ORDER_LIST:
				ShowVehicleListWindow(this->vehicle);
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (!StrEmpty(str)) {
			VehicleOrderID sel = this->OrderGetSel();
			uint value = atoi(str);

			switch (GetVehicleOrder(this->vehicle, sel)->GetConditionVariable()) {
				case OCV_MAX_SPEED:
					value = ConvertDisplaySpeedToSpeed(value);
					break;

				case OCV_RELIABILITY:
				case OCV_LOAD_PERCENTAGE:
					value = Clamp(value, 0, 100);

				default:
					break;
			}
			DoCommandP(this->vehicle->tile, this->vehicle->index + (sel << 16), MOF_COND_VALUE | Clamp(value, 0, 2047) << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case ORDER_WIDGET_NON_STOP_DROPDOWN:
				OrderClick_Nonstop(this, index);
				break;

			case ORDER_WIDGET_FULL_LOAD_DROPDOWN:
				OrderClick_FullLoad(this, index);
				break;

			case ORDER_WIDGET_UNLOAD_DROPDOWN:
				OrderClick_Unload(this, index);
				break;

			case ORDER_WIDGET_GOTO_DROPDOWN:
				switch (index) {
					case 0: OrderClick_Goto(this, 0); break;
					case 1: OrderClick_NearestDepot(this, 0); break;
					case 2: OrderClick_Conditional(this, 0); break;
					default: NOT_REACHED();
				}
				break;

			case ORDER_WIDGET_SERVICE_DROPDOWN:
				OrderClick_Service(this, index);
				break;

			case ORDER_WIDGET_COND_VARIABLE:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 16), MOF_COND_VARIABLE | index << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
				break;

			case ORDER_WIDGET_COND_COMPARATOR:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 16), MOF_COND_COMPARATOR | index << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
				break;
		}
	}

	virtual void OnDragDrop(Point pt, int widget)
	{
		switch (widget) {
			case ORDER_WIDGET_ORDER_LIST: {
				int from_order = this->OrderGetSel();
				int to_order = this->GetOrderFromPt(pt.y);

				if (!(from_order == to_order || from_order == INVALID_ORDER || from_order > this->vehicle->GetNumOrders() || to_order == INVALID_ORDER || to_order > this->vehicle->GetNumOrders()) &&
						DoCommandP(this->vehicle->tile, this->vehicle->index, from_order | (to_order << 16), CMD_MOVE_ORDER | CMD_MSG(STR_CAN_T_MOVE_THIS_ORDER))) {
					this->selected_order = -1;
				}
			} break;

			case ORDER_WIDGET_DELETE:
				OrderClick_Delete(this, 0);
				break;
		}

		ResetObjectToPlace();
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		static const KeyToEvent keytoevent[] = {
			{'D', OrderClick_Skip},
			{'F', OrderClick_Delete},
			{'G', OrderClick_Goto},
			{'H', OrderClick_Nonstop},
			{'J', OrderClick_FullLoad},
			{'K', OrderClick_Unload},
			//('?', OrderClick_Service},
		};

		if (this->vehicle->owner != _local_company) return ES_NOT_HANDLED;

		for (uint i = 0; i < lengthof(keytoevent); i++) {
			if (keycode == keytoevent[i].keycode) {
				keytoevent[i].proc(this, -1);
				return ES_HANDLED;
			}
		}
		return ES_NOT_HANDLED;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		if (this->goto_type == OPOS_GOTO) {
			/* check if we're clicking on a vehicle first.. clone orders in that case. */
			const Vehicle *v = CheckMouseOverVehicle();
			if (v != NULL && this->HandleOrderVehClick(v)) return;

			const Order cmd = GetOrderCmdFromTile(this->vehicle, tile);
			if (!cmd.IsValid()) return;

			if (DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 16), cmd.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_8833_CAN_T_INSERT_NEW_ORDER))) {
				/* With quick goto the Go To button stays active */
				if (!_settings_client.gui.quick_goto) ResetObjectToPlace();
			}
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		if (this->goto_type == OPOS_CONDITIONAL) {
			this->goto_type = OPOS_GOTO;
			if (_cursor.pos.x >= (this->left + this->widget[ORDER_WIDGET_ORDER_LIST].left) &&
					_cursor.pos.y >= (this->top  + this->widget[ORDER_WIDGET_ORDER_LIST].top) &&
					_cursor.pos.x <= (this->left + this->widget[ORDER_WIDGET_ORDER_LIST].right) &&
					_cursor.pos.y <= (this->top  + this->widget[ORDER_WIDGET_ORDER_LIST].bottom)) {
				int order_id = this->GetOrderFromPt(_cursor.pos.y - this->top);
				if (order_id != INVALID_ORDER) {
					Order order;
					order.next = NULL;
					order.index = 0;
					order.MakeConditional(order_id);

					DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 16), order.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_8833_CAN_T_INSERT_NEW_ORDER));
				}
			}
		}
		this->RaiseWidget(ORDER_WIDGET_GOTO);
		this->InvalidateWidget(ORDER_WIDGET_GOTO);
	}

	virtual void OnMouseLoop()
	{
		const Vehicle *v = _place_clicked_vehicle;
		/*
		 * Check if we clicked on a vehicle
		 * and if the GOTO button of this window is pressed
		 * This is because of all open order windows WE_MOUSELOOP is called
		 * and if you have 3 windows open, and this check is not done
		 * the order is copied to the last open window instead of the
		 * one where GOTO is enabled
		 */
		if (v != NULL && this->IsWidgetLowered(ORDER_WIDGET_GOTO)) {
			_place_clicked_vehicle = NULL;
			this->HandleOrderVehClick(v);
		}
	}

	virtual void OnResize(Point new_size, Point delta)
	{
		/* Update the scroll + matrix */
		this->vscroll.cap = (this->widget[ORDER_WIDGET_ORDER_LIST].bottom - this->widget[ORDER_WIDGET_ORDER_LIST].top) / 10;
	}

	virtual void OnTimeout()
	{
		/* unclick all buttons except for the 'goto' button (ORDER_WIDGET_GOTO), which is 'persistent' */
		for (uint i = 0; i < this->widget_count; i++) {
			if (this->IsWidgetLowered(i) && i != ORDER_WIDGET_GOTO) {
				this->RaiseWidget(i);
				this->InvalidateWidget(i);
			}
		}
	}
};

/**
 * Widget definition for "your" train orders
 */
static const Widget _orders_train_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,  COLOUR_GREY,    11,   373,     0,    13, STR_8829_ORDERS,         STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,     COLOUR_GREY,   313,   373,     0,    13, STR_TIMETABLE_VIEW,      STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW
	{  WWT_STICKYBOX,   RESIZE_LR,     COLOUR_GREY,   374,   385,     0,    13, STR_NULL,                STR_STICKY_BUTTON},                   // ORDER_WIDGET_STICKY

	{      WWT_PANEL,   RESIZE_RB,     COLOUR_GREY,     0,   373,    14,    75, 0x0,                     STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,    COLOUR_GREY,   374,   385,    14,    75, 0x0,                     STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,     0,   123,    88,    99, STR_8823_SKIP,           STR_8853_SKIP_THE_CURRENT_ORDER},     // ORDER_WIDGET_SKIP
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    88,    99, STR_8824_DELETE,         STR_8854_DELETE_THE_HIGHLIGHTED},     // ORDER_WIDGET_DELETE
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,     0,   123,    76,    87, STR_NULL,                STR_ORDER_TOOLTIP_NON_STOP},          // ORDER_WIDGET_NON_STOP_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,     0,   111,    76,    87, STR_ORDER_NON_STOP,      STR_ORDER_TOOLTIP_NON_STOP},          // ORDER_WIDGET_NON_STOP
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    88,    99, STR_EMPTY,               STR_ORDER_GO_TO_DROPDOWN_TOOLTIP},    // ORDER_WIDGET_GOTO_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   359,    88,    99, STR_8826_GO_TO,          STR_8856_INSERT_A_NEW_ORDER_BEFORE},  // ORDER_WIDGET_GOTO
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    76,    87, STR_NULL,                STR_ORDER_TOOLTIP_FULL_LOAD},         // ORDER_WIDGET_FULL_LOAD_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   124,   235,    76,    87, STR_ORDER_TOGGLE_FULL_LOAD, STR_ORDER_TOOLTIP_FULL_LOAD},      // ORDER_WIDGET_FULL_LOAD
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    76,    87, STR_NULL,                STR_ORDER_TOOLTIP_UNLOAD},            // ORDER_WIDGET_UNLOAD_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   359,    76,    87, STR_ORDER_TOGGLE_UNLOAD, STR_ORDER_TOOLTIP_UNLOAD},            // ORDER_WIDGET_UNLOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    76,    87, STR_REFIT,               STR_REFIT_TIP},                       // ORDER_WIDGET_REFIT
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    76,    87, STR_NULL,                STR_SERVICE_HINT},                    // ORDER_WIDGET_SERVICE_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   359,    76,    87, STR_SERVICE,             STR_SERVICE_HINT},                    // ORDER_WIDGET_SERVICE

	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,     0,   123,    76,    87, STR_NULL,                STR_ORDER_CONDITIONAL_VARIABLE_TOOLTIP},   // ORDER_WIDGET_COND_VARIABLE
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    76,    87, STR_NULL,                STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP}, // ORDER_WIDGET_COND_COMPARATOR
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    76,    87, STR_CONDITIONAL_VALUE,   STR_ORDER_CONDITIONAL_VALUE_TOOLTIP},      // ORDER_WIDGET_COND_VALUE

	{      WWT_PANEL,   RESIZE_RTB,    COLOUR_GREY,   372,   373,    76,    99, 0x0,                     STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{ WWT_PUSHIMGBTN,   RESIZE_LRTB,   COLOUR_GREY,   372,   385,    76,    87, SPR_SHARED_ORDERS_ICON,  STR_VEH_WITH_SHARED_ORDERS_LIST_TIP}, // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,   COLOUR_GREY,   374,   385,    88,    99, 0x0,                     STR_RESIZE_BUTTON},                   // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _orders_train_desc(
	WDP_AUTO, WDP_AUTO, 386, 100, 386, 100,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_orders_train_widgets
);

/**
 * Widget definition for "your" orders (!train)
 */
static const Widget _orders_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,  COLOUR_GREY,    11,   373,     0,    13, STR_8829_ORDERS,         STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,     COLOUR_GREY,   313,   373,     0,    13, STR_TIMETABLE_VIEW,      STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW
	{  WWT_STICKYBOX,   RESIZE_LR,     COLOUR_GREY,   374,   385,     0,    13, STR_NULL,                STR_STICKY_BUTTON},                   // ORDER_WIDGET_STICKY

	{      WWT_PANEL,   RESIZE_RB,     COLOUR_GREY,     0,   373,    14,    75, 0x0,                     STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,    COLOUR_GREY,   374,   385,    14,    75, 0x0,                     STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,     0,   123,    88,    99, STR_8823_SKIP,           STR_8853_SKIP_THE_CURRENT_ORDER},     // ORDER_WIDGET_SKIP
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    88,    99, STR_8824_DELETE,         STR_8854_DELETE_THE_HIGHLIGHTED},     // ORDER_WIDGET_DELETE
	{      WWT_EMPTY,   RESIZE_TB,     COLOUR_GREY,     0,     0,    76,    87, 0x0,                     0x0},                                 // ORDER_WIDGET_NON_STOP_DROPDOWN
	{      WWT_EMPTY,   RESIZE_TB,     COLOUR_GREY,     0,     0,    76,    87, 0x0,                     0x0},                                 // ORDER_WIDGET_NON_STOP
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    88,    99, STR_EMPTY,               STR_ORDER_GO_TO_DROPDOWN_TOOLTIP},    // ORDER_WIDGET_GOTO_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   359,    88,    99, STR_8826_GO_TO,          STR_8856_INSERT_A_NEW_ORDER_BEFORE},  // ORDER_WIDGET_GOTO
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,     0,   185,    76,    87, STR_NULL,                STR_ORDER_TOOLTIP_FULL_LOAD},         // ORDER_WIDGET_FULL_LOAD_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,     0,   173,    76,    87, STR_ORDER_TOGGLE_FULL_LOAD, STR_ORDER_TOOLTIP_FULL_LOAD},      // ORDER_WIDGET_FULL_LOAD
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   186,   371,    76,    87, STR_NULL,                STR_ORDER_TOOLTIP_UNLOAD},            // ORDER_WIDGET_UNLOAD_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   186,   359,    76,    87, STR_ORDER_TOGGLE_UNLOAD, STR_ORDER_TOOLTIP_UNLOAD},            // ORDER_WIDGET_UNLOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,     0,   185,    76,    87, STR_REFIT,               STR_REFIT_TIP},                       // ORDER_WIDGET_REFIT
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   186,   371,    76,    87, STR_NULL,                STR_SERVICE_HINT},                    // ORDER_WIDGET_SERVICE_DROPDOWN
	{    WWT_TEXTBTN,   RESIZE_TB,     COLOUR_GREY,   186,   359,    76,    87, STR_SERVICE,             STR_SERVICE_HINT},                    // ORDER_WIDGET_SERVICE

	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,     0,   123,    76,    87, STR_NULL,                STR_ORDER_CONDITIONAL_VARIABLE_TOOLTIP},   // ORDER_WIDGET_COND_VARIABLE
	{   WWT_DROPDOWN,   RESIZE_TB,     COLOUR_GREY,   124,   247,    76,    87, STR_NULL,                STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP}, // ORDER_WIDGET_COND_COMPARATOR
	{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_GREY,   248,   371,    76,    87, STR_CONDITIONAL_VALUE,   STR_ORDER_CONDITIONAL_VALUE_TOOLTIP},      // ORDER_WIDGET_COND_VALUE

	{      WWT_PANEL,   RESIZE_RTB,    COLOUR_GREY,   372,   373,    76,    99, 0x0,                     STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{ WWT_PUSHIMGBTN,   RESIZE_LRTB,   COLOUR_GREY,   372,   385,    76,    87, SPR_SHARED_ORDERS_ICON,  STR_VEH_WITH_SHARED_ORDERS_LIST_TIP}, // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,   COLOUR_GREY,   374,   385,    88,    99, 0x0,                     STR_RESIZE_BUTTON},                   // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _orders_desc(
	WDP_AUTO, WDP_AUTO, 386, 100, 386, 100,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_orders_widgets
);

/**
 * Widget definition for competitor orders
 */
static const Widget _other_orders_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_GREY,     0,    10,     0,    13, STR_00C5,           STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,  COLOUR_GREY,    11,   373,     0,    13, STR_8829_ORDERS,    STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,     COLOUR_GREY,   313,   373,     0,    13, STR_TIMETABLE_VIEW, STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW
	{  WWT_STICKYBOX,   RESIZE_LR,     COLOUR_GREY,   374,   385,     0,    13, STR_NULL,           STR_STICKY_BUTTON},                   // ORDER_WIDGET_STICKY

	{      WWT_PANEL,   RESIZE_RB,     COLOUR_GREY,     0,   373,    14,    75, 0x0,                STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,    COLOUR_GREY,   374,   385,    14,    75, 0x0,                STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SKIP
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_DELETE
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_NON_STOP_DROPDOWN
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_NON_STOP
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_GOTO_DROPDOWN
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_GOTO
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_FULL_LOAD_DROPDOWN
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_FULL_LOAD
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_UNLOAD_DROPDOWN
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_UNLOAD
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_REFIT
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SERVICE_DROPDOWN
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SERVICE

	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_COND_VARIABLE
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_COND_COMPARATOR
	{      WWT_EMPTY,   RESIZE_NONE,   COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_COND_VALUE

	{      WWT_PANEL,   RESIZE_RTB,    COLOUR_GREY,     0,   373,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{      WWT_EMPTY,   RESIZE_TB,     COLOUR_GREY,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,   COLOUR_GREY,   374,   385,    76,    87, 0x0,                STR_RESIZE_BUTTON},                   // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _other_orders_desc(
	WDP_AUTO, WDP_AUTO, 386, 88, 386, 88,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE | WDF_CONSTRUCTION,
	_other_orders_widgets
);

void ShowOrdersWindow(const Vehicle *v)
{
	DeleteWindowById(WC_VEHICLE_DETAILS, v->index, false);
	DeleteWindowById(WC_VEHICLE_TIMETABLE, v->index, false);
	if (BringWindowToFrontById(WC_VEHICLE_ORDERS, v->index) != NULL) return;

	if (v->owner != _local_company) {
		new OrdersWindow(&_other_orders_desc, v);
	} else {
		new OrdersWindow((v->type == VEH_TRAIN || v->type == VEH_ROAD) ? &_orders_train_desc : &_orders_desc, v);
	}
}
