/* $Id$ */

/** @file sound.cpp Handling of playing sounds. */

#include "stdafx.h"
#include "landscape.h"
#include "mixer.h"
#include "fileio_func.h"
#include "newgrf_sound.h"
#include "fios.h"
#include "window_gui.h"
#include "map_func.h"
#include "vehicle_base.h"
#include "debug.h"

static uint _file_count;
static FileEntry *_files;
MusicFileSettings msf;

/* Number of levels of panning per side */
#define PANNING_LEVELS 16

/** The number of sounds in the original sample.cat */
static const uint ORIGINAL_SAMPLE_COUNT = 73;

static void OpenBankFile(const char *filename)
{
	FileEntry *fe = CallocT<FileEntry>(ORIGINAL_SAMPLE_COUNT);
	_file_count = ORIGINAL_SAMPLE_COUNT;
	_files = fe;

	FioOpenFile(SOUND_SLOT, filename);
	size_t pos = FioGetPos();
	uint count = FioReadDword() / 8;

	/* Simple check for the correct number of original sounds. */
	if (count != ORIGINAL_SAMPLE_COUNT) {
		/* Corrupt sample data? Just leave the allocated memory as those tell
		 * there is no sound to play (size = 0 due to calloc). Not allocating
		 * the memory disables valid NewGRFs that replace sounds. */
		DEBUG(misc, 6, "Incorrect number of sounds in '%s', ignoring.", filename);
		return;
	}

	FioSeekTo(pos, SEEK_SET);

	for (uint i = 0; i != ORIGINAL_SAMPLE_COUNT; i++) {
		fe[i].file_slot = SOUND_SLOT;
		fe[i].file_offset = FioReadDword() + pos;
		fe[i].file_size = FioReadDword();
	}

	for (uint i = 0; i != ORIGINAL_SAMPLE_COUNT; i++, fe++) {
		char name[255];

		FioSeekTo(fe->file_offset, SEEK_SET);

		/* Check for special case, see else case */
		FioReadBlock(name, FioReadByte()); // Read the name of the sound
		if (strcmp(name, "Corrupt sound") != 0) {
			FioSeekTo(12, SEEK_CUR); // Skip past RIFF header

			/* Read riff tags */
			for (;;) {
				uint32 tag = FioReadDword();
				uint32 size = FioReadDword();

				if (tag == ' tmf') {
					FioReadWord(); // wFormatTag
					fe->channels = FioReadWord(); // wChannels
					FioReadDword();   // samples per second
					fe->rate = 11025; // seems like all samples should be played at this rate.
					FioReadDword();   // avg bytes per second
					FioReadWord();    // alignment
					fe->bits_per_sample = FioReadByte(); // bits per sample
					FioSeekTo(size - (2 + 2 + 4 + 4 + 2 + 1), SEEK_CUR);
				} else if (tag == 'atad') {
					fe->file_size = size;
					fe->file_slot = SOUND_SLOT;
					fe->file_offset = FioGetPos();
					break;
				} else {
					fe->file_size = 0;
					break;
				}
			}
		} else {
			/*
			 * Special case for the jackhammer sound
			 * (name in sample.cat is "Corrupt sound")
			 * It's no RIFF file, but raw PCM data
			 */
			fe->channels = 1;
			fe->rate = 11025;
			fe->bits_per_sample = 8;
			fe->file_slot = SOUND_SLOT;
			fe->file_offset = FioGetPos();
		}
	}
}

uint GetNumOriginalSounds()
{
	return _file_count;
}

static bool SetBankSource(MixerChannel *mc, const FileEntry *fe)
{
	assert(fe != NULL);

	if (fe->file_size == 0) return false;

	int8 *mem = MallocT<int8>(fe->file_size);

	FioSeekToFile(fe->file_slot, fe->file_offset);
	FioReadBlock(mem, fe->file_size);

	for (uint i = 0; i != fe->file_size; i++) {
		mem[i] += -128; // Convert unsigned sound data to signed
	}

	assert(fe->bits_per_sample == 8 && fe->channels == 1 && fe->file_size != 0 && fe->rate != 0);

	MxSetChannelRawSrc(mc, mem, fe->file_size, fe->rate, MX_AUTOFREE);

	return true;
}

bool SoundInitialize(const char *filename)
{
	OpenBankFile(filename);
	return true;
}

/* Low level sound player */
static void StartSound(uint sound, int panning, uint volume)
{
	if (volume == 0) return;

	const FileEntry *fe = GetSound(sound);
	if (fe == NULL) return;

	MixerChannel *mc = MxAllocateChannel();
	if (mc == NULL) return;

	if (!SetBankSource(mc, fe)) return;

	/* Apply the sound effect's own volume. */
	volume = (fe->volume * volume) / 128;

	panning = Clamp(panning, -PANNING_LEVELS, PANNING_LEVELS);
	uint left_vol = (volume * PANNING_LEVELS) - (volume * panning);
	uint right_vol = (volume * PANNING_LEVELS) + (volume * panning);
	MxSetChannelVolume(mc, left_vol * 128 / PANNING_LEVELS, right_vol * 128 / PANNING_LEVELS);
	MxActivateChannel(mc);
}


static const byte _vol_factor_by_zoom[] = {255, 190, 134, 87};
assert_compile(lengthof(_vol_factor_by_zoom) == ZOOM_LVL_COUNT);

static const byte _sound_base_vol[] = {
	128,  90, 128, 128, 128, 128, 128, 128,
	128,  90,  90, 128, 128, 128, 128, 128,
	128, 128, 128,  80, 128, 128, 128, 128,
	128, 128, 128, 128, 128, 128, 128, 128,
	128, 128,  90,  90,  90, 128,  90, 128,
	128,  90, 128, 128, 128,  90, 128, 128,
	128, 128, 128, 128,  90, 128, 128, 128,
	128,  90, 128, 128, 128, 128, 128, 128,
	128, 128,  90,  90,  90, 128, 128, 128,
	 90,
};

static const byte _sound_idx[] = {
	 2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25,
	26, 27, 28, 29, 30, 31, 32, 33,
	34, 35, 36, 37, 38, 39, 40,  0,
	 1, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 63,
	64, 65, 66, 67, 68, 69, 70, 71,
	72,
};

void SndCopyToPool()
{
	for (uint i = 0; i < _file_count; i++) {
		FileEntry *orig = &_files[_sound_idx[i]];
		FileEntry *fe = AllocateFileEntry();

		*fe = *orig;
		fe->volume = _sound_base_vol[i];
		fe->priority = 0;
	}
}

/**
 * Decide 'where' (between left and right speaker) to play the sound effect.
 * @param sound Sound effect to play
 * @param left   Left edge of virtual coordinates where the sound is produced
 * @param right  Right edge of virtual coordinates where the sound is produced
 * @param top    Top edge of virtual coordinates where the sound is produced
 * @param bottom Bottom edge of virtual coordinates where the sound is produced
 */
static void SndPlayScreenCoordFx(SoundFx sound, int left, int right, int top, int bottom)
{
	if (msf.effect_vol == 0) return;

	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		const ViewPort *vp = w->viewport;

		if (vp != NULL &&
				left < vp->virtual_left + vp->virtual_width && right > vp->virtual_left &&
				top < vp->virtual_top + vp->virtual_height && bottom > vp->virtual_top) {
			int screen_x = (left + right) / 2 - vp->virtual_left;
			int width = (vp->virtual_width == 0 ? 1 : vp->virtual_width);
			int panning = (screen_x * PANNING_LEVELS * 2) / width - PANNING_LEVELS;

			StartSound(
				sound,
				panning,
				(msf.effect_vol * _vol_factor_by_zoom[vp->zoom - ZOOM_LVL_BEGIN]) / 256
			);
			return;
		}
	}
}

void SndPlayTileFx(SoundFx sound, TileIndex tile)
{
	/* emits sound from center of the tile */
	int x = min(MapMaxX() - 1, TileX(tile)) * TILE_SIZE + TILE_SIZE / 2;
	int y = min(MapMaxY() - 1, TileY(tile)) * TILE_SIZE - TILE_SIZE / 2;
	uint z = (y < 0 ? 0 : GetSlopeZ(x, y));
	Point pt = RemapCoords(x, y, z);
	y += 2 * TILE_SIZE;
	Point pt2 = RemapCoords(x, y, GetSlopeZ(x, y));
	SndPlayScreenCoordFx(sound, pt.x, pt2.x, pt.y, pt2.y);
}

void SndPlayVehicleFx(SoundFx sound, const Vehicle *v)
{
	SndPlayScreenCoordFx(sound,
		v->coord.left, v->coord.right,
		v->coord.top, v->coord.bottom
	);
}

void SndPlayFx(SoundFx sound)
{
	StartSound(sound, 0, msf.effect_vol);
}
