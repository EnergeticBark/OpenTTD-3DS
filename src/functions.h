/* $Id$ */

/** @file functions.h Some generic functions that actually shouldn't be here. */

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "core/random_func.hpp"
#include "command_type.h"
#include "tile_cmd.h"

/* clear_land.cpp */
void DrawHillyLandTile(const TileInfo *ti);
void DrawClearLandTile(const TileInfo *ti, byte set);
void DrawClearLandFence(const TileInfo *ti);
void TileLoopClearHelper(TileIndex tile);

/* company_cmd.cpp */
bool CheckCompanyHasMoney(CommandCost cost);
void SubtractMoneyFromCompany(CommandCost cost);
void SubtractMoneyFromCompanyFract(CompanyID company, CommandCost cost);
bool CheckOwnership(Owner owner);
bool CheckTileOwnership(TileIndex tile);

void InitializeLandscapeVariables(bool only_constants);

/* misc functions */
/**
 * Mark a tile given by its coordinate dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkTileDirty(int x, int y);

/**
 * Mark a tile given by its index dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkTileDirtyByTile(TileIndex tile);

/**
 * Mark all viewports dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkAllViewportsDirty(int left, int top, int right, int bottom);
void ShowCostOrIncomeAnimation(int x, int y, int z, Money cost);
void ShowFeederIncomeAnimation(int x, int y, int z, Money cost);

void AskExitGame();
void AskExitToGameMenu();

void RedrawAutosave();

int ttd_main(int argc, char *argv[]);
void HandleExitGameRequest();

#endif /* FUNCTIONS_H */
