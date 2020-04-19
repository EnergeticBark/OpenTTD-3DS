/* $Id$ */

/** @file sdl.cpp Implementation of SDL support. */

#include "stdafx.h"

#ifdef WITH_SDL

#include "openttd.h"
#include "sdl.h"
#include <SDL.h>

#ifdef UNIX
#include <signal.h>

#ifdef __MORPHOS__
	/* The system supplied definition of SIG_DFL is wrong on MorphOS */
	#undef SIG_DFL
	#define SIG_DFL (void (*)(int))0
#endif
#endif

static int _sdl_usage;

#ifdef DYNAMICALLY_LOADED_SDL

#include "win32.h"

#define M(x) x "\0"
static const char sdl_files[] =
	M("sdl.dll")
	M("SDL_Init")
	M("SDL_InitSubSystem")
	M("SDL_GetError")
	M("SDL_QuitSubSystem")
	M("SDL_UpdateRect")
	M("SDL_UpdateRects")
	M("SDL_SetColors")
	M("SDL_WM_SetCaption")
	M("SDL_ShowCursor")
	M("SDL_FreeSurface")
	M("SDL_PollEvent")
	M("SDL_WarpMouse")
	M("SDL_GetTicks")
	M("SDL_OpenAudio")
	M("SDL_PauseAudio")
	M("SDL_CloseAudio")
	M("SDL_LockSurface")
	M("SDL_UnlockSurface")
	M("SDL_GetModState")
	M("SDL_Delay")
	M("SDL_Quit")
	M("SDL_SetVideoMode")
	M("SDL_EnableKeyRepeat")
	M("SDL_EnableUNICODE")
	M("SDL_VideoDriverName")
	M("SDL_ListModes")
	M("SDL_GetKeyState")
	M("SDL_LoadBMP_RW")
	M("SDL_RWFromFile")
	M("SDL_SetColorKey")
	M("SDL_WM_SetIcon")
	M("SDL_MapRGB")
	M("")
;
#undef M

SDLProcs sdl_proc;

static const char *LoadSdlDLL()
{
	if (sdl_proc.SDL_Init != NULL)
		return NULL;
	if (!LoadLibraryList((Function *)(void *)&sdl_proc, sdl_files))
		return "Unable to load sdl.dll";
	return NULL;
}

#endif /* DYNAMICALLY_LOADED_SDL */


#ifdef UNIX
static void SdlAbort(int sig)
{
	/* Own hand-made parachute for the cases of failed assertions. */
	SDL_CALL SDL_Quit();

	switch (sig) {
		case SIGSEGV:
		case SIGFPE:
			signal(sig, SIG_DFL);
			raise(sig);
			break;

		default:
			break;
	}
}
#endif


const char *SdlOpen(uint32 x)
{
#ifdef DYNAMICALLY_LOADED_SDL
	{
		const char *s = LoadSdlDLL();
		if (s != NULL) return s;
	}
#endif
	if (_sdl_usage++ == 0) {
		if (SDL_CALL SDL_Init(x) == -1)
			return SDL_CALL SDL_GetError();
	} else if (x != 0) {
		if (SDL_CALL SDL_InitSubSystem(x) == -1)
			return SDL_CALL SDL_GetError();
	}

#ifdef UNIX
	signal(SIGABRT, SdlAbort);
	signal(SIGSEGV, SdlAbort);
	signal(SIGFPE, SdlAbort);
#endif

	return NULL;
}

void SdlClose(uint32 x)
{
	if (x != 0)
		SDL_CALL SDL_QuitSubSystem(x);
	if (--_sdl_usage == 0) {
		SDL_CALL SDL_Quit();
		#ifdef UNIX
		signal(SIGABRT, SIG_DFL);
		signal(SIGSEGV, SIG_DFL);
		signal(SIGFPE, SIG_DFL);
		#endif
	}
}

#endif
