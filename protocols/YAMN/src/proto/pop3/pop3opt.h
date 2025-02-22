#ifndef __OPTIONS_H
#define __OPTIONS_H

#define M_SHOWACTUAL	0
#define M_SHOWDEFAULT	1

// Enables account in options
BOOL DlgEnableAccount(HWND hDlg,WPARAM wParam,LPARAM lParam);

// Sets dialog controls to match current account
BOOL DlgShowAccount(HWND hDlg,WPARAM wParam,LPARAM lParam);

// Sets colors to match colors of actual account 
BOOL DlgShowAccountColors(HWND hDlg,WPARAM wParam,LPARAM lParam);

// Initializes POP3 options for Miranda
int POP3OptInit(WPARAM wParam,LPARAM lParam);

//Sets dialog item text
BOOL DlgSetItemText(HWND hDlg,WPARAM wParam,const char*);
BOOL DlgSetItemTextW(HWND hDlg,WPARAM wParam,const wchar_t*);


#define DlgSetItemTextT DlgSetItemTextW


#endif
