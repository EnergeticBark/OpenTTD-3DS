/* $Id$ */

/** @file texteff.hpp Functions related to text effects. */

#ifndef TEXTEFF_HPP
#define TEXTEFF_HPP

#include "gfx_type.h"

/**
 * Text effect modes.
 */
enum TextEffectMode {
	TE_RISING, ///< Make the text effect slowly go upwards
	TE_STATIC, ///< Keep the text effect static

	INVALID_TE_ID = 0xFFFF,
};

typedef uint16 TextEffectID;

void MoveAllTextEffects();
TextEffectID AddTextEffect(StringID msg, int x, int y, uint16 duration, TextEffectMode mode);
void InitTextEffects();
void DrawTextEffects(DrawPixelInfo *dpi);
void UpdateTextEffect(TextEffectID effect_id, StringID msg);
void RemoveTextEffect(TextEffectID effect_id);

/* misc_gui.cpp */
TextEffectID ShowFillingPercent(int x, int y, int z, uint8 percent, StringID colour);
void UpdateFillingPercent(TextEffectID te_id, uint8 percent, StringID colour);
void HideFillingPercent(TextEffectID *te_id);

#endif /* TEXTEFF_HPP */
