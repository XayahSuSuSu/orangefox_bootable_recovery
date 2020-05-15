/*
	Copyright 2013 bigbiff/Dees_Troy TeamWin
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

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../partitions.hpp"

GUIPartitionList::GUIPartitionList(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mIconSelected = mIconUnselected = NULL;
	mUpdate = 0;
	countTotal = updateList = false;

	child = FindNode(node, "icon");
	if (child)
	{
		mIconSelected = LoadAttrImage(child, "selected");
		mIconUnselected = LoadAttrImage(child, "unselected");
	}

	// Handle the result variable
	child = FindNode(node, "data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("selectedlist");
		if (attr)
			selectedList = attr->value();
	}

	int iconWidth = 0, iconHeight = 0;

	child = FindNode(node, "iconsize");
	if (child) {
		iconWidth = LoadAttrIntScaleX(child, "w", iconWidth);
		iconHeight = LoadAttrIntScaleY(child, "h", iconHeight);
	} else {
		if (mIconSelected && mIconSelected->GetResource() && mIconUnselected && mIconUnselected->GetResource()) {
			iconWidth = std::max(mIconSelected->GetWidth(), mIconUnselected->GetWidth());
			iconHeight = std::max(mIconSelected->GetHeight(), mIconUnselected->GetHeight());
		} else if (mIconSelected && mIconSelected->GetResource()) {
			iconWidth = mIconSelected->GetWidth();
			iconHeight = mIconSelected->GetHeight();
		} else if (mIconUnselected && mIconUnselected->GetResource()) {
			iconWidth = mIconUnselected->GetWidth();
			iconHeight = mIconUnselected->GetHeight();
		}
	}
	
	SetMaxIconSize(iconWidth, iconHeight);

	child = FindNode(node, "listtype");
	if (child && (attr = child->first_attribute("name"))) {
		ListType = attr->value();
		if (ListType == "backup_total") {
			ListType = "backup";
			countTotal = true;
		}
		updateList = true;
	} else {
		mList.clear();
		LOGERR("No partition listtype specified for partitionlist GUI element\n");
		return;
	}
}

GUIPartitionList::~GUIPartitionList()
{
}

int GUIPartitionList::Update(void)
{
	if (!isConditionTrue())
		return 0;

	// Check for changes in mount points if the list type is mount and update the list and render if needed
	if (ListType == "mount") {
		int listSize = mList.size();
		for (int i = 0; i < listSize; i++) {
			if (PartitionManager.Is_Mounted_By_Path(mList.at(i).Mount_Point) && !mList.at(i).selected) {
				mList.at(i).selected = 1;
				mUpdate = 1;
			} else if (!PartitionManager.Is_Mounted_By_Path(mList.at(i).Mount_Point) && mList.at(i).selected) {
				mList.at(i).selected = 0;
				mUpdate = 1;
			}
		}
	}

	GUIScrollList::Update();

	if (updateList) {
		// Completely update the list if needed -- Used primarily for
		// restore as the list for restore will change depending on what
		// partitions were backed up
		mList.clear();
		PartitionManager.Get_Partition_List(ListType, &mList);
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
		if (ListType == "backup" || ListType == "flashimg")
			MatchList();
	}

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}

	return 0;
}

int GUIPartitionList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if (!isConditionTrue())
		return 0;

	if (varName == mVariable && !mUpdate)
	{
		if (ListType == "storage" || ListType == "part_option") {
			currentValue = value;
			SetPosition();
		} else if (ListType == "backup") {
			MatchList();
		} else if (ListType == "restore") {
			updateList = true;
			SetVisibleListLocation(0);
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

void GUIPartitionList::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		if (ListType == "storage" || ListType == "part_option" || ListType == "flashimg") {
			DataManager::GetValue(mVariable, currentValue);
			SetPosition();
		}
		updateList = true;
		mUpdate = 1;
	}
}

void GUIPartitionList::MatchList(void) {
	int i, listSize = mList.size();
	string variablelist, searchvalue;
	unsigned long long totalSize = 0, imgSize = 0, fileSize = 0;
	size_t pos;

	DataManager::GetValue(mVariable, variablelist);

	for (i = 0; i < listSize; i++) {
		searchvalue = mList.at(i).Mount_Point + ";";
		pos = variablelist.find(searchvalue);
		if (pos != string::npos) {
			mList.at(i).selected = 1;
			if (countTotal) {
				if (mList.at(i).isFiles)
					fileSize += mList.at(i).PartitionSize;
				else
					imgSize += mList.at(i).PartitionSize;
			}
		} else {
			mList.at(i).selected = 0;
		}
	}

	if (countTotal) {
		char formatSize[255];
		totalSize = imgSize + fileSize;
		sprintf(formatSize, totalSize % 1048576 == 0 ? "%.0lf" : "%.2lf", (double)totalSize / 1048576);
		DataManager::SetValue("fox_total_backup", formatSize);
		CalculateTime(fileSize, imgSize);
	}
}

//[f/d]
void GUIPartitionList::CalculateTime(unsigned long long fileSize, unsigned long long imgSize){
	unsigned long long avImg = 20, avFile = 20;

	//Because many devices are work with usb 2.0 ports and have
	//old SD cards so there is two groups of values: for high speed
	//internal memory and for slow external devices
	if (DataManager::GetCurrentStoragePath() == "/data/media/0") {
		DataManager::GetValue("of_average_img", avImg);
		DataManager::GetValue("of_average_file", avFile);
	} else {
		DataManager::GetValue("of_average_ext_img", avImg);
		DataManager::GetValue("of_average_ext_file", avFile);
	}

	//Reset to 20MB/s values when something goes wrong
	if (avImg < 1)
		avImg = 20;
	if (avFile < 1)
		avFile = 20;

	DataManager::SetValue("fox_ai_deep_learning_time",
		((fileSize / 1048576 / avFile) + (imgSize / 1048576 / avImg)) / 60);
} 

void GUIPartitionList::SetPosition() {
	int listSize = mList.size();

	SetVisibleListLocation(0);
	for (int i = 0; i < listSize; i++) {
		if (mList.at(i).Mount_Point == currentValue) {
			mList.at(i).selected = 1;
			SetVisibleListLocation(i);
		} else {
			mList.at(i).selected = 0;
		}
	}
}

size_t GUIPartitionList::GetItemCount()
{
	return mList.size();
}

void GUIPartitionList::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// note: the "selected" parameter above is for the currently touched item
	// don't confuse it with the more persistent "selected" flag per list item used below
	ImageResource* icon = mList.at(itemindex).selected ? mIconSelected : mIconUnselected;
	const std::string& text = mList.at(itemindex).Display_Name;

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIPartitionList::NotifySelect(size_t item_selected)
{
	if (item_selected < mList.size()) {
		int listSize = mList.size();
		if (ListType == "mount") {
			if (!mList.at(item_selected).selected) {
				if (PartitionManager.Mount_By_Path(mList.at(item_selected).Mount_Point, true)) {
					mList.at(item_selected).selected = 1;
					PartitionManager.Add_MTP_Storage(mList.at(item_selected).Mount_Point);
					mUpdate = 1;
				}
			} else {
				if (PartitionManager.UnMount_By_Path(mList.at(item_selected).Mount_Point, true)) {
					mList.at(item_selected).selected = 0;
					mUpdate = 1;
				}
			}
		} else if (!mVariable.empty()) {
			if (ListType == "storage") {
				int i;
				std::string str = mList.at(item_selected).Mount_Point;
				bool update_size = false;
				TWPartition* Part = PartitionManager.Find_Partition_By_Path(str);
				if (Part == NULL) {
					LOGERR("Unable to locate partition for '%s'\n", str.c_str());
					return;
				}
				if (!Part->Is_Mounted() && Part->Removable)
					update_size = true;
				if (!Part->Mount(true)) {
					// Do Nothing
				} else if (update_size && !Part->Update_Size(true)) {
					// Do Nothing
				} else {
					for (i=0; i<listSize; i++)
						mList.at(i).selected = 0;

					if (update_size) {
						char free_space[255];
						sprintf(free_space, "%llu", Part->Free / 1024 / 1024);
						mList.at(item_selected).Display_Name = Part->Storage_Name + " (";
						mList.at(item_selected).Display_Name += free_space;
						mList.at(item_selected).Display_Name += gui_parse_text("{@mbyte}");
						mList.at(item_selected).Display_Name += ")";
					}
					mList.at(item_selected).selected = 1;
					mUpdate = 1;

					DataManager::SetValue(mVariable, str);
				}
			} else {
				if (ListType == "flashimg" || ListType == "part_option" ) { // only one item can be selected for flashing images
					for (int i=0; i<listSize; i++)
						mList.at(i).selected = 0;
				}
				if (mList.at(item_selected).selected)
					mList.at(item_selected).selected = 0;
				else
					mList.at(item_selected).selected = 1;

				if (countTotal) { // [f/d] count size of backup after selecting partition
					unsigned long long totalSize = 0, imgSize = 0, fileSize = 0;
					char formatSize[255];
					for (int i=0; i<listSize; i++) {
						if(mList.at(i).selected == 1) {
							if (mList.at(i).isFiles)
								fileSize += mList.at(i).PartitionSize;
							else
								imgSize += mList.at(i).PartitionSize;
						}
					}
					totalSize = fileSize + imgSize;
					sprintf(formatSize, totalSize % 1048576 == 0 ? "%.0lf" : "%.2lf", (double)totalSize / 1048576);
					DataManager::SetValue("fox_total_backup", formatSize);
					CalculateTime(fileSize, imgSize);
				}

				int i;
				string variablelist;
				for (i=0; i<listSize; i++) {
					if (mList.at(i).selected) {
						if (ListType == "part_option") {
							variablelist += mList.at(i).Mount_Point; //[f/d] No ; in part_option
						} else {
							variablelist += mList.at(i).Mount_Point + ";";
						}
					}
				}

				mUpdate = 1;
				if (selectedList.empty())
					DataManager::SetValue(mVariable, variablelist);
				else
					DataManager::SetValue(selectedList, variablelist);
			}
		}
	}
}

