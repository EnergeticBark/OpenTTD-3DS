/* $Id$ */

/** @file ai_instance.cpp Implementation of AIInstance. */

#include "../stdafx.h"
#include "../debug.h"
#include "../settings_type.h"
#include "../vehicle_base.h"
#include "../saveload/saveload.h"
#include "../gui.h"
#include "table/strings.h"

#include <squirrel.h>
#include "../script/squirrel.hpp"
#include "../script/squirrel_helper.hpp"
#include "../script/squirrel_class.hpp"
#include "../script/squirrel_std.hpp"

#define DEFINE_SCRIPT_FILES

#include "ai_info.hpp"
#include "ai_storage.hpp"
#include "ai_instance.hpp"
#include "ai_gui.hpp"

/* Convert all AI related classes to Squirrel data.
 * Note: this line a marker in squirrel_export.sh. Do not change! */
#include "api/ai_abstractlist.hpp.sq"
#include "api/ai_accounting.hpp.sq"
#include "api/ai_airport.hpp.sq"
#include "api/ai_base.hpp.sq"
#include "api/ai_bridge.hpp.sq"
#include "api/ai_bridgelist.hpp.sq"
#include "api/ai_cargo.hpp.sq"
#include "api/ai_cargolist.hpp.sq"
#include "api/ai_company.hpp.sq"
#include "api/ai_controller.hpp.sq"
#include "api/ai_date.hpp.sq"
#include "api/ai_depotlist.hpp.sq"
#include "api/ai_engine.hpp.sq"
#include "api/ai_enginelist.hpp.sq"
#include "api/ai_error.hpp.sq"
#include "api/ai_event.hpp.sq"
#include "api/ai_event_types.hpp.sq"
#include "api/ai_execmode.hpp.sq"
#include "api/ai_gamesettings.hpp.sq"
#include "api/ai_group.hpp.sq"
#include "api/ai_grouplist.hpp.sq"
#include "api/ai_industry.hpp.sq"
#include "api/ai_industrylist.hpp.sq"
#include "api/ai_industrytype.hpp.sq"
#include "api/ai_industrytypelist.hpp.sq"
#include "api/ai_list.hpp.sq"
#include "api/ai_log.hpp.sq"
#include "api/ai_map.hpp.sq"
#include "api/ai_marine.hpp.sq"
#include "api/ai_order.hpp.sq"
#include "api/ai_rail.hpp.sq"
#include "api/ai_railtypelist.hpp.sq"
#include "api/ai_road.hpp.sq"
#include "api/ai_sign.hpp.sq"
#include "api/ai_station.hpp.sq"
#include "api/ai_stationlist.hpp.sq"
#include "api/ai_subsidy.hpp.sq"
#include "api/ai_subsidylist.hpp.sq"
#include "api/ai_testmode.hpp.sq"
#include "api/ai_tile.hpp.sq"
#include "api/ai_tilelist.hpp.sq"
#include "api/ai_town.hpp.sq"
#include "api/ai_townlist.hpp.sq"
#include "api/ai_tunnel.hpp.sq"
#include "api/ai_vehicle.hpp.sq"
#include "api/ai_vehiclelist.hpp.sq"
#include "api/ai_waypoint.hpp.sq"
#include "api/ai_waypointlist.hpp.sq"

#undef DEFINE_SCRIPT_FILES

/* static */ AIInstance *AIInstance::current_instance = NULL;

AIStorage::~AIStorage()
{
	/* Free our pointers */
	if (event_data != NULL) AIEventController::FreeEventPointer();
	if (log_data != NULL) AILog::FreeLogPointer();
}

static void PrintFunc(bool error_msg, const SQChar *message)
{
	/* Convert to OpenTTD internal capable string */
	AIController::Print(error_msg, FS2OTTD(message));
}

AIInstance::AIInstance(AIInfo *info) :
	controller(NULL),
	storage(NULL),
	engine(NULL),
	instance(NULL),
	is_started(false),
	is_dead(false),
	suspend(0),
	callback(NULL)
{
	/* Set the instance already, so we can use AIObject::Set commands */
	GetCompany(_current_company)->ai_instance = this;
	AIInstance::current_instance = this;

	this->controller = new AIController();
	this->storage    = new AIStorage();
	this->engine     = new Squirrel();
	this->engine->SetPrintFunction(&PrintFunc);

	/* The import method is available at a very early stage */
	this->engine->AddMethod("import", &AILibrary::Import, 4, ".ssi");

	/* Register the AIController */
	SQAIController_Register(this->engine);

	/* Load and execute the script for this AI */
	const char *main_script = info->GetMainScript();
	if (strcmp(main_script, "%_dummy") == 0) {
		extern void AI_CreateAIDummy(HSQUIRRELVM vm);
		AI_CreateAIDummy(this->engine->GetVM());
	} else if (!this->engine->LoadScript(main_script)) {
		this->Died();
		return;
	}

	/* Create the main-class */
	this->instance = MallocT<SQObject>(1);
	if (!this->engine->CreateClassInstance(info->GetInstanceName(), this->controller, this->instance)) {
		this->Died();
		return;
	}

	/* Register the API functions and classes */
	this->RegisterAPI();

	/* The topmost stack item is true if there is data from a savegame
	 * and false otherwise. */
	sq_pushbool(this->engine->vm, false);
}

AIInstance::~AIInstance()
{
	if (instance != NULL) this->engine->ReleaseObject(this->instance);
	if (engine != NULL) delete this->engine;
	delete this->storage;
	delete this->controller;
	free(this->instance);
}

void AIInstance::RegisterAPI()
{
/* Register all classes */
	squirrel_register_std(this->engine);
	SQAIAbstractList_Register(this->engine);
	SQAIAccounting_Register(this->engine);
	SQAIAirport_Register(this->engine);
	SQAIBase_Register(this->engine);
	SQAIBridge_Register(this->engine);
	SQAIBridgeList_Register(this->engine);
	SQAIBridgeList_Length_Register(this->engine);
	SQAICargo_Register(this->engine);
	SQAICargoList_Register(this->engine);
	SQAICargoList_IndustryAccepting_Register(this->engine);
	SQAICargoList_IndustryProducing_Register(this->engine);
	SQAICompany_Register(this->engine);
	SQAIDate_Register(this->engine);
	SQAIDepotList_Register(this->engine);
	SQAIEngine_Register(this->engine);
	SQAIEngineList_Register(this->engine);
	SQAIError_Register(this->engine);
	SQAIEvent_Register(this->engine);
	SQAIEventCompanyBankrupt_Register(this->engine);
	SQAIEventCompanyInTrouble_Register(this->engine);
	SQAIEventCompanyMerger_Register(this->engine);
	SQAIEventCompanyNew_Register(this->engine);
	SQAIEventController_Register(this->engine);
	SQAIEventDisasterZeppelinerCleared_Register(this->engine);
	SQAIEventDisasterZeppelinerCrashed_Register(this->engine);
	SQAIEventEngineAvailable_Register(this->engine);
	SQAIEventEnginePreview_Register(this->engine);
	SQAIEventIndustryClose_Register(this->engine);
	SQAIEventIndustryOpen_Register(this->engine);
	SQAIEventStationFirstVehicle_Register(this->engine);
	SQAIEventSubsidyAwarded_Register(this->engine);
	SQAIEventSubsidyExpired_Register(this->engine);
	SQAIEventSubsidyOffer_Register(this->engine);
	SQAIEventSubsidyOfferExpired_Register(this->engine);
	SQAIEventVehicleCrashed_Register(this->engine);
	SQAIEventVehicleLost_Register(this->engine);
	SQAIEventVehicleUnprofitable_Register(this->engine);
	SQAIEventVehicleWaitingInDepot_Register(this->engine);
	SQAIExecMode_Register(this->engine);
	SQAIGameSettings_Register(this->engine);
	SQAIGroup_Register(this->engine);
	SQAIGroupList_Register(this->engine);
	SQAIIndustry_Register(this->engine);
	SQAIIndustryList_Register(this->engine);
	SQAIIndustryList_CargoAccepting_Register(this->engine);
	SQAIIndustryList_CargoProducing_Register(this->engine);
	SQAIIndustryType_Register(this->engine);
	SQAIIndustryTypeList_Register(this->engine);
	SQAIList_Register(this->engine);
	SQAILog_Register(this->engine);
	SQAIMap_Register(this->engine);
	SQAIMarine_Register(this->engine);
	SQAIOrder_Register(this->engine);
	SQAIRail_Register(this->engine);
	SQAIRailTypeList_Register(this->engine);
	SQAIRoad_Register(this->engine);
	SQAISign_Register(this->engine);
	SQAIStation_Register(this->engine);
	SQAIStationList_Register(this->engine);
	SQAIStationList_Vehicle_Register(this->engine);
	SQAISubsidy_Register(this->engine);
	SQAISubsidyList_Register(this->engine);
	SQAITestMode_Register(this->engine);
	SQAITile_Register(this->engine);
	SQAITileList_Register(this->engine);
	SQAITileList_IndustryAccepting_Register(this->engine);
	SQAITileList_IndustryProducing_Register(this->engine);
	SQAITileList_StationType_Register(this->engine);
	SQAITown_Register(this->engine);
	SQAITownList_Register(this->engine);
	SQAITunnel_Register(this->engine);
	SQAIVehicle_Register(this->engine);
	SQAIVehicleList_Register(this->engine);
	SQAIVehicleList_DefaultGroup_Register(this->engine);
	SQAIVehicleList_Group_Register(this->engine);
	SQAIVehicleList_SharedOrders_Register(this->engine);
	SQAIVehicleList_Station_Register(this->engine);
	SQAIWaypoint_Register(this->engine);
	SQAIWaypointList_Register(this->engine);
	SQAIWaypointList_Vehicle_Register(this->engine);

	this->engine->SetGlobalPointer(this->engine);
}

void AIInstance::Continue()
{
	assert(this->suspend < 0);
	this->suspend = -this->suspend - 1;
}

void AIInstance::Died()
{
	DEBUG(ai, 0, "The AI died unexpectedly.");
	this->is_dead = true;

	if (this->instance != NULL) this->engine->ReleaseObject(this->instance);
	delete this->engine;
	this->instance = NULL;
	this->engine = NULL;

	ShowAIDebugWindow(_current_company);
	if (strcmp(GetCompany(_current_company)->ai_info->GetMainScript(), "%_dummy") != 0) {
		ShowErrorMessage(INVALID_STRING_ID, STR_AI_PLEASE_REPORT_CRASH, 0, 0);
	}
}

void AIInstance::GameLoop()
{
	if (this->is_dead) return;
	if (this->engine->HasScriptCrashed()) {
		/* The script crashed during saving, kill it here. */
		this->Died();
		return;
	}
	this->controller->ticks++;

	if (this->suspend   < -1) this->suspend++; // Multiplayer suspend, increase up to -1.
	if (this->suspend   < 0)  return;          // Multiplayer suspend, wait for Continue().
	if (--this->suspend > 0)  return;          // Singleplayer suspend, decrease to 0.

	/* If there is a callback to call, call that first */
	if (this->callback != NULL) {
		try {
			this->callback(this);
		} catch (AI_VMSuspend e) {
			this->suspend  = e.GetSuspendTime();
			this->callback = e.GetSuspendCallback();

			return;
		}
	}

	this->suspend  = 0;
	this->callback = NULL;

	if (!this->is_started) {
		try {
			AIObject::SetAllowDoCommand(false);
			/* Run the constructor if it exists. Don't allow any DoCommands in it. */
			if (this->engine->MethodExists(*this->instance, "constructor")) {
				if (!this->engine->CallMethod(*this->instance, "constructor")) { this->Died(); return; }
			}
			if (!this->CallLoad()) { this->Died(); return; }
			AIObject::SetAllowDoCommand(true);
			/* Start the AI by calling Start() */
			if (!this->engine->CallMethod(*this->instance, "Start",  _settings_game.ai.ai_max_opcode_till_suspend) || !this->engine->IsSuspended()) this->Died();
		} catch (AI_VMSuspend e) {
			this->suspend  = e.GetSuspendTime();
			this->callback = e.GetSuspendCallback();
		}

		this->is_started = true;
		return;
	}

	/* Continue the VM */
	try {
		if (!this->engine->Resume(_settings_game.ai.ai_max_opcode_till_suspend)) this->Died();
	} catch (AI_VMSuspend e) {
		this->suspend  = e.GetSuspendTime();
		this->callback = e.GetSuspendCallback();
	}
}

void AIInstance::CollectGarbage()
{
	if (this->is_started && !this->is_dead) this->engine->CollectGarbage();
}

/* static */ void AIInstance::DoCommandReturn(AIInstance *instance)
{
	instance->engine->InsertResult(AIObject::GetLastCommandRes());
}

/* static */ void AIInstance::DoCommandReturnVehicleID(AIInstance *instance)
{
	instance->engine->InsertResult(AIObject::GetNewVehicleID());
}

/* static */ void AIInstance::DoCommandReturnSignID(AIInstance *instance)
{
	instance->engine->InsertResult(AIObject::GetNewSignID());
}

/* static */ void AIInstance::DoCommandReturnGroupID(AIInstance *instance)
{
	instance->engine->InsertResult(AIObject::GetNewGroupID());
}

/* static */ AIStorage *AIInstance::GetStorage()
{
	assert(IsValidCompanyID(_current_company) && !IsHumanCompany(_current_company));
	return GetCompany(_current_company)->ai_instance->storage;
}

/*
 * All data is stored in the following format:
 * First 1 byte indicating if there is a data blob at all.
 * 1 byte indicating the type of data.
 * The data itself, this differs per type:
 *  - integer: a binary representation of the integer (int32).
 *  - string:  First one byte with the string length, then a 0-terminated char
 *             array. The string can't be longer then 255 bytes (including
 *             terminating '\0').
 *  - array:   All data-elements of the array are saved recursive in this
 *             format, and ended with an element of the type
 *             SQSL_ARRAY_TABLE_END.
 *  - table:   All key/value pairs are saved in this format (first key 1, then
 *             value 1, then key 2, etc.). All keys and values can have an
 *             arbitrary type (as long as it is supported by the save function
 *             of course). The table is ended with an element of the type
 *             SQSL_ARRAY_TABLE_END.
 *  - bool:    A single byte with value 1 representing true and 0 false.
 *  - null:    No data.
 */

/** The type of the data that follows in the savegame. */
enum SQSaveLoadType {
	SQSL_INT             = 0x00, ///< The following data is an integer.
	SQSL_STRING          = 0x01, ///< The following data is an string.
	SQSL_ARRAY           = 0x02, ///< The following data is an array.
	SQSL_TABLE           = 0x03, ///< The following data is an table.
	SQSL_BOOL            = 0x04, ///< The following data is a boolean.
	SQSL_NULL            = 0x05, ///< A null variable.
	SQSL_ARRAY_TABLE_END = 0xFF, ///< Marks the end of an array or table, no data follows.
};

static byte _ai_sl_byte;

static const SaveLoad _ai_byte[] = {
	SLEG_VAR(_ai_sl_byte, SLE_UINT8),
	SLE_END()
};

enum {
	AISAVE_MAX_DEPTH = 25, ///< The maximum recursive depth for items stored in the savegame.
};

/* static */ bool AIInstance::SaveObject(HSQUIRRELVM vm, SQInteger index, int max_depth, bool test)
{
	if (max_depth == 0) {
		AILog::Error("Savedata can only be nested to 25 deep. No data saved.");
		return false;
	}

	switch (sq_gettype(vm, index)) {
		case OT_INTEGER: {
			if (!test) {
				_ai_sl_byte = SQSL_INT;
				SlObject(NULL, _ai_byte);
			}
			SQInteger res;
			sq_getinteger(vm, index, &res);
			if (!test) {
				int value = (int)res;
				SlArray(&value, 1, SLE_INT32);
			}
			return true;
		}

		case OT_STRING: {
			if (!test) {
				_ai_sl_byte = SQSL_STRING;
				SlObject(NULL, _ai_byte);
			}
			const SQChar *res;
			sq_getstring(vm, index, &res);
			/* @bug if a string longer than 512 characters is given to FS2OTTD, the
			 *  internal buffer overflows. */
			const char *buf = FS2OTTD(res);
			size_t len = strlen(buf) + 1;
			if (len >= 255) {
				AILog::Error("Maximum string length is 254 chars. No data saved.");
				return false;
			}
			if (!test) {
				_ai_sl_byte = (byte)len;
				SlObject(NULL, _ai_byte);
				SlArray((void*)buf, len, SLE_CHAR);
			}
			return true;
		}

		case OT_ARRAY: {
			if (!test) {
				_ai_sl_byte = SQSL_ARRAY;
				SlObject(NULL, _ai_byte);
			}
			sq_pushnull(vm);
			while (SQ_SUCCEEDED(sq_next(vm, index - 1))) {
				/* Store the value */
				bool res = SaveObject(vm, -1, max_depth - 1, test);
				sq_pop(vm, 2);
				if (!res) {
					sq_pop(vm, 1);
					return false;
				}
			}
			sq_pop(vm, 1);
			if (!test) {
				_ai_sl_byte = SQSL_ARRAY_TABLE_END;
				SlObject(NULL, _ai_byte);
			}
			return true;
		}

		case OT_TABLE: {
			if (!test) {
				_ai_sl_byte = SQSL_TABLE;
				SlObject(NULL, _ai_byte);
			}
			sq_pushnull(vm);
			while (SQ_SUCCEEDED(sq_next(vm, index - 1))) {
				/* Store the key + value */
				bool res = SaveObject(vm, -2, max_depth - 1, test) && SaveObject(vm, -1, max_depth - 1, test);
				sq_pop(vm, 2);
				if (!res) {
					sq_pop(vm, 1);
					return false;
				}
			}
			sq_pop(vm, 1);
			if (!test) {
				_ai_sl_byte = SQSL_ARRAY_TABLE_END;
				SlObject(NULL, _ai_byte);
			}
			return true;
		}

		case OT_BOOL: {
			if (!test) {
				_ai_sl_byte = SQSL_BOOL;
				SlObject(NULL, _ai_byte);
			}
			SQBool res;
			sq_getbool(vm, index, &res);
			if (!test) {
				_ai_sl_byte = res ? 1 : 0;
				SlObject(NULL, _ai_byte);
			}
			return true;
		}

		case OT_NULL: {
			if (!test) {
				_ai_sl_byte = SQSL_NULL;
				SlObject(NULL, _ai_byte);
			}
			return true;
		}

		default:
			AILog::Error("You tried to save an unsupported type. No data saved.");
			return false;
	}
}

/* static */ void AIInstance::SaveEmpty()
{
	_ai_sl_byte = 0;
	SlObject(NULL, _ai_byte);
}

void AIInstance::Save()
{
	/* Don't save data if the AI didn't start yet or if it crashed. */
	if (this->engine == NULL || this->engine->HasScriptCrashed()) {
		SaveEmpty();
		return;
	}

	HSQUIRRELVM vm = this->engine->GetVM();
	if (!this->is_started) {
		SQBool res;
		sq_getbool(vm, -1, &res);
		if (!res) {
			SaveEmpty();
			return;
		}
		/* Push the loaded savegame data to the top of the stack. */
		sq_push(vm, -2);
		_ai_sl_byte = 1;
		SlObject(NULL, _ai_byte);
		/* Save the data that was just loaded. */
		SaveObject(vm, -1, AISAVE_MAX_DEPTH, false);
		sq_poptop(vm);
	} else if (this->engine->MethodExists(*this->instance, "Save")) {
		HSQOBJECT savedata;
		/* We don't want to be interrupted during the save function. */
		bool backup_allow = AIObject::GetAllowDoCommand();
		AIObject::SetAllowDoCommand(false);
		if (!this->engine->CallMethod(*this->instance, "Save", &savedata)) {
			/* The script crashed in the Save function. We can't kill
			 * it here, but do so in the next AI tick. */
			SaveEmpty();
			return;
		}
		AIObject::SetAllowDoCommand(backup_allow);

		if (!sq_istable(savedata)) {
			AILog::Error("Save function should return a table.");
			SaveEmpty();
			return;
		}
		sq_pushobject(vm, savedata);
		if (SaveObject(vm, -1, AISAVE_MAX_DEPTH, true)) {
			_ai_sl_byte = 1;
			SlObject(NULL, _ai_byte);
			SaveObject(vm, -1, AISAVE_MAX_DEPTH, false);
		} else {
			_ai_sl_byte = 0;
			SlObject(NULL, _ai_byte);
		}
		sq_pop(vm, 1);
	} else {
		AILog::Warning("Save function is not implemented");
		_ai_sl_byte = 0;
		SlObject(NULL, _ai_byte);
	}

}

/* static */ bool AIInstance::LoadObjects(HSQUIRRELVM vm)
{
	SlObject(NULL, _ai_byte);
	switch (_ai_sl_byte) {
		case SQSL_INT: {
			int value;
			SlArray(&value, 1, SLE_INT32);
			if (vm != NULL) sq_pushinteger(vm, (SQInteger)value);
			return true;
		}

		case SQSL_STRING: {
			SlObject(NULL, _ai_byte);
			static char buf[256];
			SlArray(buf, _ai_sl_byte, SLE_CHAR);
			if (vm != NULL) sq_pushstring(vm, OTTD2FS(buf), -1);
			return true;
		}

		case SQSL_ARRAY: {
			if (vm != NULL) sq_newarray(vm, 0);
			while (LoadObjects(vm)) {
				if (vm != NULL) sq_arrayappend(vm, -2);
				/* The value is popped from the stack by squirrel. */
			}
			return true;
		}

		case SQSL_TABLE: {
			if (vm != NULL) sq_newtable(vm);
			while (LoadObjects(vm)) {
				LoadObjects(vm);
				if (vm != NULL) sq_rawset(vm, -3);
				/* The key (-2) and value (-1) are popped from the stack by squirrel. */
			}
			return true;
		}

		case SQSL_BOOL: {
			SlObject(NULL, _ai_byte);
			if (vm != NULL) sq_pushinteger(vm, (SQBool)(_ai_sl_byte != 0));
			return true;
		}

		case SQSL_NULL: {
			if (vm != NULL) sq_pushnull(vm);
			return true;
		}

		case SQSL_ARRAY_TABLE_END: {
			return false;
		}

		default: NOT_REACHED();
	}
}

/* static */ void AIInstance::LoadEmpty()
{
	SlObject(NULL, _ai_byte);
	/* Check if there was anything saved at all. */
	if (_ai_sl_byte == 0) return;

	LoadObjects(NULL);
}

void AIInstance::Load(int version)
{
	if (this->engine == NULL || version == -1) {
		LoadEmpty();
		return;
	}
	HSQUIRRELVM vm = this->engine->GetVM();

	SlObject(NULL, _ai_byte);
	/* Check if there was anything saved at all. */
	if (_ai_sl_byte == 0) return;

	/* First remove the value "false" since we have data to load. */
	sq_poptop(vm);
	sq_pushinteger(vm, version);
	LoadObjects(vm);
	sq_pushbool(vm, true);
}

bool AIInstance::CallLoad()
{
	HSQUIRRELVM vm = this->engine->GetVM();
	/* Is there save data that we should load? */
	SQBool res;
	sq_getbool(vm, -1, &res);
	sq_poptop(vm);
	if (!res) return true;

	if (!this->engine->MethodExists(*this->instance, "Load")) {
		AILog::Warning("Loading failed: there was data for the AI to load, but the AI does not have a Load() function.");

		/* Pop the savegame data and version. */
		sq_pop(vm, 2);
		return true;
	}

	/* Go to the instance-root */
	sq_pushobject(vm, *this->instance);
	/* Find the function-name inside the script */
	sq_pushstring(vm, OTTD2FS("Load"), -1);
	/* Change the "Load" string in a function pointer */
	sq_get(vm, -2);
	/* Push the main instance as "this" object */
	sq_pushobject(vm, *this->instance);
	/* Push the version data and savegame data as arguments */
	sq_push(vm, -5);
	sq_push(vm, -5);

	/* Call the AI load function. sq_call removes the arguments (but not the
	 * function pointer) from the stack. */
	if (SQ_FAILED(sq_call(vm, 3, SQFalse, SQFalse))) return false;

	/* Pop 1) The version, 2) the savegame data, 3) the object instance, 4) the function pointer. */
	sq_pop(vm, 4);
	return true;
}
