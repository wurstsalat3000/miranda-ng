/*
Copyright (C) 2012-22 Miranda NG team (https://miranda-ng.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

BOOL ScreenToClient(HWND hWnd, LPRECT lpRect);

/////////////////////////////////////////////////////////////////////////////////////////
// User info dialog

class AvatarUserInfoDlg : public CUserInfoPageDlg
{
	HANDLE hHook = nullptr;

	CCtrlSpin spinPoints, spinDifference;
	CCtrlCheck chkTransparent;
	CCtrlButton btnHide, btnChange, btnDefaults, btnProtect, btnReset, btnDelete;

	void ReloadAvatar()
	{
		SaveTransparentData(m_hwnd, m_hContact, IsDlgButtonChecked(m_hwnd, IDC_PROTECTAVATAR));
		ChangeAvatar(m_hContact, true);
	}

	INT_PTR OnChangeAvatar(UINT, WPARAM, LPARAM)
	{
		InvalidateRect(GetDlgItem(m_hwnd, IDC_PROTOPIC), nullptr, TRUE);
		return 0;
	}

	UI_MESSAGE_MAP(AvatarUserInfoDlg, CUserInfoPageDlg);
		UI_MESSAGE(DM_AVATARCHANGED, OnChangeAvatar);
	UI_MESSAGE_MAP_END();

public:
	AvatarUserInfoDlg() :
		CUserInfoPageDlg(g_plugin, IDD_USER_AVATAR),
		btnHide(this, IDC_HIDEAVATAR),
		btnReset(this, IDC_RESET),
		btnChange(this, IDC_CHANGE),
		btnDelete(this, IDC_DELETE),
		btnProtect(this, IDC_PROTECTAVATAR),
		btnDefaults(this, ID_USE_DEFAULTS),
		chkTransparent(this, IDC_MAKETRANSPBKG),
		spinPoints(this, IDC_BKG_NUM_POINTS_SPIN, 8, 2),
		spinDifference(this, IDC_BKG_NUM_POINTS_SPIN, 100)
	{
		btnHide.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_Hide);
		btnReset.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_Reset);
		btnChange.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_Change);
		btnDelete.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_Delete);
		btnProtect.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_Protect);
		btnDefaults.OnClick = Callback(this, &AvatarUserInfoDlg::onClick_UseDefaults);

		chkTransparent.OnChange = Callback(this, &AvatarUserInfoDlg::onChange_Transparent);
	}

	bool OnInitDialog() override
	{
		hHook = HookEventMessage(ME_AV_AVATARCHANGED, m_hwnd, DM_AVATARCHANGED);

		LoadTransparentData(m_hwnd, GetContactThatHaveTheAvatar(m_hContact));
		return true;
	}

	bool OnRefresh() override
	{
		HWND protopic = GetDlgItem(m_hwnd, IDC_PROTOPIC);
		SendMessage(protopic, AVATAR_SETCONTACT, 0, m_hContact);
		SendMessage(protopic, AVATAR_SETAVATARBORDERCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNSHADOW));
		SendMessage(protopic, AVATAR_SETNOAVATARTEXT, 0, (LPARAM)LPGENW("Contact has no avatar"));
		SendMessage(protopic, AVATAR_RESPECTHIDDEN, 0, (LPARAM)FALSE);
		SendMessage(protopic, AVATAR_SETRESIZEIFSMALLER, 0, (LPARAM)FALSE);

		SendMessage(m_hwnd, DM_SETAVATARNAME, 0, 0);
		CheckDlgButton(m_hwnd, IDC_PROTECTAVATAR, db_get_b(m_hContact, "ContactPhoto", "Locked", 0) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(m_hwnd, IDC_HIDEAVATAR, Contact::IsHidden(m_hContact) ? BST_CHECKED : BST_UNCHECKED);
		return false;
	}

	bool IsEmpty() const override
	{
		auto wszFileName(db_get_wsm(m_hContact, "ContactPhoto", "File"));
		return (wszFileName.IsEmpty() || _waccess(wszFileName, 0) != 0);
	}

	void OnDestroy() override
	{
		UnhookEvent(hHook);
	}

	void onClick_UseDefaults(CCtrlButton *)
	{
		m_hContact = GetContactThatHaveTheAvatar(m_hContact);

		db_unset(m_hContact, "ContactPhoto", "MakeTransparentBkg");
		db_unset(m_hContact, "ContactPhoto", "TranspBkgNumPoints");
		db_unset(m_hContact, "ContactPhoto", "TranspBkgColorDiff");

		LoadTransparentData(m_hwnd, m_hContact);
		ReloadAvatar();
	}

	void onClick_Change(CCtrlButton *)
	{
		SetAvatar(m_hContact, 0);
		SendMessage(m_hwnd, DM_SETAVATARNAME, 0, 0);
		CheckDlgButton(m_hwnd, IDC_PROTECTAVATAR, db_get_b(m_hContact, "ContactPhoto", "Locked", 0) ? BST_CHECKED : BST_UNCHECKED);
	}

	void onClick_Hide(CCtrlButton *)
	{
		bool hidden = IsDlgButtonChecked(m_hwnd, IDC_HIDEAVATAR) != 0;
		SetAvatarAttribute(m_hContact, AVS_HIDEONCLIST, hidden);
		if (hidden != Contact::IsHidden(m_hContact))
			Contact::Hide(m_hContact, hidden);
	}

	void onClick_Protect(CCtrlButton *)
	{
		BOOL locked = IsDlgButtonChecked(m_hwnd, IDC_PROTECTAVATAR);
		SaveTransparentData(m_hwnd, m_hContact, locked);
		ProtectAvatar(m_hContact, locked ? 1 : 0);
	}

	void onChange_Transparent(CCtrlCheck *)
	{
		BOOL enable = IsDlgButtonChecked(m_hwnd, IDC_MAKETRANSPBKG);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_NUM_POINTS_L), enable);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_NUM_POINTS_SPIN), enable);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_NUM_POINTS), enable);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_COLOR_DIFFERENCE_L), enable);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_COLOR_DIFFERENCE_SPIN), enable);
		EnableWindow(GetDlgItem(m_hwnd, IDC_BKG_COLOR_DIFFERENCE), enable);
		ReloadAvatar();
	}

	void onClick_Reset(CCtrlButton *)
	{
		ProtectAvatar(m_hContact, 0);
		if (MessageBox(nullptr, TranslateT("Delete picture file from disk (may be necessary to force a reload, but will delete local pictures)?"), TranslateT("Reset contact picture"), MB_YESNO) == IDYES) {
			DBVARIANT dbv = { 0 };
			if (!db_get_ws(m_hContact, "ContactPhoto", "File", &dbv)) {
				DeleteFile(dbv.pwszVal);
				db_free(&dbv);
			}
		}
		db_unset(m_hContact, "ContactPhoto", "Locked");
		db_unset(m_hContact, "ContactPhoto", "Backup");
		db_unset(m_hContact, "ContactPhoto", "RFile");
		db_unset(m_hContact, "ContactPhoto", "File");
		db_unset(m_hContact, "ContactPhoto", "Format");

		char *szProto = Proto_GetBaseAccountName(m_hContact);
		db_unset(m_hContact, szProto, "AvatarHash");
		DeleteAvatarFromCache(m_hContact, FALSE);

		QueueAdd(m_hContact);
	}

	void onClick_Delete(CCtrlButton *)
	{
		ProtectAvatar(m_hContact, 0);
		if (MessageBoxW(nullptr, TranslateT("Delete picture file from disk (may be necessary to force a reload, but will delete local pictures)?"), TranslateT("Reset contact picture"), MB_YESNO) == IDYES) {
			DBVARIANT dbv = { 0 };
			if (!db_get_ws(m_hContact, "ContactPhoto", "File", &dbv)) {
				DeleteFile(dbv.pwszVal);
				db_free(&dbv);
			}
		}
		db_unset(m_hContact, "ContactPhoto", "Locked");
		db_unset(m_hContact, "ContactPhoto", "Backup");
		db_unset(m_hContact, "ContactPhoto", "RFile");
		db_unset(m_hContact, "ContactPhoto", "File");
		db_unset(m_hContact, "ContactPhoto", "Format");
		DeleteAvatarFromCache(m_hContact, FALSE);
		SendMessage(m_hwnd, DM_SETAVATARNAME, 0, 0);
	}
};

/////////////////////////////////////////////////////////////////////////////////////////
// Protocol avatar dialog

static char* GetSelectedProtocol(HWND hwndDlg)
{
	HWND hwndList = GetDlgItem(hwndDlg, IDC_PROTOCOLS);

	// Get selection
	int iItem = ListView_GetSelectionMark(hwndList);
	if (iItem < 0)
		return nullptr;

	// Get protocol name
	LVITEM item = { 0 };
	item.mask = LVIF_PARAM;
	item.iItem = iItem;
	SendMessage(hwndList, LVM_GETITEMA, 0, (LPARAM)&item);
	return (char*)item.lParam;
}

static void EnableDisableControls(HWND hwndDlg, char *proto)
{
	if (IsDlgButtonChecked(hwndDlg, IDC_PER_PROTO)) {
		if (proto == nullptr) {
			EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGE), FALSE);
			EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE), FALSE);
		}
		else {
			if (!ProtoServiceExists(proto, PS_SETMYAVATAR)) {
				EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGE), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE), FALSE);
			}
			else {
				EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGE), TRUE);

				int width, height;
				SendDlgItemMessage(hwndDlg, IDC_PROTOPIC, AVATAR_GETUSEDSPACE, (WPARAM)&width, (LPARAM)&height);
				EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE), (LPARAM)width != 0 || height != 0);
			}
		}
	}
	else {
		EnableWindow(GetDlgItem(hwndDlg, IDC_CHANGE), TRUE);

		if (g_plugin.getByte("GlobalUserAvatarNotConsistent", 1))
			EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE), TRUE);
		else {
			int width, height;
			SendDlgItemMessage(hwndDlg, IDC_PROTOPIC, AVATAR_GETUSEDSPACE, (WPARAM)&width, (LPARAM)&height);
			EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE), (LPARAM)width != 0 || height != 0);
		}
	}
}

static void OffsetWindow(HWND parent, HWND hwnd, int dx, int dy)
{
	RECT rc;
	GetWindowRect(hwnd, &rc);
	ScreenToClient(parent, &rc);
	OffsetRect(&rc, dx, dy);
	MoveWindow(hwnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

static void EnableDisableProtocols(HWND hwndDlg, BOOL init)
{
	int diff = 147; // Pre-calc
	BOOL perProto = IsDlgButtonChecked(hwndDlg, IDC_PER_PROTO);
	HWND hwndList = GetDlgItem(hwndDlg, IDC_PROTOCOLS);

	if (perProto) {
		if (!init && !IsWindowVisible(hwndList)) {
			// Show list of protocols
			ShowWindow(hwndList, SW_SHOW);

			// Move controls
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_PROTOPIC), diff, 0);
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_CHANGE), diff, 0);
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_DELETE), diff, 0);
		}

		char * proto = GetSelectedProtocol(hwndDlg);
		if (proto == nullptr) {
			ListView_SetItemState(hwndList, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x0F);
		}
		else {
			SendDlgItemMessage(hwndDlg, IDC_PROTOPIC, AVATAR_SETPROTOCOL, 0, (LPARAM)proto);
			EnableDisableControls(hwndDlg, proto);
		}
	}
	else {
		if (init || IsWindowVisible(hwndList)) {
			// Show list of protocols
			ShowWindow(hwndList, SW_HIDE);

			// Move controls
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_PROTOPIC), -diff, 0);
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_CHANGE), -diff, 0);
			OffsetWindow(hwndDlg, GetDlgItem(hwndDlg, IDC_DELETE), -diff, 0);
		}

		SendDlgItemMessage(hwndDlg, IDC_PROTOPIC, AVATAR_SETPROTOCOL, 0, NULL);
	}
}

class AvatarProtoInfoDlg : public CUserInfoPageDlg
{
	CCtrlCheck chkPerProto;
	CCtrlButton btnChange, btnDelete;
	CCtrlListView protocols;

public:
	AvatarProtoInfoDlg() :
		CUserInfoPageDlg(g_plugin, IDD_PROTO_AVATARS),
		protocols(this, IDC_PROTOCOLS),
		btnChange(this, IDC_CHANGE),
		btnDelete(this, IDC_DELETE),
		chkPerProto(this, IDC_PER_PROTO)
	{
		btnChange.OnClick = Callback(this, &AvatarProtoInfoDlg::onClick_Change);
		btnDelete.OnClick = Callback(this, &AvatarProtoInfoDlg::onClick_Delete);

		chkPerProto.OnChange = Callback(this, &AvatarProtoInfoDlg::onChange_PerProto);
	}

	bool OnInitDialog() override
	{
		HWND protopic = GetDlgItem(m_hwnd, IDC_PROTOPIC);
		SendMessage(protopic, AVATAR_SETAVATARBORDERCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNSHADOW));
		SendMessage(protopic, AVATAR_SETNOAVATARTEXT, 0, (LPARAM)LPGENW("No avatar"));
		SendMessage(protopic, AVATAR_SETRESIZEIFSMALLER, 0, (LPARAM)FALSE);

		protocols.SetExtendedListViewStyleEx(0, LVS_EX_SUBITEMIMAGES);

		HIMAGELIST hIml = ImageList_Create(16, 16, ILC_MASK | ILC_COLOR32, 4, 0);
		protocols.SetImageList(hIml, LVSIL_SMALL);

		LVCOLUMN lvc = { 0 };
		lvc.mask = LVCF_FMT;
		lvc.fmt = LVCFMT_IMAGE | LVCFMT_LEFT;
		protocols.InsertColumn(0, &lvc);

		LVITEM item = { 0 };
		item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
		item.iItem = 1000;

		// List protocols
		int num = 0;
		for (auto &it : Accounts()) {
			if (!ProtoServiceExists(it->szModuleName, PS_GETMYAVATAR))
				continue;

			if (!Proto_IsAvatarsEnabled(it->szModuleName))
				continue;

			ImageList_AddIcon(hIml, Skin_LoadProtoIcon(it->szModuleName, ID_STATUS_ONLINE));
			item.pszText = it->tszAccountName;
			item.iImage = num;
			item.lParam = (LPARAM)it->szModuleName;
			protocols.InsertItem(&item);
			num++;
		}

		protocols.SetColumnWidth(0, LVSCW_AUTOSIZE);
		protocols.Arrange(LVA_ALIGNLEFT | LVA_ALIGNTOP);

		// Check if should show per protocol avatars
		CheckDlgButton(m_hwnd, IDC_PER_PROTO, g_plugin.getByte("PerProtocolUserAvatars", 1) ? BST_CHECKED : BST_UNCHECKED);
		EnableDisableProtocols(m_hwnd, TRUE);
		return true;
	}

	void onItemChanged_List(CCtrlListView::TEventInfo *ev)
	{
		LPNMLISTVIEW li = ev->nmlv;
		if (li->uNewState & LVIS_SELECTED) {
			SendDlgItemMessage(m_hwnd, IDC_PROTOPIC, AVATAR_SETPROTOCOL, 0, li->lParam);
			EnableDisableControls(m_hwnd, (char*)li->lParam);
		}
	}

	INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) override
	{
		if (msg == WM_NOTIFY) {
			LPNMHDR hdr = (LPNMHDR)lParam;
			if (hdr->idFrom == IDC_PROTOPIC && hdr->code == NM_AVATAR_CHANGED)
				EnableDisableControls(m_hwnd, GetSelectedProtocol(m_hwnd));
		}
		return CDlgBase::DlgProc(msg, wParam, lParam);
	}

	void onClick_Change(CCtrlButton *)
	{
		if (BST_UNCHECKED == IsDlgButtonChecked(m_hwnd, IDC_PER_PROTO))
			SetMyAvatar(NULL, NULL);
		else {
			char *proto = GetSelectedProtocol(m_hwnd);
			if (proto != nullptr)
				SetMyAvatar((WPARAM)proto, NULL);
		}
	}

	void onClick_Delete(CCtrlButton *)
	{
		if (BST_UNCHECKED == IsDlgButtonChecked(m_hwnd, IDC_PER_PROTO)) {
			if (MessageBox(m_hwnd, TranslateT("Are you sure you want to remove your avatar?"), TranslateT("Global avatar"), MB_YESNO) == IDYES)
				SetMyAvatar(NULL, (LPARAM)L"");
		}
		else {
			if (char *proto = GetSelectedProtocol(m_hwnd)) {
				char description[256];
				CallProtoService(proto, PS_GETNAME, _countof(description), (LPARAM)description);
				wchar_t *descr = mir_a2u(description);
				if (MessageBox(m_hwnd, TranslateT("Are you sure you want to remove your avatar?"), descr, MB_YESNO) == IDYES)
					SetMyAvatar((WPARAM)proto, (LPARAM)L"");
				mir_free(descr);
			}
		}
	}

	void onChange_PerProto(CCtrlCheck*)
	{
		g_plugin.setByte("PerProtocolUserAvatars", IsDlgButtonChecked(m_hwnd, IDC_PER_PROTO) ? 1 : 0);
		EnableDisableProtocols(m_hwnd, FALSE);
	}
};

int OnDetailsInit(WPARAM wParam, LPARAM hContact)
{
	USERINFOPAGE uip = {};
	uip.szTitle.a = LPGEN("Avatar");

	if (hContact == NULL) {
		// User dialog
		uip.pDialog = new AvatarProtoInfoDlg();
		g_plugin.addUserInfo(wParam, &uip);
	}
	else {
		char *szProto = Proto_GetBaseAccountName(hContact);
		if (szProto == nullptr || g_plugin.getByte(szProto, 1)) {
			// Contact dialog
			uip.flags = ODPF_ICON;
			uip.position = -2000000000;
			uip.pDialog = new AvatarUserInfoDlg();
			uip.dwInitParam = (LPARAM)g_plugin.getIconHandle(IDI_AVATAR);
			g_plugin.addUserInfo(wParam, &uip);
		}
	}
	return 0;
}
