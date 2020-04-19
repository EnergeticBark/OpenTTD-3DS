/* $Id$ */

/** @file null.hpp The blitter that doesn't blit. */

#ifndef BLITTER_NULL_HPP
#define BLITTER_NULL_HPP

#include "base.hpp"
#include "factory.hpp"

class Blitter_Null : public Blitter {
public:
	/* virtual */ uint8 GetScreenDepth() { return 0; }
	/* virtual */ void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom) {};
	/* virtual */ void DrawColourMappingRect(void *dst, int width, int height, int pal) {};
	/* virtual */ Sprite *Encode(SpriteLoader::Sprite *sprite, Blitter::AllocatorProc *allocator);
	/* virtual */ void *MoveTo(const void *video, int x, int y) { return NULL; };
	/* virtual */ void SetPixel(void *video, int x, int y, uint8 colour) {};
	/* virtual */ void SetPixelIfEmpty(void *video, int x, int y, uint8 colour) {};
	/* virtual */ void DrawRect(void *video, int width, int height, uint8 colour) {};
	/* virtual */ void DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour) {};
	/* virtual */ void CopyFromBuffer(void *video, const void *src, int width, int height) {};
	/* virtual */ void CopyToBuffer(const void *video, void *dst, int width, int height) {};
	/* virtual */ void CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch) {};
	/* virtual */ void ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y) {};
	/* virtual */ int BufferSize(int width, int height) { return 0; };
	/* virtual */ void PaletteAnimate(uint start, uint count) { };
	/* virtual */ Blitter::PaletteAnimation UsePaletteAnimation() { return Blitter::PALETTE_ANIMATION_NONE; };

	/* virtual */ const char *GetName() { return "null"; }
	/* virtual */ int GetBytesPerPixel() { return 0; }
};

class FBlitter_Null: public BlitterFactory<FBlitter_Null> {
public:
	/* virtual */ const char *GetName() { return "null"; }
	/* virtual */ const char *GetDescription() { return "Null Blitter (does nothing)"; }
	/* virtual */ Blitter *CreateInstance() { return new Blitter_Null(); }
};

#endif /* BLITTER_NULL_HPP */
