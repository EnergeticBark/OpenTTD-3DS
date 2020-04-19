/* $Id$ */

/** @file newgrf_canal.cpp Implementation of NewGRF canals. */

#include "stdafx.h"
#include "core/overflowsafe_type.hpp"
#include "tile_type.h"
#include "debug.h"
#include "newgrf_commons.h"
#include "newgrf_spritegroup.h"
#include "newgrf_canal.h"
#include "tile_map.h"
#include "water_map.h"


/** Table of canal 'feature' sprite groups */
WaterFeature _water_feature[CF_END];


/* Random bits and triggers are not supported for canals, so the following
 * three functions are stubs. */
static uint32 CanalGetRandomBits(const ResolverObject *object)
{
	/* Return random bits only for water tiles, not station tiles */
	return IsTileType(object->u.canal.tile, MP_WATER) ? GetWaterTileRandomBits(object->u.canal.tile) : 0;
}


static uint32 CanalGetTriggers(const ResolverObject *object)
{
	return 0;
}


static void CanalSetTriggers(const ResolverObject *object, int triggers)
{
	return;
}


static uint32 CanalGetVariable(const ResolverObject *object, byte variable, byte parameter, bool *available)
{
	TileIndex tile = object->u.canal.tile;

	switch (variable) {
		/* Height of tile */
		case 0x80: return GetTileZ(tile) / TILE_HEIGHT;

		/* Terrain type */
		case 0x81: return GetTerrainType(tile);

		/* Random data for river or canal tiles, otherwise zero */
		case 0x83: return GetWaterTileRandomBits(tile);
	}

	DEBUG(grf, 1, "Unhandled canal property 0x%02X", variable);

	*available = false;
	return UINT_MAX;
}


static const SpriteGroup *CanalResolveReal(const ResolverObject *object, const SpriteGroup *group)
{
	if (group->g.real.num_loaded == 0) return NULL;

	return group->g.real.loaded[0];
}


static void NewCanalResolver(ResolverObject *res, TileIndex tile, const GRFFile *grffile)
{
	res->GetRandomBits = &CanalGetRandomBits;
	res->GetTriggers   = &CanalGetTriggers;
	res->SetTriggers   = &CanalSetTriggers;
	res->GetVariable   = &CanalGetVariable;
	res->ResolveReal   = &CanalResolveReal;

	res->u.canal.tile = tile;

	res->callback        = CBID_NO_CALLBACK;
	res->callback_param1 = 0;
	res->callback_param2 = 0;
	res->last_value      = 0;
	res->trigger         = 0;
	res->reseed          = 0;
	res->count           = 0;
	res->grffile         = grffile;
}


SpriteID GetCanalSprite(CanalFeature feature, TileIndex tile)
{
	ResolverObject object;
	const SpriteGroup *group;

	NewCanalResolver(&object, tile, _water_feature[feature].grffile);

	group = Resolve(_water_feature[feature].group, &object);
	if (group == NULL || group->type != SGT_RESULT) return 0;

	return group->g.result.sprite;
}
