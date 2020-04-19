/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "station_base.h"
#include "oldpool_func.h"

/* Initialize the cargopacket-pool */
DEFINE_OLD_POOL_GENERIC(CargoPacket, CargoPacket)

void InitializeCargoPackets()
{
	/* Clean the cargo packet pool and create 1 block in it */
	_CargoPacket_pool.CleanPool();
	_CargoPacket_pool.AddBlockToPool();
}

CargoPacket::CargoPacket(StationID source, uint16 count)
{
	if (source != INVALID_STATION) assert(count != 0);

	this->source          = source;
	this->source_xy       = (source != INVALID_STATION) ? GetStation(source)->xy : 0;
	this->loaded_at_xy    = this->source_xy;

	this->count           = count;
	this->days_in_transit = 0;
	this->feeder_share    = 0;
	this->paid_for        = false;
}

CargoPacket::~CargoPacket()
{
	this->count = 0;
}

bool CargoPacket::SameSource(const CargoPacket *cp) const
{
	return this->source_xy == cp->source_xy && this->days_in_transit == cp->days_in_transit && this->paid_for == cp->paid_for;
}

/*
 *
 * Cargo list implementation
 *
 */

CargoList::~CargoList()
{
	while (!packets.empty()) {
		delete packets.front();
		packets.pop_front();
	}
}

const CargoList::List *CargoList::Packets() const
{
	return &packets;
}

void CargoList::AgeCargo()
{
	if (empty) return;

	uint dit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		if ((*it)->days_in_transit != 0xFF) (*it)->days_in_transit++;
		dit += (*it)->days_in_transit * (*it)->count;
	}
	days_in_transit = dit / count;
}

bool CargoList::Empty() const
{
	return empty;
}

uint CargoList::Count() const
{
	return count;
}

bool CargoList::UnpaidCargo() const
{
	return unpaid_cargo;
}

Money CargoList::FeederShare() const
{
	return feeder_share;
}

StationID CargoList::Source() const
{
	return source;
}

uint CargoList::DaysInTransit() const
{
	return days_in_transit;
}

void CargoList::Append(CargoPacket *cp)
{
	assert(cp != NULL);
	assert(cp->IsValid());

	for (List::iterator it = packets.begin(); it != packets.end(); it++) {
		if ((*it)->SameSource(cp) && (*it)->count + cp->count <= 65535) {
			(*it)->count        += cp->count;
			(*it)->feeder_share += cp->feeder_share;
			delete cp;

			InvalidateCache();
			return;
		}
	}

	/* The packet could not be merged with another one */
	packets.push_back(cp);
	InvalidateCache();
}


void CargoList::Truncate(uint count)
{
	for (List::iterator it = packets.begin(); it != packets.end(); it++) {
		uint local_count = (*it)->count;
		if (local_count <= count) {
			count -= local_count;
			continue;
		}

		(*it)->count = count;
		count = 0;
	}

	while (!packets.empty()) {
		CargoPacket *cp = packets.back();
		if (cp->count != 0) break;
		delete cp;
		packets.pop_back();
	}

	InvalidateCache();
}

bool CargoList::MoveTo(CargoList *dest, uint count, CargoList::MoveToAction mta, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	CargoList tmp;

	while (!packets.empty() && count > 0) {
		CargoPacket *cp = *packets.begin();
		if (cp->count <= count) {
			/* Can move the complete packet */
			packets.remove(cp);
			switch (mta) {
				case MTA_FINAL_DELIVERY:
					if (cp->source == data) {
						tmp.Append(cp);
					} else {
						count -= cp->count;
						delete cp;
					}
					break;
				case MTA_CARGO_LOAD:
					cp->loaded_at_xy = data;
					/* When cargo is moved into another vehicle you have *always* paid for it */
					cp->paid_for     = false;
					/* FALL THROUGH */
				case MTA_OTHER:
					count -= cp->count;
					dest->packets.push_back(cp);
					break;
			}
		} else {
			/* Can move only part of the packet, so split it into two pieces */
			if (mta != MTA_FINAL_DELIVERY) {
				CargoPacket *cp_new = new CargoPacket();

				Money fs = cp->feeder_share * count / static_cast<uint>(cp->count);
				cp->feeder_share -= fs;

				cp_new->source          = cp->source;
				cp_new->source_xy       = cp->source_xy;
				cp_new->loaded_at_xy    = (mta == MTA_CARGO_LOAD) ? data : cp->loaded_at_xy;

				cp_new->days_in_transit = cp->days_in_transit;
				cp_new->feeder_share    = fs;
				/* When cargo is moved into another vehicle you have *always* paid for it */
				cp_new->paid_for        = (mta == MTA_CARGO_LOAD) ? false : cp->paid_for;

				cp_new->count = count;
				dest->packets.push_back(cp_new);
			}
			cp->count -= count;

			count = 0;
		}
	}

	bool remaining = !packets.empty();

	if (mta == MTA_FINAL_DELIVERY && !tmp.Empty()) {
		/* There are some packets that could not be delivered at the station, put them back */
		tmp.MoveTo(this, UINT_MAX);
		tmp.packets.clear();
	}

	if (dest != NULL) dest->InvalidateCache();
	InvalidateCache();

	return remaining;
}

void CargoList::InvalidateCache()
{
	empty = packets.empty();
	count = 0;
	unpaid_cargo = false;
	feeder_share = 0;
	source = INVALID_STATION;
	days_in_transit = 0;

	if (empty) return;

	uint dit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		count        += (*it)->count;
		unpaid_cargo |= !(*it)->paid_for;
		dit          += (*it)->days_in_transit * (*it)->count;
		feeder_share += (*it)->feeder_share;
	}
	days_in_transit = dit / count;
	source = (*packets.begin())->source;
}

