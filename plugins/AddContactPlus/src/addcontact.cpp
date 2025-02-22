/*
AddContact+ plugin for Miranda NG

Copyright (C) 2007-11 Bartosz 'Dezeath' Białek
Copyright (C) 2012-22 Miranda NG team

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "stdafx.h"

struct AddDialogParam : public MZeroedObject
{
	AddDialogParam() {}

	HANDLE handle;
	CMStringA proto;
	PROTOSEARCHRESULT *psr;
};

void AddContactDlgOpts(HWND hdlg, const char* szProto, BOOL bAuthOptsOnly = FALSE)
{
	uint32_t flags = (szProto) ? CallProtoService(szProto, PS_GETCAPS, PFLAGNUM_4, 0) : 0;
	if (IsDlgButtonChecked(hdlg, IDC_ADDTEMP)) {
		EnableWindow(GetDlgItem(hdlg, IDC_AUTH), FALSE);
		EnableWindow(GetDlgItem(hdlg, IDC_AUTHREQ), FALSE);
		EnableWindow(GetDlgItem(hdlg, IDC_AUTHGB), FALSE);
	}
	else {
		EnableWindow(GetDlgItem(hdlg, IDC_AUTH), !(flags & PF4_FORCEAUTH));
		EnableWindow(GetDlgItem(hdlg, IDC_AUTHREQ), (flags & PF4_NOCUSTOMAUTH) ? FALSE : IsDlgButtonChecked(hdlg, IDC_AUTH));
		EnableWindow(GetDlgItem(hdlg, IDC_AUTHGB), (flags & PF4_NOCUSTOMAUTH) ? FALSE : IsDlgButtonChecked(hdlg, IDC_AUTH));
	}

	if (bAuthOptsOnly)
		return;

	SetDlgItemText(hdlg, IDC_AUTHREQ, (flags & PF4_NOCUSTOMAUTH) ? L"" : TranslateT("Please authorize my request and add me to your contact list."));

	wchar_t *wszUniqueId = (wchar_t *)CallProtoService(szProto, PS_GETCAPS, PFLAG_UNIQUEIDTEXT, 0);
	if (wszUniqueId)
		SetDlgItemText(hdlg, IDC_IDLABEL, CMStringW(wszUniqueId) + L":");
	else
		SetDlgItemText(hdlg, IDC_IDLABEL, TranslateT("Contact ID:"));

	flags = (szProto) ? CallProtoService(szProto, PS_GETCAPS, PFLAGNUM_1, 0) : 0;
	if (flags & PF1_NUMERICUSERID) {
		char buffer[65];
		SetWindowLongPtr(GetDlgItem(hdlg, IDC_USERID), GWL_STYLE, GetWindowLongPtr(GetDlgItem(hdlg, IDC_USERID), GWL_STYLE) | ES_NUMBER);
		if (strstr(szProto, "GG"))
			_ultoa(INT_MAX, buffer, 10);
		else
			_ultoa(ULONG_MAX, buffer, 10);
		SendDlgItemMessage(hdlg, IDC_USERID, EM_LIMITTEXT, (WPARAM)mir_strlen(buffer), 0);
	}
	else {
		SetWindowLongPtr(GetDlgItem(hdlg, IDC_USERID), GWL_STYLE, GetWindowLongPtr(GetDlgItem(hdlg, IDC_USERID), GWL_STYLE) & ~ES_NUMBER);
		SendDlgItemMessage(hdlg, IDC_USERID, EM_LIMITTEXT, 255, 0);
	}
}

bool AddContactDlgAccounts(HWND hdlg, AddDialogParam *acs)
{
	int iAccCount = 0;

	for (auto &pa : Accounts()) {
		if (!pa->IsEnabled())
			continue;
		
		uint32_t dwCaps = (uint32_t)CallProtoService(pa->szModuleName, PS_GETCAPS, PFLAGNUM_1, 0);
		if (dwCaps & PF1_BASICSEARCH || dwCaps & PF1_EXTSEARCH || dwCaps & PF1_SEARCHBYEMAIL || dwCaps & PF1_SEARCHBYNAME)
			iAccCount++;
	}

	if (iAccCount == 0) {
		if (GetParent(hdlg) == nullptr)
			DestroyWindow(hdlg);
		else
			EndDialog(hdlg, 0);
		return false;
	}

	SIZE textSize;
	RECT rc;
	int iIndex = 0, cbWidth = 0;

	HIMAGELIST hIml = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), ILC_COLOR32 | ILC_MASK, iAccCount, 0);
	ImageList_Destroy((HIMAGELIST)SendDlgItemMessage(hdlg, IDC_PROTO, CBEM_SETIMAGELIST, 0, (LPARAM)hIml));
	SendDlgItemMessage(hdlg, IDC_PROTO, CB_RESETCONTENT, 0, 0);

	COMBOBOXEXITEM cbei = { 0 };
	cbei.mask = CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_TEXT | CBEIF_LPARAM;
	HDC hdc = GetDC(hdlg);
	SelectObject(hdc, (HFONT)SendDlgItemMessage(hdlg, IDC_PROTO, WM_GETFONT, 0, 0));

	for (auto &pa : Accounts()) {
		if (!pa->IsEnabled())
			continue;
		
		uint32_t dwCaps = (uint32_t)CallProtoService(pa->szModuleName, PS_GETCAPS, PFLAGNUM_1, 0);
		if (!(dwCaps & PF1_BASICSEARCH) && !(dwCaps & PF1_EXTSEARCH) && !(dwCaps & PF1_SEARCHBYEMAIL) && !(dwCaps & PF1_SEARCHBYNAME))
			continue;

		cbei.pszText = pa->tszAccountName;
		GetTextExtentPoint32(hdc, cbei.pszText, (int)mir_wstrlen(cbei.pszText), &textSize);
		if (textSize.cx > cbWidth)
			cbWidth = textSize.cx;
		
		HICON hIcon = (HICON)CallProtoService(pa->szModuleName, PS_LOADICON, PLI_PROTOCOL | PLIF_SMALL, 0);
		cbei.iImage = cbei.iSelectedImage = ImageList_AddIcon(hIml, hIcon);
		DestroyIcon(hIcon);
		cbei.lParam = (LPARAM)pa->szModuleName;
		SendDlgItemMessage(hdlg, IDC_PROTO, CBEM_INSERTITEM, 0, (LPARAM)&cbei);
		if (cbei.lParam && !mir_strcmp(acs->proto, pa->szModuleName))
			iIndex = cbei.iItem;
		cbei.iItem++;
	}
	cbWidth += 32;
	SendDlgItemMessage(hdlg, IDC_PROTO, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&rc);
	if ((rc.right - rc.left) < cbWidth)
		SendDlgItemMessage(hdlg, IDC_PROTO, CB_SETDROPPEDWIDTH, cbWidth, 0);
	SendDlgItemMessage(hdlg, IDC_PROTO, CB_SETCURSEL, iIndex, 0);
	SendMessage(hdlg, WM_COMMAND, MAKEWPARAM(IDC_PROTO, CBN_SELCHANGE), (LPARAM)GetDlgItem(hdlg, IDC_PROTO));
	if (iAccCount == 1)
		SetFocus(GetDlgItem(hdlg, IDC_USERID));

	return true;
}

#define DM_ADDCONTACT_CHANGEICONS WM_USER + 11
#define DM_ADDCONTACT_CHANGEACCLIST WM_USER + 12

INT_PTR CALLBACK AddContactDlgProc(HWND hdlg, UINT msg, WPARAM wparam, LPARAM)
{
	AddDialogParam *acs = (AddDialogParam*)GetWindowLongPtr(hdlg, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG:
		acs = new AddDialogParam();
		SetWindowLongPtr(hdlg, GWLP_USERDATA, (LONG_PTR)acs);

		Utils_RestoreWindowPositionNoSize(hdlg, NULL, MODULENAME, "");
		TranslateDialogDefault(hdlg);
		Window_SetIcon_IcoLib(hdlg, IcoLib_GetIconHandle(ICON_ADD));

		HookEventMessage(ME_SKIN_ICONSCHANGED, hdlg, DM_ADDCONTACT_CHANGEICONS);
		HookEventMessage(ME_PROTO_ACCLISTCHANGED, hdlg, DM_ADDCONTACT_CHANGEACCLIST);
		{
			wchar_t *szGroup;
			for (int i = 1; (szGroup = Clist_GroupGetName(i, nullptr)) != nullptr; i++) {
				int id = SendDlgItemMessage(hdlg, IDC_GROUP, CB_ADDSTRING, 0, (LPARAM)szGroup);
				SendDlgItemMessage(hdlg, IDC_GROUP, CB_SETITEMDATA, (WPARAM)id, (LPARAM)i);
			}
		}
		SendDlgItemMessage(hdlg, IDC_GROUP, CB_INSERTSTRING, 0, (LPARAM)TranslateT("None"));
		SendDlgItemMessage(hdlg, IDC_GROUP, CB_SETCURSEL, 0, 0);
		{
			ptrA szProto(g_plugin.getStringA("LastProto"));
			if (szProto)
				acs->proto = szProto;
		}
		if (AddContactDlgAccounts(hdlg, acs)) {
			// By default check these checkboxes
			CheckDlgButton(hdlg, IDC_AUTH, BST_CHECKED);
			AddContactDlgOpts(hdlg, acs->proto);
			EnableWindow(GetDlgItem(hdlg, IDOK), FALSE);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case IDC_USERID:
			if (HIWORD(wparam) == EN_CHANGE) {
				wchar_t szUserId[256];
				if (GetDlgItemText(hdlg, IDC_USERID, szUserId, _countof(szUserId))) {
					if (!IsWindowEnabled(GetDlgItem(hdlg, IDOK)))
						EnableWindow(GetDlgItem(hdlg, IDOK), TRUE);
				}
				else if (IsWindowEnabled(GetDlgItem(hdlg, IDOK)))
					EnableWindow(GetDlgItem(hdlg, IDOK), FALSE);
			}
			break;

		case IDC_PROTO:
			if (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_SELENDOK) {
				acs->proto = (char*)SendDlgItemMessage(hdlg, IDC_PROTO, CB_GETITEMDATA, (WPARAM)SendDlgItemMessage(hdlg, IDC_PROTO, CB_GETCURSEL, 0, 0), 0);
				// TODO remember last setting for each proto?
				AddContactDlgOpts(hdlg, acs->proto);
			}
			break;

		case IDC_ADDTEMP:
			AddContactDlgOpts(hdlg, acs->proto, TRUE);
			break;

		case IDC_AUTH:
			{
				uint32_t flags = CallProtoService(acs->proto, PS_GETCAPS, PFLAGNUM_4, 0);
				if (flags & PF4_NOCUSTOMAUTH) {
					EnableWindow(GetDlgItem(hdlg, IDC_AUTHREQ), FALSE);
					EnableWindow(GetDlgItem(hdlg, IDC_AUTHGB), FALSE);
				}
				else {
					EnableWindow(GetDlgItem(hdlg, IDC_AUTHREQ), IsDlgButtonChecked(hdlg, IDC_AUTH));
					EnableWindow(GetDlgItem(hdlg, IDC_AUTHGB), IsDlgButtonChecked(hdlg, IDC_AUTH));
				}
			}
			break;

		case IDOK:
			{
				wchar_t szUserId[256];
				GetDlgItemText(hdlg, IDC_USERID, szUserId, _countof(szUserId));

				if (*rtrimw(szUserId) == 0 ||
					(strstr(acs->proto, "GG") && wcstoul(szUserId, nullptr, 10) > INT_MAX) || // Gadu-Gadu protocol
					((CallProtoService(acs->proto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_NUMERICUSERID) && !wcstoul(szUserId, nullptr, 10)))
				{
					MessageBox(nullptr,
						TranslateT("The contact cannot be added to your contact list. Please make sure the contact ID is entered correctly."),
						TranslateT("Add contact"), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
					break;
				}

				PROTOSEARCHRESULT *psr;
				psr = (PROTOSEARCHRESULT*)mir_calloc(sizeof(PROTOSEARCHRESULT));
				psr->cbSize = sizeof(PROTOSEARCHRESULT);
				psr->flags = PSR_UNICODE;
				psr->id.w = mir_wstrdup(szUserId);
				acs->psr = psr;

				MCONTACT hContact = (MCONTACT)CallProtoService(acs->proto, PS_ADDTOLIST, IsDlgButtonChecked(hdlg, IDC_ADDTEMP) ? PALF_TEMPORARY : 0, (LPARAM)acs->psr);
				if (hContact == NULL) {
					MessageBox(nullptr,
						TranslateT("The contact cannot be added to your contact list. If you are not logged into the selected account, please try to do so. Also, make sure the contact ID is entered correctly."),
						TranslateT("Add contact"), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
					break;
				}

				wchar_t szHandle[256];
				if (GetDlgItemText(hdlg, IDC_MYHANDLE, szHandle, _countof(szHandle)))
					db_set_ws(hContact, "CList", "MyHandle", szHandle);

				int item = SendDlgItemMessage(hdlg, IDC_GROUP, CB_GETCURSEL, 0, 0);
				if (item > 0) {
					item = SendDlgItemMessage(hdlg, IDC_GROUP, CB_GETITEMDATA, item, 0);
					Clist_ContactChangeGroup(hContact, item);
				}

				if (BST_UNCHECKED == IsDlgButtonChecked(hdlg, IDC_ADDTEMP)) {
					Contact::PutOnList(hContact);

					if (IsDlgButtonChecked(hdlg, IDC_AUTH)) {
						uint32_t flags = CallProtoService(acs->proto, PS_GETCAPS, PFLAGNUM_4, 0);
						if (flags & PF4_NOCUSTOMAUTH)
							ProtoChainSend(hContact, PSS_AUTHREQUEST, 0, 0);
						else {
							wchar_t tszReason[512];
							GetDlgItemText(hdlg, IDC_AUTHREQ, tszReason, _countof(tszReason));
							ProtoChainSend(hContact, PSS_AUTHREQUEST, 0, (LPARAM)tszReason);
						}
					}
				}

				if (GetAsyncKeyState(VK_CONTROL))
					CallService(MS_MSG_SENDMESSAGE, hContact, NULL);
			}
			// fall through
		case IDCANCEL:
			if (GetParent(hdlg) == nullptr)
				DestroyWindow(hdlg);
			else
				EndDialog(hdlg, 0);
		}
		break;

	case WM_CLOSE:
		/* if there is no parent for the dialog, its a modeless dialog and can't be killed using EndDialog() */
		if (GetParent(hdlg) == nullptr)
			DestroyWindow(hdlg);
		else
			EndDialog(hdlg, 0);
		break;

	case DM_ADDCONTACT_CHANGEICONS:
		Window_FreeIcon_IcoLib(hdlg);
		Window_SetIcon_IcoLib(hdlg, IcoLib_GetIconHandle(ICON_ADD));
		break;

	case DM_ADDCONTACT_CHANGEACCLIST:
		AddContactDlgAccounts(hdlg, acs);
		break;

	case WM_DESTROY:
		hAddDlg = nullptr;
		Window_FreeIcon_IcoLib(hdlg);
		ImageList_Destroy((HIMAGELIST)SendDlgItemMessage(hdlg, IDC_PROTO, CBEM_GETIMAGELIST, 0, 0));
		if (acs) {
			g_plugin.setString("LastProto", acs->proto);
			if (acs->psr) {
				mir_free(acs->psr->nick.w);
				mir_free(acs->psr->firstName.w);
				mir_free(acs->psr->lastName.w);
				mir_free(acs->psr->email.w);
				mir_free(acs->psr);
			}
			delete acs;
		}
		Utils_SaveWindowPosition(hdlg, NULL, MODULENAME, "");
		break;
	}

	return FALSE;
}
