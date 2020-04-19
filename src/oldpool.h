/* $Id$ */

/** @file oldpool.h Base for the old pool. */

#ifndef OLDPOOL_H
#define OLDPOOL_H

#include "core/math_func.hpp"

/* The function that is called after a new block is added
     start_item is the first item of the new made block */
typedef void OldMemoryPoolNewBlock(uint start_item);
/* The function that is called before a block is cleaned up */
typedef void OldMemoryPoolCleanBlock(uint start_item, uint end_item);

/**
 * Stuff for dynamic vehicles. Use the wrappers to access the OldMemoryPool
 *  please try to avoid manual calls!
 */
struct OldMemoryPoolBase {
	void CleanPool();
	bool AddBlockToPool();
	bool AddBlockIfNeeded(uint index);

protected:
	OldMemoryPoolBase(const char *name, uint max_blocks, uint block_size_bits, uint item_size,
				OldMemoryPoolNewBlock *new_block_proc, OldMemoryPoolCleanBlock *clean_block_proc) :
		name(name), max_blocks(max_blocks), block_size_bits(block_size_bits),
		new_block_proc(new_block_proc), clean_block_proc(clean_block_proc), current_blocks(0),
		total_items(0), cleaning_pool(false), item_size(item_size), first_free_index(0), blocks(NULL) {}

	const char *name;           ///< Name of the pool (just for debugging)

	const uint max_blocks;      ///< The max amount of blocks this pool can have
	const uint block_size_bits; ///< The size of each block in bits

	/** Pointer to a function that is called after a new block is added */
	OldMemoryPoolNewBlock *new_block_proc;
	/** Pointer to a function that is called to clean a block */
	OldMemoryPoolCleanBlock *clean_block_proc;

	uint current_blocks;        ///< How many blocks we have in our pool
	uint total_items;           ///< How many items we now have in this pool

	bool cleaning_pool;         ///< Are we currently cleaning the pool?
public:
	const uint item_size;       ///< How many bytes one block is
	uint first_free_index;      ///< The index of the first free pool item in this pool
	byte **blocks;              ///< An array of blocks (one block hold all the items)

	/**
	 * Check if the index of pool item being deleted is lower than cached first_free_index
	 * @param index index of pool item
	 * @note usage of min() will result in better code on some architectures
	 */
	inline void UpdateFirstFreeIndex(uint index)
	{
		first_free_index = min(first_free_index, index);
	}

	/**
	 * Get the size of this pool, i.e. the total number of items you
	 * can put into it at the current moment; the pool might still
	 * be able to increase the size of the pool.
	 * @return the size of the pool
	 */
	inline uint GetSize() const
	{
		return this->total_items;
	}

	/**
	 * Can this pool allocate more blocks, i.e. is the maximum amount
	 * of allocated blocks not yet reached?
	 * @return the if and only if the amount of allocable blocks is
	 *         less than the amount of allocated blocks.
	 */
	inline bool CanAllocateMoreBlocks() const
	{
		return this->current_blocks < this->max_blocks;
	}

	/**
	 * Get the maximum number of allocable blocks.
	 * @return the numebr of blocks
	 */
	inline uint GetBlockCount() const
	{
		return this->current_blocks;
	}

	/**
	 * Get the name of this pool.
	 * @return the name
	 */
	inline const char *GetName() const
	{
		return this->name;
	}

	/**
	 * Is the pool in the cleaning phase?
	 * @return true if it is
	 */
	inline bool CleaningPool() const
	{
		return this->cleaning_pool;
	}
};

template <typename T>
struct OldMemoryPool : public OldMemoryPoolBase {
	OldMemoryPool(const char *name, uint max_blocks, uint block_size_bits, uint item_size,
				OldMemoryPoolNewBlock *new_block_proc, OldMemoryPoolCleanBlock *clean_block_proc) :
		OldMemoryPoolBase(name, max_blocks, block_size_bits, item_size, new_block_proc, clean_block_proc) {}

	/**
	 * Get the pool entry at the given index.
	 * @param index the index into the pool
	 * @pre index < this->GetSize()
	 * @return the pool entry.
	 */
	inline T *Get(uint index) const
	{
		assert(index < this->GetSize());
		return (T*)(this->blocks[index >> this->block_size_bits] +
				(index & ((1 << this->block_size_bits) - 1)) * this->item_size);
	}
};

/**
 * Generic function to initialize a new block in a pool.
 * @param start_item the first item that needs to be initialized
 */
template <typename T, OldMemoryPool<T> *Tpool>
static void PoolNewBlock(uint start_item)
{
	for (T *t = Tpool->Get(start_item); t != NULL; t = (t->index + 1U < Tpool->GetSize()) ? Tpool->Get(t->index + 1U) : NULL) {
		t = new (t) T();
		t->index = start_item++;
	}
}

/**
 * Generic function to free a new block in a pool.
 * @param start_item the first item that needs to be cleaned
 * @param end_item   the last item that needs to be cleaned
 */
template <typename T, OldMemoryPool<T> *Tpool>
static void PoolCleanBlock(uint start_item, uint end_item)
{
	for (uint i = start_item; i <= end_item; i++) {
		T *t = Tpool->Get(i);
		delete t;
	}
}

/**
 * Template providing a predicate to allow STL containers of
 * pointers to pool items to be sorted by index.
 */
template <typename T>
struct PoolItemIndexLess {
	/**
	 * The actual comparator.
	 * @param lhs the left hand side of the comparison.
	 * @param rhs the right hand side of the comparison.
	 * @return true if lhs' index is less than rhs' index.
	 */
	bool operator()(const T *lhs, const T *rhs) const
	{
		return lhs->index < rhs->index;
	}
};

/**
 * Generalization for all pool items that are saved in the savegame.
 * It specifies all the mechanics to access the pool easily.
 */
template <typename T, typename Tid, OldMemoryPool<T> *Tpool>
struct PoolItem {
	/**
	 * The pool-wide index of this object.
	 */
	Tid index;

	/**
	 * We like to have the correct class destructed.
	 * @warning It is called even for object allocated on stack,
	 *          so it is not present in the TPool!
	 *          Then, index is undefined, not associated with TPool in any way.
	 * @note    The idea is to free up allocated memory etc.
	 */
	virtual ~PoolItem()
	{

	}

	/**
	 * Constructor of given class.
	 * @warning It is called even for object allocated on stack,
	 *          so it may not be present in TPool!
	 *          Then, index is undefined, not associated with TPool in any way.
	 * @note    The idea is to initialize variables (except index)
	 */
	PoolItem()
	{

	}

	/**
	 * An overriden version of new that allocates memory on the pool.
	 * @param size the size of the variable (unused)
	 * @pre CanAllocateItem()
	 * @return the memory that is 'allocated'
	 */
	void *operator new(size_t size)
	{
		return AllocateRaw();
	}

	/**
	 * 'Free' the memory allocated by the overriden new.
	 * @param p the memory to 'free'
	 * @note we only update Tpool->first_free_index
	 */
	void operator delete(void *p)
	{
		Tpool->UpdateFirstFreeIndex(((T*)p)->index);
	}

	/**
	 * An overriden version of new, so you can directly allocate a new object with
	 * the correct index when one is loading the savegame.
	 * @param size  the size of the variable (unused)
	 * @param index the index of the object
	 * @return the memory that is 'allocated'
	 */
	void *operator new(size_t size, int index)
	{
		if (!Tpool->AddBlockIfNeeded(index)) error("%s: failed loading savegame: too many %s", Tpool->GetName(), Tpool->GetName());

		return Tpool->Get(index);
	}

	/**
	 * 'Free' the memory allocated by the overriden new.
	 * @param p     the memory to 'free'
	 * @param index the original parameter given to create the memory
	 * @note we only update Tpool->first_free_index
	 */
	void operator delete(void *p, int index)
	{
		Tpool->UpdateFirstFreeIndex(index);
	}

	/**
	 * An overriden version of new, so you can use the vehicle instance
	 * instead of a newly allocated piece of memory.
	 * @param size the size of the variable (unused)
	 * @param pn   the already existing object to use as 'storage' backend
	 * @return the memory that is 'allocated'
	 */
	void *operator new(size_t size, T *pn)
	{
		return pn;
	}

	/**
	 * 'Free' the memory allocated by the overriden new.
	 * @param p  the memory to 'free'
	 * @param pn the pointer that was given to 'new' on creation.
	 * @note we only update Tpool->first_free_index
	 */
	void operator delete(void *p, T *pn)
	{
		Tpool->UpdateFirstFreeIndex(pn->index);
	}

private:
	static T *AllocateSafeRaw(uint &first);

protected:
	/**
	 * Allocate a pool item; possibly allocate a new block in the pool.
	 * @pre CanAllocateItem()
	 * @return the allocated pool item.
	 */
	static inline T *AllocateRaw()
	{
		return AllocateSafeRaw(Tpool->first_free_index);
	}

	/**
	 * Allocate a pool item; possibly allocate a new block in the pool.
	 * @param first the first pool item to start searching
	 * @pre CanAllocateItem()
	 * @return the allocated pool item.
	 */
	static inline T *AllocateRaw(uint &first)
	{
		if (first >= Tpool->GetSize() && !Tpool->AddBlockToPool()) return NULL;

		return AllocateSafeRaw(first);
	}

	/**
	 * Are we cleaning this pool?
	 * @return true if we are
	 */
	static inline bool CleaningPool()
	{
		return Tpool->CleaningPool();
	}

public:
	static bool CanAllocateItem(uint count = 1);
};


#define OLD_POOL_ENUM(name, type, block_size_bits, max_blocks) \
	enum { \
		name##_POOL_BLOCK_SIZE_BITS = block_size_bits, \
		name##_POOL_MAX_BLOCKS      = max_blocks \
	};


#define OLD_POOL_ACCESSORS(name, type) \
	static inline type *Get##name(uint index) { return _##name##_pool.Get(index);  } \
	static inline uint Get##name##PoolSize()  { return _##name##_pool.GetSize(); }


#define DECLARE_OLD_POOL(name, type, block_size_bits, max_blocks) \
	OLD_POOL_ENUM(name, type, block_size_bits, max_blocks) \
	extern OldMemoryPool<type> _##name##_pool; \
	OLD_POOL_ACCESSORS(name, type)


#define DEFINE_OLD_POOL(name, type, new_block_proc, clean_block_proc) \
	OldMemoryPool<type> _##name##_pool( \
		#name, name##_POOL_MAX_BLOCKS, name##_POOL_BLOCK_SIZE_BITS, sizeof(type), \
		new_block_proc, clean_block_proc);

#define DEFINE_OLD_POOL_GENERIC(name, type) \
	OldMemoryPool<type> _##name##_pool( \
		#name, name##_POOL_MAX_BLOCKS, name##_POOL_BLOCK_SIZE_BITS, sizeof(type), \
		PoolNewBlock<type, &_##name##_pool>, PoolCleanBlock<type, &_##name##_pool>); \
		template type *PoolItem<type, type##ID, &_##name##_pool>::AllocateSafeRaw(uint &first); \
		template bool PoolItem<type, type##ID, &_##name##_pool>::CanAllocateItem(uint count);


#define STATIC_OLD_POOL(name, type, block_size_bits, max_blocks, new_block_proc, clean_block_proc) \
	OLD_POOL_ENUM(name, type, block_size_bits, max_blocks) \
	static DEFINE_OLD_POOL(name, type, new_block_proc, clean_block_proc) \
	OLD_POOL_ACCESSORS(name, type)

#endif /* OLDPOOL_H */
