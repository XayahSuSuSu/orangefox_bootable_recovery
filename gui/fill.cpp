/*
	Copyright 2017 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

// fill.cpp - GUIFill object

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

GUIFill::GUIFill(xml_node<>* node) : GUIObject(node)
{
	bool has_color = false;
	mCircle = NULL;
	mColor = LoadAttrColor(node, "color", &has_color);
	if (!has_color) {
		LOGERR("No color specified for fill\n");
		return;
	}

	mIsRounded = LoadAttrString(node, "rounded", "0");

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);

	return;
}

GUIFill::~GUIFill()
{
	if (mCircle)
		gr_free_surface(mCircle);
}

int GUIFill::Render(void)
{
	if (!isConditionTrue())
		return 0;

	if(mIsRounded == "1") {
		int w, h, half;
		half = mRenderH / 2;
		mCircle = gr_render_circle(half, mColor.red, mColor.green, mColor.blue, mColor.alpha);
		w = gr_get_width(mCircle);
		h = gr_get_height(mCircle);
		mRenderH = h;
		gr_blit(mCircle, 0, 0, w, h, mRenderX - half, mRenderY);
		gr_blit(mCircle, 0, 0, w, h, mRenderX + mRenderW + half - mRenderH, mRenderY);
	}

	gr_color(mColor.red, mColor.green, mColor.blue, mColor.alpha);
	gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);

	return 0;
}

