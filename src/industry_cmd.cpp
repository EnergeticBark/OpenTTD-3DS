/* $Id$ */

/** @file industry_cmd.cpp Handling of industry tiles. */

#include "stdafx.h"
#include "openttd.h"
#include "clear_map.h"
#include "industry_map.h"
#include "station_map.h"
#include "train.h"
#include "landscape.h"
#include "viewport_func.h"
#include "command_func.h"
#include "town.h"
#include "news_func.h"
#include "variables.h"
#include "cheat_type.h"
#include "genworld.h"
#include "tree_map.h"
#include "newgrf.h"
#include "newgrf_commons.h"
#include "newgrf_industries.h"
#include "newgrf_industrytiles.h"
#include "autoslope.h"
#include "transparency.h"
#include "water.h"
#include "strings_func.h"
#include "functions.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "oldpool_func.h"
#include "animated_tile_func.h"
#include "effectvehicle_func.h"
#include "ai/ai.hpp"

#include "table/strings.h"
#include "table/industry_land.h"
#include "table/build_industry.h"

void ShowIndustryViewWindow(int industry);
void BuildOilRig(TileIndex tile);

static byte _industry_sound_ctr;
static TileIndex _industry_sound_tile;

int _total_industries;                      ///< General counter
uint16 _industry_counts[NUM_INDUSTRYTYPES]; ///< Number of industries per type ingame

IndustrySpec _industry_specs[NUM_INDUSTRYTYPES];
IndustryTileSpec _industry_tile_specs[NUM_INDUSTRYTILES];

/** This function initialize the spec arrays of both
 * industry and industry tiles.
 * It adjusts the enabling of the industry too, based on climate availability.
 * This will allow for clearer testings */
void ResetIndustries()
{
	memset(&_industry_specs, 0, sizeof(_industry_specs));
	memcpy(&_industry_specs, &_origin_industry_specs, sizeof(_origin_industry_specs));

	/* once performed, enable only the current climate industries */
	for (IndustryType i = 0; i < NUM_INDUSTRYTYPES; i++) {
		_industry_specs[i].enabled = i < NEW_INDUSTRYOFFSET &&
				HasBit(_origin_industry_specs[i].climate_availability, _settings_game.game_creation.landscape);
	}

	memset(&_industry_tile_specs, 0, sizeof(_industry_tile_specs));
	memcpy(&_industry_tile_specs, &_origin_industry_tile_specs, sizeof(_origin_industry_tile_specs));

	/* Reset any overrides that have been set. */
	_industile_mngr.ResetOverride();
	_industry_mngr.ResetOverride();
}

void ResetIndustryCreationProbility(IndustryType type)
{
	assert(type < INVALID_INDUSTRYTYPE);
	_industry_specs[type].appear_creation[_settings_game.game_creation.landscape] = 0;
}

DEFINE_OLD_POOL_GENERIC(Industry, Industry)

/**
 * Retrieve the type for this industry.  Although it is accessed by a tile,
 * it will return the general type of industry, and not the sprite index
 * as would do GetIndustryGfx.
 * @param tile that is queried
 * @pre IsTileType(tile, MP_INDUSTRY)
 * @return general type for this industry, as defined in industry.h
 **/
IndustryType GetIndustryType(TileIndex tile)
{
	assert(IsTileType(tile, MP_INDUSTRY));

	const Industry *ind = GetIndustryByTile(tile);
	return ind->IsValid() ? ind->type : (IndustryType)IT_INVALID;
}

/**
 * Accessor for array _industry_specs.
 * This will ensure at once : proper access and
 * not allowing modifications of it.
 * @param thistype of industry (which is the index in _industry_specs)
 * @pre thistype < NUM_INDUSTRYTYPES
 * @return a pointer to the corresponding industry spec
 **/
const IndustrySpec *GetIndustrySpec(IndustryType thistype)
{
	assert(thistype < NUM_INDUSTRYTYPES);
	return &_industry_specs[thistype];
}

/**
 * Accessor for array _industry_tile_specs.
 * This will ensure at once : proper access and
 * not allowing modifications of it.
 * @param gfx of industrytile (which is the index in _industry_tile_specs)
 * @pre gfx < INVALID_INDUSTRYTILE
 * @return a pointer to the corresponding industrytile spec
 **/
const IndustryTileSpec *GetIndustryTileSpec(IndustryGfx gfx)
{
	assert(gfx < INVALID_INDUSTRYTILE);
	return &_industry_tile_specs[gfx];
}

Industry::~Industry()
{
	if (CleaningPool()) return;

	/* Industry can also be destroyed when not fully initialized.
	 * This means that we do not have to clear tiles either. */
	if (this->width == 0) {
		this->xy = INVALID_TILE;
		return;
	}

	BEGIN_TILE_LOOP(tile_cur, this->width, this->height, this->xy);
		if (IsTileType(tile_cur, MP_INDUSTRY)) {
			if (GetIndustryIndex(tile_cur) == this->index) {
				/* MakeWaterKeepingClass() can also handle 'land' */
				MakeWaterKeepingClass(tile_cur, OWNER_NONE);
			}
		} else if (IsTileType(tile_cur, MP_STATION) && IsOilRig(tile_cur)) {
			DeleteOilRig(tile_cur);
		}
	END_TILE_LOOP(tile_cur, this->width, this->height, this->xy);

	if (GetIndustrySpec(this->type)->behaviour & INDUSTRYBEH_PLANT_FIELDS) {
		/* Remove the farmland and convert it to regular tiles over time. */
		BEGIN_TILE_LOOP(tile_cur, 42, 42, this->xy - TileDiffXY(21, 21)) {
			tile_cur = TILE_MASK(tile_cur);
			if (IsTileType(tile_cur, MP_CLEAR) && IsClearGround(tile_cur, CLEAR_FIELDS) &&
					GetIndustryIndexOfField(tile_cur) == this->index) {
				SetIndustryIndexOfField(tile_cur, INVALID_INDUSTRY);
			}
		} END_TILE_LOOP(tile_cur, 42, 42, this->xy - TileDiff(21, 21))
	}

	/* don't let any disaster vehicle target invalid industry */
	ReleaseDisastersTargetingIndustry(this->index);

	DecIndustryTypeCount(this->type);

	DeleteSubsidyWithIndustry(this->index);
	DeleteWindowById(WC_INDUSTRY_VIEW, this->index);
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, 0);
	this->xy = INVALID_TILE;
}

static void IndustryDrawSugarMine(const TileInfo *ti)
{
	const DrawIndustryAnimationStruct *d;

	if (!IsIndustryCompleted(ti->tile)) return;

	d = &_draw_industry_spec1[GetIndustryAnimationState(ti->tile)];

	AddChildSpriteScreen(SPR_IT_SUGAR_MINE_SIEVE + d->image_1, PAL_NONE, d->x, 0);

	if (d->image_2 != 0) {
		AddChildSpriteScreen(SPR_IT_SUGAR_MINE_CLOUDS + d->image_2 - 1, PAL_NONE, 8, 41);
	}

	if (d->image_3 != 0) {
		AddChildSpriteScreen(SPR_IT_SUGAR_MINE_PILE + d->image_3 - 1, PAL_NONE,
			_drawtile_proc1[d->image_3 - 1].x, _drawtile_proc1[d->image_3 - 1].y);
	}
}

static void IndustryDrawToffeeQuarry(const TileInfo *ti)
{
	uint8 x = 0;

	if (IsIndustryCompleted(ti->tile)) {
		x = _industry_anim_offs_toffee[GetIndustryAnimationState(ti->tile)];
		if (x == 0xFF)
			x = 0;
	}

	AddChildSpriteScreen(SPR_IT_TOFFEE_QUARRY_SHOVEL, PAL_NONE, 22 - x, 24 + x);
	AddChildSpriteScreen(SPR_IT_TOFFEE_QUARRY_TOFFEE, PAL_NONE, 6, 14);
}

static void IndustryDrawBubbleGenerator( const TileInfo *ti)
{
	if (IsIndustryCompleted(ti->tile)) {
		AddChildSpriteScreen(SPR_IT_BUBBLE_GENERATOR_BUBBLE, PAL_NONE, 5, _industry_anim_offs_bubbles[GetIndustryAnimationState(ti->tile)]);
	} else {
		AddChildSpriteScreen(SPR_IT_BUBBLE_GENERATOR_SPRING, PAL_NONE, 3, 67);
	}
}

static void IndustryDrawToyFactory(const TileInfo *ti)
{
	const DrawIndustryAnimationStruct *d;

	d = &_industry_anim_offs_toys[GetIndustryAnimationState(ti->tile)];

	if (d->image_1 != 0xFF) {
		AddChildSpriteScreen(SPR_IT_TOY_FACTORY_CLAY, PAL_NONE, d->x, 96 + d->image_1);
	}

	if (d->image_2 != 0xFF) {
		AddChildSpriteScreen(SPR_IT_TOY_FACTORY_ROBOT, PAL_NONE, 16 - d->image_2 * 2, 100 + d->image_2);
	}

	AddChildSpriteScreen(SPR_IT_TOY_FACTORY_STAMP, PAL_NONE, 7, d->image_3);
	AddChildSpriteScreen(SPR_IT_TOY_FACTORY_STAMP_HOLDER, PAL_NONE, 0, 42);
}

static void IndustryDrawCoalPlantSparks(const TileInfo *ti)
{
	if (IsIndustryCompleted(ti->tile)) {
		uint8 image = GetIndustryAnimationState(ti->tile);

		if (image != 0 && image < 7) {
			AddChildSpriteScreen(image + SPR_IT_POWER_PLANT_TRANSFORMERS,
				PAL_NONE,
				_coal_plant_sparks[image - 1].x,
				_coal_plant_sparks[image - 1].y
			);
		}
	}
}

typedef void IndustryDrawTileProc(const TileInfo *ti);
static IndustryDrawTileProc * const _industry_draw_tile_procs[5] = {
	IndustryDrawSugarMine,
	IndustryDrawToffeeQuarry,
	IndustryDrawBubbleGenerator,
	IndustryDrawToyFactory,
	IndustryDrawCoalPlantSparks,
};

static void DrawTile_Industry(TileInfo *ti)
{
	IndustryGfx gfx = GetIndustryGfx(ti->tile);
	Industry *ind = GetIndustryByTile(ti->tile);
	const IndustryTileSpec *indts = GetIndustryTileSpec(gfx);
	const DrawBuildingsTileStruct *dits;
	SpriteID image;
	SpriteID pal;

	/* Retrieve pointer to the draw industry tile struct */
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		/* Draw the tile using the specialized method of newgrf industrytile.
		 * DrawNewIndustry will return false if ever the resolver could not
		 * find any sprite to display.  So in this case, we will jump on the
		 * substitute gfx instead. */
		if (indts->grf_prop.spritegroup != NULL && DrawNewIndustryTile(ti, ind, gfx, indts)) {
			return;
		} else {
			/* No sprite group (or no valid one) found, meaning no graphics associated.
			 * Use the substitute one instead */
			if (indts->grf_prop.subst_id != INVALID_INDUSTRYTILE) {
				gfx = indts->grf_prop.subst_id;
				/* And point the industrytile spec accordingly */
				indts = GetIndustryTileSpec(gfx);
			}
		}
	}

	dits = &_industry_draw_tile_data[gfx << 2 | (indts->anim_state ?
			GetIndustryAnimationState(ti->tile) & INDUSTRY_COMPLETED :
			GetIndustryConstructionStage(ti->tile))];

	image = dits->ground.sprite;
	if (HasBit(image, PALETTE_MODIFIER_COLOUR) && dits->ground.pal == PAL_NONE) {
		pal = GENERAL_SPRITE_COLOUR(ind->random_colour);
	} else {
		pal = dits->ground.pal;
	}

	/* DrawFoundation() modifes ti->z and ti->tileh */
	if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

	/* If the ground sprite is the default flat water sprite, draw also canal/river borders.
	 * Do not do this if the tile's WaterClass is 'land'. */
	if (image == SPR_FLAT_WATER_TILE && IsIndustryTileOnWater(ti->tile)) {
		DrawWaterClassGround(ti);
	} else {
		DrawGroundSprite(image, pal);
	}

	/* If industries are transparent and invisible, do not draw the upper part */
	if (IsInvisibilitySet(TO_INDUSTRIES)) return;

	/* Add industry on top of the ground? */
	image = dits->building.sprite;
	if (image != 0) {
		AddSortableSpriteToDraw(image,
			(HasBit(image, PALETTE_MODIFIER_COLOUR) && dits->building.pal == PAL_NONE) ? GENERAL_SPRITE_COLOUR(ind->random_colour) : dits->building.pal,
			ti->x + dits->subtile_x,
			ti->y + dits->subtile_y,
			dits->width,
			dits->height,
			dits->dz,
			ti->z,
			IsTransparencySet(TO_INDUSTRIES));

		if (IsTransparencySet(TO_INDUSTRIES)) return;
	}

	{
		int proc = dits->draw_proc - 1;
		if (proc >= 0) _industry_draw_tile_procs[proc](ti);
	}
}

static uint GetSlopeZ_Industry(TileIndex tile, uint x, uint y)
{
	return GetTileMaxZ(tile);
}

static Foundation GetFoundation_Industry(TileIndex tile, Slope tileh)
{
	IndustryGfx gfx = GetIndustryGfx(tile);

	/* For NewGRF industry tiles we might not be drawing a foundation. We need to
	 * account for this, as other structures should
	 * draw the wall of the foundation in this case.
	 */
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		const IndustryTileSpec *indts = GetIndustryTileSpec(gfx);
		if (indts->grf_prop.spritegroup != NULL && HasBit(indts->callback_flags, CBM_INDT_DRAW_FOUNDATIONS)) {
			uint32 callback_res = GetIndustryTileCallback(CBID_INDUSTRY_DRAW_FOUNDATIONS, 0, 0, gfx, GetIndustryByTile(tile), tile);
			if (callback_res == 0) return FOUNDATION_NONE;
		}
	}
	return FlatteningFoundation(tileh);
}

static void GetAcceptedCargo_Industry(TileIndex tile, AcceptedCargo ac)
{
	IndustryGfx gfx = GetIndustryGfx(tile);
	const IndustryTileSpec *itspec = GetIndustryTileSpec(gfx);

	/* When we have to use a callback, we put our data in the next two variables */
	CargoID raw_accepts_cargo[lengthof(itspec->accepts_cargo)];
	uint8 raw_acceptance[lengthof(itspec->acceptance)];

	/* And then these will always point to a same sized array with the required data */
	const CargoID *accepts_cargo = itspec->accepts_cargo;
	const uint8 *acceptance = itspec->acceptance;

	if (HasBit(itspec->callback_flags, CBM_INDT_ACCEPT_CARGO)) {
		uint16 res = GetIndustryTileCallback(CBID_INDTILE_ACCEPT_CARGO, 0, 0, gfx, GetIndustryByTile(tile), tile);
		if (res != CALLBACK_FAILED) {
			accepts_cargo = raw_accepts_cargo;
			for (uint i = 0; i < lengthof(itspec->accepts_cargo); i++) raw_accepts_cargo[i] = GetCargoTranslation(GB(res, i * 5, 5), itspec->grf_prop.grffile);
		}
	}

	if (HasBit(itspec->callback_flags, CBM_INDT_CARGO_ACCEPTANCE)) {
		uint16 res = GetIndustryTileCallback(CBID_INDTILE_CARGO_ACCEPTANCE, 0, 0, gfx, GetIndustryByTile(tile), tile);
		if (res != CALLBACK_FAILED) {
			acceptance = raw_acceptance;
			for (uint i = 0; i < lengthof(itspec->accepts_cargo); i++) raw_acceptance[i] = GB(res, i * 4, 4);
		}
	}

	for (byte i = 0; i < lengthof(itspec->accepts_cargo); i++) {
		CargoID a = accepts_cargo[i];
		/* Only set the value once. */
		if (a != CT_INVALID && ac[a] == 0) ac[a] = acceptance[i];
	}
}

static void GetTileDesc_Industry(TileIndex tile, TileDesc *td)
{
	const Industry *i = GetIndustryByTile(tile);
	const IndustrySpec *is = GetIndustrySpec(i->type);

	td->owner[0] = i->owner;
	td->str = is->name;
	if (!IsIndustryCompleted(tile)) {
		SetDParamX(td->dparam, 0, td->str);
		td->str = STR_2058_UNDER_CONSTRUCTION;
	}

	if (is->grf_prop.grffile != NULL) {
		td->grf = GetGRFConfig(is->grf_prop.grffile->grfid)->name;
	}
}

static CommandCost ClearTile_Industry(TileIndex tile, DoCommandFlag flags)
{
	Industry *i = GetIndustryByTile(tile);
	const IndustrySpec *indspec = GetIndustrySpec(i->type);

	/* water can destroy industries
	 * in editor you can bulldoze industries
	 * with magic_bulldozer cheat you can destroy industries
	 * (area around OILRIG is water, so water shouldn't flood it
	 */
	if ((_current_company != OWNER_WATER && _game_mode != GM_EDITOR &&
			!_cheats.magic_bulldozer.value) ||
			((flags & DC_AUTO) != 0) ||
			(_current_company == OWNER_WATER &&
				((indspec->behaviour & INDUSTRYBEH_BUILT_ONWATER) ||
				HasBit(GetIndustryTileSpec(GetIndustryGfx(tile))->slopes_refused, 5)))) {
		SetDParam(0, indspec->name);
		return_cmd_error(flags & DC_AUTO ? STR_4800_IN_THE_WAY : INVALID_STRING_ID);
	}

	if (flags & DC_EXEC) {
		AI::BroadcastNewEvent(new AIEventIndustryClose(i->index));
		delete i;
	}
	return CommandCost(EXPENSES_CONSTRUCTION, indspec->GetRemovalCost());
}

static void TransportIndustryGoods(TileIndex tile)
{
	Industry *i = GetIndustryByTile(tile);
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	bool moved_cargo = false;

	for (uint j = 0; j < lengthof(i->produced_cargo_waiting); j++) {
		uint cw = min(i->produced_cargo_waiting[j], 255);
		if (cw > indspec->minimal_cargo && i->produced_cargo[j] != CT_INVALID) {
			i->produced_cargo_waiting[j] -= cw;

			/* fluctuating economy? */
			if (_economy.fluct <= 0) cw = (cw + 1) / 2;

			i->this_month_production[j] += cw;

			uint am = MoveGoodsToStation(i->xy, i->width, i->height, i->produced_cargo[j], cw);
			i->this_month_transported[j] += am;

			moved_cargo |= (am != 0);
		}
	}

	if (moved_cargo && !StartStopIndustryTileAnimation(i, IAT_INDUSTRY_DISTRIBUTES_CARGO)) {
		uint newgfx = GetIndustryTileSpec(GetIndustryGfx(tile))->anim_production;

		if (newgfx != INDUSTRYTILE_NOANIM) {
			ResetIndustryConstructionStage(tile);
			SetIndustryCompleted(tile, true);
			SetIndustryGfx(tile, newgfx);
			MarkTileDirtyByTile(tile);
		}
	}
}


static void AnimateTile_Industry(TileIndex tile)
{
	byte m;
	IndustryGfx gfx = GetIndustryGfx(tile);

	if (GetIndustryTileSpec(gfx)->animation_info != 0xFFFF) {
		AnimateNewIndustryTile(tile);
		return;
	}

	switch (gfx) {
	case GFX_SUGAR_MINE_SIEVE:
		if ((_tick_counter & 1) == 0) {
			m = GetIndustryAnimationState(tile) + 1;

			switch (m & 7) {
			case 2: SndPlayTileFx(SND_2D_RIP_2, tile); break;
			case 6: SndPlayTileFx(SND_29_RIP, tile); break;
			}

			if (m >= 96) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetIndustryAnimationState(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_TOFFEE_QUARY:
		if ((_tick_counter & 3) == 0) {
			m = GetIndustryAnimationState(tile);

			if (_industry_anim_offs_toffee[m] == 0xFF) {
				SndPlayTileFx(SND_30_CARTOON_SOUND, tile);
			}

			if (++m >= 70) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetIndustryAnimationState(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_BUBBLE_CATCHER:
		if ((_tick_counter & 1) == 0) {
			m = GetIndustryAnimationState(tile);

			if (++m >= 40) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetIndustryAnimationState(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	/* Sparks on a coal plant */
	case GFX_POWERPLANT_SPARKS:
		if ((_tick_counter & 3) == 0) {
			m = GetIndustryAnimationState(tile);
			if (m == 6) {
				SetIndustryAnimationState(tile, 0);
				DeleteAnimatedTile(tile);
			} else {
				SetIndustryAnimationState(tile, m + 1);
				MarkTileDirtyByTile(tile);
			}
		}
		break;

	case GFX_TOY_FACTORY:
		if ((_tick_counter & 1) == 0) {
			m = GetIndustryAnimationState(tile) + 1;

			switch (m) {
				case  1: SndPlayTileFx(SND_2C_MACHINERY, tile); break;
				case 23: SndPlayTileFx(SND_2B_COMEDY_HIT, tile); break;
				case 28: SndPlayTileFx(SND_2A_EXTRACT_AND_POP, tile); break;
				default:
					if (m >= 50) {
						int n = GetIndustryAnimationLoop(tile) + 1;
						m = 0;
						if (n >= 8) {
							n = 0;
							DeleteAnimatedTile(tile);
						}
						SetIndustryAnimationLoop(tile, n);
					}
			}

			SetIndustryAnimationState(tile, m);
			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_PLASTIC_FOUNTAIN_ANIMATED_1: case GFX_PLASTIC_FOUNTAIN_ANIMATED_2:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_3: case GFX_PLASTIC_FOUNTAIN_ANIMATED_4:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_5: case GFX_PLASTIC_FOUNTAIN_ANIMATED_6:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_7: case GFX_PLASTIC_FOUNTAIN_ANIMATED_8:
		if ((_tick_counter & 3) == 0) {
			IndustryGfx gfx = GetIndustryGfx(tile);

			gfx = (gfx < 155) ? gfx + 1 : 148;
			SetIndustryGfx(tile, gfx);
			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_OILWELL_ANIMATED_1:
	case GFX_OILWELL_ANIMATED_2:
	case GFX_OILWELL_ANIMATED_3:
		if ((_tick_counter & 7) == 0) {
			bool b = Chance16(1, 7);
			IndustryGfx gfx = GetIndustryGfx(tile);

			m = GetIndustryAnimationState(tile) + 1;
			if (m == 4 && (m = 0, ++gfx) == GFX_OILWELL_ANIMATED_3 + 1 && (gfx = GFX_OILWELL_ANIMATED_1, b)) {
				SetIndustryGfx(tile, GFX_OILWELL_NOT_ANIMATED);
				SetIndustryConstructionStage(tile, 3);
				DeleteAnimatedTile(tile);
			} else {
				SetIndustryAnimationState(tile, m);
				SetIndustryGfx(tile, gfx);
				MarkTileDirtyByTile(tile);
			}
		}
		break;

	case GFX_COAL_MINE_TOWER_ANIMATED:
	case GFX_COPPER_MINE_TOWER_ANIMATED:
	case GFX_GOLD_MINE_TOWER_ANIMATED: {
			int state = _tick_counter & 0x7FF;

			if ((state -= 0x400) < 0)
				return;

			if (state < 0x1A0) {
				if (state < 0x20 || state >= 0x180) {
					m = GetIndustryAnimationState(tile);
					if (!(m & 0x40)) {
						SetIndustryAnimationState(tile, m | 0x40);
						SndPlayTileFx(SND_0B_MINING_MACHINERY, tile);
					}
					if (state & 7)
						return;
				} else {
					if (state & 3)
						return;
				}
				m = (GetIndustryAnimationState(tile) + 1) | 0x40;
				if (m > 0xC2) m = 0xC0;
				SetIndustryAnimationState(tile, m);
				MarkTileDirtyByTile(tile);
			} else if (state >= 0x200 && state < 0x3A0) {
				int i;
				i = (state < 0x220 || state >= 0x380) ? 7 : 3;
				if (state & i)
					return;

				m = (GetIndustryAnimationState(tile) & 0xBF) - 1;
				if (m < 0x80) m = 0x82;
				SetIndustryAnimationState(tile, m);
				MarkTileDirtyByTile(tile);
			}
		} break;
	}
}

static void CreateChimneySmoke(TileIndex tile)
{
	uint x = TileX(tile) * TILE_SIZE;
	uint y = TileY(tile) * TILE_SIZE;
	uint z = GetTileMaxZ(tile);

	CreateEffectVehicle(x + 15, y + 14, z + 59, EV_CHIMNEY_SMOKE);
}

static void MakeIndustryTileBigger(TileIndex tile)
{
	byte cnt = GetIndustryConstructionCounter(tile) + 1;
	byte stage;

	if (cnt != 4) {
		SetIndustryConstructionCounter(tile, cnt);
		return;
	}

	stage = GetIndustryConstructionStage(tile) + 1;
	SetIndustryConstructionCounter(tile, 0);
	SetIndustryConstructionStage(tile, stage);
	StartStopIndustryTileAnimation(tile, IAT_CONSTRUCTION_STATE_CHANGE);
	if (stage == INDUSTRY_COMPLETED) SetIndustryCompleted(tile, true);

	MarkTileDirtyByTile(tile);

	if (!IsIndustryCompleted(tile)) return;

	IndustryGfx gfx = GetIndustryGfx(tile);
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		/* New industries are already animated on construction. */
		return;
	}

	switch (gfx) {
	case GFX_POWERPLANT_CHIMNEY:
		CreateChimneySmoke(tile);
		break;

	case GFX_OILRIG_1:
		if (GetIndustryGfx(tile + TileDiffXY(0, 1)) == GFX_OILRIG_1) BuildOilRig(tile);
		break;

	case GFX_TOY_FACTORY:
	case GFX_BUBBLE_CATCHER:
	case GFX_TOFFEE_QUARY:
		SetIndustryAnimationState(tile, 0);
		SetIndustryAnimationLoop(tile, 0);
		break;

	case GFX_PLASTIC_FOUNTAIN_ANIMATED_1: case GFX_PLASTIC_FOUNTAIN_ANIMATED_2:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_3: case GFX_PLASTIC_FOUNTAIN_ANIMATED_4:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_5: case GFX_PLASTIC_FOUNTAIN_ANIMATED_6:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_7: case GFX_PLASTIC_FOUNTAIN_ANIMATED_8:
		AddAnimatedTile(tile);
		break;
	}
}

static void TileLoopIndustry_BubbleGenerator(TileIndex tile)
{
	int dir;
	Vehicle *v;
	static const int8 _bubble_spawn_location[3][4] = {
		{ 11,   0, -4, -14 },
		{ -4, -10, -4,   1 },
		{ 49,  59, 60,  65 },
	};

	SndPlayTileFx(SND_2E_EXTRACT_AND_POP, tile);

	dir = Random() & 3;

	v = CreateEffectVehicleAbove(
		TileX(tile) * TILE_SIZE + _bubble_spawn_location[0][dir],
		TileY(tile) * TILE_SIZE + _bubble_spawn_location[1][dir],
		_bubble_spawn_location[2][dir],
		EV_BUBBLE
	);

	if (v != NULL) v->u.effect.animation_substate = dir;
}

static void TileLoop_Industry(TileIndex tile)
{
	IndustryGfx newgfx;
	IndustryGfx gfx;

	if (IsIndustryTileOnWater(tile)) TileLoop_Water(tile);

	TriggerIndustryTile(tile, INDTILE_TRIGGER_TILE_LOOP);

	if (!IsIndustryCompleted(tile)) {
		MakeIndustryTileBigger(tile);
		return;
	}

	if (_game_mode == GM_EDITOR) return;

	TransportIndustryGoods(tile);

	if (StartStopIndustryTileAnimation(tile, IAT_TILELOOP)) return;

	newgfx = GetIndustryTileSpec(GetIndustryGfx(tile))->anim_next;
	if (newgfx != INDUSTRYTILE_NOANIM) {
		ResetIndustryConstructionStage(tile);
		SetIndustryGfx(tile, newgfx);
		MarkTileDirtyByTile(tile);
		return;
	}

	gfx = GetIndustryGfx(tile);

	switch (gfx) {
	case GFX_COAL_MINE_TOWER_NOT_ANIMATED:
	case GFX_COPPER_MINE_TOWER_NOT_ANIMATED:
	case GFX_GOLD_MINE_TOWER_NOT_ANIMATED:
		if (!(_tick_counter & 0x400) && Chance16(1, 2)) {
			switch (gfx) {
				case GFX_COAL_MINE_TOWER_NOT_ANIMATED:   gfx = GFX_COAL_MINE_TOWER_ANIMATED;   break;
				case GFX_COPPER_MINE_TOWER_NOT_ANIMATED: gfx = GFX_COPPER_MINE_TOWER_ANIMATED; break;
				case GFX_GOLD_MINE_TOWER_NOT_ANIMATED:   gfx = GFX_GOLD_MINE_TOWER_ANIMATED;   break;
			}
			SetIndustryGfx(tile, gfx);
			SetIndustryAnimationState(tile, 0x80);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_OILWELL_NOT_ANIMATED:
		if (Chance16(1, 6)) {
			SetIndustryGfx(tile, GFX_OILWELL_ANIMATED_1);
			SetIndustryAnimationState(tile, 0);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_COAL_MINE_TOWER_ANIMATED:
	case GFX_COPPER_MINE_TOWER_ANIMATED:
	case GFX_GOLD_MINE_TOWER_ANIMATED:
		if (!(_tick_counter & 0x400)) {
			switch (gfx) {
				case GFX_COAL_MINE_TOWER_ANIMATED:   gfx = GFX_COAL_MINE_TOWER_NOT_ANIMATED;   break;
				case GFX_COPPER_MINE_TOWER_ANIMATED: gfx = GFX_COPPER_MINE_TOWER_NOT_ANIMATED; break;
				case GFX_GOLD_MINE_TOWER_ANIMATED:   gfx = GFX_GOLD_MINE_TOWER_NOT_ANIMATED;   break;
			}
			SetIndustryGfx(tile, gfx);
			SetIndustryCompleted(tile, true);
			SetIndustryConstructionStage(tile, 3);
			DeleteAnimatedTile(tile);
		}
		break;

	case GFX_POWERPLANT_SPARKS:
		if (Chance16(1, 3)) {
			SndPlayTileFx(SND_0C_ELECTRIC_SPARK, tile);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_COPPER_MINE_CHIMNEY:
		CreateEffectVehicleAbove(TileX(tile) * TILE_SIZE + 6, TileY(tile) * TILE_SIZE + 6, 43, EV_SMOKE);
		break;


	case GFX_TOY_FACTORY: {
			Industry *i = GetIndustryByTile(tile);
			if (i->was_cargo_delivered) {
				i->was_cargo_delivered = false;
				SetIndustryAnimationLoop(tile, 0);
				AddAnimatedTile(tile);
			}
		}
		break;

	case GFX_BUBBLE_GENERATOR:
		TileLoopIndustry_BubbleGenerator(tile);
		break;

	case GFX_TOFFEE_QUARY:
		AddAnimatedTile(tile);
		break;

	case GFX_SUGAR_MINE_SIEVE:
		if (Chance16(1, 3)) AddAnimatedTile(tile);
		break;
	}
}

static bool ClickTile_Industry(TileIndex tile)
{
	ShowIndustryViewWindow(GetIndustryIndex(tile));
	return true;
}

static TrackStatus GetTileTrackStatus_Industry(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static void GetProducedCargo_Industry(TileIndex tile, CargoID *b)
{
	const Industry *i = GetIndustryByTile(tile);

	b[0] = i->produced_cargo[0];
	b[1] = i->produced_cargo[1];
}

static void ChangeTileOwner_Industry(TileIndex tile, Owner old_owner, Owner new_owner)
{
	/* If the founder merges, the industry was created by the merged company */
	Industry *i = GetIndustryByTile(tile);
	if (i->founder == old_owner) i->founder = (new_owner == INVALID_OWNER) ? OWNER_NONE : new_owner;
}

static const byte _plantfarmfield_type[] = {1, 1, 1, 1, 1, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6};

static bool IsBadFarmFieldTile(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case MP_CLEAR: return IsClearGround(tile, CLEAR_FIELDS) || IsClearGround(tile, CLEAR_SNOW) || IsClearGround(tile, CLEAR_DESERT);
		case MP_TREES: return (GetTreeGround(tile) == TREE_GROUND_SHORE);
		default:       return true;
	}
}

static bool IsBadFarmFieldTile2(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case MP_CLEAR: return IsClearGround(tile, CLEAR_SNOW) || IsClearGround(tile, CLEAR_DESERT);
		case MP_TREES: return (GetTreeGround(tile) == TREE_GROUND_SHORE);
		default:       return true;
	}
}

static void SetupFarmFieldFence(TileIndex tile, int size, byte type, Axis direction)
{
	do {
		tile = TILE_MASK(tile);

		if (IsTileType(tile, MP_CLEAR) || IsTileType(tile, MP_TREES)) {
			byte or_ = type;

			if (or_ == 1 && Chance16(1, 7)) or_ = 2;

			if (direction == AXIS_X) {
				SetFenceSE(tile, or_);
			} else {
				SetFenceSW(tile, or_);
			}
		}

		tile += (direction == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));
	} while (--size);
}

static void PlantFarmField(TileIndex tile, IndustryID industry)
{
	uint size_x, size_y;
	uint32 r;
	uint count;
	uint counter;
	uint field_type;
	int type;

	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) + TILE_HEIGHT * 2 >= GetSnowLine())
			return;
	}

	/* determine field size */
	r = (Random() & 0x303) + 0x404;
	if (_settings_game.game_creation.landscape == LT_ARCTIC) r += 0x404;
	size_x = GB(r, 0, 8);
	size_y = GB(r, 8, 8);

	/* offset tile to match size */
	tile -= TileDiffXY(size_x / 2, size_y / 2);

	if (TileX(tile) + size_x >= MapSizeX() || TileY(tile) + size_y >= MapSizeY()) return;

	/* check the amount of bad tiles */
	count = 0;
	BEGIN_TILE_LOOP(cur_tile, size_x, size_y, tile)
		assert(cur_tile < MapSize());
		count += IsBadFarmFieldTile(cur_tile);
	END_TILE_LOOP(cur_tile, size_x, size_y, tile)
	if (count * 2 >= size_x * size_y) return;

	/* determine type of field */
	r = Random();
	counter = GB(r, 5, 3);
	field_type = GB(r, 8, 8) * 9 >> 8;

	/* make field */
	BEGIN_TILE_LOOP(cur_tile, size_x, size_y, tile)
		assert(cur_tile < MapSize());
		if (!IsBadFarmFieldTile2(cur_tile)) {
			MakeField(cur_tile, field_type, industry);
			SetClearCounter(cur_tile, counter);
			MarkTileDirtyByTile(cur_tile);
		}
	END_TILE_LOOP(cur_tile, size_x, size_y, tile)

	type = 3;
	if (_settings_game.game_creation.landscape != LT_ARCTIC && _settings_game.game_creation.landscape != LT_TROPIC) {
		type = _plantfarmfield_type[Random() & 0xF];
	}

	SetupFarmFieldFence(tile - TileDiffXY(1, 0), size_y, type, AXIS_Y);
	SetupFarmFieldFence(tile - TileDiffXY(0, 1), size_x, type, AXIS_X);
	SetupFarmFieldFence(tile + TileDiffXY(size_x - 1, 0), size_y, type, AXIS_Y);
	SetupFarmFieldFence(tile + TileDiffXY(0, size_y - 1), size_x, type, AXIS_X);
}

void PlantRandomFarmField(const Industry *i)
{
	int x = i->width  / 2 + Random() % 31 - 16;
	int y = i->height / 2 + Random() % 31 - 16;

	TileIndex tile = TileAddWrap(i->xy, x, y);

	if (tile != INVALID_TILE) PlantFarmField(tile, i->index);
}

/**
 * Search callback function for ChopLumberMillTrees
 * @param tile to test
 * @param user_data that is passed by the caller.  In this case, nothing
 * @return the result of the test
 */
static bool SearchLumberMillTrees(TileIndex tile, void *user_data)
{
	if (IsTileType(tile, MP_TREES) && GetTreeGrowth(tile) > 2) { ///< 3 and up means all fully grown trees
		CompanyID old_company = _current_company;
		/* found a tree */

		_current_company = OWNER_NONE;
		_industry_sound_ctr = 1;
		_industry_sound_tile = tile;
		SndPlayTileFx(SND_38_CHAINSAW, tile);

		DoCommand(tile, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR);

		_current_company = old_company;
		return true;
	}
	return false;
}

/**
 * Perform a circular search around the Lumber Mill in order to find trees to cut
 * @param i industry
 */
static void ChopLumberMillTrees(Industry *i)
{
	TileIndex tile = i->xy;

	if (!IsIndustryCompleted(tile)) return;  ///< Can't proceed if not completed

	if (CircularTileSearch(&tile, 40, SearchLumberMillTrees, NULL)) ///< 40x40 tiles  to search
		i->produced_cargo_waiting[0] = min(0xffff, i->produced_cargo_waiting[0] + 45); ///< Found a tree, add according value to waiting cargo
}

static void ProduceIndustryGoods(Industry *i)
{
	uint32 r;
	uint num;
	const IndustrySpec *indsp = GetIndustrySpec(i->type);

	/* play a sound? */
	if ((i->counter & 0x3F) == 0) {
		if (Chance16R(1, 14, r) && (num = indsp->number_of_sounds) != 0) {
			SndPlayTileFx(
				(SoundFx)(indsp->random_sounds[((r >> 16) * num) >> 16]),
				i->xy);
		}
	}

	i->counter--;

	/* produce some cargo */
	if ((i->counter & 0xFF) == 0) {
		if (HasBit(indsp->callback_flags, CBM_IND_PRODUCTION_256_TICKS)) IndustryProductionCallback(i, 1);

		IndustryBehaviour indbehav = indsp->behaviour;
		i->produced_cargo_waiting[0] = min(0xffff, i->produced_cargo_waiting[0] + i->production_rate[0]);
		i->produced_cargo_waiting[1] = min(0xffff, i->produced_cargo_waiting[1] + i->production_rate[1]);

		if ((indbehav & INDUSTRYBEH_PLANT_FIELDS) != 0) {
			bool plant;
			if (HasBit(indsp->callback_flags, CBM_IND_SPECIAL_EFFECT)) {
				plant = (GetIndustryCallback(CBID_INDUSTRY_SPECIAL_EFFECT, Random(), 0, i, i->type, i->xy) != 0);
			} else {
				plant = Chance16(1, 8);
			}

			if (plant) PlantRandomFarmField(i);
		}
		if ((indbehav & INDUSTRYBEH_CUT_TREES) != 0) {
			bool cut = ((i->counter & 0x1FF) == 0);
			if (HasBit(indsp->callback_flags, CBM_IND_SPECIAL_EFFECT)) {
				cut = (GetIndustryCallback(CBID_INDUSTRY_SPECIAL_EFFECT, 0, 1, i, i->type, i->xy) != 0);
			}

			if (cut) ChopLumberMillTrees(i);
		}

		TriggerIndustry(i, INDUSTRY_TRIGGER_INDUSTRY_TICK);
		StartStopIndustryTileAnimation(i, IAT_INDUSTRY_TICK);
	}
}

void OnTick_Industry()
{
	Industry *i;

	if (_industry_sound_ctr != 0) {
		_industry_sound_ctr++;

		if (_industry_sound_ctr == 75) {
			SndPlayTileFx(SND_37_BALLOON_SQUEAK, _industry_sound_tile);
		} else if (_industry_sound_ctr == 160) {
			_industry_sound_ctr = 0;
			SndPlayTileFx(SND_36_CARTOON_CRASH, _industry_sound_tile);
		}
	}

	if (_game_mode == GM_EDITOR) return;

	FOR_ALL_INDUSTRIES(i) {
		ProduceIndustryGoods(i);
	}
}

static bool CheckNewIndustry_NULL(TileIndex tile)
{
	return true;
}

static bool CheckNewIndustry_Forest(TileIndex tile)
{
	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) < HighestSnowLine() + TILE_HEIGHT * 2U) {
			_error_message = STR_4831_FOREST_CAN_ONLY_BE_PLANTED;
			return false;
		}
	}
	return true;
}

static bool CheckNewIndustry_OilRefinery(TileIndex tile)
{
	if (_game_mode == GM_EDITOR) return true;
	if (DistanceFromEdge(TILE_ADDXY(tile, 1, 1)) < _settings_game.game_creation.oil_refinery_limit) return true;

	_error_message = STR_483B_CAN_ONLY_BE_POSITIONED;
	return false;
}

extern bool _ignore_restrictions;

static bool CheckNewIndustry_OilRig(TileIndex tile)
{
	if (_game_mode == GM_EDITOR && _ignore_restrictions) return true;
	if (TileHeight(tile) == 0 &&
			DistanceFromEdge(TILE_ADDXY(tile, 1, 1)) < _settings_game.game_creation.oil_refinery_limit) return true;

	_error_message = STR_483B_CAN_ONLY_BE_POSITIONED;
	return false;
}

static bool CheckNewIndustry_Farm(TileIndex tile)
{
	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) + TILE_HEIGHT * 2 >= HighestSnowLine()) {
			_error_message = STR_0239_SITE_UNSUITABLE;
			return false;
		}
	}
	return true;
}

static bool CheckNewIndustry_Plantation(TileIndex tile)
{
	if (GetTropicZone(tile) == TROPICZONE_DESERT) {
		_error_message = STR_0239_SITE_UNSUITABLE;
		return false;
	}

	return true;
}

static bool CheckNewIndustry_Water(TileIndex tile)
{
	if (GetTropicZone(tile) != TROPICZONE_DESERT) {
		_error_message = STR_0318_CAN_ONLY_BE_BUILT_IN_DESERT;
		return false;
	}

	return true;
}

static bool CheckNewIndustry_Lumbermill(TileIndex tile)
{
	if (GetTropicZone(tile) != TROPICZONE_RAINFOREST) {
		_error_message = STR_0317_CAN_ONLY_BE_BUILT_IN_RAINFOREST;
		return false;
	}
	return true;
}

static bool CheckNewIndustry_BubbleGen(TileIndex tile)
{
	return GetTileZ(tile) <= TILE_HEIGHT * 4;
}

typedef bool CheckNewIndustryProc(TileIndex tile);
static CheckNewIndustryProc * const _check_new_industry_procs[CHECK_END] = {
	CheckNewIndustry_NULL,
	CheckNewIndustry_Forest,
	CheckNewIndustry_OilRefinery,
	CheckNewIndustry_Farm,
	CheckNewIndustry_Plantation,
	CheckNewIndustry_Water,
	CheckNewIndustry_Lumbermill,
	CheckNewIndustry_BubbleGen,
	CheckNewIndustry_OilRig
};

static const Town *CheckMultipleIndustryInTown(TileIndex tile, int type)
{
	const Town *t;
	const Industry *i;

	t = ClosestTownFromTile(tile, UINT_MAX);

	if (_settings_game.economy.multiple_industry_per_town) return t;

	FOR_ALL_INDUSTRIES(i) {
		if (i->type == (byte)type &&
				i->town == t) {
			_error_message = STR_0287_ONLY_ONE_ALLOWED_PER_TOWN;
			return NULL;
		}
	}

	return t;
}

bool IsSlopeRefused(Slope current, Slope refused)
{
	if (IsSteepSlope(current)) return true;
	if (current != SLOPE_FLAT) {
		if (IsSteepSlope(refused)) return true;

		Slope t = ComplementSlope(current);

		if (refused & SLOPE_W && (t & SLOPE_NW)) return true;
		if (refused & SLOPE_S && (t & SLOPE_NE)) return true;
		if (refused & SLOPE_E && (t & SLOPE_SW)) return true;
		if (refused & SLOPE_N && (t & SLOPE_SE)) return true;
	}

	return false;
}

static bool CheckIfIndustryTilesAreFree(TileIndex tile, const IndustryTileTable *it, uint itspec_index, int type, bool *custom_shape_check = NULL)
{
	_error_message = STR_0239_SITE_UNSUITABLE;
	bool refused_slope = false;
	bool custom_shape = false;

	do {
		IndustryGfx gfx = GetTranslatedIndustryTileID(it->gfx);
		if (TileX(tile) + it->ti.x >= MapSizeX()) return false;
		if (TileY(tile) + it->ti.y >= MapSizeY()) return false;
		TileIndex cur_tile = tile + ToTileIndexDiff(it->ti);

		if (!IsValidTile(cur_tile)) {
			if (gfx == GFX_WATERTILE_SPECIALCHECK) continue;
			return false;
		}

		if (gfx == GFX_WATERTILE_SPECIALCHECK) {
			if (!IsTileType(cur_tile, MP_WATER) ||
					GetTileSlope(cur_tile, NULL) != SLOPE_FLAT) {
				return false;
			}
		} else {
			if (!EnsureNoVehicleOnGround(cur_tile)) return false;
			if (MayHaveBridgeAbove(cur_tile) && IsBridgeAbove(cur_tile)) return false;

			const IndustryTileSpec *its = GetIndustryTileSpec(gfx);

			IndustryBehaviour ind_behav = GetIndustrySpec(type)->behaviour;

			/* Perform land/water check if not disabled */
			if (!HasBit(its->slopes_refused, 5) && (IsWaterTile(cur_tile) == !(ind_behav & INDUSTRYBEH_BUILT_ONWATER))) return false;

			if (HasBit(its->callback_flags, CBM_INDT_SHAPE_CHECK)) {
				custom_shape = true;
				if (!PerformIndustryTileSlopeCheck(tile, cur_tile, its, type, gfx, itspec_index)) return false;
			} else {
				Slope tileh = GetTileSlope(cur_tile, NULL);
				refused_slope |= IsSlopeRefused(tileh, its->slopes_refused);
			}

			if ((ind_behav & (INDUSTRYBEH_ONLY_INTOWN | INDUSTRYBEH_TOWN1200_MORE)) || // Tile must be a house
					((ind_behav & INDUSTRYBEH_ONLY_NEARTOWN) && IsTileType(cur_tile, MP_HOUSE))) { // Tile is allowed to be a house (and it is a house)
				if (!IsTileType(cur_tile, MP_HOUSE)) {
					_error_message = STR_030D_CAN_ONLY_BE_BUILT_IN_TOWNS;
					return false;
				}

				/* Clear the tiles as OWNER_TOWN to not affect town rating, and to not clear protected buildings */
				CompanyID old_company = _current_company;
				_current_company = OWNER_TOWN;
				bool not_clearable = CmdFailed(DoCommand(cur_tile, 0, 0, DC_NONE, CMD_LANDSCAPE_CLEAR));
				_current_company = old_company;

				if (not_clearable) return false;
			} else {
				/* Clear the tiles, but do not affect town ratings */
				bool not_clearable = CmdFailed(DoCommand(cur_tile, 0, 0, DC_AUTO | DC_NO_TEST_TOWN_RATING | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR));

				if (not_clearable) return false;
			}
		}
	} while ((++it)->ti.x != -0x80);

	if (custom_shape_check != NULL) *custom_shape_check = custom_shape;

	/* It is almost impossible to have a fully flat land in TG, so what we
	 *  do is that we check if we can make the land flat later on. See
	 *  CheckIfCanLevelIndustryPlatform(). */
	return !refused_slope || (_settings_game.game_creation.land_generator == LG_TERRAGENESIS && _generating_world && !custom_shape && !_ignore_restrictions);
}

static bool CheckIfIndustryIsAllowed(TileIndex tile, int type, const Town *t)
{
	if ((GetIndustrySpec(type)->behaviour & INDUSTRYBEH_TOWN1200_MORE) && t->population < 1200) {
		_error_message = STR_029D_CAN_ONLY_BE_BUILT_IN_TOWNS;
		return false;
	}

	if ((GetIndustrySpec(type)->behaviour & INDUSTRYBEH_ONLY_NEARTOWN) && DistanceMax(t->xy, tile) > 9) {
		_error_message = STR_0239_SITE_UNSUITABLE;
		return false;
	}

	return true;
}

static bool CheckCanTerraformSurroundingTiles(TileIndex tile, uint height, int internal)
{
	int size_x, size_y;
	uint curh;

	size_x = 2;
	size_y = 2;

	/* Check if we don't leave the map */
	if (TileX(tile) == 0 || TileY(tile) == 0 || GetTileType(tile) == MP_VOID) return false;

	tile += TileDiffXY(-1, -1);
	BEGIN_TILE_LOOP(tile_walk, size_x, size_y, tile) {
		curh = TileHeight(tile_walk);
		/* Is the tile clear? */
		if ((GetTileType(tile_walk) != MP_CLEAR) && (GetTileType(tile_walk) != MP_TREES))
			return false;

		/* Don't allow too big of a change if this is the sub-tile check */
		if (internal != 0 && Delta(curh, height) > 1) return false;

		/* Different height, so the surrounding tiles of this tile
		 *  has to be correct too (in level, or almost in level)
		 *  else you get a chain-reaction of terraforming. */
		if (internal == 0 && curh != height) {
			if (TileX(tile_walk) == 0 || TileY(tile_walk) == 0 || !CheckCanTerraformSurroundingTiles(tile_walk + TileDiffXY(-1, -1), height, internal + 1))
				return false;
		}
	} END_TILE_LOOP(tile_walk, size_x, size_y, tile);

	return true;
}

/**
 * This function tries to flatten out the land below an industry, without
 *  damaging the surroundings too much.
 */
static bool CheckIfCanLevelIndustryPlatform(TileIndex tile, DoCommandFlag flags, const IndustryTileTable *it, int type)
{
	const int MKEND = -0x80;   // used for last element in an IndustryTileTable (see build_industry.h)
	int max_x = 0;
	int max_y = 0;
	TileIndex cur_tile;
	uint size_x, size_y;
	uint h, curh;

	/* Finds dimensions of largest variant of this industry */
	do {
		if (it->gfx == 0xFF) continue;  //  FF been a marquer for a check on clear water, skip it
		if (it->ti.x > max_x) max_x = it->ti.x;
		if (it->ti.y > max_y) max_y = it->ti.y;
	} while ((++it)->ti.x != MKEND);

	/* Remember level height */
	h = TileHeight(tile);

	if (TileX(tile) <= 1 || TileY(tile) <= 1) return false;
	/* Check that all tiles in area and surrounding are clear
	 * this determines that there are no obstructing items */
	cur_tile = tile + TileDiffXY(-1, -1);
	size_x = max_x + 4;
	size_y = max_y + 4;

	/* Check if we don't leave the map */
	if (TileX(cur_tile) + size_x >= MapMaxX() || TileY(cur_tile) + size_y >= MapMaxY()) return false;

	/* _current_company is OWNER_NONE for randomly generated industries and in editor, or the company who funded or prospected the industry.
	 * Perform terraforming as OWNER_TOWN to disable autoslope and town ratings. */
	CompanyID old_company = _current_company;
	_current_company = OWNER_TOWN;

	BEGIN_TILE_LOOP(tile_walk, size_x, size_y, cur_tile) {
		curh = TileHeight(tile_walk);
		if (curh != h) {
			/* This tile needs terraforming. Check if we can do that without
			 *  damaging the surroundings too much. */
			if (!CheckCanTerraformSurroundingTiles(tile_walk, h, 0)) {
				_current_company = old_company;
				return false;
			}
			/* This is not 100% correct check, but the best we can do without modifying the map.
			 *  What is missing, is if the difference in height is more than 1.. */
			if (CmdFailed(DoCommand(tile_walk, SLOPE_N, (curh > h) ? 0 : 1, flags & ~DC_EXEC, CMD_TERRAFORM_LAND))) {
				_current_company = old_company;
				return false;
			}
		}
	} END_TILE_LOOP(tile_walk, size_x, size_y, cur_tile)

	if (flags & DC_EXEC) {
		/* Terraform the land under the industry */
		BEGIN_TILE_LOOP(tile_walk, size_x, size_y, cur_tile) {
			curh = TileHeight(tile_walk);
			while (curh != h) {
				/* We give the terraforming for free here, because we can't calculate
				 *  exact cost in the test-round, and as we all know, that will cause
				 *  a nice assert if they don't match ;) */
				DoCommand(tile_walk, SLOPE_N, (curh > h) ? 0 : 1, flags, CMD_TERRAFORM_LAND);
				curh += (curh > h) ? -1 : 1;
			}
		} END_TILE_LOOP(tile_walk, size_x, size_y, cur_tile)
	}

	_current_company = old_company;
	return true;
}


static bool CheckIfFarEnoughFromIndustry(TileIndex tile, int type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);
	const Industry *i;

	if (_settings_game.economy.same_industry_close && indspec->IsRawIndustry())
		/* Allow primary industries to be placed close to any other industry */
		return true;

	FOR_ALL_INDUSTRIES(i) {
		/* Within 14 tiles from another industry is considered close */
		bool in_low_distance = DistanceMax(tile, i->xy) <= 14;

		/* check if an industry that accepts the same goods is nearby */
		if (in_low_distance &&
				!indspec->IsRawIndustry() && // not a primary industry?
				indspec->accepts_cargo[0] == i->accepts_cargo[0] && (
				/* at least one of those options must be true */
				_game_mode != GM_EDITOR || // editor must not be stopped
				!_settings_game.economy.same_industry_close ||
				!_settings_game.economy.multiple_industry_per_town)) {
			_error_message = STR_INDUSTRY_TOO_CLOSE;
			return false;
		}

		/* check if there are any conflicting industry types around */
		if ((i->type == indspec->conflicting[0] ||
				i->type == indspec->conflicting[1] ||
				i->type == indspec->conflicting[2]) &&
				in_low_distance) {
			_error_message = STR_INDUSTRY_TOO_CLOSE;
			return false;
		}
	}
	return true;
}

/** Production level maximum, minimum and default values.
 * It is not a value been really used in order to change, but rather an indicator
 * of how the industry is behaving. */
enum ProductionLevels {
	PRODLEVEL_CLOSURE = 0x00,  ///< signal set to actually close the industry
	PRODLEVEL_MINIMUM = 0x04,  ///< below this level, the industry is set to be closing
	PRODLEVEL_DEFAULT = 0x10,  ///< default level set when the industry is created
	PRODLEVEL_MAXIMUM = 0x80,  ///< the industry is running at full speed
};

static void DoCreateNewIndustry(Industry *i, TileIndex tile, int type, const IndustryTileTable *it, byte layout, const Town *t, Owner owner, Owner founder)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);
	uint32 r;
	uint j;

	i->xy = tile;
	i->width = i->height = 0;
	i->type = type;
	IncIndustryTypeCount(type);

	i->produced_cargo[0] = indspec->produced_cargo[0];
	i->produced_cargo[1] = indspec->produced_cargo[1];
	i->accepts_cargo[0] = indspec->accepts_cargo[0];
	i->accepts_cargo[1] = indspec->accepts_cargo[1];
	i->accepts_cargo[2] = indspec->accepts_cargo[2];
	i->production_rate[0] = indspec->production_rate[0];
	i->production_rate[1] = indspec->production_rate[1];

	/* don't use smooth economy for industries using production related callbacks */
	if (_settings_game.economy.smooth_economy &&
	    !(HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_256_TICKS) || HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_CARGO_ARRIVAL)) && // production callbacks
	    !(HasBit(indspec->callback_flags, CBM_IND_MONTHLYPROD_CHANGE) || HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_CHANGE))             // production change callbacks
	) {
		i->production_rate[0] = min((RandomRange(256) + 128) * i->production_rate[0] >> 8 , 255);
		i->production_rate[1] = min((RandomRange(256) + 128) * i->production_rate[1] >> 8 , 255);
	}

	i->town = t;
	i->owner = owner;

	r = Random();
	i->random_colour = GB(r, 0, 4);
	i->counter = GB(r, 4, 12);
	i->random = GB(r, 16, 16);
	i->produced_cargo_waiting[0] = 0;
	i->produced_cargo_waiting[1] = 0;
	i->incoming_cargo_waiting[0] = 0;
	i->incoming_cargo_waiting[1] = 0;
	i->incoming_cargo_waiting[2] = 0;
	i->this_month_production[0] = 0;
	i->this_month_production[1] = 0;
	i->this_month_transported[0] = 0;
	i->this_month_transported[1] = 0;
	i->last_month_pct_transported[0] = 0;
	i->last_month_pct_transported[1] = 0;
	i->last_month_transported[0] = 0;
	i->last_month_transported[1] = 0;
	i->was_cargo_delivered = false;
	i->last_prod_year = _cur_year;
	i->last_month_production[0] = i->production_rate[0] * 8;
	i->last_month_production[1] = i->production_rate[1] * 8;
	i->founder = founder;

	if (HasBit(indspec->callback_flags, CBM_IND_DECIDE_COLOUR)) {
		uint16 res = GetIndustryCallback(CBID_INDUSTRY_DECIDE_COLOUR, 0, 0, i, type, INVALID_TILE);
		if (res != CALLBACK_FAILED) i->random_colour = GB(res, 0, 4);
	}

	if (HasBit(indspec->callback_flags, CBM_IND_INPUT_CARGO_TYPES)) {
		for (j = 0; j < lengthof(i->accepts_cargo); j++) i->accepts_cargo[j] = CT_INVALID;
		for (j = 0; j < lengthof(i->accepts_cargo); j++) {
			uint16 res = GetIndustryCallback(CBID_INDUSTRY_INPUT_CARGO_TYPES, j, 0, i, type, INVALID_TILE);
			if (res == CALLBACK_FAILED || GB(res, 0, 8) == CT_INVALID) break;
			i->accepts_cargo[j] = GetCargoTranslation(GB(res, 0, 8), indspec->grf_prop.grffile);
		}
	}

	if (HasBit(indspec->callback_flags, CBM_IND_OUTPUT_CARGO_TYPES)) {
		for (j = 0; j < lengthof(i->produced_cargo); j++) i->produced_cargo[j] = CT_INVALID;
		for (j = 0; j < lengthof(i->produced_cargo); j++) {
			uint16 res = GetIndustryCallback(CBID_INDUSTRY_OUTPUT_CARGO_TYPES, j, 0, i, type, INVALID_TILE);
			if (res == CALLBACK_FAILED || GB(res, 0, 8) == CT_INVALID) break;
			i->produced_cargo[j] = GetCargoTranslation(GB(res, 0, 8), indspec->grf_prop.grffile);
		}
	}

	i->construction_date = _date;
	i->construction_type = (_game_mode == GM_EDITOR) ? ICT_SCENARIO_EDITOR :
			(_generating_world ? ICT_MAP_GENERATION : ICT_NORMAL_GAMEPLAY);

	/* Adding 1 here makes it conform to specs of var44 of varaction2 for industries
	 * 0 = created prior of newindustries
	 * else, chosen layout + 1 */
	i->selected_layout = layout + 1;

	if (!_generating_world) i->last_month_production[0] = i->last_month_production[1] = 0;

	i->prod_level = PRODLEVEL_DEFAULT;

	do {
		TileIndex cur_tile = tile + ToTileIndexDiff(it->ti);

		if (it->gfx != GFX_WATERTILE_SPECIALCHECK) {
			byte size;

			size = it->ti.x;
			if (size > i->width) i->width = size;
			size = it->ti.y;
			if (size > i->height)i->height = size;

			WaterClass wc = (IsWaterTile(cur_tile) ? GetWaterClass(cur_tile) : WATER_CLASS_INVALID);

			DoCommand(cur_tile, 0, 0, DC_EXEC | DC_NO_TEST_TOWN_RATING | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR);

			MakeIndustry(cur_tile, i->index, it->gfx, Random(), wc);

			if (_generating_world) {
				SetIndustryConstructionCounter(cur_tile, 3);
				SetIndustryConstructionStage(cur_tile, 2);
			}

			/* it->gfx is stored in the map. But the translated ID cur_gfx is the interesting one */
			IndustryGfx cur_gfx = GetTranslatedIndustryTileID(it->gfx);
			const IndustryTileSpec *its = GetIndustryTileSpec(cur_gfx);
			if (its->animation_info != 0xFFFF) AddAnimatedTile(cur_tile);
		}
	} while ((++it)->ti.x != -0x80);

	i->width++;
	i->height++;

	if (GetIndustrySpec(i->type)->behaviour & INDUSTRYBEH_PLANT_ON_BUILT) {
		for (j = 0; j != 50; j++) PlantRandomFarmField(i);
	}
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, 0);
}

/** Helper function for Build/Fund an industry
 * @param tile tile where industry is built
 * @param type of industry to build
 * @param flags of operations to conduct
 * @param indspec pointer to industry specifications
 * @param itspec_index the index of the itsepc to build/fund
 * @param seed random seed (possibly) used by industries
 * @param founder Founder of the industry
 * @return the pointer of the newly created industry, or NULL if it failed
 */
static Industry *CreateNewIndustryHelper(TileIndex tile, IndustryType type, DoCommandFlag flags, const IndustrySpec *indspec, uint itspec_index, uint32 seed, Owner founder)
{
	const IndustryTileTable *it = indspec->table[itspec_index];
	bool custom_shape_check = false;

	if (!CheckIfIndustryTilesAreFree(tile, it, itspec_index, type, &custom_shape_check)) return NULL;

	if (HasBit(GetIndustrySpec(type)->callback_flags, CBM_IND_LOCATION)) {
		if (!CheckIfCallBackAllowsCreation(tile, type, itspec_index, seed)) return NULL;
	} else {
		if (!_check_new_industry_procs[indspec->check_proc](tile)) return NULL;
	}

	if (!custom_shape_check && _settings_game.game_creation.land_generator == LG_TERRAGENESIS && _generating_world && !_ignore_restrictions && !CheckIfCanLevelIndustryPlatform(tile, DC_NONE, it, type)) return NULL;
	if (!CheckIfFarEnoughFromIndustry(tile, type)) return NULL;

	const Town *t = CheckMultipleIndustryInTown(tile, type);
	if (t == NULL) return NULL;

	if (!CheckIfIndustryIsAllowed(tile, type, t)) return NULL;

	if (!Industry::CanAllocateItem()) return NULL;

	if (flags & DC_EXEC) {
		Industry *i = new Industry(tile);
		if (!custom_shape_check) CheckIfCanLevelIndustryPlatform(tile, DC_EXEC, it, type);
		DoCreateNewIndustry(i, tile, type, it, itspec_index, t, OWNER_NONE, founder);

		return i;
	}

	/* We need to return a non-NULL pointer to tell we have created an industry.
	 * However, we haven't created a real one (no DC_EXEC), so return a fake one. */
	return GetIndustry(0);
}

/** Build/Fund an industry
 * @param tile tile where industry is built
 * @param flags of operations to conduct
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - industry type see build_industry.h and see industry.h
 * - p1 = (bit 16 - 31) - first layout to try
 * @param p2 seed to use for variable 8F
 * @return index of the newly create industry, or CMD_ERROR if it failed
 */
CommandCost CmdBuildIndustry(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	const IndustrySpec *indspec = GetIndustrySpec(GB(p1, 0, 16));
	const Industry *ind = NULL;

	/* Check if the to-be built/founded industry is available for this climate. */
	if (!indspec->enabled) {
		return CMD_ERROR;
	}

	/* If the setting for raw-material industries is not on, you cannot build raw-material industries.
	 * Raw material industries are industries that do not accept cargo (at least for now) */
	if (_game_mode != GM_EDITOR && _settings_game.construction.raw_industry_construction == 0 && indspec->IsRawIndustry()) {
		return CMD_ERROR;
	}

	if (_game_mode != GM_EDITOR && _settings_game.construction.raw_industry_construction == 2 && indspec->IsRawIndustry()) {
		if (flags & DC_EXEC) {
			/* Prospected industries are build as OWNER_TOWN to not e.g. be build on owned land of the founder */
			CompanyID founder = _current_company;
			_current_company = OWNER_TOWN;
			/* Prospecting has a chance to fail, however we cannot guarantee that something can
			 * be built on the map, so the chance gets lower when the map is fuller, but there
			 * is nothing we can really do about that. */
			if (Random() <= indspec->prospecting_chance) {
				for (int i = 0; i < 5000; i++) {
					/* We should not have more than one Random() in a function call
					 * because parameter evaluation order is not guaranteed in the c++ standard
					 */
					tile = RandomTile();
					ind = CreateNewIndustryHelper(tile, p1, flags, indspec, RandomRange(indspec->num_table), p2, founder);
					if (ind != NULL) {
						break;
					}
				}
			}
			_current_company = founder;
		}
	} else {
		int count = indspec->num_table;
		const IndustryTileTable * const *itt = indspec->table;
		int num = Clamp(GB(p1, 16, 16), 0, count - 1);

		_error_message = STR_0239_SITE_UNSUITABLE;
		do {
			if (--count < 0) return CMD_ERROR;
			if (--num < 0) num = indspec->num_table - 1;
		} while (!CheckIfIndustryTilesAreFree(tile, itt[num], num, p1));

		ind = CreateNewIndustryHelper(tile, p1, flags, indspec, num, p2, _current_company);
		if (ind == NULL) return CMD_ERROR;
	}

	if (flags & DC_EXEC && _game_mode != GM_EDITOR && ind != NULL) {
		SetDParam(0, indspec->name);
		if (indspec->new_industry_text > STR_LAST_STRINGID) {
			SetDParam(1, STR_TOWN);
			SetDParam(2, ind->town->index);
		} else {
			SetDParam(1, ind->town->index);
		}
		AddNewsItem(indspec->new_industry_text, NS_INDUSTRY_OPEN, ind->xy, 0);
		AI::BroadcastNewEvent(new AIEventIndustryOpen(ind->index));
	}

	return CommandCost(EXPENSES_OTHER, indspec->GetConstructionCost());
}


static Industry *CreateNewIndustry(TileIndex tile, IndustryType type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	uint32 seed = Random();
	return CreateNewIndustryHelper(tile, type, DC_EXEC, indspec, RandomRange(indspec->num_table), seed, OWNER_NONE);
}

enum {
	NB_NUMOFINDUSTRY = 11,
	NB_DIFFICULTY_LEVEL = 5,
};

static const byte _numof_industry_table[NB_DIFFICULTY_LEVEL][NB_NUMOFINDUSTRY] = {
	/* difficulty settings for number of industries */
	{0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0},   // none
	{0, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1},   // very low
	{0, 1, 1, 1, 2, 2, 3, 3,  4,  4,  5},   // low
	{0, 1, 2, 3, 4, 5, 6, 7,  8,  9, 10},   // normal
	{0, 2, 3, 4, 6, 7, 8, 9, 10, 10, 10},   // high
};

/** This function is the one who really do the creation work
 * of random industries during game creation
 * @param type IndustryType of the desired industry
 * @param amount of industries that need to be built */
static void PlaceInitialIndustry(IndustryType type, int amount)
{
	/* We need to bypass the amount given in parameter if it exceeds the maximum dimension of the
	 * _numof_industry_table.  newgrf can specify a big amount */
	int num = (amount > NB_NUMOFINDUSTRY) ? amount : _numof_industry_table[_settings_game.difficulty.number_industries][amount];
	const IndustrySpec *ind_spc = GetIndustrySpec(type);

	/* These are always placed next to the coastline, so we scale by the perimeter instead. */
	num = (ind_spc->check_proc == CHECK_REFINERY || ind_spc->check_proc == CHECK_OIL_RIG) ? ScaleByMapSize1D(num) : ScaleByMapSize(num);

	if (_settings_game.difficulty.number_industries != 0) {
		CompanyID old_company = _current_company;
		_current_company = OWNER_NONE;
		assert(num > 0);

		do {
			uint i;

			IncreaseGeneratingWorldProgress(GWP_INDUSTRY);

			for (i = 0; i < 2000; i++) {
				if (CreateNewIndustry(RandomTile(), type) != NULL) break;
			}
		} while (--num);

		_current_company = old_company;
	}
}

/** This function will create ramdon industries during game creation.
 * It will scale the amount of industries by map size as well as difficulty level */
void GenerateIndustries()
{
	uint i = 0;
	uint8 chance;
	IndustryType it;
	const IndustrySpec *ind_spc;

	/* Find the total amount of industries */
	if (_settings_game.difficulty.number_industries > 0) {
		for (it = 0; it < NUM_INDUSTRYTYPES; it++) {

			ind_spc = GetIndustrySpec(it);

			if (!CheckIfCallBackAllowsAvailability(it, IACT_MAPGENERATION)) {
				ResetIndustryCreationProbility(it);
			}

			chance = ind_spc->appear_creation[_settings_game.game_creation.landscape];
			if (ind_spc->enabled && chance > 0) {
				/* once the chance of appearance is determind, it have to be scaled by
				 * the difficulty level. The "chance" in question is more an index into
				 * the _numof_industry_table,in fact */
				int num = (chance > NB_NUMOFINDUSTRY) ? chance : _numof_industry_table[_settings_game.difficulty.number_industries][chance];

				/* These are always placed next to the coastline, so we scale by the perimeter instead. */
				num = (ind_spc->check_proc == CHECK_REFINERY || ind_spc->check_proc == CHECK_OIL_RIG) ? ScaleByMapSize1D(num) : ScaleByMapSize(num);
				i += num;
			}
		}
	}

	SetGeneratingWorldProgress(GWP_INDUSTRY, i);

	if (_settings_game.difficulty.number_industries > 0) {
		for (it = 0; it < NUM_INDUSTRYTYPES; it++) {
			/* Once the number of industries has been determined, let's really create them.
			 * The test for chance allows us to try create industries that are available only
			 * for this landscape.
			 * @todo :  Do we really have to pass chance as un-scaled value, since we've already
			 *          processed that scaling above? No, don't think so.  Will find a way. */
			ind_spc = GetIndustrySpec(it);
			if (ind_spc->enabled) {
				chance = ind_spc->appear_creation[_settings_game.game_creation.landscape];
				if (chance > 0) PlaceInitialIndustry(it, chance);
			}
		}
	}
}

static void UpdateIndustryStatistics(Industry *i)
{
	byte pct;
	bool refresh = false;

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] != CT_INVALID) {
			pct = 0;
			if (i->this_month_production[j] != 0) {
				i->last_prod_year = _cur_year;
				pct = min(i->this_month_transported[j] * 256 / i->this_month_production[j], 255);
			}
			i->last_month_pct_transported[j] = pct;

			i->last_month_production[j] = i->this_month_production[j];
			i->this_month_production[j] = 0;

			i->last_month_transported[j] = i->this_month_transported[j];
			i->this_month_transported[j] = 0;
			refresh = true;
		}
	}

	if (refresh) InvalidateWindow(WC_INDUSTRY_VIEW, i->index);
}

/** Simple helper that will collect data for the generation of industries */
struct ProbabilityHelper {
	uint16 prob;      ///< probability
	IndustryType ind; ///< industry id correcponding
};

/**
 * Try to create a random industry, during gameplay
 */
static void MaybeNewIndustry(void)
{
	Industry *ind;               // will receive the industry's creation pointer
	IndustryType rndtype, j;     // Loop controlers
	const IndustrySpec *ind_spc;
	uint num = 0;
	ProbabilityHelper cumulative_probs[NUM_INDUSTRYTYPES]; // probability collector
	uint16 probability_max = 0;

	/* Generate a list of all possible industries that can be built. */
	for (j = 0; j < NUM_INDUSTRYTYPES; j++) {
		ind_spc = GetIndustrySpec(j);
		byte chance = ind_spc->appear_ingame[_settings_game.game_creation.landscape];

		if (!ind_spc->enabled || chance == 0) continue;

		/* If there is no Callback CBID_INDUSTRY_AVAILABLE or if this one did anot failed,
		 * and if appearing chance for this landscape is above 0, this industry can be chosen */
		if (CheckIfCallBackAllowsAvailability(j, IACT_RANDOMCREATION)) {
			probability_max += chance;
			/* adds the result for this industry */
			cumulative_probs[num].ind = j;
			cumulative_probs[num++].prob = probability_max;
		}
	}

	/* Find a random type, with maximum being what has been evaluate above*/
	rndtype = RandomRange(probability_max);
	for (j = 0; j < NUM_INDUSTRYTYPES; j++) {
		/* and choose the index of the industry that matches as close as possible this random type */
		if (cumulative_probs[j].prob >= rndtype) break;
	}

	ind_spc = GetIndustrySpec(cumulative_probs[j].ind);
	/*  Check if it is allowed */
	if ((ind_spc->behaviour & INDUSTRYBEH_BEFORE_1950) && _cur_year > 1950) return;
	if ((ind_spc->behaviour & INDUSTRYBEH_AFTER_1960) && _cur_year < 1960) return;

	/* try to create 2000 times this industry */
	num = 2000;
	for (;;) {
		ind = CreateNewIndustry(RandomTile(), cumulative_probs[j].ind);
		if (ind != NULL) break;
		if (--num == 0) return;
	}

	SetDParam(0, ind_spc->name);
	if (ind_spc->new_industry_text > STR_LAST_STRINGID) {
		SetDParam(1, STR_TOWN);
		SetDParam(2, ind->town->index);
	} else {
		SetDParam(1, ind->town->index);
	}
	AddNewsItem(ind_spc->new_industry_text, NS_INDUSTRY_OPEN, ind->xy, 0);
	AI::BroadcastNewEvent(new AIEventIndustryOpen(ind->index));
}

/**
 * Protects an industry from closure if the appropriate flags and conditions are met
 * INDUSTRYBEH_CANCLOSE_LASTINSTANCE must be set (which, by default, it is not) and the
 * count of industries of this type must one (or lower) in order to be protected
 * against closure.
 * @param type IndustryType been queried
 * @result true if protection is on, false otherwise (except for oil wells)
 */
static bool CheckIndustryCloseDownProtection(IndustryType type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	/* oil wells (or the industries with that flag set) are always allowed to closedown */
	if (indspec->behaviour & INDUSTRYBEH_DONT_INCR_PROD && _settings_game.game_creation.landscape == LT_TEMPERATE) return false;
	return (indspec->behaviour & INDUSTRYBEH_CANCLOSE_LASTINSTANCE) == 0 && GetIndustryTypeCount(type) <= 1;
}

/**
 * Can given cargo type be accepted or produced by the industry?
 * @param cargo: Cargo type
 * @param ind: Industry
 * @param *c_accepts: Pointer to boolean for acceptance of cargo
 * @param *c_produces: Pointer to boolean for production of cargo
 * @return: \c *c_accepts is set when industry accepts the cargo type,
 *          \c *c_produces is set when the industry produces the cargo type
 */
static void CanCargoServiceIndustry(CargoID cargo, Industry *ind, bool *c_accepts, bool *c_produces)
{
	const IndustrySpec *indspec = GetIndustrySpec(ind->type);

	/* Check for acceptance of cargo */
	for (byte j = 0; j < lengthof(ind->accepts_cargo); j++) {
		if (ind->accepts_cargo[j] == CT_INVALID) continue;
		if (cargo == ind->accepts_cargo[j]) {
			if (HasBit(indspec->callback_flags, CBM_IND_REFUSE_CARGO)) {
				uint16 res = GetIndustryCallback(CBID_INDUSTRY_REFUSE_CARGO,
						0, GetReverseCargoTranslation(cargo, indspec->grf_prop.grffile),
						ind, ind->type, ind->xy);
				if (res == 0) continue;
			}
			*c_accepts = true;
			break;
		}
	}

	/* Check for produced cargo */
	for (byte j = 0; j < lengthof(ind->produced_cargo); j++) {
		if (ind->produced_cargo[j] == CT_INVALID) continue;
		if (cargo == ind->produced_cargo[j]) {
			*c_produces = true;
			break;
		}
	}
}

/**
 * Compute who can service the industry.
 *
 * Here, 'can service' means that he/she has trains and stations close enough
 * to the industry with the right cargo type and the right orders (ie has the
 * technical means).
 *
 * @param ind: Industry being investigated.
 *
 * @return: 0 if nobody can service the industry, 2 if the local company can
 * service the industry, and 1 otherwise (only competitors can service the
 * industry)
 */
int WhoCanServiceIndustry(Industry *ind)
{
	/* Find all stations within reach of the industry */
	StationList stations;
	FindStationsAroundTiles(ind->xy, ind->width, ind->height, &stations);

	if (stations.Length() == 0) return 0; // No stations found at all => nobody services

	const Vehicle *v;
	int result = 0;
	FOR_ALL_VEHICLES(v) {
		/* Is it worthwhile to try this vehicle? */
		if (v->owner != _local_company && result != 0) continue;

		/* Check whether it accepts the right kind of cargo */
		bool c_accepts = false;
		bool c_produces = false;
		if (v->type == VEH_TRAIN && IsFrontEngine(v)) {
			for (const Vehicle *u = v; u != NULL; u = u->Next()) {
				CanCargoServiceIndustry(u->cargo_type, ind, &c_accepts, &c_produces);
			}
		} else if (v->type == VEH_ROAD || v->type == VEH_SHIP || v->type == VEH_AIRCRAFT) {
			CanCargoServiceIndustry(v->cargo_type, ind, &c_accepts, &c_produces);
		} else {
			continue;
		}
		if (!c_accepts && !c_produces) continue; // Wrong cargo

		/* Check orders of the vehicle.
		 * We cannot check the first of shared orders only, since the first vehicle in such a chain
		 * may have a different cargo type.
		 */
		const Order *o;
		FOR_VEHICLE_ORDERS(v, o) {
			if (o->IsType(OT_GOTO_STATION) && !(o->GetUnloadType() & OUFB_TRANSFER)) {
				/* Vehicle visits a station to load or unload */
				Station *st = GetStation(o->GetDestination());
				if (!st->IsValid()) continue;

				/* Same cargo produced by industry is dropped here => not serviced by vehicle v */
				if ((o->GetUnloadType() & OUFB_UNLOAD) && !c_accepts) break;

				if (stations.Contains(st)) {
					if (v->owner == _local_company) return 2; // Company services industry
					result = 1; // Competitor services industry
				}
			}
		}
	}
	return result;
}

/**
 * Report news that industry production has changed significantly
 *
 * @param ind: Industry with changed production
 * @param type: Cargo type that has changed
 * @param percent: Percentage of change (>0 means increase, <0 means decrease)
 */
static void ReportNewsProductionChangeIndustry(Industry *ind, CargoID type, int percent)
{
	NewsSubtype ns;

	switch (WhoCanServiceIndustry(ind)) {
		case 0: ns = NS_INDUSTRY_NOBODY;  break;
		case 1: ns = NS_INDUSTRY_OTHER;   break;
		case 2: ns = NS_INDUSTRY_COMPANY; break;
		default: NOT_REACHED(); break;
	}
	SetDParam(2, abs(percent));
	SetDParam(0, GetCargo(type)->name);
	SetDParam(1, ind->index);
	AddNewsItem(
		percent >= 0 ? STR_INDUSTRY_PROD_GOUP : STR_INDUSTRY_PROD_GODOWN,
		ns,
		ind->xy + TileDiffXY(1, 1), 0
	);
}

enum {
	PERCENT_TRANSPORTED_60 = 153,
	PERCENT_TRANSPORTED_80 = 204,
};

/** Change industry production or do closure
 * @param i Industry for which changes are performed
 * @param monthly true if it's the monthly call, false if it's the random call
 */
static void ChangeIndustryProduction(Industry *i, bool monthly)
{
	StringID str = STR_NULL;
	bool closeit = false;
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	bool standard = false;
	bool suppress_message = false;
	bool recalculate_multipliers = false; ///< reinitialize production_rate to match prod_level
	/* don't use smooth economy for industries using production related callbacks */
	bool smooth_economy = _settings_game.economy.smooth_economy &&
	                      !(HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_256_TICKS) || HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_CARGO_ARRIVAL)) && // production callbacks
	                      !(HasBit(indspec->callback_flags, CBM_IND_MONTHLYPROD_CHANGE) || HasBit(indspec->callback_flags, CBM_IND_PRODUCTION_CHANGE));            // production change callbacks
	byte div = 0;
	byte mul = 0;
	int8 increment = 0;

	bool callback_enabled = HasBit(indspec->callback_flags, monthly ? CBM_IND_MONTHLYPROD_CHANGE : CBM_IND_PRODUCTION_CHANGE);
	if (callback_enabled) {
		uint16 res = GetIndustryCallback(monthly ? CBID_INDUSTRY_MONTHLYPROD_CHANGE : CBID_INDUSTRY_PRODUCTION_CHANGE, 0, Random(), i, i->type, i->xy);
		if (res != CALLBACK_FAILED) { // failed callback means "do nothing"
			suppress_message = HasBit(res, 7);
			/* Get the custom message if any */
			if (HasBit(res, 8)) str = MapGRFStringID(indspec->grf_prop.grffile->grfid, GB(GetRegister(0x100), 0, 16));
			res = GB(res, 0, 4);
			switch(res) {
				default: NOT_REACHED();
				case 0x0: break;                  // Do nothing, but show the custom message if any
				case 0x1: div = 1; break;         // Halve industry production. If production reaches the quarter of the default, the industry is closed instead.
				case 0x2: mul = 1; break;         // Double industry production if it hasn't reached eight times of the original yet.
				case 0x3: closeit = true; break;  // The industry announces imminent closure, and is physically removed from the map next month.
				case 0x4: standard = true; break; // Do the standard random production change as if this industry was a primary one.
				case 0x5: case 0x6: case 0x7:     // Divide production by 4, 8, 16
				case 0x8: div = res - 0x3; break; // Divide production by 32
				case 0x9: case 0xA: case 0xB:     // Multiply production by 4, 8, 16
				case 0xC: mul = res - 0x7; break; // Multiply production by 32
				case 0xD:                         // decrement production
				case 0xE:                         // increment production
					increment = res == 0x0D ? -1 : 1;
					break;
				case 0xF:                         // Set production to third byte of register 0x100
					i->prod_level = Clamp(GB(GetRegister(0x100), 16, 8), PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
					recalculate_multipliers = true;
					break;
			}
		}
	} else {
		if (monthly != smooth_economy) return;
		if (indspec->life_type == INDUSTRYLIFE_BLACK_HOLE) return;
	}

	if (standard || (!callback_enabled && (indspec->life_type & (INDUSTRYLIFE_ORGANIC | INDUSTRYLIFE_EXTRACTIVE)) != 0)) {
		/* decrease or increase */
		bool only_decrease = (indspec->behaviour & INDUSTRYBEH_DONT_INCR_PROD) && _settings_game.game_creation.landscape == LT_TEMPERATE;

		if (smooth_economy) {
			closeit = true;
			for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
				if (i->produced_cargo[j] == CT_INVALID) continue;
				uint32 r = Random();
				int old_prod, new_prod, percent;
				/* If over 60% is transported, mult is 1, else mult is -1. */
				int mult = (i->last_month_pct_transported[j] > PERCENT_TRANSPORTED_60) ? 1 : -1;

				new_prod = old_prod = i->production_rate[j];

				/* For industries with only_decrease flags (temperate terrain Oil Wells),
				 * the multiplier will always be -1 so they will only decrease. */
				if (only_decrease) {
					mult = -1;
				/* For normal industries, if over 60% is transported, 33% chance for decrease.
				 * Bonus for very high station ratings (over 80%): 16% chance for decrease. */
				} else if (Chance16I(1, ((i->last_month_pct_transported[j] > PERCENT_TRANSPORTED_80) ? 6 : 3), r)) {
					mult *= -1;
				}

				/* 4.5% chance for 3-23% (or 1 unit for very low productions) production change,
				 * determined by mult value. If mult = 1 prod. increases, else (-1) it decreases. */
				if (Chance16I(1, 22, r >> 16)) {
					new_prod += mult * (max(((RandomRange(50) + 10) * old_prod) >> 8, 1U));
				}

				/* Prevent production to overflow or Oil Rig passengers to be over-"produced" */
				new_prod = Clamp(new_prod, 1, 255);

				if (((indspec->behaviour & INDUSTRYBEH_BUILT_ONWATER) != 0) && j == 1)
					new_prod = Clamp(new_prod, 0, 16);

				/* Do not stop closing the industry when it has the lowest possible production rate */
				if (new_prod == old_prod && old_prod > 1) {
					closeit = false;
					continue;
				}

				percent = (old_prod == 0) ? 100 : (new_prod * 100 / old_prod - 100);
				i->production_rate[j] = new_prod;

				/* Close the industry when it has the lowest possible production rate */
				if (new_prod > 1) closeit = false;

				if (abs(percent) >= 10) {
					ReportNewsProductionChangeIndustry(i, i->produced_cargo[j], percent);
				}
			}
		} else {
			if (only_decrease || Chance16(1, 3)) {
				/* If more than 60% transported, 66% chance of increase, else 33% chance of increase */
				if (!only_decrease && (i->last_month_pct_transported[0] > PERCENT_TRANSPORTED_60) != Chance16(1, 3)) {
					mul = 1; // Increase production
				} else {
					div = 1; // Decrease production
				}
			}
		}
	}

	if (!callback_enabled && indspec->life_type & INDUSTRYLIFE_PROCESSING) {
		if ( (byte)(_cur_year - i->last_prod_year) >= 5 && Chance16(1, smooth_economy ? 180 : 2)) {
			closeit = true;
		}
	}

	/* Increase if needed */
	while (mul-- != 0 && i->prod_level < PRODLEVEL_MAXIMUM) {
		i->prod_level = min(i->prod_level * 2, PRODLEVEL_MAXIMUM);
		recalculate_multipliers = true;
		if (str == STR_NULL) str = indspec->production_up_text;
	}

	/* Decrease if needed */
	while (div-- != 0 && !closeit) {
		if (i->prod_level == PRODLEVEL_MINIMUM) {
			closeit = true;
		} else {
			i->prod_level = max(i->prod_level / 2, (int)PRODLEVEL_MINIMUM); // typecast to int required to please MSVC
			recalculate_multipliers = true;
			if (str == STR_NULL) str = indspec->production_down_text;
		}
	}

	/* Increase or Decreasing the production level if needed */
	if (increment != 0) {
		if (increment < 0 && i->prod_level == PRODLEVEL_MINIMUM) {
			closeit = true;
		} else {
			i->prod_level = ClampU(i->prod_level + increment, PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
			recalculate_multipliers = true;
		}
	}

	/* Recalculate production_rate
	 * For non-smooth economy these should always be synchronized with prod_level */
	if (recalculate_multipliers) {
		/* Rates are rounded up, so e.g. oilrig always produces some passengers */
		i->production_rate[0] = min((indspec->production_rate[0] * i->prod_level + PRODLEVEL_DEFAULT - 1) / PRODLEVEL_DEFAULT, 0xFF);
		i->production_rate[1] = min((indspec->production_rate[1] * i->prod_level + PRODLEVEL_DEFAULT - 1) / PRODLEVEL_DEFAULT, 0xFF);
	}

	/* Close if needed and allowed */
	if (closeit && !CheckIndustryCloseDownProtection(i->type)) {
		i->prod_level = PRODLEVEL_CLOSURE;
		str = indspec->closure_text;
	}

	if (!suppress_message && str != STR_NULL) {
		NewsSubtype ns;
		/* Compute news category */
		if (closeit) {
			ns = NS_INDUSTRY_CLOSE;
			AI::BroadcastNewEvent(new AIEventIndustryClose(i->index));
		} else {
			switch (WhoCanServiceIndustry(i)) {
				case 0: ns = NS_INDUSTRY_NOBODY;  break;
				case 1: ns = NS_INDUSTRY_OTHER;   break;
				case 2: ns = NS_INDUSTRY_COMPANY; break;
				default: NOT_REACHED(); break;
			}
		}
		/* Set parameters of news string */
		if (str > STR_LAST_STRINGID) {
			SetDParam(0, STR_TOWN);
			SetDParam(1, i->town->index);
			SetDParam(2, indspec->name);
		} else if (closeit) {
			SetDParam(0, STR_INDUSTRY_FORMAT);
			SetDParam(1, i->town->index);
			SetDParam(2, indspec->name);
		} else {
			SetDParam(0, i->index);
		}
		/* and report the news to the user */
		AddNewsItem(str,
			ns,
			i->xy + TileDiffXY(1, 1), 0);
	}
}

/** Daily handler for the industry changes
 * Taking the original map size of 256*256, the number of random changes was always of just one unit.
 * But it cannot be the same on smaller or bigger maps. That number has to be scaled up or down.
 * For small maps, it implies that less than one change per month is required, while on bigger maps,
 * it would be way more. The daily loop handles those changes. */
void IndustryDailyLoop()
{
	_economy.industry_daily_change_counter += _economy.industry_daily_increment;

	/* Bits 16-31 of industry_construction_counter contain the number of industries to change/create today,
	 * the lower 16 bit are a fractional part that might accumulate over several days until it
	 * is sufficient for an industry. */
	uint16 change_loop = _economy.industry_daily_change_counter >> 16;

	/* Reset the active part of the counter, just keeping the "factional part" */
	_economy.industry_daily_change_counter &= 0xFFFF;

	if (change_loop == 0) {
		return;  // Nothing to do? get out
	}

	CompanyID old_company = _current_company;
	_current_company = OWNER_NONE;

	/* perform the required industry changes for the day */
	for (uint16 j = 0; j < change_loop; j++) {
		/* 3% chance that we start a new industry */
		if (Chance16(3, 100)) {
			MaybeNewIndustry();
		} else {
			Industry *i = GetRandomIndustry();
			if (i != NULL) ChangeIndustryProduction(i, false);
		}
	}

	_current_company = old_company;

	/* production-change */
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, 1);
}

void IndustryMonthlyLoop()
{
	Industry *i;
	CompanyID old_company = _current_company;
	_current_company = OWNER_NONE;

	FOR_ALL_INDUSTRIES(i) {
		UpdateIndustryStatistics(i);
		if (i->prod_level == PRODLEVEL_CLOSURE) {
			delete i;
		} else {
			ChangeIndustryProduction(i, true);
		}
	}

	_current_company = old_company;

	/* production-change */
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, 1);
}


void InitializeIndustries()
{
	_Industry_pool.CleanPool();
	_Industry_pool.AddBlockToPool();

	ResetIndustryCounts();
	_industry_sound_tile = 0;
}

bool IndustrySpec::IsRawIndustry() const
{
	/* Lumber mills are extractive/organic, but can always be built like a non-raw industry */
	return (this->life_type & (INDUSTRYLIFE_EXTRACTIVE | INDUSTRYLIFE_ORGANIC)) != 0 &&
			(this->behaviour & INDUSTRYBEH_CUT_TREES) == 0;
}

Money IndustrySpec::GetConstructionCost() const
{
	return (_price.build_industry *
			(_settings_game.construction.raw_industry_construction == 1 && this->IsRawIndustry() ?
					this->raw_industry_cost_multiplier :
					this->cost_multiplier
			)) >> 8;
}

Money IndustrySpec::GetRemovalCost() const
{
	return (_price.remove_house * this->removal_cost_multiplier) >> 8;
}

static CommandCost TerraformTile_Industry(TileIndex tile, DoCommandFlag flags, uint z_new, Slope tileh_new)
{
	if (AutoslopeEnabled()) {
		/* We imitate here TTDP's behaviour:
		 *  - Both new and old slope must not be steep.
		 *  - TileMaxZ must not be changed.
		 *  - Allow autoslope by default.
		 *  - Disallow autoslope if callback succeeds and returns non-zero.
		 */
		Slope tileh_old = GetTileSlope(tile, NULL);
		/* TileMaxZ must not be changed. Slopes must not be steep. */
		if (!IsSteepSlope(tileh_old) && !IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new))) {
			const IndustryGfx gfx = GetIndustryGfx(tile);
			const IndustryTileSpec *itspec = GetIndustryTileSpec(gfx);

			/* Call callback 3C 'disable autosloping for industry tiles'. */
			if (HasBit(itspec->callback_flags, CBM_INDT_AUTOSLOPE)) {
				/* If the callback fails, allow autoslope. */
				uint16 res = GetIndustryTileCallback(CBID_INDUSTRY_AUTOSLOPE, 0, 0, gfx, GetIndustryByTile(tile), tile);
				if ((res == 0) || (res == CALLBACK_FAILED)) return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
			} else {
				/* allow autoslope */
				return CommandCost(EXPENSES_CONSTRUCTION, _price.terraform);
			}
		}
	}
	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

extern const TileTypeProcs _tile_type_industry_procs = {
	DrawTile_Industry,           // draw_tile_proc
	GetSlopeZ_Industry,          // get_slope_z_proc
	ClearTile_Industry,          // clear_tile_proc
	GetAcceptedCargo_Industry,   // get_accepted_cargo_proc
	GetTileDesc_Industry,        // get_tile_desc_proc
	GetTileTrackStatus_Industry, // get_tile_track_status_proc
	ClickTile_Industry,          // click_tile_proc
	AnimateTile_Industry,        // animate_tile_proc
	TileLoop_Industry,           // tile_loop_proc
	ChangeTileOwner_Industry,    // change_tile_owner_proc
	GetProducedCargo_Industry,   // get_produced_cargo_proc
	NULL,                        // vehicle_enter_tile_proc
	GetFoundation_Industry,      // get_foundation_proc
	TerraformTile_Industry,      // terraform_tile_proc
};
