/*
	Copyright (C) 2018-2020 OrangeFox Recovery Project
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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
#include "../data.hpp"
#include "pages.hpp"

#include <string>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

GUIGesture::GUIGesture(xml_node<>* node)
	: GUIObject(node)
{
	mAction = NULL;
	mRendered = false;
	hasFill = false;
	mMode = 0;
	mSensetivity = 300;

	if (!node)  return;

	mAction = new GUIAction(node);

	mFillColor = LoadAttrColor(FindNode(node, "fill"), "color", &hasFill);
	if (!hasFill) {
		LOGERR("No fill specified for gesture.\n");
	}

	xml_node<>* child = FindNode(node, "settings");
	mMode = LoadAttrInt(child, "side", mMode);
	mSensetivity = LoadAttrIntScaleX(child, "sense", mSensetivity);

	int x = 0, y = 0, w = 0, h = 0;
	if (hasFill)
		LoadPlacement(FindNode(node, "placement"), &x, &y, &w, &h, &TextPlacement);

	SetRenderPos(x, y, w, h);
}

GUIGesture::~GUIGesture()
{
	delete mAction;
}

int GUIGesture::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	if (hasFill) {
		gr_color(mFillColor.red, mFillColor.green, mFillColor.blue, mFillColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
	}
	mRendered = true;
	return 0;
}

int GUIGesture::Update(void)
{
	if (!isConditionTrue())	return (mRendered ? 2 : 0);
	if (!mRendered)			return 2;
	return 0;
}

int GUIGesture::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;
	}
	if (mAction)		mAction->SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

int GUIGesture::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if (!isConditionTrue())	 return -1;

	int center = DataManager::GetIntValue("center_y");

	if (state == TOUCH_START)
		vibrateLock = false; //Execute gesture action only one time

	if ((
		(mMode == 0 && y < mRenderY - mSensetivity && y > center) ||
		(mMode == 1 && x < mRenderX - mSensetivity) ||
		(mMode == 2 && x > mRenderX + mSensetivity + mRenderW) ||
		(mMode == 3 && y > mRenderY + mSensetivity + mRenderH && y < center)
		) && !vibrateLock)  {
		#ifndef TW_NO_HAPTICS
			DataManager::Vibrate("tw_button_vibrate");
		#endif
		vibrateLock = true;
		return (mAction ? mAction->NotifyTouch(TOUCH_RELEASE, x, y) : 1);
	}
	return 0;
}
