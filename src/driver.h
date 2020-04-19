/* $Id$ */

/** @file driver.h Base for all drivers (video, sound, music, etc). */

#ifndef DRIVER_H
#define DRIVER_H

#include "core/enum_type.hpp"
#include "core/string_compare_type.hpp"
#include "string_func.h"
#include <map>

const char *GetDriverParam(const char * const *parm, const char *name);
bool GetDriverParamBool(const char * const *parm, const char *name);
int GetDriverParamInt(const char * const *parm, const char *name, int def);

class Driver {
public:
	virtual const char *Start(const char * const *parm) = 0;

	virtual void Stop() = 0;

	virtual ~Driver() { }

	enum Type {
		DT_BEGIN = 0,
		DT_SOUND = 0,
		DT_MUSIC,
		DT_VIDEO,
		DT_END,
	};
};

DECLARE_POSTFIX_INCREMENT(Driver::Type);


class DriverFactoryBase {
private:
	Driver::Type type;
	const char *name;
	int priority;

	typedef std::map<const char *, DriverFactoryBase *, StringCompare> Drivers;

	static Drivers &GetDrivers()
	{
		static Drivers &s_drivers = *new Drivers();
		return s_drivers;
	}

	static Driver **GetActiveDriver(Driver::Type type)
	{
		static Driver *s_driver[3] = { NULL, NULL, NULL };
		return &s_driver[type];
	}

	static const char *GetDriverTypeName(Driver::Type type)
	{
		static const char *driver_type_name[] = { "sound", "music", "video" };
		return driver_type_name[type];
	}

protected:
	void RegisterDriver(const char *name, Driver::Type type, int priority);

public:
	DriverFactoryBase() :
		name(NULL)
	{}

	virtual ~DriverFactoryBase();

	/** Shuts down all active drivers
	 */
	static void ShutdownDrivers()
	{
		for (Driver::Type dt = Driver::DT_BEGIN; dt < Driver::DT_END; dt++) {
			Driver *driver = *GetActiveDriver(dt);
			if (driver != NULL) driver->Stop();
		}
	}

	static const Driver *SelectDriver(const char *name, Driver::Type type);
	static char *GetDriversInfo(char *p, const char *last);

	/**
	 * Get a nice description of the driver-class.
	 */
	virtual const char *GetDescription() = 0;

	/**
	 * Create an instance of this driver-class.
	 */
	virtual Driver *CreateInstance() = 0;
};

#endif /* DRIVER_H */
