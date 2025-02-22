//This file is part of HTTPServer a Miranda IM plugin
//Copyright (C)2002 Kennet Nielsen
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "Glob.h"

#define MS_SHARE_NEW_FILE "HTTPServer/ShareNewFile"
#define MS_SHOW_STATISTICS_VIEW "HTTPServer/ShowStatisticsView"

#define WM_RELOAD_STATISTICS (WM_USER+10)

static HANDLE hShareNewFileService = nullptr;
static HANDLE hShowStatisticsViewService = nullptr;

static HANDLE hShareNewFileMenuItem = nullptr;
static HANDLE hShowStatisticsViewMenuItem = nullptr;

HWND hwndStatsticView = nullptr;
bool bLastAutoRefress = false;

bool bShowPopups = true;

#define szDefaultExternalSrvName "http://%ExternalIP%:%Port%%SrvPath%"

#define szDefaultUrlAddress "http://checkip.dyndns.org"
#define szDefaultPageKeyword "Current IP Address: "

string sUrlAddress = szDefaultUrlAddress;
string sPageKeyword = szDefaultPageKeyword;


/////////////////////////////////////////////////////////////////////
// Member Function : ReplaceAll
// Type            : Global
// Parameters      : sSrc       - string to replace in
//                   pszReplace - what to replace
//                   sNew       - the string to insert insted of pszReplace
// Returns         : void
// Description     : will replace all acurances of a string with another string
//                   used to replace %user%, and other user
// References      : -
// Remarks         : -
// Created         : 020422, 22 April 2002
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void ReplaceAll(string &sSrc, const char * pszReplace, const string &sNew)
{
	string::size_type nCur = 0;
	int nRepalceLen = (int)mir_strlen(pszReplace);
	while ((nCur = sSrc.find(pszReplace, nCur)) != sSrc.npos) {
		sSrc.replace(nCur, nRepalceLen, sNew);
		nCur += sNew.size();
	}
}

void ReplaceAll(string &sSrc, const char * pszReplace, const char * pszNew)
{
	string sNew = pszNew;
	ReplaceAll(sSrc, pszReplace, sNew);
}


/////////////////////////////////////////////////////////////////////
// Member Function : DBGetString
// Type            : Global
// Parameters      : hContact  - ?
//                   szModule  - ?
//                   szSetting - ?
//                   pszError  - ?
// Returns         : string
// Description     : Reads a string from the database
//                   Just like those in database.h
// References      : -
// Remarks         : -
// Created         : 020422, 22 April 2002
// Developer       : KN
/////////////////////////////////////////////////////////////////////

string DBGetString(MCONTACT hContact, const char *szModule, const char *szSetting, const char * pszError)
{
	string ret;
	DBVARIANT dbv = { 0 };
	if (!db_get_s(hContact, szModule, szSetting, &dbv)) {
		ret = dbv.pszVal;
		db_free(&dbv);
	}
	else ret = pszError;

	return ret;
}


/////////////////////////////////////////////////////////////////////
// Member Function : UpdateStatisticsView
// Type            : Global
// Parameters      : None
// Returns         : void
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030904, 04 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void UpdateStatisticsView()
{
	if (hwndStatsticView)
		PostMessage(hwndStatsticView, WM_RELOAD_STATISTICS, 0, 0);
}

/////////////////////////////////////////////////////////////////////
// Member Function : GetExternIP
// Type            : Global
// Parameters      : szURL     - The url of the page where IP is written
//                   szPattern - The text before the IP in page (can be "")
// Returns         : External IP or 0 if error
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 031113, 13 november 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

unsigned long GetExternIP(const char *szURL, const char *szPattern)
{
	HCURSOR hPrevCursor = ::SetCursor(::LoadCursor(nullptr, IDC_WAIT));

	NETLIBHTTPREQUEST nlhr;
	memset(&nlhr, 0, sizeof(nlhr));
	nlhr.cbSize = sizeof(nlhr);
	nlhr.requestType = REQUEST_GET;
	nlhr.flags = NLHRF_DUMPASTEXT;
	nlhr.szUrl = (char*)szURL;

	IN_ADDR externIP;
	externIP.s_addr = 0;

	NLHR_PTR nlreply(Netlib_HttpTransaction(hNetlibUser, &nlhr));
	if (nlreply) {
		if (nlreply->resultCode >= 200 && nlreply->resultCode < 300) {
			nlreply->pData[nlreply->dataLength] = 0;// make sure its null terminated
			char * pszIp = strstr(nlreply->pData, szPattern);
			if (pszIp == nullptr)
				pszIp = nlreply->pData;
			else
				pszIp += mir_strlen(szPattern);
			while ((*pszIp < '0' || *pszIp > '9') && *pszIp)
				pszIp++;

			char * pszEnd = pszIp;
			while ((*pszEnd >= '0' && *pszEnd <= '9') || *pszEnd == '.')
				pszEnd++;
			*pszEnd = NULL;

			if ((externIP.s_addr = inet_addr(pszIp)) == INADDR_NONE)
				externIP.s_addr = 0;
		}
	}
	::SetCursor(hPrevCursor);
	return ntohl(externIP.s_addr);
}

/////////////////////////////////////////////////////////////////////
// Member Function : sCreateLink
// Type            : Global
// Parameters      : pszSrvPath - ?
//                   pszIndexPath - ?
// Returns         : string
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030915, 15 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

string sCreateLink(const char * pszSrvPath)
{
	char szTemp[30];
	string sLink = DBGetString(0, MODULENAME, "ExternalSrvName", szDefaultExternalSrvName);
	mir_snprintf(szTemp, "%d.%d.%d.%d", SplitIpAddress(dwLocalIpAddress));
	ReplaceAll(sLink, "%LocalIP%", szTemp);

	if (sLink.find("%ExternalIP%") != sLink.npos) {
		static uint32_t dwExternalIpAddressGenerated = 0;

		// Get the IP again after 10 minutes
		if (!dwExternalIpAddress || GetTickCount() - dwExternalIpAddressGenerated > 10 * 60 * 1000) {
			dwExternalIpAddress = GetExternIP(sUrlAddress.c_str(), sPageKeyword.c_str());
			dwExternalIpAddressGenerated = GetTickCount();
		}

		mir_snprintf(szTemp, "%d.%d.%d.%d", SplitIpAddress(dwExternalIpAddress));
		ReplaceAll(sLink, "%ExternalIP%", szTemp);
	}

	mir_snprintf(szTemp, "%d", dwLocalPortUsed, szTemp);
	ReplaceAll(sLink, "%Port%", szTemp);

	string sSrvPath = pszSrvPath;
	ReplaceAll(sSrvPath, " ", "%20");
	ReplaceAll(sLink, "%SrvPath%", sSrvPath);
	return sLink;
}

/////////////////////////////////////////////////////////////////////
// Member Function : ShareNewFileDialogHook
// Type            : Global
// Parameters      : hdlg      - ?
//                   uiMsg     - ?
//                   wParam    - ?
//                   parameter - ?
// Returns         : UINT_PTR CALLBACK
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030829, 29 august 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

UINT_PTR CALLBACK ShareNewFileDialogHook(
	HWND hDlg,      // handle to child dialog box
	UINT uiMsg,     // message identifier
	WPARAM wParam,  // message parameter
	LPARAM lParam   // message parameter
)
{
	static const char* pszShareDirStr = Translate("Share Current Directory");

	static int nInit = 0;

	STFileShareInfo * pstShare = (STFileShareInfo *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	switch (uiMsg) {
	case WM_INITDIALOG:
		pstShare = (STFileShareInfo *)((OPENFILENAME *)lParam)->lCustData;
		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pstShare);

		SetDlgItemInt(hDlg, IDC_MAX_DOWNLOADS, pstShare->nMaxDownloads, TRUE);
		SendDlgItemMessage(hDlg, IDC_ALLOWED_IPADDRESS, IPM_SETADDRESS, 0, pstShare->dwAllowedIP);
		SendDlgItemMessage(hDlg, IDC_ALLOWED_IP_MASK, IPM_SETADDRESS, 0, pstShare->dwAllowedMask);

		if (*pstShare->pszSrvPath)
			nInit = 2;
		else
			nInit = 1;

		return false;

	case WM_NOTIFY:
	{
		OFNOTIFY * pNotify = (OFNOTIFY*)lParam;
		switch (pNotify->hdr.code) {
		case CDN_FOLDERCHANGE:
		case CDN_SELCHANGE:
		{
			static char szSelection[MAX_PATH] = "";
			HWND hWndFileDlg = GetParent(hDlg);

			*szSelection = '/';
			CommDlg_OpenSave_GetSpec(hWndFileDlg, (LPARAM)&szSelection[1], _MAX_PATH);

			HWND hFileName = GetDlgItem(hWndFileDlg, edt1);
			char pszFileName[MAX_PATH];
			GetWindowText(hFileName, pszFileName, _countof(pszFileName));

			if (mir_strcmp(pstShare->pszSrvPath, szSelection) &&
				mir_strcmp(pszFileName, pszShareDirStr)) {
				// a file was selected

				// only reenable windows / set default values when a folder was selected before
				if (pstShare->pszSrvPath[mir_strlen(pstShare->pszSrvPath) - 1] == '/') {
					pNotify->lpOFN->Flags |= OFN_FILEMUSTEXIST;
					EnableWindow(hFileName, TRUE);
					EnableWindow(GetDlgItem(hDlg, IDC_MAX_DOWNLOADS), TRUE);
					SetDlgItemInt(hDlg, IDC_MAX_DOWNLOADS, nDefaultDownloadLimit, true);
				}
			}
			else {
				// a directory was selected
				pNotify->lpOFN->Flags &= ~OFN_FILEMUSTEXIST;
				mir_strcpy(pNotify->lpOFN->lpstrFile, pszShareDirStr);
				CommDlg_OpenSave_SetControlText(hWndFileDlg, edt1, pszShareDirStr);
				EnableWindow(hFileName, FALSE);
				EnableWindow(GetDlgItem(hDlg, IDC_MAX_DOWNLOADS), FALSE);
				SetDlgItemInt(hDlg, IDC_MAX_DOWNLOADS, (UINT)-1, true);

				CommDlg_OpenSave_GetFolderPath(hWndFileDlg, szSelection, MAX_PATH);
				char* pszFolder = szSelection;
				char* pszTmp = szSelection;
				while (*pszTmp != '\0') {
					if (*pszTmp == '\\' && *(pszTmp + 1))
						pszFolder = pszTmp + 1;
					pszTmp++;
				}

				pszTmp = strchr(szSelection, ':');
				if (pszTmp != nullptr)
					*pszTmp = '\0';

				memmove(&szSelection[1], pszFolder, mir_strlen(pszFolder) + 1);
				szSelection[0] = '/';
				if (szSelection[mir_strlen(szSelection) - 1] != '/')
					mir_strcat(szSelection, "/");

				// only write to IDC_SHARE_NAME when a file / other folder was selected before
				if (!mir_strcmp(szSelection, pstShare->pszSrvPath))
					return false;
			}

			if (nInit != 0) {
				// when the dialog is created a CDN_FOLDERCHANGE and a CDN_SELCHANGE message
				// is posted. When the dialog is used for editting make sure the right server
				// path (not the auto generated) is written to IDC_SHARE_NAME
				if (nInit == 2)
					SetDlgItemText(hDlg, IDC_SHARE_NAME, pstShare->pszSrvPath);

				nInit--;
			}
			else {
				SetDlgItemText(hDlg, IDC_SHARE_NAME, szSelection);
			}

			mir_strcpy(pstShare->pszSrvPath, szSelection);

			return false;
		}

		case CDN_FILEOK:
		{
			GetDlgItemText(hDlg, IDC_SHARE_NAME, pstShare->pszSrvPath, _MAX_PATH);

			char* pszTmp = strstr(pstShare->pszRealPath, pszShareDirStr);
			if (pszTmp) {
				*pszTmp = '\0';
				if (pstShare->pszSrvPath[mir_strlen(pstShare->pszSrvPath) - 1] != '/')
					mir_strcat(pstShare->pszSrvPath, "/");
			}
			else {
				if (pstShare->pszSrvPath[mir_strlen(pstShare->pszSrvPath) - 1] == '/')
					pstShare->pszSrvPath[mir_strlen(pstShare->pszSrvPath) - 1] = '\0';
			}

			BOOL bTranslated = false;
			pstShare->nMaxDownloads = GetDlgItemInt(hDlg, IDC_MAX_DOWNLOADS, &bTranslated, true);
			if (pstShare->nMaxDownloads <= 0 && pstShare->nMaxDownloads != -1)
				bTranslated = false;

			SendDlgItemMessage(hDlg, IDC_ALLOWED_IPADDRESS, IPM_GETADDRESS, 0, (LPARAM)&(pstShare->dwAllowedIP));
			SendDlgItemMessage(hDlg, IDC_ALLOWED_IP_MASK, IPM_GETADDRESS, 0, (LPARAM)&(pstShare->dwAllowedMask));

			//if( ! (pstShare->dwAllowedIP & pstShare->dwAllowedMask)

			if (!bTranslated || (mir_strlen(pstShare->pszSrvPath) <= 0)) {
				SetWindowLongPtr(hDlg, DWLP_MSGRESULT, 1);
				return true;
			}
			return false;
		}
		}
		break;
	}

	case WM_DROPFILES:
	{
		HDROP hDrop = (HDROP)wParam;
		char szDropedFile[MAX_PATH];
		int nLen = DragQueryFile(hDrop, 0, szDropedFile, sizeof(szDropedFile));
		if (nLen > 0) {
			char * psz = strrchr(szDropedFile, '\\');
			if (psz) {
				char oldNext = psz[1];
				psz[1] = '\0';
				// Fill in the directory
				SendMessage(GetParent(hDlg), CDM_SETCONTROLTEXT, edt1, (LPARAM)szDropedFile);
				// click on the OK button. to move to dir
				SendMessage(GetParent(hDlg), WM_COMMAND, IDOK, 0);
				psz[1] = oldNext;
				// Fill in the file name this will cause a call to the CDN_SELCHANGE
				// and there by set the share name
				SendMessage(GetParent(hDlg), CDM_SETCONTROLTEXT, edt1, (LPARAM)&psz[1]);
			}
		}
		DragFinish(hDrop);
		return 0;
	}

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_TOGGLE_MASK:
		{
			uint32_t dwCur;
			SendDlgItemMessage(hDlg, IDC_ALLOWED_IP_MASK, IPM_GETADDRESS, 0, (LPARAM)&dwCur);
			SendDlgItemMessage(hDlg, IDC_ALLOWED_IP_MASK, IPM_SETADDRESS, 0, (LPARAM)dwCur == 0xFFFFFFFF ? 0 : 0xFFFFFFFF);
			return TRUE;
		}
		}
	}
	return false;
}

/////////////////////////////////////////////////////////////////////
// Member Function : bShowShareNewFileDlg
// Type            : Global
// Parameters      : pstNewShare - ?
// Returns         : Returns true if
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 031011, 11 oktober 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

bool bShowShareNewFileDlg(HWND hwndOwner, STFileShareInfo * pstNewShare)
{
	char temp[MAX_PATH];
	mir_snprintf(temp, "%s (*.*)%c*.*%c%c", Translate("All files"), 0, 0, 0);

	OPENFILENAME ofn = { 0 };
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.lpstrFilter = temp;
	ofn.lpstrFile = pstNewShare->pszRealPath;
	ofn.nMaxFile = pstNewShare->dwMaxRealPath;

	char szInitialDir[MAX_PATH];
	if (ofn.lpstrFile[mir_strlen(ofn.lpstrFile) - 1] == '\\') {
		ofn.lpstrInitialDir = szInitialDir;
		mir_strcpy(szInitialDir, ofn.lpstrFile);
		*ofn.lpstrFile = '\0';
	}

	ofn.Flags = /*OFN_DONTADDTORECENT |*/ OFN_NOREADONLYRETURN
		| OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_EXPLORER | OFN_ENABLESIZING
		| OFN_ALLOWMULTISELECT;
	ofn.hwndOwner = hwndOwner;
	ofn.hInstance = g_plugin.getInst();
	ofn.lpstrTitle = Translate("Specify a file to share");
	ofn.lpTemplateName = MAKEINTRESOURCE(IDD_NEW_SHARE_PROPERTIES);
	ofn.lpfnHook = ShareNewFileDialogHook;
	ofn.lCustData = (LPARAM)pstNewShare;
	if (!GetOpenFileName(&ofn))
		return false;

	if (strchr(pstNewShare->pszSrvPath, '"')) {
		// multiple files selected
		// Serverpath: "file1" "file2" "file3"
		// move one after the other to front of string (in place)
		// terminate it with \0 append to realpath and add the share		
		char* pszFileNamePos = pstNewShare->pszSrvPath;
		char* szRealDirectoryEnd = &pstNewShare->pszRealPath[mir_strlen(pstNewShare->pszRealPath)];

		*szRealDirectoryEnd = '\\';
		szRealDirectoryEnd++;

		while (pszFileNamePos && *pszFileNamePos) {
			pszFileNamePos = strchr(pszFileNamePos, '"');
			if (pszFileNamePos) {
				pszFileNamePos++;
				char *start = pszFileNamePos;
				pszFileNamePos = strchr(pszFileNamePos, '"');
				if (pszFileNamePos) {
					char *end = pszFileNamePos;
					memmove(pstNewShare->pszSrvPath + 1, start, end - start);
					*(end - (start - (pstNewShare->pszSrvPath + 1))) = '\0';

					int realPathLen = szRealDirectoryEnd - pstNewShare->pszRealPath;
					strncpy(szRealDirectoryEnd, pstNewShare->pszSrvPath + 1,
						pstNewShare->dwMaxRealPath - realPathLen - 1);
					pstNewShare->pszRealPath[pstNewShare->dwMaxRealPath] = '\0';

					if (CallService(MS_HTTP_ADD_CHANGE_REMOVE, 0, (LPARAM)pstNewShare)) {
						MessageBoxW(nullptr, TranslateT("Failed to share new file"), TranslateW(_A2W(MSG_BOX_TITLE)), MB_OK);
						return false;
					}
					pszFileNamePos++;
				}
			}
		}

	}
	else {
		if (CallService(MS_HTTP_ADD_CHANGE_REMOVE, 0, (LPARAM)pstNewShare)) {
			MessageBoxW(nullptr, TranslateT("Failed to share new file"), TranslateW(_A2W(MSG_BOX_TITLE)), MB_OK);
			return false;
		}
	}

	UpdateStatisticsView();
	return true;
}

/////////////////////////////////////////////////////////////////////
// Member Function : UpdateStatisticView
// Type            : Global
// Parameters      : hwndDlg - ?
//                   false   - ?
// Returns         : void
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030904, 04 september 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

void UpdateStatisticView(HWND hwndDlg, bool bRefressUsersOnly = false)
{
	HWND hShareList = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
	HWND hUserList = GetDlgItem(hwndDlg, IDC_CURRENT_USERS);
	bool bShowHiddenShares = IsDlgButtonChecked(hwndDlg, IDC_SHOWHIDDENSHARES) == BST_CHECKED;

	if (!bRefressUsersOnly)
		ListView_DeleteAllItems(hShareList);
	ListView_DeleteAllItems(hUserList);

	mir_cslock lck(csFileShareListAccess);

	char szTmp[50];
	in_addr stAddr;

	bool bAutoRefress = false;

	LVITEM sItem = { 0 };
	int nShareNr = 0;
	int nUserNr = 0;
	for (CLFileShareNode * pclCur = pclFirstNode; pclCur; pclCur = pclCur->pclNext) {
		if (!bRefressUsersOnly &&
			(bShowHiddenShares || !strstr(pclCur->st.pszRealPath, "\\@"))) {
			sItem.mask = LVIF_TEXT /*| LVIF_PARAM | LVIF_IMAGE*/;
			sItem.iItem = nShareNr;
			sItem.iSubItem = 0;
			sItem.pszText = pclCur->st.pszSrvPath;
			ListView_InsertItem(hShareList, &sItem);


			mir_snprintf(szTmp, "%d", pclCur->st.nMaxDownloads);
			sItem.iSubItem = 1;
			sItem.pszText = szTmp;
			ListView_SetItem(hShareList, &sItem);

			stAddr.S_un.S_addr = htonl(pclCur->st.dwAllowedIP);
			sItem.iSubItem = 2;
			sItem.pszText = inet_ntoa(stAddr);
			ListView_SetItem(hShareList, &sItem);

			stAddr.S_un.S_addr = htonl(pclCur->st.dwAllowedMask);
			sItem.iSubItem = 3;
			sItem.pszText = inet_ntoa(stAddr);
			ListView_SetItem(hShareList, &sItem);

			sItem.iSubItem = 4;
			sItem.pszText = pclCur->st.pszRealPath;
			ListView_SetItem(hShareList, &sItem);

			nShareNr++;
		}

		for (CLShareUser * pclCurUser = pclCur->pclGetUsers(); pclCurUser; pclCurUser = pclCurUser->pclNext) {
			bAutoRefress = true;

			sItem.mask = LVIF_TEXT /*| LVIF_PARAM | LVIF_IMAGE*/;
			sItem.iItem = nUserNr;
			sItem.iSubItem = 0;
			sItem.pszText = pclCurUser->szCurrentDLSrvPath; //pclCur->st.pszSrvPath;
			ListView_InsertItem(hUserList, &sItem);

			sItem.iSubItem = 1;
			sItem.pszText = inet_ntoa(pclCurUser->stAddr);
			ListView_SetItem(hUserList, &sItem);

			sItem.iSubItem = 2;
			sItem.pszText = (char*)pclCurUser->pszCustomInfo();
			ListView_SetItem(hUserList, &sItem);

			if (pclCurUser->dwTotalSize) {
				mir_snprintf(szTmp, "%d %%", (pclCurUser->dwCurrentDL * 100) / pclCurUser->dwTotalSize);
			}
			else {
				mir_strcpy(szTmp, "? %%");
			}
			sItem.iSubItem = 3;
			sItem.pszText = szTmp;
			ListView_SetItem(hUserList, &sItem);

			uint32_t dwSpeed = pclCurUser->dwGetDownloadSpeed();
			if (dwSpeed > 10000) {
				dwSpeed += 512; // make sure we round ot down correctly.
				dwSpeed /= 1024;
				mir_snprintf(szTmp, "%d KB/Sec", dwSpeed);
			}
			else mir_snprintf(szTmp, "%d B/Sec", dwSpeed);

			sItem.iSubItem = 4;
			sItem.pszText = szTmp;
			ListView_SetItem(hUserList, &sItem);

			nUserNr++;
		}
	}

	if (bLastAutoRefress != bAutoRefress) {
		if (bAutoRefress)
			SetTimer(hwndDlg, 0, 1000, nullptr);
		else
			KillTimer(hwndDlg, 0);
		bLastAutoRefress = bAutoRefress;
	}
}

/////////////////////////////////////////////////////////////////////
// Member Function : SetWindowsCtrls
// Type            : Global
// Parameters      : hwndDlg - ?
// Returns         : void
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030904, 04 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void SetWindowsCtrls(HWND hwndDlg)
{
	RECT rNewSize;
	GetClientRect(hwndDlg, &rNewSize);

	const int nSpacing = 8;
	int nCtrlHight = (rNewSize.bottom - (nSpacing * 3)) / 3 - 20;

	SetWindowPos(GetDlgItem(hwndDlg, IDC_CURRENT_SHARES), nullptr,
		nSpacing,
		35,
		rNewSize.right - (nSpacing * 2),
		nCtrlHight * 2,
		SWP_NOZORDER);

	SetWindowPos(GetDlgItem(hwndDlg, IDC_CURRENT_USERS), nullptr,
		nSpacing,
		(nSpacing * 2) + nCtrlHight * 2 + 25,
		rNewSize.right - (nSpacing * 2),
		nCtrlHight + 35,
		SWP_NOZORDER);
}

/////////////////////////////////////////////////////////////////////
// Member Function : DlgProcStatsticView
// Type            : Global
// Parameters      : hwndDlg - ?
//                   msg     - ?
//                   wParam  - ?
//                   lParam  - ?
// Returns         : static BOOL CALLBACK
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030904, 04 september 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

static INT_PTR CALLBACK DlgProcStatsticView(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
	{
		SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)g_plugin.getIcon(IDI_SHARE_NEW_FILE));

		TranslateDialogDefault(hwndDlg);

		HWND hShareList = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
		HWND hUserList = GetDlgItem(hwndDlg, IDC_CURRENT_USERS);

		{ // init adv. win styles
			uint32_t dw = ListView_GetExtendedListViewStyle(hShareList);
			dw |= LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT;
			ListView_SetExtendedListViewStyle(hShareList, dw /*| LVS_EX_LABELTIP*/);
		}
		{ // init adv. win styles
			uint32_t dw = ListView_GetExtendedListViewStyle(hUserList);
			dw |= LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT;
			ListView_SetExtendedListViewStyle(hUserList, dw /*| LVS_EX_LABELTIP*/);
		}

		LVCOLUMN cCol = { 0 };
		cCol.mask = LVCF_TEXT | LVCF_WIDTH;
		cCol.cx = g_plugin.getWord("StatWnd_cx1", 126);
		cCol.pszText = Translate("Share name");
		ListView_InsertColumn(hShareList, 0, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx2", 48);
		cCol.pszText = Translate("Max Downloads");
		ListView_InsertColumn(hShareList, 1, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx3", 96);
		cCol.pszText = Translate("Allowed IP");
		ListView_InsertColumn(hShareList, 2, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx4", 104);
		cCol.pszText = Translate("Allowed Mask");
		ListView_InsertColumn(hShareList, 3, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx5", 252);
		cCol.pszText = Translate("Real path");
		ListView_InsertColumn(hShareList, 4, &cCol);

		cCol.mask = LVCF_TEXT | LVCF_WIDTH;
		cCol.cx = g_plugin.getWord("StatWnd_cx6", 142);
		cCol.pszText = Translate("Share name");
		ListView_InsertColumn(hUserList, 0, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx7", 111);
		cCol.pszText = Translate("User");
		ListView_InsertColumn(hUserList, 1, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx8", 100);
		cCol.pszText = Translate("Agent");
		ListView_InsertColumn(hUserList, 2, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx9", 100);
		cCol.pszText = Translate("Completed");
		ListView_InsertColumn(hUserList, 3, &cCol);
		cCol.cx = g_plugin.getWord("StatWnd_cx10", 100);
		cCol.pszText = Translate("Speed");
		ListView_InsertColumn(hUserList, 4, &cCol);

		bool b = g_plugin.getByte("StatWnd_ShowHidden", 0) != 0;
		CheckDlgButton(hwndDlg, IDC_SHOWHIDDENSHARES, b ? BST_CHECKED : BST_UNCHECKED);

		bLastAutoRefress = false;
		UpdateStatisticView(hwndDlg);
		Utils_RestoreWindowPosition(hwndDlg, 0, MODULENAME, "StatWnd_");
		SetWindowsCtrls(hwndDlg);
	}
	return TRUE;

	case WM_SIZE:
	case WM_SIZING:
		SetWindowsCtrls(hwndDlg);
		return TRUE;

	case WM_TIMER:
		UpdateStatisticView(hwndDlg, true);
		return TRUE;

	case WM_RELOAD_STATISTICS:
		UpdateStatisticView(hwndDlg);
		return TRUE;

	case WM_DESTROY:
		hwndStatsticView = nullptr;
		return 0;

	case WM_DROPFILES:
	{
		HDROP hDrop = (HDROP)wParam;
		char szDropedFile[MAX_PATH];
		char szServPath[MAX_PATH] = { 0 };

		int nLen = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
		for (int i = 0; i < nLen; i++) {
			DragQueryFile(hDrop, i, szDropedFile, sizeof(szDropedFile));

			STFileShareInfo stNewShare = { 0 };
			stNewShare.lStructSize = sizeof(STFileShareInfo);
			stNewShare.nMaxDownloads = nDefaultDownloadLimit;
			stNewShare.pszRealPath = szDropedFile;
			stNewShare.dwMaxRealPath = sizeof(szDropedFile);
			stNewShare.pszSrvPath = szServPath;
			stNewShare.dwMaxSrvPath = sizeof(szServPath);

			szServPath[0] = '/';
			char* fileName = strrchr(szDropedFile, '\\');
			if (fileName)
				strncpy(&szServPath[1], fileName + 1, MAX_PATH - 2);

			if (CallService(MS_HTTP_ADD_CHANGE_REMOVE, 0, (LPARAM)&stNewShare)) {
				MessageBoxW(nullptr, TranslateT("Failed to share new file"), TranslateW(_A2W(MSG_BOX_TITLE)), MB_OK);
				return false;
			}
		}

		UpdateStatisticsView();
		DragFinish(hDrop);
		return 0;
	}

	case WM_CONTEXTMENU:
	{
		if (wParam != (WPARAM)GetDlgItem(hwndDlg, IDC_CURRENT_SHARES))
			return FALSE;

		HWND hShareList = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
		if (ListView_GetNextItem(hShareList, -1, LVIS_SELECTED) == -1)
			return FALSE;

		HMENU hMainMenu = LoadMenu(g_plugin.getInst(), MAKEINTRESOURCE(IDR_MENU1));
		if (hMainMenu) {
			HMENU hMenu = GetSubMenu(hMainMenu, 0);

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			if (pt.x == -1 && pt.y == -1) {
				HWND hMapUser = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
				int nFirst = ListView_GetNextItem(hMapUser, -1, LVNI_FOCUSED);
				if (nFirst >= 0) {
					ListView_GetItemPosition(hMapUser, nFirst, &pt);
				}

				if (pt.y < 16)
					pt.y = 16;
				else {
					RECT rUserList;
					GetClientRect(hMapUser, &rUserList);
					if (pt.y > rUserList.bottom - 16)
						pt.y = rUserList.bottom - 16;
					else
						pt.y += 8;
				}
				pt.x = 8;
				ClientToScreen(hMapUser, &pt);
			}

			TranslateMenu(hMenu);
			TrackPopupMenu(hMenu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
				pt.x, pt.y, 0, hwndDlg, nullptr);

			DestroyMenu(hMainMenu);
		}
		return TRUE;
	}

	case WM_COMMAND:
	{
		HWND hShareList = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
		char szTmp[MAX_PATH];
		LVITEM sItem = { 0 };
		sItem.mask = LVIF_TEXT;
		sItem.pszText = szTmp;
		sItem.cchTextMax = _countof(szTmp);

		switch (LOWORD(wParam)) {
		case IDC_SHOWHIDDENSHARES:
		{
			UpdateStatisticView(hwndDlg);
			return TRUE;
		}

		case ID_SHARELIST_NEWSHARE:
		{
			CallService(MS_SHARE_NEW_FILE, 0, (LPARAM)hwndDlg);
			return TRUE;
		}

		case ID_SHARELIST_EDITSHARE:
		case ID_SHARELIST_REMOVESHARE:
		{
			STFileShareInfo stShareInfo = { 0 };
			stShareInfo.lStructSize = sizeof(STFileShareInfo);
			stShareInfo.pszSrvPath = szTmp;
			stShareInfo.dwMaxSrvPath = sizeof(szTmp);

			sItem.iItem = ListView_GetNextItem(hShareList, -1, LVIS_SELECTED);
			while (sItem.iItem != -1) {
				if (ListView_GetItem(hShareList, &sItem)) {
					if (LOWORD(wParam) == ID_SHARELIST_REMOVESHARE)
						CallService(MS_HTTP_ADD_CHANGE_REMOVE, 0, (LPARAM)&stShareInfo);
					else {
						char szRealPath[MAX_PATH];
						stShareInfo.pszRealPath = szRealPath;
						stShareInfo.dwMaxRealPath = sizeof(szRealPath);
						CallService(MS_HTTP_GET_SHARE, 0, (LPARAM)&stShareInfo);
						bShowShareNewFileDlg(hwndDlg, &stShareInfo);
					}
				}
				sItem.iItem = ListView_GetNextItem(hShareList, sItem.iItem, LVIS_SELECTED);
			}
			UpdateStatisticView(hwndDlg);
			return TRUE;
		}

		case ID_SHARELIST_OPEN:
		case ID_SHARELIST_COPYLINK:
			sItem.iItem = ListView_GetNextItem(hShareList, -1, LVIS_SELECTED);
			if (sItem.iItem != -1) {
				if (ListView_GetItem(hShareList, &sItem)) {
					string sLink = sCreateLink(sItem.pszText);
					if (sLink.size() <= 0)
						return TRUE;

					if (LOWORD(wParam) == ID_SHARELIST_COPYLINK)
						Utils_ClipboardCopy(sLink.c_str());
					else
						Utils_OpenUrl(sLink.c_str());
				}
			}

			return TRUE;
		}
		break;
	}

	case WM_CLOSE:
		HWND hShareList = GetDlgItem(hwndDlg, IDC_CURRENT_SHARES);
		HWND hUserList = GetDlgItem(hwndDlg, IDC_CURRENT_USERS);

		g_plugin.setWord("StatWnd_cx1", (uint16_t)ListView_GetColumnWidth(hShareList, 0));
		g_plugin.setWord("StatWnd_cx2", (uint16_t)ListView_GetColumnWidth(hShareList, 1));
		g_plugin.setWord("StatWnd_cx3", (uint16_t)ListView_GetColumnWidth(hShareList, 2));
		g_plugin.setWord("StatWnd_cx4", (uint16_t)ListView_GetColumnWidth(hShareList, 3));
		g_plugin.setWord("StatWnd_cx5", (uint16_t)ListView_GetColumnWidth(hShareList, 4));
		g_plugin.setWord("StatWnd_cx6", (uint16_t)ListView_GetColumnWidth(hUserList, 0));
		g_plugin.setWord("StatWnd_cx7", (uint16_t)ListView_GetColumnWidth(hUserList, 1));
		g_plugin.setWord("StatWnd_cx8", (uint16_t)ListView_GetColumnWidth(hUserList, 2));
		g_plugin.setWord("StatWnd_cx9", (uint16_t)ListView_GetColumnWidth(hUserList, 3));
		g_plugin.setWord("StatWnd_cx10", (uint16_t)ListView_GetColumnWidth(hUserList, 4));

		bool b = IsDlgButtonChecked(hwndDlg, IDC_SHOWHIDDENSHARES) == BST_CHECKED;
		g_plugin.setByte("StatWnd_ShowHidden", b);

		Utils_SaveWindowPosition(hwndDlg, 0, MODULENAME, "StatWnd_");
		DestroyWindow(hwndDlg);
		return TRUE;
	}
	return FALSE;
}

/////////////////////////////////////////////////////////////////////
// Member Function : SendLinkToUser
// Type            : Global
// Parameters      : wParam - (MCONTACT)hContact
//                   lParam - ?
// Returns         : static int
// Description     : Send the link to the given contact
//
// References      : -
// Remarks         : -
// Created         : 030826, 26 august 2003
// Developer       : Sérgio Rolanski
/////////////////////////////////////////////////////////////////////

void SendLinkToUser(WPARAM wParam, char *pszSrvPath)
{
	string sLink = sCreateLink(pszSrvPath);
	CallService(MS_MSG_SENDMESSAGE, wParam, (LPARAM)sLink.c_str());
}

/////////////////////////////////////////////////////////////////////
// Member Function : nShareNewFile
// Type            : Global
// Parameters      : wParam - (MCONTACT)hContact
//                   lParam - ?
// Returns         : static int

// Description     : Called when user selects my menu item
//
// References      : -
// Remarks         : -
// Created         : 030826, 26 august 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

static INT_PTR nShareNewFile(WPARAM hContact, LPARAM lParam)
{
	// used to be _MAX_PATH
	// changed it since selecting multiple files requires a bigger buffer
	char szNewFile[10000] = { 0 };
	char szSrvPath[10000] = { 0 };

	STFileShareInfo stNewShare = { 0 };
	stNewShare.lStructSize = sizeof(STFileShareInfo);
	stNewShare.nMaxDownloads = 1;
	stNewShare.pszRealPath = szNewFile;
	stNewShare.dwMaxRealPath = sizeof(szNewFile);
	stNewShare.pszSrvPath = szSrvPath;
	stNewShare.dwMaxSrvPath = sizeof(szSrvPath);

	if (hContact) {
		// Try to locate an IP address.
		DBVARIANT dbv = { 0 };
		if (!db_get_s(hContact, "Protocol", "p", &dbv)) {
			stNewShare.dwAllowedIP = db_get_dw(hContact, dbv.pszVal, "IP", 0);
			if (!stNewShare.dwAllowedIP)
				stNewShare.dwAllowedIP = db_get_dw(hContact, dbv.pszVal, "RealIP", 0);
			if (!stNewShare.dwAllowedIP)
				stNewShare.dwAllowedIP = g_plugin.getDword(hContact, "LastUsedIP", 0);
		}
		db_free(&dbv);

		stNewShare.dwAllowedMask = g_plugin.getDword(hContact, "LastUsedMask", 0);
	}

	if (!stNewShare.dwAllowedMask) {
		if (stNewShare.dwAllowedIP)
			stNewShare.dwAllowedMask = 0xFFFFFFFF;
		else
			stNewShare.dwAllowedMask = 0;
	}

	if (!bShowShareNewFileDlg((lParam ? HWND(lParam) : g_clistApi.hwndContactList), &stNewShare))
		return 0;

	if (stNewShare.dwAllowedIP)
		g_plugin.setDword(hContact, "LastUsedIP", stNewShare.dwAllowedIP);
	else
		g_plugin.delSetting(hContact, "LastUsedIP");

	if (stNewShare.dwAllowedMask && stNewShare.dwAllowedMask != 0xFFFFFFFF)
		g_plugin.setDword(hContact, "LastUsedMask", stNewShare.dwAllowedMask);
	else
		g_plugin.delSetting(hContact, "LastUsedMask");

	SendLinkToUser(hContact, stNewShare.pszSrvPath);
	return 0;
}

static INT_PTR nShowStatisticsView(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	if (hwndStatsticView) {
		BringWindowToTop(hwndStatsticView);
		return 0;
	}
	hwndStatsticView = CreateDialogParam(g_plugin.getInst(), MAKEINTRESOURCE(IDD_STATISTICS_VIEW), nullptr, DlgProcStatsticView, (LPARAM)NULL);
	ShowWindow(hwndStatsticView, SW_SHOWNORMAL);
	return 0;
}

/////////////////////////////////////////////////////////////////////
// Member Function : OptionsDlgProc
// Type            : Global
// Parameters      : hwndDlg - ?
//                   msg     - ?
//                   wParam  - ?
//                   lParam  - ?
// Returns         : static BOOL CALLBACK
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030911, 11 september 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

static INT_PTR CALLBACK OptionsDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
	{
		string sDefExt = DBGetString(0, MODULENAME, "ExternalSrvName", szDefaultExternalSrvName);
		SetDlgItemText(hwndDlg, IDC_EXTERNAL_SRV_NAME, sDefExt.c_str());

		CheckDlgButton(hwndDlg, IDC_WRITE_LOG_FILE, g_plugin.getByte("WriteLogFile", 0) != 0 ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_SHOW_POPUPS, bShowPopups ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_LIMIT_ONLY_WHEN_ONLINE, bLimitOnlyWhenOnline ? BST_CHECKED : BST_UNCHECKED);


		{// Url Address
			SetDlgItemText(hwndDlg, IDC_URL_ADDRESS, sUrlAddress.c_str());
			HWND hComboBox = GetDlgItem(hwndDlg, IDC_URL_ADDRESS);
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://checkip.dyndns.org");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://checkip.dyndns.org:8245/");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://dynamic.zoneedit.com/checkip.html");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://ipdetect.dnspark.com/");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://update.dynu.com/basic/ipcheck.asp");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://www.dnsart.com/myip.php");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"http://www.dnsart.com:7777/myip.php");
		}

		{// Page keyword
			SetDlgItemText(hwndDlg, IDC_PAGE_KEYWORD, sPageKeyword.c_str());
			HWND hComboBox = GetDlgItem(hwndDlg, IDC_PAGE_KEYWORD);
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"");
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)LPGEN("Current IP Address: "));
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)LPGEN("Current Address: "));
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)LPGEN("IP Address: "));
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)LPGEN("You are browsing from"));
			SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)"<HTML><BODY>");
		}

		SetDlgItemInt(hwndDlg, IDC_MAX_SPEED, nMaxUploadSpeed >> 10, true);
		SetDlgItemInt(hwndDlg, IDC_MAX_CONN_TOTAL, nMaxConnectionsTotal, true);
		SetDlgItemInt(hwndDlg, IDC_MAX_CONN_PER_USER, nMaxConnectionsPerUser, true);
		SetDlgItemInt(hwndDlg, IDC_DEFAULT_DOWNLOAD_LIMIT, nDefaultDownloadLimit, true);

		indexCreationMode =
			(eIndexCreationMode)g_plugin.getByte("IndexCreationMode", 3);

		switch (indexCreationMode) {
		case INDEX_CREATION_HTML:
			CheckRadioButton(hwndDlg, IDC_INDEX_OFF, IDC_INDEX_DETECT, IDC_INDEX_HTML);
			break;
		case INDEX_CREATION_XML:
			CheckRadioButton(hwndDlg, IDC_INDEX_OFF, IDC_INDEX_DETECT, IDC_INDEX_XML);
			break;
		case INDEX_CREATION_DETECT:
			CheckRadioButton(hwndDlg, IDC_INDEX_OFF, IDC_INDEX_DETECT, IDC_INDEX_DETECT);
			break;
		default: // INDEX_CREATION_DISABLE
			CheckRadioButton(hwndDlg, IDC_INDEX_OFF, IDC_INDEX_DETECT, IDC_INDEX_OFF);
			break;
		}

		TranslateDialogDefault(hwndDlg);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_MAX_SPEED:
		case IDC_MAX_CONN_PER_USER:
		case IDC_MAX_CONN_TOTAL:
		case IDC_EXTERNAL_SRV_NAME:
			if (HIWORD(wParam) == EN_CHANGE)
				SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
			return TRUE;

		case IDC_URL_ADDRESS:
		case IDC_PAGE_KEYWORD:
			if (HIWORD(wParam) == CBN_EDITUPDATE || HIWORD(wParam) == CBN_SELCHANGE)
				SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
			return TRUE;

		case IDC_INDEX_HTML:
		case IDC_INDEX_OFF:
		case IDC_INDEX_XML:
		case IDC_INDEX_DETECT:
		case IDC_LIMIT_ONLY_WHEN_ONLINE:
		case IDC_SHOW_POPUPS:
		case IDC_WRITE_LOG_FILE:
			if (HIWORD(wParam) == BN_CLICKED)
				SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
			return TRUE;

		case IDC_EXTERNAL_SRV_DEFAULT:
			if (HIWORD(wParam) == BN_CLICKED) {
				SetDlgItemText(hwndDlg, IDC_EXTERNAL_SRV_NAME, szDefaultExternalSrvName);
				SetDlgItemText(hwndDlg, IDC_URL_ADDRESS, szDefaultUrlAddress);
				SetDlgItemText(hwndDlg, IDC_PAGE_KEYWORD, szDefaultPageKeyword);
				SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
			}
			return TRUE;

		case IDC_OPEN_LOG:
			bOpenLogFile();
			return TRUE;

		case IDC_TEST_EXTERNALIP:
			char szUrl[500];
			char szKeyWord[1000];
			GetDlgItemText(hwndDlg, IDC_URL_ADDRESS, szUrl, _countof(szUrl));
			GetDlgItemText(hwndDlg, IDC_PAGE_KEYWORD, szKeyWord, _countof(szKeyWord));
			uint32_t dwExternalIP = GetExternIP(szUrl, szKeyWord);

			mir_snprintf(szKeyWord, Translate("Your external IP was detected as %d.%d.%d.%d\r\nby: %s"),
				SplitIpAddress(dwExternalIP),
				szUrl);
			MessageBox(hwndDlg, szKeyWord, Translate(MSG_BOX_TITLE), MB_OK);
		}
		break;

	case WM_NOTIFY:
		NMHDR * p = ((LPNMHDR)lParam);
		switch (p->code) {
		case PSN_APPLY:
		{
			char szTemp[500];
			if (GetDlgItemText(hwndDlg, IDC_EXTERNAL_SRV_NAME, szTemp, _countof(szTemp)))
				g_plugin.setString("ExternalSrvName", szTemp);

			g_plugin.setByte("WriteLogFile", IsDlgButtonChecked(hwndDlg, IDC_WRITE_LOG_FILE) == BST_CHECKED);

			bShowPopups = IsDlgButtonChecked(hwndDlg, IDC_SHOW_POPUPS) == BST_CHECKED;
			g_plugin.setByte("ShowPopups", bShowPopups);

			GetDlgItemText(hwndDlg, IDC_URL_ADDRESS, szTemp, _countof(szTemp));
			sUrlAddress = szTemp;
			g_plugin.setString("UrlAddress", sUrlAddress.c_str());

			GetDlgItemText(hwndDlg, IDC_PAGE_KEYWORD, szTemp, _countof(szTemp));
			sPageKeyword = szTemp;
			g_plugin.setString("PageKeyword", sPageKeyword.c_str());
			dwExternalIpAddress = 0;

			BOOL bTranslated = false;
			int nTemp = GetDlgItemInt(hwndDlg, IDC_MAX_SPEED, &bTranslated, true);
			if (bTranslated) {
				nMaxUploadSpeed = nTemp << 10;
				g_plugin.setDword("MaxUploadSpeed", nMaxUploadSpeed);
			}

			nTemp = GetDlgItemInt(hwndDlg, IDC_MAX_CONN_TOTAL, &bTranslated, true);
			if (bTranslated) {
				nMaxConnectionsTotal = nTemp;
				g_plugin.setDword("MaxConnectionsTotal", nMaxConnectionsTotal);
			}

			nTemp = GetDlgItemInt(hwndDlg, IDC_MAX_CONN_PER_USER, &bTranslated, true);
			if (bTranslated) {
				nMaxConnectionsPerUser = nTemp;
				g_plugin.setDword("MaxConnectionsPerUser", nMaxConnectionsPerUser);
			}

			nTemp = GetDlgItemInt(hwndDlg, IDC_DEFAULT_DOWNLOAD_LIMIT, &bTranslated, true);
			if (bTranslated) {
				nDefaultDownloadLimit = nTemp;
				g_plugin.setDword("DefaultDownloadLimit", nDefaultDownloadLimit);
			}

			bLimitOnlyWhenOnline = IsDlgButtonChecked(hwndDlg, IDC_LIMIT_ONLY_WHEN_ONLINE) == BST_CHECKED;
			g_plugin.setByte("LimitOnlyWhenOnline", bLimitOnlyWhenOnline);

			if (IsDlgButtonChecked(hwndDlg, IDC_INDEX_HTML) == BST_CHECKED ||
				IsDlgButtonChecked(hwndDlg, IDC_INDEX_DETECT) == BST_CHECKED) {
				if (IsDlgButtonChecked(hwndDlg, IDC_INDEX_HTML) == BST_CHECKED)
					indexCreationMode = INDEX_CREATION_HTML;
				else
					indexCreationMode = INDEX_CREATION_DETECT;

				if (!LoadIndexHTMLTemplate()) {
					CheckRadioButton(hwndDlg, IDC_INDEX_OFF, IDC_INDEX_XML, IDC_INDEX_OFF);
					indexCreationMode = INDEX_CREATION_DISABLE;
				}
			}
			else if (IsDlgButtonChecked(hwndDlg, IDC_INDEX_XML) == BST_CHECKED) {
				FreeIndexHTMLTemplate();
				indexCreationMode = INDEX_CREATION_XML;
			}
			else {
				FreeIndexHTMLTemplate();
				indexCreationMode = INDEX_CREATION_DISABLE;
			}

			g_plugin.setByte("IndexCreationMode", (uint8_t)indexCreationMode);

			return TRUE;
		}
		break;
		}
	}
	return FALSE;
}

/////////////////////////////////////////////////////////////////////
// Member Function : OptionsInitialize
// Type            : Global
// Parameters      : wParam - ?
//                   LPARAM - ?
// Returns         : int
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030911, 11 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

int OptionsInitialize(WPARAM wParam, LPARAM /*lParam*/)
{
	OPTIONSDIALOGPAGE odp = {};
	odp.position = 900000000;
	odp.pszTemplate = MAKEINTRESOURCE(IDD_OPT_HTTP_SERVER);
	odp.flags = ODPF_BOLDGROUPS | ODPF_UNICODE;
	odp.szTitle.w = LPGENW("HTTP Server");
	odp.szGroup.w = LPGENW("Network");
	odp.pfnDlgProc = OptionsDlgProc;
	g_plugin.addOptions(wParam, &odp);
	return 0;
}

/////////////////////////////////////////////////////////////////////
// Member Function : MainThreadCallback
// Type            : Global
// Parameters      : dwParam - ?
// Returns         : void CALLBACK
// Description     : Called from main thread.
//                   Because MS_POPUP_ADDPOPUP may only be called from that
//                   ShowPopupWindow activates this method via APC
// References      : -
// Remarks         : -
// Created         : 030902, 02 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void CALLBACK MainThreadCallback(void *param)
{
	POPUPDATA *pclData = (POPUPDATA*)param;
	if (g_plugin.getByte("WriteLogFile", 0) != 0)
		LogEvent(pclData->lpzContactName, pclData->lpzText);

	PUAddPopup(pclData);
	delete pclData;
}

/////////////////////////////////////////////////////////////////////
// Member Function : PopupWindowProc
// Type            : Global
// Parameters      : hWnd    - ?
//                   message - ?
//                   wParam  - ?
//                   lParam  - ?
// Returns         : LRESULT CALLBACK
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 031011, 11 oktober 2003
// Developer       : KN, Houdini
/////////////////////////////////////////////////////////////////////

static void CALLBACK OpenStatisticViewFromPopupProc(void*)
{
	nShowStatisticsView(0, 0);
}

LRESULT CALLBACK PopupWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PUGetPluginData(hWnd);

	switch (message) {
	case WM_LBUTTONDOWN:
		CallFunctionAsync(OpenStatisticViewFromPopupProc, 0);
		PUDeletePopup(hWnd);
		return 0;

	case WM_CONTEXTMENU:
		PUDeletePopup(hWnd);
		return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////
// Member Function : ShowPopupWindow
// Type            : Global
// Parameters      : pszText - ?
// Returns         : void
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030831, 31 august 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void ShowPopupWindow(const char * pszName, const char * pszText, COLORREF ColorBack /*= 0*/)
{
	if (!bShowPopups)
		return;

	POPUPDATA *pclData = new POPUPDATA;
	memset(pclData, 0, sizeof(POPUPDATA));
	pclData->lchIcon = g_plugin.getIcon(IDI_SHARE_NEW_FILE);
	strncpy(pclData->lpzContactName, pszName, sizeof(pclData->lpzContactName) - 1);   // -1 so that there aways will be a null termination !!
	strncpy(pclData->lpzText, pszText, sizeof(pclData->lpzText) - 1);
	pclData->colorBack = ColorBack;
	pclData->PluginWindowProc = PopupWindowProc;
	CallFunctionAsync(MainThreadCallback, pclData);
}

/////////////////////////////////////////////////////////////////////
// Member Function : InitGuiElements
// Type            : Global
// Parameters      : None
// Returns         : void
// Description     :
//
// References      : -
// Remarks         : -
// Created         : 030902, 02 september 2003
// Developer       : KN
/////////////////////////////////////////////////////////////////////

void InitGuiElements()
{
	INITCOMMONCONTROLSEX stInitCom;
	stInitCom.dwSize = sizeof(INITCOMMONCONTROLSEX);
	stInitCom.dwICC = ICC_INTERNET_CLASSES;
	InitCommonControlsEx(&stInitCom);

	sUrlAddress = DBGetString(0, MODULENAME, "UrlAddress", szDefaultUrlAddress);
	sPageKeyword = DBGetString(0, MODULENAME, "PageKeyword", szDefaultPageKeyword);

	hShareNewFileService = CreateServiceFunction(MS_SHARE_NEW_FILE, nShareNewFile);
	hShowStatisticsViewService = CreateServiceFunction(MS_SHOW_STATISTICS_VIEW, nShowStatisticsView);

	CMenuItem mi(&g_plugin);
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_SHARE_NEW_FILE);

	SET_UID(mi, 0xb30a6ab5, 0x17a8, 0x4e2e, 0x84, 0x52, 0x5f, 0xbc, 0x83, 0x35, 0xf2, 0x6);
	mi.position = -2000019955;
	mi.name.a = LPGEN("HTTP Share new file");
	mi.pszService = MS_SHARE_NEW_FILE;
	hShareNewFileMenuItem = Menu_AddContactMenuItem(&mi);

	SET_UID(mi, 0x68db84c9, 0xe6b4, 0x4b4f, 0x93, 0x4b, 0xfd, 0x34, 0x2d, 0x83, 0x11, 0xe7);
	mi.position = 1000085005;
	mi.name.a = LPGEN("Show HTTP server statistics");
	mi.pszService = MS_SHOW_STATISTICS_VIEW;
	hShowStatisticsViewMenuItem = Menu_AddMainMenuItem(&mi);

	bShowPopups = g_plugin.getByte("ShowPopups", bShowPopups) != 0;
}
