/* $Id$ */

/** @file grf.hpp Base for reading sprites from (New)GRFs. */

#ifndef SPRITELOADER_GRF_HPP
#define SPRITELOADER_GRF_HPP

#include "spriteloader.hpp"

class SpriteLoaderGrf : public SpriteLoader {
public:
	/**
	 * Load a sprite from the disk and return a sprite struct which is the same for all loaders.
	 */
	bool LoadSprite(SpriteLoader::Sprite *sprite, uint8 file_slot, size_t file_pos, SpriteType sprite_type);
};

#endif /* SPRITELOADER_GRF_HPP */
