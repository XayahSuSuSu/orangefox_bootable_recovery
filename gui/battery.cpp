/*
		Copyright (C) 2018-2020 OrangeFox Recovery Project
        Copyright 2012 to 2016 bigbiff/Dees_Troy TeamWin
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

// battert.cpp - GUIBattery object by fordownloads@orangefox team

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

GUIBattery::GUIBattery(xml_node<>* node)
	: GUIObject(node)
{
	mStateMode = false;
	xml_node <> *child;
	mImg100 = mImg75 = mImg50 = mImg25 = mImg15 = mImgc15 = mImg5 = mImg = mLowImg = NULL;

	if (!node)
		return;

	child = FindNode(node, "dynamic"); // Classic vertical battery; changes smoothly
	if (child) {
		mImg = LoadAttrImage(child, "img"); //default empty image
		mLowImg = LoadAttrImage(child, "imgLow"); // empty image for <15%
		mColor = LoadAttrColor(child, "color", COLOR(0,0,0,255)); // color for dynamic part
		mColorLow = LoadAttrColor(child, "colorLow", COLOR(128,0,0,255)); // color for dynamic part when <15%
		mDX = LoadAttrInt(child, "dx", 0); //dynamic part placement
		mDY = LoadAttrInt(child, "dy", 0);
		mDW = LoadAttrInt(child, "dw", 32);
		mDH = LoadAttrInt(child, "dh", 18);
	} else {
		child = FindNode(node, "states"); // Battery based on images
		if (child) {
			mImg100 = LoadAttrImage(child, "100");
			mImg75  = LoadAttrImage(child, "75");
			mImg50  = LoadAttrImage(child, "50");
			mImg25  = LoadAttrImage(child, "25");
			mImg15  = LoadAttrImage(child, "15");
			mImgc15  = LoadAttrImage(child, "c15");
			mImg5   = LoadAttrImage(child, "5");
			mStateMode = true;
		} else {
			LOGERR("Battery object not loaded!\n");
			return;
		}
	}
	child = FindNode(node, "charging"); // charging icon
	if (child) {
		mCharge = LoadAttrImage(child, "img");
		mCX = LoadAttrInt(child, "x", 0); //placement
		mCY = LoadAttrInt(child, "y", 0);
		if (mCharge && mCharge->GetResource())
		{
			mCW = mCharge->GetWidth();
			mCH = mCharge->GetHeight();
		}
	}

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH, &mPlacement);

	// Adjust for placement
	if (mPlacement != TOP_LEFT && mPlacement != BOTTOM_LEFT)
	{
		if (mPlacement == CENTER)
			mRenderX -= (mRenderW / 2);
		else
			mRenderX -= mRenderW;
	}
	if (mPlacement != TOP_LEFT && mPlacement != TOP_RIGHT)
	{
		if (mPlacement == CENTER)
			mRenderY -= (mRenderH / 2);
		else
			mRenderY -= mRenderH;
	}
	SetPlacement(TOP_LEFT);
	
	mDX += mRenderX;
	mDY += mRenderY;
	mCX += mRenderX;
	mCY += mRenderY;
}

int GUIBattery::Render(void)
{
	if (!isConditionTrue())
		return 0;

	int mBatteryPercent = DataManager::GetIntValue("tw_battery_t");
	int mBatteryCharge = DataManager::GetIntValue("charging_now");
	ImageResource* finalImage;

	if (mStateMode) {

		     if (mBatteryPercent > 75) finalImage = mImg100;
		else if (mBatteryPercent > 50) finalImage = mImg75 ;
		else if (mBatteryPercent > 25) finalImage = mImg50 ;
		else if (mBatteryPercent > 15) finalImage = mImg25 ;
		else if (mBatteryPercent <= 15 && mBatteryCharge == 1) finalImage = mImgc15;
		else if (mBatteryPercent > 5)  finalImage = mImg15 ;
		else 				           finalImage = mImg5  ;

	} else {

		if (mBatteryPercent > 15 || mBatteryCharge == 1) {
			finalImage = mImg;
			gr_color(mColor.red, mColor.green, mColor.blue, mColor.alpha);
		} else {
			finalImage = mLowImg;
			gr_color(mColorLow.red, mColorLow.green, mColorLow.blue, mColorLow.alpha);
		}

		int height = mDH * mBatteryPercent / 100;
		gr_fill(mDX, mDY+mDH-height, mDW, height);

	}

	if (!finalImage || !finalImage->GetResource())
		return -1;
	gr_blit(finalImage->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);

	if (!mCharge || !mCharge->GetResource())
		return 0;
	if (DataManager::GetIntValue("charging_now") == 1)
		gr_blit(mCharge->GetResource(), 0, 0, mCW, mCH, mCX, mCY);

	return 0;
}
