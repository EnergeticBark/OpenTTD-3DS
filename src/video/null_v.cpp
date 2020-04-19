/* $Id$ */

/** @file null_v.cpp The videio driver that doesn't blit. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../blitter/factory.hpp"
#include "null_v.h"

static FVideoDriver_Null iFVideoDriver_Null;

const char *VideoDriver_Null::Start(const char * const *parm)
{
	this->ticks = GetDriverParamInt(parm, "ticks", 1000);
	_screen.width  = _screen.pitch = _cur_resolution.width;
	_screen.height = _cur_resolution.height;
	ScreenSizeChanged();

	/* Do not render, nor blit */
	DEBUG(misc, 1, "Forcing blitter 'null'...");
	BlitterFactoryBase::SelectBlitter("null");
	return NULL;
}

void VideoDriver_Null::Stop() { }

void VideoDriver_Null::MakeDirty(int left, int top, int width, int height) {}

void VideoDriver_Null::MainLoop()
{
	uint i;

	for (i = 0; i < this->ticks; i++) {
		GameLoop();
		_screen.dst_ptr = NULL;
		UpdateWindows();
	}
}

bool VideoDriver_Null::ChangeResolution(int w, int h) { return false; }

bool VideoDriver_Null::ToggleFullscreen(bool fs) { return false; }
