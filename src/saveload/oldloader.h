/* $Id$ */

/** @file oldloader.h Declarations of strctures and function used in loader of old savegames */

#ifndef OLDLOADER_H
#define OLDLOADER_H

#include "saveload.h"

enum {
	BUFFER_SIZE = 4096,
	OLD_MAP_SIZE = 256 * 256,
};

struct LoadgameState {
	FILE *file;

	uint chunk_size;

	bool decoding;
	byte decode_char;

	uint buffer_count;
	uint buffer_cur;
	byte buffer[BUFFER_SIZE];

	uint total_read;
	bool failed;
};

/* OldChunk-Type */
enum OldChunkType {
	OC_SIMPLE    = 0,
	OC_NULL      = 1,
	OC_CHUNK     = 2,
	OC_ASSERT    = 3,
	/* 4 bits allocated (16 max) */

	OC_TTD       = 1 << 4, ///< chunk is valid ONLY for TTD savegames
	OC_TTO       = 1 << 5, ///< -//- TTO (default is neither of these)
	/* 4 bits allocated */

	OC_VAR_I8    = 1 << 8,
	OC_VAR_U8    = 2 << 8,
	OC_VAR_I16   = 3 << 8,
	OC_VAR_U16   = 4 << 8,
	OC_VAR_I32   = 5 << 8,
	OC_VAR_U32   = 6 << 8,
	OC_VAR_I64   = 7 << 8,
	OC_VAR_U64   = 8 << 8,
	/* 8 bits allocated (256 max) */

	OC_FILE_I8   = 1 << 16,
	OC_FILE_U8   = 2 << 16,
	OC_FILE_I16  = 3 << 16,
	OC_FILE_U16  = 4 << 16,
	OC_FILE_I32  = 5 << 16,
	OC_FILE_U32  = 6 << 16,
	/* 8 bits allocated (256 max) */

	OC_INT8      = OC_VAR_I8   | OC_FILE_I8,
	OC_UINT8     = OC_VAR_U8   | OC_FILE_U8,
	OC_INT16     = OC_VAR_I16  | OC_FILE_I16,
	OC_UINT16    = OC_VAR_U16  | OC_FILE_U16,
	OC_INT32     = OC_VAR_I32  | OC_FILE_I32,
	OC_UINT32    = OC_VAR_U32  | OC_FILE_U32,

	OC_TILE      = OC_VAR_U32  | OC_FILE_U16,

	/**
	 * Dereference the pointer once before writing to it,
	 * so we do not have to use big static arrays.
	 */
	OC_DEREFERENCE_POINTER = 1 << 31,

	OC_END       = 0 ///< End of the whole chunk, all 32 bits set to zero
};

DECLARE_ENUM_AS_BIT_SET(OldChunkType);

typedef bool OldChunkProc(LoadgameState *ls, int num);

struct OldChunks {
	OldChunkType type;   ///< Type of field
	uint32 amount;       ///< Amount of fields

	void *ptr;           ///< Pointer where to save the data (may only be set if offset is 0)
	uint offset;         ///< Offset from basepointer (may only be set if ptr is NULL)
	OldChunkProc *proc;  ///< Pointer to function that is called with OC_CHUNK
};

/* If it fails, check lines above.. */
assert_compile(sizeof(TileIndex) == 4);

extern uint _bump_assert_value;
byte ReadByte(LoadgameState *ls);
bool LoadChunk(LoadgameState *ls, void *base, const OldChunks *chunks);

bool LoadTTDMain(LoadgameState *ls);
bool LoadTTOMain(LoadgameState *ls);

static inline uint16 ReadUint16(LoadgameState *ls)
{
	byte x = ReadByte(ls);
	return x | ReadByte(ls) << 8;
}

static inline uint32 ReadUint32(LoadgameState *ls)
{
	uint16 x = ReadUint16(ls);
	return x | ReadUint16(ls) << 16;
}

/* Help:
 *  - OCL_SVAR: load 'type' to offset 'offset' in a struct of type 'base', which must also
 *       be given via base in LoadChunk() as real pointer
 *  - OCL_VAR: load 'type' to a global var
 *  - OCL_END: every struct must end with this
 *  - OCL_NULL: read 'amount' of bytes and send them to /dev/null or something
 *  - OCL_CHUNK: load an other proc to load a part of the savegame, 'amount' times
 *  - OCL_ASSERT: to check if we are really at the place we expect to be.. because old savegames are too binary to be sure ;)
 */
#define OCL_SVAR(type, base, offset)         { type,                 1,    NULL, (uint)cpp_offsetof(base, offset), NULL }
#define OCL_VAR(type, amount, pointer)       { type,            amount, pointer,    0,                             NULL }
#define OCL_END()                            { OC_END,               0,    NULL,    0,                             NULL }
#define OCL_CNULL(type, amount)              { OC_NULL | type,  amount,    NULL,    0,                             NULL }
#define OCL_CCHUNK(type, amount, proc)       { OC_CHUNK | type, amount,    NULL,    0,                             proc }
#define OCL_ASSERT(type, size)               { OC_ASSERT | type,     1,    NULL, size,                             NULL }
#define OCL_NULL(amount)        OCL_CNULL((OldChunkType)0, amount)
#define OCL_CHUNK(amount, proc) OCL_CCHUNK((OldChunkType)0, amount, proc)

#endif /* OLDLOADER_H */
