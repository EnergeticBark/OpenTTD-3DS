/* $Id$ */

/** @file music_gui.cpp GUI for the music playback. */

#include "stdafx.h"
#include "openttd.h"
#include "fileio_func.h"
#include "music.h"
#include "music/music_driver.hpp"
#include "window_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "gfx_func.h"
#include "core/math_func.hpp"
#include "core/random_func.hpp"

#include "table/strings.h"
#include "table/sprites.h"

static byte _music_wnd_cursong;
static bool _song_is_active;
static byte _cur_playlist[NUM_SONGS_PLAYLIST];



static byte _playlist_all[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0
};

static byte _playlist_old_style[] = {
	1, 8, 2, 9, 14, 15, 19, 13, 0
};

static byte _playlist_new_style[] = {
	6, 11, 10, 17, 21, 18, 5, 0
};

static byte _playlist_ezy_street[] = {
	12, 7, 16, 3, 20, 4, 0
};

static byte * const _playlists[] = {
	_playlist_all,
	_playlist_old_style,
	_playlist_new_style,
	_playlist_ezy_street,
	msf.custom_1,
	msf.custom_2,
};

static void SkipToPrevSong()
{
	byte *b = _cur_playlist;
	byte *p = b;
	byte t;

	if (b[0] == 0) return; // empty playlist

	do p++; while (p[0] != 0); // find the end

	t = *--p; // and copy the bytes
	while (p != b) {
		p--;
		p[1] = p[0];
	}
	*b = t;

	_song_is_active = false;
}

static void SkipToNextSong()
{
	byte *b = _cur_playlist;
	byte t;

	t = b[0];
	if (t != 0) {
		while (b[1] != 0) {
			b[0] = b[1];
			b++;
		}
		b[0] = t;
	}

	_song_is_active = false;
}

static void MusicVolumeChanged(byte new_vol)
{
	_music_driver->SetVolume(new_vol);
}

static void DoPlaySong()
{
	char filename[MAX_PATH];
	FioFindFullPath(filename, lengthof(filename), GM_DIR,
			_origin_songs_specs[_music_wnd_cursong - 1].filename);
	_music_driver->PlaySong(filename);
}

static void DoStopMusic()
{
	_music_driver->StopSong();
}

static void SelectSongToPlay()
{
	uint i = 0;
	uint j = 0;

	memset(_cur_playlist, 0, sizeof(_cur_playlist));
	do {
		/* We are now checking for the existence of that file prior
		 * to add it to the list of available songs */
		if (FioCheckFileExists(_origin_songs_specs[_playlists[msf.playlist][i] - 1].filename, GM_DIR)) {
			_cur_playlist[j] = _playlists[msf.playlist][i];
			j++;
		}
	} while (_playlists[msf.playlist][++i] != 0 && j < lengthof(_cur_playlist) - 1);

	/* Do not shuffle when on the intro-start window, as the song to play has to be the original TTD Theme*/
	if (msf.shuffle && _game_mode != GM_MENU) {
		i = 500;
		do {
			uint32 r = InteractiveRandom();
			byte *a = &_cur_playlist[GB(r, 0, 5)];
			byte *b = &_cur_playlist[GB(r, 8, 5)];

			if (*a != 0 && *b != 0) {
				byte t = *a;
				*a = *b;
				*b = t;
			}
		} while (--i);
	}
}

static void StopMusic()
{
	_music_wnd_cursong = 0;
	DoStopMusic();
	_song_is_active = false;
	InvalidateWindowWidget(WC_MUSIC_WINDOW, 0, 9);
}

static void PlayPlaylistSong()
{
	if (_cur_playlist[0] == 0) {
		SelectSongToPlay();
		/* if there is not songs in the playlist, it may indicate
		 * no file on the gm folder, or even no gm folder.
		 * Stop the playback, then */
		if (_cur_playlist[0] == 0) {
			_song_is_active = false;
			_music_wnd_cursong = 0;
			msf.playing = false;
			return;
		}
	}
	_music_wnd_cursong = _cur_playlist[0];
	DoPlaySong();
	_song_is_active = true;

	InvalidateWindowWidget(WC_MUSIC_WINDOW, 0, 9);
}

void ResetMusic()
{
	_music_wnd_cursong = 1;
	DoPlaySong();
}

void MusicLoop()
{
	if (!msf.playing && _song_is_active) {
		StopMusic();
	} else if (msf.playing && !_song_is_active) {
		PlayPlaylistSong();
	}

	if (!_song_is_active) return;

	if (!_music_driver->IsSongPlaying()) {
		if (_game_mode != GM_MENU) {
			StopMusic();
			SkipToNextSong();
			PlayPlaylistSong();
		} else {
			ResetMusic();
		}
	}
}

struct MusicTrackSelectionWindow : public Window {
private:
	enum MusicTrackSelectionWidgets {
		MTSW_CLOSE,
		MTSW_CAPTION,
		MTSW_BACKGROUND,
		MTSW_LIST_LEFT,
		MTSW_LIST_RIGHT,
		MTSW_ALL,
		MTSW_OLD,
		MTSW_NEW,
		MTSW_EZY,
		MTSW_CUSTOM1,
		MTSW_CUSTOM2,
		MTSW_CLEAR,
		MTSW_SAVE,
	};

public:
	MusicTrackSelectionWindow(const WindowDesc *desc, WindowNumber number) : Window(desc, number)
	{
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		const byte *p;
		uint i;
		int y;

		this->SetWidgetDisabledState(MTSW_CLEAR, msf.playlist <= 3);
		this->LowerWidget(MTSW_LIST_LEFT);
		this->LowerWidget(MTSW_LIST_RIGHT);
		this->DrawWidgets();

		GfxFillRect(  3, 23, 3 + 177,   23 + 191, 0);
		GfxFillRect(251, 23, 251 + 177, 23 + 191, 0);

		DrawStringCentered(92, 15, STR_01EE_TRACK_INDEX, TC_FROMSTRING);

		SetDParam(0, STR_01D5_ALL + msf.playlist);
		DrawStringCentered(340, 15, STR_01EF_PROGRAM, TC_FROMSTRING);

		for (i = 1; i <= NUM_SONGS_AVAILABLE; i++) {
			SetDParam(0, i);
			SetDParam(2, i);
			SetDParam(1, SPECSTR_SONGNAME);
			DrawString(4, 23 + (i - 1) * 6, (i < 10) ? STR_01EC_0 : STR_01ED, TC_FROMSTRING);
		}

		for (i = 0; i != 6; i++) {
			DrawStringCentered(216, 45 + i * 8, STR_01D5_ALL + i, (i == msf.playlist) ? TC_WHITE : TC_BLACK);
		}

		DrawStringCentered(216, 45 + 8 * 6 + 16, STR_01F0_CLEAR, TC_FROMSTRING);
#if 0
		DrawStringCentered(216, 45 + 8 * 6 + 16 * 2, STR_01F1_SAVE, TC_FROMSTRING);
#endif

		y = 23;
		for (p = _playlists[msf.playlist], i = 0; (i = *p) != 0; p++) {
			SetDParam(0, i);
			SetDParam(1, SPECSTR_SONGNAME);
			SetDParam(2, i);
			DrawString(252, y, (i < 10) ? STR_01EC_0 : STR_01ED, TC_FROMSTRING);
			y += 6;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case MTSW_LIST_LEFT: { // add to playlist
				int y = (pt.y - 23) / 6;
				uint i;
				byte *p;

				if (msf.playlist < 4) return;
				if (!IsInsideMM(y, 0, NUM_SONGS_AVAILABLE)) return;

				p = _playlists[msf.playlist];
				for (i = 0; i != NUM_SONGS_PLAYLIST - 1; i++) {
					if (p[i] == 0) {
						p[i] = y + 1;
						p[i + 1] = 0;
						this->SetDirty();
						SelectSongToPlay();
						break;
					}
				}
			} break;

			case MTSW_LIST_RIGHT: { // remove from playlist
				int y = (pt.y - 23) / 6;
				uint i;
				byte *p;

				if (msf.playlist < 4) return;
				if (!IsInsideMM(y, 0, NUM_SONGS_AVAILABLE)) return;

				p = _playlists[msf.playlist];
				for (i = y; i != NUM_SONGS_PLAYLIST - 1; i++) {
					p[i] = p[i + 1];
				}

				this->SetDirty();
				SelectSongToPlay();
			} break;

			case MTSW_CLEAR: // clear
				_playlists[msf.playlist][0] = 0;
				this->SetDirty();
				StopMusic();
				SelectSongToPlay();
				break;

#if 0
			case MTSW_SAVE: // save
				ShowInfo("MusicTrackSelectionWndProc:save not implemented");
				break;
#endif

			case MTSW_ALL: case MTSW_OLD: case MTSW_NEW:
			case MTSW_EZY: case MTSW_CUSTOM1: case MTSW_CUSTOM2: // set playlist
				msf.playlist = widget - MTSW_ALL;
				this->SetDirty();
				InvalidateWindow(WC_MUSIC_WINDOW, 0);
				StopMusic();
				SelectSongToPlay();
				break;
		}
	}
};

static const Widget _music_track_selection_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,                         STR_018B_CLOSE_WINDOW},                  // MTSW_CLOSE
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   431,     0,    13, STR_01EB_MUSIC_PROGRAM_SELECTION, STR_018C_WINDOW_TITLE_DRAG_THIS},        // MTSW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   431,    14,   217, 0x0,                              STR_NULL},                               // MTSW_BACKGROUND
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     2,   181,    22,   215, 0x0,                              STR_01FA_CLICK_ON_MUSIC_TRACK_TO},       // MTSW_LIST_LEFT
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,   250,   429,    22,   215, 0x0,                              STR_CLICK_ON_TRACK_TO_REMOVE},           // MTSW_LIST_RIGHT
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    44,    51, 0x0,                              STR_01F3_SELECT_ALL_TRACKS_PROGRAM},     // MTSW_ALL
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    52,    59, 0x0,                              STR_01F4_SELECT_OLD_STYLE_MUSIC},        // MTSW_OLD
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    60,    67, 0x0,                              STR_01F5_SELECT_NEW_STYLE_MUSIC},        // MTSW_NEW
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    68,    75, 0x0,                              STR_0330_SELECT_EZY_STREET_STYLE},       // MTSW_EZY
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    76,    83, 0x0,                              STR_01F6_SELECT_CUSTOM_1_USER_DEFINED},  // MTSW_CUSTOM1
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    84,    91, 0x0,                              STR_01F7_SELECT_CUSTOM_2_USER_DEFINED},  // MTSW_CUSTOM2
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,   108,   115, 0x0,                              STR_01F8_CLEAR_CURRENT_PROGRAM_CUSTOM1}, // MTSW_CLEAR
#if 0
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,   124,   131, 0x0,                              STR_01F9_SAVE_MUSIC_SETTINGS},           // MTSW_SAVE
#endif
{   WIDGETS_END},
};

static const WindowDesc _music_track_selection_desc(
	104, 131, 432, 218, 432, 218,
	WC_MUSIC_TRACK_SELECTION, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_music_track_selection_widgets
);

static void ShowMusicTrackSelection()
{
	AllocateWindowDescFront<MusicTrackSelectionWindow>(&_music_track_selection_desc, 0);
}

struct MusicWindow : public Window {
private:
	enum MusicWidgets {
		MW_CLOSE,
		MW_CAPTION,
		MW_PREV,
		MW_NEXT,
		MW_STOP,
		MW_PLAY,
		MW_SLIDERS,
		MW_GAUGE,
		MW_BACKGROUND,
		MW_INFO,
		MW_SHUFFLE,
		MW_PROGRAMME,
		MW_ALL,
		MW_OLD,
		MW_NEW,
		MW_EZY,
		MW_CUSTOM1,
		MW_CUSTOM2,
	};

public:
	MusicWindow(const WindowDesc *desc, WindowNumber number) : Window(desc, number)
	{
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		uint i;
		StringID str;

		this->RaiseWidget(MW_GAUGE);
		this->RaiseWidget(MW_INFO);
		this->DrawWidgets();

		GfxFillRect(187, 16, 200, 33, 0);

		for (i = 0; i != 8; i++) {
			int colour = 0xD0;
			if (i > 4) {
				colour = 0xBF;
				if (i > 6) {
					colour = 0xB8;
				}
			}
			GfxFillRect(187, NUM_SONGS_PLAYLIST - i * 2, 200, NUM_SONGS_PLAYLIST - i * 2, colour);
		}

		GfxFillRect(60, 46, 239, 52, 0);

		if (_song_is_active == 0 || _music_wnd_cursong == 0) {
			str = STR_01E3;
		} else {
			SetDParam(0, _music_wnd_cursong);
			str = (_music_wnd_cursong < 10) ? STR_01E4_0 : STR_01E5;
		}
		DrawString(62, 46, str, TC_FROMSTRING);

		str = STR_01E6;
		if (_song_is_active != 0 && _music_wnd_cursong != 0) {
			str = STR_01E7;
			SetDParam(0, SPECSTR_SONGNAME);
			SetDParam(1, _music_wnd_cursong);
		}
		DrawStringCentered(155, 46, str, TC_FROMSTRING);


		DrawString(60, 38, STR_01E8_TRACK_XTITLE, TC_FROMSTRING);

		for (i = 0; i != 6; i++) {
			DrawStringCentered(25 + i * 50, 59, STR_01D5_ALL + i, msf.playlist == i ? TC_WHITE : TC_BLACK);
		}

		DrawStringCentered( 31, 43, STR_01E9_SHUFFLE, (msf.shuffle ? TC_WHITE : TC_BLACK));
		DrawStringCentered(269, 43, STR_01EA_PROGRAM, TC_FROMSTRING);
		DrawStringCentered(141, 15, STR_01DB_MUSIC_VOLUME, TC_FROMSTRING);
		DrawStringCentered(141, 29, STR_01DD_MIN_MAX, TC_FROMSTRING);
		DrawStringCentered(247, 15, STR_01DC_EFFECTS_VOLUME, TC_FROMSTRING);
		DrawStringCentered(247, 29, STR_01DD_MIN_MAX, TC_FROMSTRING);

		DrawFrameRect(108, 23, 174, 26, COLOUR_GREY, FR_LOWERED);
		DrawFrameRect(214, 23, 280, 26, COLOUR_GREY, FR_LOWERED);

		DrawFrameRect(
			108 + msf.music_vol / 2, 22, 111 + msf.music_vol / 2, 28, COLOUR_GREY, FR_NONE
		);

		DrawFrameRect(
			214 + msf.effect_vol / 2, 22, 217 + msf.effect_vol / 2, 28, COLOUR_GREY, FR_NONE
		);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case MW_PREV: // skip to prev
				if (!_song_is_active) return;
				SkipToPrevSong();
				break;

			case MW_NEXT: // skip to next
				if (!_song_is_active) return;
				SkipToNextSong();
				break;

			case MW_STOP: // stop playing
				msf.playing = false;
				break;

			case MW_PLAY: // start playing
				msf.playing = true;
				break;

			case MW_SLIDERS: { // volume sliders
				byte *vol, new_vol;
				int x = pt.x - 88;

				if (x < 0) return;

				vol = &msf.music_vol;
				if (x >= 106) {
					vol = &msf.effect_vol;
					x -= 106;
				}

				new_vol = min(max(x - 21, 0) * 2, 127);
				if (new_vol != *vol) {
					*vol = new_vol;
					if (vol == &msf.music_vol) MusicVolumeChanged(new_vol);
					this->SetDirty();
				}

				_left_button_clicked = false;
			} break;

			case MW_SHUFFLE: // toggle shuffle
				msf.shuffle ^= 1;
				StopMusic();
				SelectSongToPlay();
				break;

			case MW_PROGRAMME: // show track selection
				ShowMusicTrackSelection();
				break;

			case MW_ALL: case MW_OLD: case MW_NEW:
			case MW_EZY: case MW_CUSTOM1: case MW_CUSTOM2: // playlist
				msf.playlist = widget - MW_ALL;
				this->SetDirty();
				InvalidateWindow(WC_MUSIC_TRACK_SELECTION, 0);
				StopMusic();
				SelectSongToPlay();
				break;
		}
	}

#if 0
	virtual void OnTick()
	{
		this->InvalidateWidget(MW_GAUGE);
	}
#endif
};

static const Widget _music_window_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_00C5,              STR_018B_CLOSE_WINDOW},                    // MW_CLOSE
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   299,     0,    13, STR_01D2_JAZZ_JUKEBOX, STR_018C_WINDOW_TITLE_DRAG_THIS},          // MW_CAPTION
{ WWT_PUSHIMGBTN,   RESIZE_NONE,  COLOUR_GREY,     0,    21,    14,    35, SPR_IMG_SKIP_TO_PREV,  STR_01DE_SKIP_TO_PREVIOUS_TRACK},          // MW_PREV
{ WWT_PUSHIMGBTN,   RESIZE_NONE,  COLOUR_GREY,    22,    43,    14,    35, SPR_IMG_SKIP_TO_NEXT,  STR_01DF_SKIP_TO_NEXT_TRACK_IN_SELECTION}, // MW_NEXT
{ WWT_PUSHIMGBTN,   RESIZE_NONE,  COLOUR_GREY,    44,    65,    14,    35, SPR_IMG_STOP_MUSIC,    STR_01E0_STOP_PLAYING_MUSIC},              // MW_STOP
{ WWT_PUSHIMGBTN,   RESIZE_NONE,  COLOUR_GREY,    66,    87,    14,    35, SPR_IMG_PLAY_MUSIC,    STR_01E1_START_PLAYING_MUSIC},             // MW_PLAY
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,    88,   299,    14,    35, 0x0,                   STR_01E2_DRAG_SLIDERS_TO_SET_MUSIC},       // MW_SLIDERS
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,   186,   201,    15,    34, 0x0,                   STR_NULL},                                 // MW_GAUGE
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   299,    36,    57, 0x0,                   STR_NULL},                                 // MW_BACKGROUND
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,    59,   240,    45,    53, 0x0,                   STR_NULL},                                 // MW_INFO
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,     6,    55,    42,    49, 0x0,                   STR_01FB_TOGGLE_PROGRAM_SHUFFLE},          // MW_SHUFFLE
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   244,   293,    42,    49, 0x0,                   STR_01FC_SHOW_MUSIC_TRACK_SELECTION},      // MW_PROGRAMME
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,     0,    49,    58,    65, 0x0,                   STR_01F3_SELECT_ALL_TRACKS_PROGRAM},       // MW_ALL
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,    50,    99,    58,    65, 0x0,                   STR_01F4_SELECT_OLD_STYLE_MUSIC},          // MW_OLD
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   100,   149,    58,    65, 0x0,                   STR_01F5_SELECT_NEW_STYLE_MUSIC},          // MW_NEW
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   150,   199,    58,    65, 0x0,                   STR_0330_SELECT_EZY_STREET_STYLE},         // MW_EZY
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   200,   249,    58,    65, 0x0,                   STR_01F6_SELECT_CUSTOM_1_USER_DEFINED},    // MW_CUSTOM1
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   250,   299,    58,    65, 0x0,                   STR_01F7_SELECT_CUSTOM_2_USER_DEFINED},    // MW_CUSTOM2
{   WIDGETS_END},
};

static const WindowDesc _music_window_desc(
	0, 22, 300, 66, 300, 66,
	WC_MUSIC_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_music_window_widgets
);

void ShowMusicWindow()
{
	AllocateWindowDescFront<MusicWindow>(&_music_window_desc, 0);
}
