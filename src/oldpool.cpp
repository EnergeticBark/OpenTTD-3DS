/* $Id$ */

/** @file oldpool.cpp Implementation of the old pool. */

#include "stdafx.h"
#include "debug.h"
#include "oldpool.h"
#include "core/alloc_func.hpp"

/**
 * Clean a pool in a safe way (does free all blocks)
 */
void OldMemoryPoolBase::CleanPool()
{
	uint i;

	DEBUG(misc, 4, "[Pool] (%s) cleaning pool..", this->name);

	this->cleaning_pool = true;
	/* Free all blocks */
	for (i = 0; i < this->current_blocks; i++) {
		if (this->clean_block_proc != NULL) {
			this->clean_block_proc(i * (1 << this->block_size_bits), (i + 1) * (1 << this->block_size_bits) - 1);
		}
		free(this->blocks[i]);
	}
	this->cleaning_pool = false;

	/* Free the block itself */
	free(this->blocks);

	/* Clear up some critical data */
	this->total_items = 0;
	this->current_blocks = 0;
	this->blocks = NULL;
	this->first_free_index = 0;
}

/**
 * This function tries to increase the size of array by adding
 *  1 block too it
 *
 * @return Returns false if the pool could not be increased
 */
bool OldMemoryPoolBase::AddBlockToPool()
{
	/* Is the pool at his max? */
	if (this->max_blocks == this->current_blocks) return false;

	this->total_items = (this->current_blocks + 1) * (1 << this->block_size_bits);

	DEBUG(misc, 4, "[Pool] (%s) increasing size of pool to %d items (%d bytes)", this->name, this->total_items, this->total_items * this->item_size);

	/* Increase the poolsize */
	this->blocks = ReallocT(this->blocks, this->current_blocks + 1);

	/* Allocate memory to the new block item */
	this->blocks[this->current_blocks] = CallocT<byte>(this->item_size * (1 << this->block_size_bits));

	/* Call a custom function if defined (e.g. to fill indexes) */
	if (this->new_block_proc != NULL) this->new_block_proc(this->current_blocks * (1 << this->block_size_bits));

	/* We have a new block */
	this->current_blocks++;

	return true;
}

/**
 * Adds blocks to the pool if needed (and possible) till index fits inside the pool
 *
 * @return Returns false if adding failed
 */
bool OldMemoryPoolBase::AddBlockIfNeeded(uint index)
{
	while (index >= this->total_items) {
		if (!this->AddBlockToPool()) return false;
	}

	return true;
}
