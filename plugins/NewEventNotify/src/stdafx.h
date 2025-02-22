/*
	Name: NewEventNotify - Plugin for Miranda IM
	File: neweventnotify.h - Main Header File
	Version: 0.2.2.2
	Description: Notifies you about some events
	Author: icebreaker, <icebreaker@newmail.net>
	Date: 18.07.02 13:59 / Update: 16.09.02 17:45
	Copyright: (C) 2002 Starzinger Michael

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#pragma once

//---------------------------
//---Includes
#define _CRT_NONSTDC_NO_DEPRECATE

#include <stddef.h>
#include <windows.h>
#include <time.h>

#include <vector>

#include <newpluginapi.h>
#include <m_database.h>
#include <m_langpack.h>
#include <m_popup.h>
#include <m_skin.h>
#include <m_srmm_int.h>
#include <m_clistint.h>
#include <m_protosvc.h>
#include <m_message.h>
#include <m_options.h>

#include <m_metacontacts.h>

#include "resource.h"
#include "version.h"

//---------------------------
//---Definitions

#define MODULENAME "NewEventNotify"

#define DEFAULT_COLBACK RGB(255,255,128)
#define DEFAULT_COLTEXT RGB(0,0,0)
#define DEFAULT_DELAY   0

#define EVENTTYPE_ERRMSG 25366

#define MASK_MESSAGE    0x0001
#define MASK_ERROR      0x0002
#define MASK_FILE       0x0004
#define MASK_OTHER      0x0008

#define MASK_DISMISS    0x0001
#define MASK_OPEN       0x0002
#define MASK_REMOVE     0x0004

#define SETTING_LIFETIME_MIN		1
#define SETTING_LIFETIME_MAX		60
#define SETTING_LIFETIME_DEFAULT	20

#define MAX_DATASIZE	24

#ifndef WM_MOUSEWHEEL
	#define WM_MOUSEWHEEL 0x020A
#endif
#define TIMER_TO_ACTION 50685

//Entries in the database, don't translate
#define OPT_DISABLE "Disabled"
#define OPT_MUCDISABLE "MUCDisabled"
#define OPT_PREVIEW "Preview"
#define OPT_MENUITEM "MenuItem"
#define OPT_LIMITPREVIEW "LimitPreview"

#define OPT_COLDEFAULT_MESSAGE "DefaultColorMsg"
#define OPT_COLBACK_MESSAGE "ColorBackMsg"
#define OPT_COLTEXT_MESSAGE "ColorTextMsg"
#define OPT_DELAY_MESSAGE "DelayMessage"

#define OPT_COLDEFAULT_FILE "DefaultColorFile"
#define OPT_COLBACK_FILE "ColorBackFile"
#define OPT_COLTEXT_FILE "ColorTextFile"
#define OPT_DELAY_FILE "DelayFile"

#define OPT_COLDEFAULT_ERR "DefaultColorErr"
#define OPT_COLBACK_ERR "ColorBackErr"
#define OPT_COLTEXT_ERR "ColorTextErr"
#define OPT_DELAY_ERR "DelayErr"

#define OPT_COLDEFAULT_OTHERS "DefaultColorOthers"
#define OPT_COLBACK_OTHERS "ColorBackOthers"
#define OPT_COLTEXT_OTHERS "ColorTextOthers"
#define OPT_DELAY_OTHERS "DelayOthers"

#define OPT_MASKNOTIFY "Notify"
#define OPT_MASKACTL "ActionLeft"
#define OPT_MASKACTR "ActionRight"
#define OPT_MASKACTTE "ActionTimeExpires"
#define OPT_MSGWINDOWCHECK "WindowCheck"
#define OPT_MSGREPLYWINDOW "ReplyWindow"
#define OPT_MERGEPOPUP "MergePopup"
#define OPT_SHOW_DATE "ShowDate"
#define OPT_SHOW_TIME "ShowTime"
#define OPT_SHOW_HEADERS "ShowHeaders"
#define OPT_NUMBER_MSG "NumberMsg"
#define OPT_SHOW_ON "ShowOldOrNew"
#define OPT_HIDESEND "HideSend"
#define OPT_NORSS "NoRSSAnnounces"
#define OPT_READCHECK "ReadCheck"
//---------------------------
//---Translateable Strings

#define POPUP_COMMENT_MESSAGE LPGEN("Message")
#define POPUP_COMMENT_FILE LPGEN("File")
#define POPUP_COMMENT_CONTACTS LPGEN("Contacts")
#define POPUP_COMMENT_ADDED LPGEN("You were added!")
#define POPUP_COMMENT_AUTH LPGEN("Requests your authorization")

#define POPUP_COMMENT_OTHER LPGEN("Unknown Event")

#define MENUITEM_NAME LPGEN("Notify of new events")

//---------------------------
//---Structures

struct CMPlugin : public PLUGIN<CMPlugin>
{
	CMPlugin();

	int Load() override;

	void OptionsRead(void);
	void OptionsWrite(void);

	CMOption<bool> bPopups, bMucPopups;

	bool bPreview;
	bool bDisableNonMessage;
	bool bMsgWindowCheck;
	bool bMsgReplyWindow;
	bool bMergePopup;
	bool bShowDate;
	bool bShowTime;
	bool bShowHeaders;
	bool bShowON;
	bool bHideSend;
	bool bNoRSS;
	bool iNoSounds;
	bool bReadCheck;
	bool bWindowCheck;

	struct
	{
		COLORREF backColor, textColor;
		int iDelay;
		bool bDefault;

	} msg, file, err, other;

	uint32_t maskNotify;
	uint32_t maskActL;
	uint32_t maskActR;
	uint32_t maskActTE;

	int iDelayDefault;
	int iLimitPreview;

	uint8_t iNumberMsg;
};

struct PLUGIN_DATA : public MZeroedObject
{
	MCONTACT hContact;
	UINT eventType;
	HWND hWnd;
	std::vector<MEVENT> events;
	long iSeconds;
};

//---------------------------
//---External Procedure Definitions

int PopupShow(MCONTACT hContact, MEVENT hEvent, UINT eventType);
int PopupUpdate(PLUGIN_DATA &pdata, MEVENT hEvent);
int PopupAct(HWND hWnd, UINT mask, PLUGIN_DATA *pdata);
int OptionsAdd(WPARAM addInfo, LPARAM);
int CheckMsgWnd(MCONTACT hContact);

PLUGIN_DATA* PU_GetByContact(MCONTACT hContact, UINT eventType);
