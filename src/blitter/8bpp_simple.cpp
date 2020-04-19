/* $Id$ */

/** @file 8bpp_simple.cpp Implementation of the simple 8 bpp blitter. */

#include "../stdafx.h"
#include "../zoom_func.h"
#include "8bpp_simple.hpp"

static FBlitter_8bppSimple iFBlitter_8bppSimple;

void Blitter_8bppSimple::Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom)
{
	const uint8 *src, *src_line;
	uint8 *dst, *dst_line;

	/* Find where to start reading in the source sprite */
	src_line = (const uint8 *)bp->sprite + (bp->skip_top * bp->sprite_width + bp->skip_left) * ScaleByZoom(1, zoom);
	dst_line = (uint8 *)bp->dst + bp->top * bp->pitch + bp->left;

	for (int y = 0; y < bp->height; y++) {
		dst = dst_line;
		dst_line += bp->pitch;

		src = src_line;
		src_line += bp->sprite_width * ScaleByZoom(1, zoom);

		for (int x = 0; x < bp->width; x++) {
			uint colour = 0;

			switch (mode) {
				case BM_COLOUR_REMAP:
					colour = bp->remap[*src];
					break;

				case BM_TRANSPARENT:
					if (*src != 0) colour = bp->remap[*dst];
					break;

				default:
					colour = *src;
					break;
			}
			if (colour != 0) *dst = colour;
			dst++;
			src += ScaleByZoom(1, zoom);
		}
	}
}

Sprite *Blitter_8bppSimple::Encode(SpriteLoader::Sprite *sprite, Blitter::AllocatorProc *allocator)
{
	Sprite *dest_sprite;
	dest_sprite = (Sprite *)allocator(sizeof(*dest_sprite) + sprite->height * sprite->width);;

	dest_sprite->height = sprite->height;
	dest_sprite->width  = sprite->width;
	dest_sprite->x_offs = sprite->x_offs;
	dest_sprite->y_offs = sprite->y_offs;

	/* Copy over only the 'remap' channel, as that is what we care about in 8bpp */
	for (int i = 0; i < sprite->height * sprite->width; i++) {
		dest_sprite->data[i] = sprite->data[i].m;
	}

	return dest_sprite;
}
