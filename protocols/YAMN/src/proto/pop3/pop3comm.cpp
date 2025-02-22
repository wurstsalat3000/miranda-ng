/*
 * This code implements POP3 server checking for new mail and so on.
 * There's function SynchroPOP3 in this file- for checking and synchronising POP3 account
 * and DeleteMailsPOP3- for deleting mails from POP3 server
 *
 * Note this file acts as main file for internal plugin.
 *
 * (c) majvan 2002-2004
 * 18/08
 */


#include "../../stdafx.h"

#define ERRORSTR_MAXLEN	1024	//in wide-chars

//--------------------------------------------------------------------------------------------------

HANDLE hNetLib = nullptr;
PSCOUNTER CPOP3Account::AccountWriterSO = nullptr;

//Creates new CPOP3Account structure
CAccount *WINAPI CreatePOP3Account(HYAMNPROTOPLUGIN Plugin, DWORD CAccountVersion);

//Deletes CPOP3Account structure
void WINAPI DeletePOP3Account(CAccount *Which);

//Sets stop flag to account
void WINAPI StopPOP3Account(CAccount *Which);

//Function registers standard functions for YAMN
int RegisterPOP3Plugin(WPARAM, LPARAM);

//Unloads all variables created on heap (delete[])
DWORD WINAPI UnLoadPOP3(void *);

//Function writes POP3 accounts using YAMN exported functions
DWORD WINAPI WritePOP3Accounts();

//Function stores plugin's data for account to file
DWORD WINAPI WritePOP3Options(HANDLE, CAccount *);

//Function reads plugin's data for account from file
DWORD WINAPI ReadPOP3Options(CAccount *, char **, char *);

//Creates new mail for an account
HYAMNMAIL WINAPI CreatePOP3Mail(CAccount *Account, DWORD CMimeMailVersion);

//Function does all needed work when connection failed or any error occured
//Creates structure containing error code, closes internet session, runs "bad connect" function
static void PostErrorProc(HPOP3ACCOUNT ActualAccount, void *ParamToBadConnect, uint32_t POP3PluginParam, BOOL UseSSL);

//Checks POP3 account and stores all info to account. It deletes old mails=> synchro
// WhichTemp- pointer to strucure containing needed information
DWORD WINAPI SynchroPOP3(struct CheckParam *WhichTemp);

//Deletes mails from POP3 server
// WhichTemp- structure containing needed information (queued messages to delete)
//Function deletes from memory queue in WhichTemp structure
void __cdecl DeleteMailsPOP3(void *param);

//Function makes readable message about error. It sends it back to YAMN, so YAMN then
//can show it to the message window
wchar_t* WINAPI GetErrorString(DWORD Code);

//Function deletes string allocated in GetErrorString
void WINAPI DeleteErrorString(LPVOID String);

//Extracts info from result of POP3's STAT command
// stream- source string
// len- length of source string
// mboxsize- adreess to integer, that receives size of mailbox
// mails- adreess to integer, that receives number of mails
void ExtractStat(char *stream, int *mboxsize, int *mails);

//Extracts mail ID on mailbox
// stream- source string
// len- length of source string
// queue- address of first message, where first ID will be stored
void ExtractUIDL(char *stream, int len, HYAMNMAIL queue);

//Extracts mail size on mailbox
// stream- source string
// len- length of source string
// queue- address of first message, where size of message #1 will be stored
void ExtractList(char *stream, int len, HYAMNMAIL queue);

void ExtractMail(char *stream, int len, HYAMNMAIL queue);

YAMNExportedFcns *pYAMNFcn = nullptr;
MailExportedFcns *pYAMNMailFcn = nullptr;

YAMN_PROTOIMPORTFCN POP3ProtocolFunctions =
{
	CreatePOP3Account,
	DeletePOP3Account,
	StopPOP3Account,
	WritePOP3Options,
	ReadPOP3Options,
	SynchroPOP3,
	SynchroPOP3,
	SynchroPOP3,
	DeleteMailsPOP3,
	GetErrorString,
	nullptr,
	DeleteErrorString,
	WritePOP3Accounts,
	nullptr,
	UnLoadPOP3,
};

YAMN_MAILIMPORTFCN POP3MailFunctions =
{
	CreatePOP3Mail,
	nullptr,
	nullptr,
	nullptr,
};

PYAMN_VARIABLES pYAMNVar = nullptr;
HYAMNPROTOPLUGIN POP3Plugin = nullptr;

YAMN_PROTOREGISTRATION POP3ProtocolRegistration =
{
	"POP3 protocol (internal)",
	__VERSION_STRING_DOTS,
	__COPYRIGHT,
	__DESCRIPTION,
	__AUTHORWEB,
};

static wchar_t *FileName = nullptr;

HANDLE RegisterNLClient(char *name);

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

CPOP3Account::CPOP3Account()
{
	//NOTE! This constructor constructs CAccount structure. If your plugin is not internal,
	//you will need these constructors. All you need is in Account.cpp. Just copy to your source code
	//constructor and destructor of CAccount.
	UseInternetFree = CreateEvent(nullptr, FALSE, TRUE, nullptr);
	InternetQueries = new SCOUNTER;
	AbilityFlags = YAMN_ACC_BROWSE | YAMN_ACC_POPUP;

	SetAccountStatus((CAccount *)this, TranslateT("Disconnected"));
}

CPOP3Account::~CPOP3Account()
{
	CloseHandle(UseInternetFree);
	if (InternetQueries != nullptr)
		delete InternetQueries;
}

CAccount *WINAPI CreatePOP3Account(HYAMNPROTOPLUGIN, DWORD)
{
	//First, we should check whether CAccountVersion matches.
	//But this is internal plugin, so YAMN's CAccount structure and our CAccount structure are
	//the same, so we do not need to test version. Otherwise, if CAccount version does not match
	//in your plugin, you should return NULL, like this:
	//	if (CAccountVersion != YAMN_ACCOUNTVERSION) return NULL;

	//Now it is needed to construct our POP3 account and return its handle
	return (CAccount *)new struct CPOP3Account();
}

void WINAPI DeletePOP3Account(CAccount *Which)
{
	delete (HPOP3ACCOUNT)Which;
}

void WINAPI StopPOP3Account(CAccount *Which)
{
	((HPOP3ACCOUNT)Which)->Client.Stopped = TRUE;
	if (((HPOP3ACCOUNT)Which)->Client.NetClient != nullptr)			//we should inform also network client. Usefull only when network client implements this feature
		((HPOP3ACCOUNT)Which)->Client.NetClient->Stopped = TRUE;
}

//This function is like main function for POP3 internal protocol
int RegisterPOP3Plugin(WPARAM, LPARAM)
{

	//Get YAMN variables we can use
	if (nullptr == (pYAMNVar = (PYAMN_VARIABLES)CallService(MS_YAMN_GETVARIABLES, (WPARAM)YAMN_VARIABLESVERSION, 0)))
		return 0;

	//We have to get pointers to YAMN exported functions: allocate structure and fill it
	if (nullptr == (pYAMNFcn = new struct YAMNExportedFcns))
	{
		UnLoadPOP3(nullptr); return 0;
	}

	//Register new pop3 user in netlib
	if (nullptr == (hNetLib = RegisterNLClient("YAMN (POP3)")))
	{
		UnLoadPOP3(nullptr); return 0;
	}

	pYAMNFcn->SetProtocolPluginFcnImportFcn = (YAMN_SETPROTOCOLPLUGINFCNIMPORTFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SETPROTOCOLPLUGINFCNIMPORTID, 0);
	pYAMNFcn->WaitToWriteFcn = (YAMN_WAITTOWRITEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_WAITTOWRITEID, 0);
	pYAMNFcn->WriteDoneFcn = (YAMN_WRITEDONEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_WRITEDONEID, 0);
	pYAMNFcn->WaitToReadFcn = (YAMN_WAITTOREADFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_WAITTOREADID, 0);
	pYAMNFcn->ReadDoneFcn = (YAMN_READDONEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_READDONEID, 0);
	pYAMNFcn->SCGetNumberFcn = (YAMN_SCMANAGEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SCGETNUMBERID, 0);
	pYAMNFcn->SCIncFcn = (YAMN_SCMANAGEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SCINCID, 0);
	pYAMNFcn->SCDecFcn = (YAMN_SCMANAGEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SCDECID, 0);
	pYAMNFcn->SetStatusFcn = (YAMN_SETSTATUSFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SETSTATUSID, 0);
	pYAMNFcn->GetStatusFcn = (YAMN_GETSTATUSFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_GETSTATUSID, 0);

	if (nullptr == (pYAMNMailFcn = new struct MailExportedFcns))
	{
		UnLoadPOP3(nullptr); return 0;
	}

	pYAMNMailFcn->SynchroMessagesFcn = (YAMN_SYNCHROMIMEMSGSFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_SYNCHROMIMEMSGSID, 0);
	pYAMNMailFcn->TranslateHeaderFcn = (YAMN_TRANSLATEHEADERFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_TRANSLATEHEADERID, 0);
	pYAMNMailFcn->AppendQueueFcn = (YAMN_APPENDQUEUEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_APPENDQUEUEID, 0);
	pYAMNMailFcn->DeleteMessagesToEndFcn = (YAMN_DELETEMIMEQUEUEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_DELETEMIMEQUEUEID, 0);
	pYAMNMailFcn->DeleteMessageFromQueueFcn = (YAMN_DELETEMIMEMESSAGEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_DELETEMIMEMESSAGEID, 0);
	pYAMNMailFcn->FindMessageByIDFcn = (YAMN_FINDMIMEMESSAGEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_FINDMIMEMESSAGEID, 0);
	pYAMNMailFcn->CreateNewDeleteQueueFcn = (YAMN_CREATENEWDELETEQUEUEFCN)CallService(MS_YAMN_GETFCNPTR, (WPARAM)YAMN_CREATENEWDELETEQUEUEID, 0);

	//set static variable
	if (CPOP3Account::AccountWriterSO == nullptr) {
		if (nullptr == (CPOP3Account::AccountWriterSO = new SCOUNTER))
		{
			UnLoadPOP3(nullptr); return 0;
		}
	}

	//First, we register this plugin
	//it is quite impossible this function returns zero (failure) as YAMN and internal plugin structre versions are the same
	POP3ProtocolRegistration.Name = Translate("POP3 protocol (internal)");
	POP3ProtocolRegistration.Description = Translate(__DESCRIPTION);
	if (nullptr == (POP3Plugin = (HYAMNPROTOPLUGIN)CallService(MS_YAMN_REGISTERPROTOPLUGIN, (WPARAM)&POP3ProtocolRegistration, (LPARAM)YAMN_PROTOREGISTRATIONVERSION)))
		return 0;

	//Next we set our imported functions for YAMN
	if (!SetProtocolPluginFcnImport(POP3Plugin, &POP3ProtocolFunctions, YAMN_PROTOIMPORTFCNVERSION, &POP3MailFunctions, YAMN_MAILIMPORTFCNVERSION))
		return 0;

	//Then, we read all mails for accounts.
	//You must first register account, before using this function as YAMN must use CreatePOP3Account function to add new accounts
	//But if CreatePOP3Account is not implemented (equals to NULL), YAMN creates account as YAMN's standard CAccount *
	if (FileName) CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0);	//shoud not happen (only for secure)
	FileName = (wchar_t *)CallService(MS_YAMN_GETFILENAME, (WPARAM)L"pop3", 0);

	switch (CallService(MS_YAMN_READACCOUNTS, (WPARAM)POP3Plugin, (LPARAM)FileName)) {
	case EACC_FILEVERSION:
		MessageBox(nullptr, TranslateT("Found new version of account book, not compatible with this version of YAMN."), TranslateT("YAMN (internal POP3) read error"), MB_OK);
		CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0);
		FileName = nullptr;
		return 0;
	case EACC_FILECOMPATIBILITY:
		MessageBox(nullptr, TranslateT("Error reading account file. Account file corrupted."), TranslateT("YAMN (internal POP3) read error"), MB_OK);
		CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0);
		FileName = nullptr;
		return 0;
	case EACC_ALLOC:
		MessageBox(nullptr, TranslateT("Memory allocation error while data reading"), TranslateT("YAMN (internal POP3) read error"), MB_OK);
		CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0);
		FileName = nullptr;
		return 0;
	case EACC_SYSTEM:
		if (ERROR_FILE_NOT_FOUND != GetLastError())
		{
			wchar_t temp[1024] = { 0 };
			mir_snwprintf(temp, L"%s\n%s", TranslateT("Reading file error. File already in use?"), FileName);
			MessageBox(nullptr, temp, TranslateT("YAMN (internal POP3) read error"), MB_OK);
			CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0);
			FileName = nullptr;
			return 0;
		}
		break;
	}

	CAccount *Finder;
	DBVARIANT dbv;

	for (Finder = POP3Plugin->FirstAccount; Finder != nullptr; Finder = Finder->Next) {
		Finder->hContact = NULL;
		for (auto &hContact : Contacts(YAMN_DBMODULE)) {
			if (!g_plugin.getString(hContact, "Id", &dbv)) {
				if (mir_strcmp(dbv.pszVal, Finder->Name) == 0) {
					Finder->hContact = hContact;
					g_plugin.setWord(Finder->hContact, "Status", ID_STATUS_ONLINE);
					db_set_s(Finder->hContact, "CList", "StatusMsg", Translate("No new mail message"));
					if ((Finder->Flags & YAMN_ACC_ENA) && (Finder->NewMailN.Flags & YAMN_ACC_CONT))
						Contact::Hide(Finder->hContact, false);

					if (!(Finder->Flags & YAMN_ACC_ENA) || !(Finder->NewMailN.Flags & YAMN_ACC_CONT))
						Contact::Hide(Finder->hContact);
				}
				db_free(&dbv);
			}
		}

		if (!Finder->hContact && (Finder->Flags & YAMN_ACC_ENA) && (Finder->NewMailN.Flags & YAMN_ACC_CONT)) {
			// No account contact found, have to create one
			Finder->hContact = db_add_contact();
			Proto_AddToContact(Finder->hContact, YAMN_DBMODULE);
			g_plugin.setString(Finder->hContact, "Id", Finder->Name);
			g_plugin.setString(Finder->hContact, "Nick", Finder->Name);
			g_plugin.setWord(Finder->hContact, "Status", YAMN_STATUS);
		}
	}

	return 0;
}

DWORD WINAPI UnLoadPOP3(void *)
{
	//pYAMNVar is only a pointr, no need delete or free
	if (hNetLib) {
		Netlib_CloseHandle(hNetLib); hNetLib = nullptr;
	}
	if (CPOP3Account::AccountWriterSO) {
		delete CPOP3Account::AccountWriterSO; CPOP3Account::AccountWriterSO = nullptr;
	}
	if (pYAMNMailFcn) {
		delete pYAMNMailFcn; pYAMNMailFcn = nullptr;
	}
	if (pYAMNFcn) {
		delete pYAMNFcn; pYAMNFcn = nullptr;
	}
	if (FileName) {
		CallService(MS_YAMN_DELETEFILENAME, (WPARAM)FileName, 0); FileName = nullptr;
	}

#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"UnLoadPOP3:done\n");
#endif
	return 1;
}

DWORD WINAPI WritePOP3Accounts()
{
	uint32_t ReturnValue = CallService(MS_YAMN_WRITEACCOUNTS, (WPARAM)POP3Plugin, (LPARAM)FileName);
	if (ReturnValue == EACC_SYSTEM) {
		wchar_t temp[1024] = { 0 };
		mir_snwprintf(temp, L"%s\n%s", TranslateT("Error while copying data to disk occurred. Is file in use?"), FileName);
		MessageBox(nullptr, temp, TranslateT("POP3 plugin - write file error"), MB_OK);
	}

	return ReturnValue;
}

DWORD WINAPI WritePOP3Options(HANDLE File, CAccount *Which)
{
	DWORD WrittenBytes;
	uint32_t Ver = POP3_FILEVERSION;

	if ((!WriteFile(File, (char *)&Ver, sizeof(uint32_t), &WrittenBytes, nullptr)) ||
		(!WriteFile(File, (char *)&((HPOP3ACCOUNT)Which)->CP, sizeof(uint16_t), &WrittenBytes, nullptr)))
		return EACC_SYSTEM;
	return 0;
}

DWORD WINAPI ReadPOP3Options(CAccount *Which, char **Parser, char *End)
{
	uint32_t Ver;
#ifdef DEBUG_FILEREAD
	wchar_t Debug[256];
#endif
	Ver = *(uint32_t *)(*Parser);
	(*Parser) += sizeof(uint32_t);
	if (*Parser >= End)
		return EACC_FILECOMPATIBILITY;
	if (Ver != POP3_FILEVERSION)
		return EACC_FILECOMPATIBILITY;

	((HPOP3ACCOUNT)Which)->CP = *(uint16_t *)(*Parser);
	(*Parser) += sizeof(uint16_t);
	if (*Parser >= End)
		return EACC_FILECOMPATIBILITY;
#ifdef DEBUG_FILEREAD
	mir_snwprintf(Debug, L"CodePage: %d, remaining %d chars", ((HPOP3ACCOUNT)Which)->CP, End-*Parser);
	MessageBox(NULL,Debug,L"debug",MB_OK);
#endif
	return 0;
}

HYAMNMAIL WINAPI CreatePOP3Mail(CAccount *Account, DWORD)
{
	HYAMNMAIL NewMail;
	//First, we should check whether MAILDATA matches.
	//But this is internal plugin, so YAMN's MAILDATA structure and our MAILDATA structure are
	//the same, so we do not need to test version. Otherwise, if MAILDATA version does not match
	//in your plugin, you should return NULL, like this:
	//	if (MailDataVersion != YAMN_MAILDATAVERSION) return NULL;

	//Now it is needed to construct our POP3 account and return its handle
	if (nullptr == (NewMail = new YAMNMAIL))
		return nullptr;

	if (nullptr == (NewMail->MailData = new CMailData()))
	{
		delete NewMail;
		return nullptr;
	}
	NewMail->MailData->CP = ((HPOP3ACCOUNT)Account)->CP;
	return (HYAMNMAIL)NewMail;
}

static void SetContactStatus(CAccount *account, int status)
{
	if ((account->hContact) && (account->NewMailN.Flags & YAMN_ACC_CONT))
		g_plugin.setWord(account->hContact, "Status", status);
}

static void PostErrorProc(HPOP3ACCOUNT ActualAccount, void *ParamToBadConnection, uint32_t POP3PluginParam, BOOL UseSSL)
{
	char *DataRX;

	//We create new structure, that we pass to bad connection dialog procedure. This procedure next calls YAMN imported fuction
	//from POP3 protocol to determine the description of error. We can describe error from our error code structure, because later,
	//when YAMN calls our function, it passes us our error code. This is pointer to structure for POP3 protocol in fact. 
	PPOP3_ERRORCODE ErrorCode;

	//We store status before we do Quit(), because quit can destroy our errorcode status
	if (nullptr != (ErrorCode = new POP3_ERRORCODE))
	{
		ErrorCode->SSL = UseSSL;
		ErrorCode->AppError = ActualAccount->SystemError;
		ErrorCode->POP3Error = ActualAccount->Client.POP3Error;
		ErrorCode->NetError = ActualAccount->Client.NetClient->NetworkError;
		ErrorCode->SystemError = ActualAccount->Client.NetClient->SystemError;
	}

	if (POP3PluginParam == (uint32_t)NULL)		//if it was normal YAMN call (force check or so on)
	{
		try
		{
			DataRX = ActualAccount->Client.Quit();
			if (DataRX != nullptr)
				free(DataRX);
		}
		catch (...)
		{
		}
		//We always close connection if error occured
		try
		{
			ActualAccount->Client.NetClient->Disconnect();
		}
		catch (...)
		{
		}

		SetAccountStatus(ActualAccount, TranslateT("Disconnected"));

		//If we cannot allocate memory, do nothing
		if (ErrorCode == nullptr)
		{
			SetEvent(ActualAccount->UseInternetFree);
			return;
		}
	}
	else						//else it was called from POP3 plugin, probably error when deleting old mail (POP3 synchro calls POP3 delete)
		if (ErrorCode == nullptr)
			return;

	if ((ActualAccount->BadConnectN.Flags & YAMN_ACC_MSG) || (ActualAccount->BadConnectN.Flags & YAMN_ACC_ICO) || (ActualAccount->BadConnectN.Flags & YAMN_ACC_POP))
	{
		YAMN_BADCONNECTIONPARAM cp = { (HANDLE)nullptr, ActualAccount, (UINT_PTR)ErrorCode, ParamToBadConnection };

		CallService(MS_YAMN_BADCONNECTION, (WPARAM)&cp, (LPARAM)YAMN_BADCONNECTIONVERSION);
	}
	if (POP3PluginParam == (uint32_t)NULL)		//if it was normal YAMN call
		SetEvent(ActualAccount->UseInternetFree);
}

//Checks POP3 account and synchronizes it
DWORD WINAPI SynchroPOP3(struct CheckParam * WhichTemp)
{
	HPOP3ACCOUNT ActualAccount;
	CPop3Client *MyClient;
	HYAMNMAIL NewMails = nullptr, MsgQueuePtr = nullptr;
	char* DataRX = nullptr, *Temp;
	int mboxsize, msgs, i;
	SYSTEMTIME now;
	LPVOID YAMNParam;
	uint32_t CheckFlags;
	BOOL UsingInternet = FALSE;
	struct {
		char *ServerName;
		uint32_t ServerPort;
		char *ServerLogin;
		char *ServerPasswd;
		uint32_t Flags;
		uint32_t NFlags;
		uint32_t NNFlags;
	} ActualCopied;

	//First, we should compare our version of CheckParam structure, but here it is not needed, because YAMN and internal plugin
	//have the same version. But your plugin should do that in this way:
	//	if (((struct CheckParam *)WhichTemp)->Ver != YAMN_CHECKVERSION)
	//	{
	//		SetEvent(((struct CheckParam *)WhichTemp)->ThreadRunningEV);				//don't forget to unblock YAMN
	//		return (uint32_t)-1;									//ok, but we should return value.
	//			//When our plugin returns e.g. 0xFFFFFFFF (=-1, this is only our plugin value, YAMN does nothing with return value,
	//			//but only tests if it is nonzero. If yes, it calls GetErrorStringFcn. We know problem occured in YAMN incompatibility
	//			//and then we can in our GetErrorStringFcn e.g. return string "Uncompatible version of YAMN".
	//	}

	ActualAccount = (HPOP3ACCOUNT)WhichTemp->AccountParam;			//copy address of structure from calling thread to stack of this thread
	YAMNParam = WhichTemp->BrowserParam;
	CheckFlags = WhichTemp->Flags;
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:Incrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
	SCInc(ActualAccount->UsingThreads);
	//Unblock YAMN, signal that we have copied all parameters from YAMN thread stack
	if (INVALID_HANDLE_VALUE != WhichTemp->ThreadRunningEV)
		SetEvent(WhichTemp->ThreadRunningEV);

#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:ActualAccountSO-read wait\n");
#endif
	if (WAIT_OBJECT_0 != WaitToRead(ActualAccount))
	{
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:ActualAccountSO-read wait failed\n");
#endif
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
		SCDec(ActualAccount->UsingThreads);
		return 0;
	}
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:ActualAccountSO-read enter\n");
#endif
	MyClient = &ActualAccount->Client;
	//Now, copy all needed information about account to local variables, so ActualAccount is not blocked in read mode during all connection process, which can last for several minutes.
	ActualCopied.ServerName = _strdup(ActualAccount->Server->Name);
	ActualCopied.ServerPort = ActualAccount->Server->Port;
	ActualCopied.Flags = ActualAccount->Flags;
	ActualCopied.ServerLogin = _strdup(ActualAccount->Server->Login);
	ActualCopied.ServerPasswd = _strdup(ActualAccount->Server->Passwd);
	ActualCopied.NFlags = ActualAccount->NewMailN.Flags;
	ActualCopied.NNFlags = ActualAccount->NoNewMailN.Flags;

#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:ActualAccountSO-read done\n");
#endif
	ReadDone(ActualAccount);

	SCInc(ActualAccount->InternetQueries);				//increment counter, that there is one more thread waiting for connection
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:InternetFreeEV-wait\n");
#endif
	WaitForSingleObject(ActualAccount->UseInternetFree, INFINITE);	//wait until we can use connection
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:InternetFreeEV-enter\n");
#endif
	SCDec(ActualAccount->InternetQueries);

	//OK, we enter the "use internet" section. But after we start communication, we can test if we did not enter the "use internet" section only for the reason,
	//that previous thread release the internet section because this account has stop signal (we stop account and there are 2 threads: one communicating,
	//the second one waiting for network access- the first one ends because we want to stop account, this one is released, but should be stopped as well).
	if (!ActualAccount->AbleToWork)
	{
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:stop signal-InternetFreeEV-done\n");
#endif
		SetEvent(ActualAccount->UseInternetFree);
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:stop signal-Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
		SCDec(ActualAccount->UsingThreads);
		return 0;
	}
	UsingInternet = TRUE;

	GetLocalTime(&now);
	ActualAccount->SystemError = 0;					//now we can use internet for this socket. First, clear errorcode.
	try
	{
		SetContactStatus(ActualAccount, ID_STATUS_OCCUPIED);
#ifdef DEBUG_COMM
		DebugLog(CommFile,"<--------Communication-------->\n");
#endif
		//	if we are already connected, we have open session (another thread left us open session), so we don't need to login
		//	note that connected state without logging cannot occur, because if we close session, we always close socket too (we must close socket is the right word :))
		if ((MyClient->NetClient == nullptr) || !MyClient->NetClient->Connected())
		{
			SetAccountStatus(ActualAccount, TranslateT("Connecting to server"));

			DataRX = MyClient->Connect(ActualCopied.ServerName, ActualCopied.ServerPort, ActualCopied.Flags & YAMN_ACC_SSL23, ActualCopied.Flags & YAMN_ACC_NOTLS);
			char *timestamp = nullptr;

			if (DataRX != nullptr)
			{
				if (ActualCopied.Flags & YAMN_ACC_APOP)
				{
					char *lpos = strchr(DataRX, '<');
					char *rpos = strchr(DataRX, '>');
					if (lpos && rpos && rpos > lpos) {
						int sz = (int)(rpos - lpos + 2);
						timestamp = new char[sz];
						memcpy(timestamp, lpos, sz - 1);
						timestamp[sz - 1] = '\0';
					}
				}
				free(DataRX);
				DataRX = nullptr;
			}

			SetAccountStatus(ActualAccount, TranslateT("Entering POP3 account"));

			if (ActualCopied.Flags & YAMN_ACC_APOP)
			{
				DataRX = MyClient->APOP(ActualCopied.ServerLogin, ActualCopied.ServerPasswd, timestamp);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				delete[] timestamp;
			}
			else {
				DataRX = MyClient->User(ActualCopied.ServerLogin);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				DataRX = MyClient->Pass(ActualCopied.ServerPasswd);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
			}
		}
		SetAccountStatus(ActualAccount, TranslateT("Searching for new mail message"));

		DataRX = MyClient->Stat();

#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<--------Account checking-------->\n");
		DebugLog(DecodeFile,"<Extracting stat>\n");
#endif
		ExtractStat(DataRX, &mboxsize, &msgs);
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<MailBoxSize>%d</MailBoxSize>\n",mboxsize);
		DebugLog(DecodeFile,"<Msgs>%d</Msgs>\n",msgs);
		DebugLog(DecodeFile,"</Extracting stat>\n");
#endif
		if (DataRX != nullptr)
			free(DataRX);
		DataRX = nullptr;
		for (i = 0; i < msgs; i++)
		{
			if (!i)
				MsgQueuePtr = NewMails = (HYAMNMAIL)CallService(MS_YAMN_CREATEACCOUNTMAIL, (WPARAM)ActualAccount, (LPARAM)YAMN_MAILVERSION);
			else
			{
				MsgQueuePtr->Next = (HYAMNMAIL)CallService(MS_YAMN_CREATEACCOUNTMAIL, (WPARAM)ActualAccount, (LPARAM)YAMN_MAILVERSION);
				MsgQueuePtr = MsgQueuePtr->Next;
			}
			if (MsgQueuePtr == nullptr)
			{
				ActualAccount->SystemError = EPOP3_QUEUEALLOC;
				throw (uint32_t)ActualAccount->SystemError;
			}
		}

		if (msgs)
		{
			DataRX = MyClient->List();
#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"<Extracting list>\n");
#endif
			ExtractList(DataRX, MyClient->NetClient->Rcv, NewMails);
#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"</Extracting list>\n");
#endif
			if (DataRX != nullptr)
				free(DataRX);
			DataRX = nullptr;

#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"<Extracting UIDL>\n");
#endif
			DataRX = MyClient->Uidl();
			ExtractUIDL(DataRX, MyClient->NetClient->Rcv, NewMails);
#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"</Extracting UIDL>\n");
#endif
			if (DataRX != nullptr)
				free(DataRX);
			DataRX = nullptr;
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait\n");
#endif
		if (WAIT_OBJECT_0 != MsgsWaitToWrite(ActualAccount))
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait failed\n");
#endif
			throw (uint32_t)(ActualAccount->SystemError = EACC_STOPPED);
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write enter\n");
#endif
		ActualAccount->LastChecked = now;
		for (MsgQueuePtr = (HYAMNMAIL)ActualAccount->Mails; MsgQueuePtr != nullptr; MsgQueuePtr = MsgQueuePtr->Next) {
			if (MsgQueuePtr->Flags&YAMN_MSG_BODYREQUESTED) {
				HYAMNMAIL NewMsgsPtr = nullptr;
				for (NewMsgsPtr = (HYAMNMAIL)NewMails; NewMsgsPtr != nullptr; NewMsgsPtr = NewMsgsPtr->Next) {
					if (!mir_strcmp(MsgQueuePtr->ID, NewMsgsPtr->ID)) {
						wchar_t accstatus[512];
						mir_snwprintf(accstatus, TranslateT("Reading body %s"), NewMsgsPtr->ID);
						SetAccountStatus(ActualAccount, accstatus);
						DataRX = MyClient->Top(MsgQueuePtr->Number, 100);
#ifdef DEBUG_DECODE
						DebugLog(DecodeFile,"<Reading body>\n");
						DebugLog(DecodeFile,"<Header>%s</Header>\n",DataRX);
#endif
						if (DataRX != nullptr)
						{
							Temp = DataRX;
							while ((Temp < DataRX + MyClient->NetClient->Rcv) && (WS(Temp) || ENDLINE(Temp))) Temp++;

							if (OKLINE(DataRX))
								for (Temp = DataRX; (Temp < DataRX + MyClient->NetClient->Rcv) && (!ENDLINE(Temp)); Temp++);
							while ((Temp < DataRX + MyClient->NetClient->Rcv) && ENDLINE(Temp)) Temp++;
						}
						else
							continue;
						//delete all the headers of the old mail MsgQueuePtr->MailData->TranslatedHeader
						struct CMimeItem *TH = MsgQueuePtr->MailData->TranslatedHeader;
						if (TH) for (; MsgQueuePtr->MailData->TranslatedHeader != nullptr;)
						{
							TH = TH->Next;
							if (MsgQueuePtr->MailData->TranslatedHeader->name != nullptr)
								delete[] MsgQueuePtr->MailData->TranslatedHeader->name;
							if (MsgQueuePtr->MailData->TranslatedHeader->value != nullptr)
								delete[] MsgQueuePtr->MailData->TranslatedHeader->value;
							delete MsgQueuePtr->MailData->TranslatedHeader;
							MsgQueuePtr->MailData->TranslatedHeader = TH;
						}

						TranslateHeader(Temp, MyClient->NetClient->Rcv - (Temp - DataRX), &MsgQueuePtr->MailData->TranslatedHeader);


#ifdef DEBUG_DECODE
						DebugLog(DecodeFile,"</Reading body>\n");
#endif
						MsgQueuePtr->Flags |= YAMN_MSG_BODYRECEIVED;

						if (DataRX != nullptr)
							free(DataRX);
						DataRX = nullptr;
						break;
					}
				}
			}
		}

		SynchroMessages(ActualAccount, (HYAMNMAIL *)&ActualAccount->Mails, nullptr, (HYAMNMAIL *)&NewMails, nullptr);		//we get only new mails on server!
		//		NewMails=NULL;

#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write done\n");
#endif
		MsgsWriteDone(ActualAccount);
		for (MsgQueuePtr = (HYAMNMAIL)ActualAccount->Mails; MsgQueuePtr != nullptr; MsgQueuePtr = MsgQueuePtr->Next) {
			if ((MsgQueuePtr->Flags&YAMN_MSG_BODYREQUESTED) && (MsgQueuePtr->Flags&YAMN_MSG_BODYRECEIVED)) {
				MsgQueuePtr->Flags &= ~YAMN_MSG_BODYREQUESTED;
				if (MsgQueuePtr->MsgWindow)
					SendMessage(MsgQueuePtr->MsgWindow, WM_YAMN_CHANGECONTENT, 0, 0);
			}
		}

		for (msgs = 0, MsgQueuePtr = NewMails; MsgQueuePtr != nullptr; MsgQueuePtr = MsgQueuePtr->Next, msgs++);			//get number of new mails

		try
		{
			wchar_t accstatus[512];

			for (i = 0, MsgQueuePtr = NewMails; MsgQueuePtr != nullptr; i++)
			{
				BOOL autoretr = (ActualAccount->Flags & YAMN_ACC_BODY) != 0;
				DataRX = MyClient->Top(MsgQueuePtr->Number, autoretr ? 100 : 0);
				mir_snwprintf(accstatus, TranslateT("Reading new mail messages (%d%% done)"), 100 * i / msgs);
				SetAccountStatus(ActualAccount, accstatus);

#ifdef DEBUG_DECODE
				DebugLog(DecodeFile,"<New mail>\n");
				DebugLog(DecodeFile,"<Header>%s</Header>\n",DataRX);
#endif
				if (DataRX != nullptr)
				{
					Temp = DataRX;
					while ((Temp < DataRX + MyClient->NetClient->Rcv) && (WS(Temp) || ENDLINE(Temp))) Temp++;

					if (OKLINE(DataRX))
						for (Temp = DataRX; (Temp < DataRX + MyClient->NetClient->Rcv) && (!ENDLINE(Temp)); Temp++);
					while ((Temp < DataRX + MyClient->NetClient->Rcv) && ENDLINE(Temp)) Temp++;
				}
				else
					continue;

				TranslateHeader(Temp, MyClient->NetClient->Rcv - (Temp - DataRX), &MsgQueuePtr->MailData->TranslatedHeader);


#ifdef DEBUG_DECODE
				DebugLog(DecodeFile,"</New mail>\n");
#endif
				MsgQueuePtr->Flags |= YAMN_MSG_NORMALNEW;
				if (autoretr) MsgQueuePtr->Flags |= YAMN_MSG_BODYRECEIVED;

				//We are going to filter mail. Warning!- we must not be in read access neither write access to mails when calling this service
				//This is done, because the "NewMails" queue is not synchronised. It is because it is new queue. Only this thread uses new queue yet, it is not
				//connected to account mail queue.
				//				CallService(MS_YAMN_FILTERMAIL,(WPARAM)ActualAccount,(LPARAM)MsgQueuePtr);
				FilterMailSvc((WPARAM)ActualAccount, (LPARAM)MsgQueuePtr);

				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;

				//MsgQueuePtr->MailData->Body=MyClient->Retr(MsgQueuePtr->Number);

				MsgQueuePtr = MsgQueuePtr->Next;

			}
#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"</--------Account checking-------->\n");
#endif

#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait\n");
#endif
			if (WAIT_OBJECT_0 != MsgsWaitToWrite(ActualAccount))
			{
#ifdef DEBUG_SYNCHRO
				DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait failed\n");
#endif
				throw (uint32_t)ActualAccount->SystemError == EACC_STOPPED;
			}
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write enter\n");
#endif
			if (ActualAccount->Mails == nullptr)
				ActualAccount->Mails = NewMails;
			else
			{
				ActualAccount->LastMail = ActualAccount->LastChecked;
				AppendQueue((HYAMNMAIL)ActualAccount->Mails, NewMails);
			}
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write done\n");
#endif
			MsgsWriteDone(ActualAccount);

			//	we are going to delete mails having SPAM flag level3 and 4 (see m_mails.h) set
			{
				struct DeleteParam ParamToDeleteMails = { YAMN_DELETEVERSION, INVALID_HANDLE_VALUE, ActualAccount, YAMNParam, (void *)POP3_DELETEFROMCHECK };

				//	Delete mails from server. Here we should not be in write access for account's mails
				DeleteMailsPOP3(&ParamToDeleteMails);
			}

			//	if there is no waiting thread for internet connection close it
			//	else leave connection open
			if (0 == SCGetNumber(ActualAccount->InternetQueries))
			{
				DataRX = MyClient->Quit();
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				MyClient->NetClient->Disconnect();

				SetAccountStatus(ActualAccount, TranslateT("Disconnected"));
			}

			UsingInternet = FALSE;
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:InternetFreeEV-done\n");
#endif
			SetEvent(ActualAccount->UseInternetFree);

			ActualAccount->LastSChecked = ActualAccount->LastChecked;
			ActualAccount->LastSynchronised = ActualAccount->LastChecked;
		}
		catch (...)
		{
			throw;			//go to the main exception handling
		}

		{
			YAMN_MAILBROWSERPARAM Param = { (HANDLE)nullptr, ActualAccount, ActualCopied.NFlags, ActualCopied.NNFlags, YAMNParam };

			if (CheckFlags & YAMN_FORCECHECK)
				Param.nnflags |= YAMN_ACC_POP;				//if force check, show popup anyway and if mailbrowser was opened, do not close
			Param.nnflags |= YAMN_ACC_MSGP; //do not close browser if already open
			CallService(MS_YAMN_MAILBROWSER, (WPARAM)&Param, (LPARAM)YAMN_MAILBROWSERVERSION);
		}
		SetContactStatus(ActualAccount, ActualAccount->isCounting ? ID_STATUS_ONLINE : ID_STATUS_OFFLINE);
	}
#ifdef DEBUG_COMM
	catch(uint32_t ErrorCode)
#else
	catch (uint32_t)
#endif
	{
		if (ActualAccount->Client.POP3Error == EPOP3_STOPPED)
			ActualAccount->SystemError = EACC_STOPPED;
#ifdef DEBUG_COMM
		DebugLog(CommFile,"ERROR: %x\n",ErrorCode);
#endif
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait\n");
#endif
		if (WAIT_OBJECT_0 == MsgsWaitToWrite(ActualAccount))
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write enter\n");
#endif
			ActualAccount->LastChecked = now;
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write done\n");
#endif
			MsgsWriteDone(ActualAccount);
		}
#ifdef DEBUG_SYNCHRO
		else
			DebugLog(SynchroFile,"CheckPOP3:ActualAccountMsgsSO-write wait failed\n");
#endif

		DeleteMIMEQueue(ActualAccount, NewMails);

		if (DataRX != nullptr)
			free(DataRX);
		DataRX = nullptr;
		switch (ActualAccount->SystemError)
		{
		case EACC_QUEUEALLOC:
		case EACC_STOPPED:
			ActualAccount->Client.NetClient->Disconnect();
			break;
		default:
			PostErrorProc(ActualAccount, YAMNParam, (uint32_t)NULL, MyClient->SSL);	//it closes internet connection too
		}

		if (UsingInternet)	//if our thread still uses internet
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:InternetFreeEV-done\n");
#endif
			SetEvent(ActualAccount->UseInternetFree);
		}
		SetContactStatus(ActualAccount, ID_STATUS_NA);
	}
	free(ActualCopied.ServerName);
	free(ActualCopied.ServerLogin);
	free(ActualCopied.ServerPasswd);
#ifdef DEBUG_COMM
	DebugLog(CommFile,"</--------Communication-------->\n");
#endif
	//	WriteAccounts();
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"CheckPOP3:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
	SCDec(ActualAccount->UsingThreads);
	return 0;
}

void __cdecl DeleteMailsPOP3(void *param)
{
	DeleteParam *WhichTemp = (DeleteParam *)param;

	CPop3Client *MyClient;
	HYAMNMAIL DeleteMails, NewMails = nullptr, MsgQueuePtr = nullptr;
	char* DataRX = nullptr;
	int mboxsize = 0, msgs = 0, i;
	BOOL UsingInternet = FALSE;
	struct {
		char *ServerName;
		uint32_t ServerPort;
		char *ServerLogin;
		char *ServerPasswd;
		uint32_t Flags;
		uint32_t NFlags;
		uint32_t NNFlags;
	} ActualCopied;

	//First, we should compare our version of DeleteParam structure, but here it is not needed, because YAMN and internal plugin
	//have the same version. But your plugin should do that in this way:
	//	if (((struct DeleteParam *)WhichTemp)->Ver != YAMN_DELETEVERSION)
	//	{
	//		SetEvent(((struct DeleteParam *)WhichTemp)->ThreadRunningEV);				//don't forget to unblock YAMN
	//		return (uint32_t)-1;									//ok, but we should return value.
	//			//When our plugin returns e.g. 0xFFFFFFFF (this is only our plugin value, YAMN does nothing with return value,
	//			//but only tests if it is nonzero. If yes, it calls GetErrorStringFcn), we know problem occured in YAMN incompatibility
	//			//and then we can in our GetErrorStringFcn e.g. return string "Uncompatible version of YAMN".
	//	}

	HPOP3ACCOUNT ActualAccount = (HPOP3ACCOUNT)WhichTemp->AccountParam;			//copy address of structure from calling thread to stack of this thread
	LPVOID YAMNParam = WhichTemp->BrowserParam;
	UINT_PTR POP3PluginParam = (UINT_PTR)((struct DeleteParam *)WhichTemp)->CustomParam;
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"DeleteMailsPOP3:Incrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
	SCInc(ActualAccount->UsingThreads);
	if (INVALID_HANDLE_VALUE != WhichTemp->ThreadRunningEV)
		SetEvent(WhichTemp->ThreadRunningEV);

#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountSO-read wait\n");
#endif
	if (WAIT_OBJECT_0 != WaitToRead(ActualAccount))
	{
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountSO-read wait failed\n");
#endif
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
		SCDec(ActualAccount->UsingThreads);
		return;
	}
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountSO-read enter\n");
#endif
	if (nullptr == (DeleteMails = (HYAMNMAIL)CreateNewDeleteQueue((HYAMNMAIL)ActualAccount->Mails)))	//if there's no mail for deleting, return
	{
		if (POP3_DELETEFROMCHECK != POP3PluginParam)						//We do not wait for free internet when calling from SynchroPOP3. It is because UseInternetFree is blocked
		{
			YAMN_MAILBROWSERPARAM Param = { (HANDLE)nullptr, ActualAccount, YAMN_ACC_MSGP, YAMN_ACC_MSGP, YAMNParam };		//Just update the window

			CallService(MS_YAMN_MAILBROWSER, (WPARAM)&Param, (LPARAM)YAMN_MAILBROWSERVERSION);
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountSO-read done\n");
#endif
		ReadDone(ActualAccount);
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
		SCDec(ActualAccount->UsingThreads);

		return;
	}
	MyClient = &(ActualAccount->Client);

	//Now, copy all needed information about account to local variables, so ActualAccount is not blocked in read mode during all connection process, which can last for several minutes.
	ActualCopied.ServerName = _strdup(ActualAccount->Server->Name);
	ActualCopied.ServerPort = ActualAccount->Server->Port;
	ActualCopied.Flags = ActualAccount->Flags;
	ActualCopied.ServerLogin = _strdup(ActualAccount->Server->Login);
	ActualCopied.ServerPasswd = _strdup(ActualAccount->Server->Passwd);
	ActualCopied.NFlags = ActualAccount->NewMailN.Flags;
	ActualCopied.NNFlags = ActualAccount->NoNewMailN.Flags;

#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountSO-read done\n");
#endif
	ReadDone(ActualAccount);

	SCInc(ActualAccount->InternetQueries);	//This is POP3-internal SCOUNTER, we set another thread wait for this account to be connected to inet
	if (POP3_DELETEFROMCHECK != POP3PluginParam)	//We do not wait for free internet when calling from SynchroPOP3. It is because UseInternetFree is blocked
	{
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:InternetFreeEV-wait\n");
#endif
		WaitForSingleObject(ActualAccount->UseInternetFree, INFINITE);
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:InternetFreeEV-enter\n");
#endif
	}
	SCDec(ActualAccount->InternetQueries);
	UsingInternet = TRUE;

	try
	{
		SetContactStatus(ActualAccount, ID_STATUS_OCCUPIED);
#ifdef DEBUG_COMM
		DebugLog(CommFile,"<--------Communication-------->\n");
#endif
		if ((MyClient->NetClient == nullptr) || !MyClient->NetClient->Connected())
		{
			SetAccountStatus(ActualAccount, TranslateT("Connecting to server"));

			DataRX = MyClient->Connect(ActualCopied.ServerName, ActualCopied.ServerPort, ActualCopied.Flags & YAMN_ACC_SSL23, ActualCopied.Flags & YAMN_ACC_NOTLS);

			char *timestamp = nullptr;
			if (DataRX != nullptr) {
				if (ActualAccount->Flags & YAMN_ACC_APOP) {
					char *lpos = strchr(DataRX, '<');
					char *rpos = strchr(DataRX, '>');
					if (lpos && rpos && rpos > lpos) {
						int sz = (int)(rpos - lpos + 2);
						timestamp = new char[sz];
						memcpy(timestamp, lpos, sz - 1);
						timestamp[sz - 1] = '\0';
					}
				}
				free(DataRX);
				DataRX = nullptr;
			}
			SetAccountStatus(ActualAccount, TranslateT("Entering POP3 account"));

			if (ActualAccount->Flags & YAMN_ACC_APOP)
			{
				DataRX = MyClient->APOP(ActualCopied.ServerLogin, ActualCopied.ServerPasswd, timestamp);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				delete[] timestamp;
			}
			else {
				DataRX = MyClient->User(ActualCopied.ServerLogin);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				DataRX = MyClient->Pass(ActualCopied.ServerPasswd);
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
			}
		}

#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<--------Deleting requested mails-------->\n");
#endif
		if (POP3_DELETEFROMCHECK != POP3PluginParam)	//We do not need to get mails on server as we have already it from check function
		{
			SetAccountStatus(ActualAccount, TranslateT("Deleting requested mails"));

			DataRX = MyClient->Stat();

#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"<Extracting stat>\n");
#endif
			ExtractStat(DataRX, &mboxsize, &msgs);
#ifdef DEBUG_DECODE
			DebugLog(DecodeFile,"<MailBoxSize>%d</MailBoxSize>\n",mboxsize);
			DebugLog(DecodeFile,"<Msgs>%d</Msgs>\n",msgs);
			DebugLog(DecodeFile,"</Extracting stat>\n");
#endif
			if (DataRX != nullptr)
				free(DataRX);
			DataRX = nullptr;
			for (i = 0; i < msgs; i++)
			{
				if (!i)
					MsgQueuePtr = NewMails = (HYAMNMAIL)CallService(MS_YAMN_CREATEACCOUNTMAIL, (WPARAM)ActualAccount, (LPARAM)YAMN_MAILVERSION);
				else
				{
					MsgQueuePtr->Next = (HYAMNMAIL)CallService(MS_YAMN_CREATEACCOUNTMAIL, (WPARAM)ActualAccount, (LPARAM)YAMN_MAILVERSION);
					MsgQueuePtr = MsgQueuePtr->Next;
				}
				if (MsgQueuePtr == nullptr)
				{
					ActualAccount->SystemError = EPOP3_QUEUEALLOC;
					throw (uint32_t)ActualAccount->SystemError;
				}
			}

			if (msgs)
			{
#ifdef DEBUG_DECODE
				DebugLog(DecodeFile,"<Extracting UIDL>\n");
#endif
				DataRX = MyClient->Uidl();
				ExtractUIDL(DataRX, MyClient->NetClient->Rcv, NewMails);
#ifdef DEBUG_DECODE
				DebugLog(DecodeFile,"</Extracting UIDL>\n");
#endif
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				//	we get "new mails" on server (NewMails will contain all mails on server not found in DeleteMails)
				//	but also in DeleteMails we get only those, which are still on server with their responsable numbers
				SynchroMessages(ActualAccount, (HYAMNMAIL *)&DeleteMails, nullptr, (HYAMNMAIL *)&NewMails, nullptr);
			}
		}
		else
			SetAccountStatus(ActualAccount, TranslateT("Deleting spam"));
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountMsgsSO-write wait\n");
#endif
		if (WAIT_OBJECT_0 != MsgsWaitToWrite(ActualAccount))
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountMsgsSO-write wait failed\n");
#endif
			throw (uint32_t)EACC_STOPPED;
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountMsgsSO-write enter\n");
#endif
		if (msgs || POP3_DELETEFROMCHECK == POP3PluginParam)
		{
			try
			{
				HYAMNMAIL Temp;

				for (i = 0, MsgQueuePtr = DeleteMails; MsgQueuePtr != nullptr; i++)
				{
					if (!(MsgQueuePtr->Flags & YAMN_MSG_VIRTUAL))	//of course we can only delete real mails, not virtual
					{
						DataRX = MyClient->Dele(MsgQueuePtr->Number);
						Temp = MsgQueuePtr->Next;
						if (POP3_FOK == MyClient->AckFlag)			//if server answers that mail was deleted
						{
							DeleteMIMEMessage((HYAMNMAIL *)&DeleteMails, MsgQueuePtr);
							HYAMNMAIL DeletedMail = FindMIMEMessageByID((HYAMNMAIL)ActualAccount->Mails, MsgQueuePtr->ID);
							if ((MsgQueuePtr->Flags & YAMN_MSG_MEMDELETE))		//if mail should be deleted from memory (or disk)
							{
								DeleteMIMEMessage((HYAMNMAIL *)&ActualAccount->Mails, DeletedMail);	//remove from queue
								CallService(MS_YAMN_DELETEACCOUNTMAIL, (WPARAM)POP3Plugin, (LPARAM)DeletedMail);
							}
							else			//else mark it only as "deleted mail"
							{
								DeletedMail->Flags |= (YAMN_MSG_VIRTUAL | YAMN_MSG_DELETED);
								DeletedMail->Flags &= ~(YAMN_MSG_NEW | YAMN_MSG_USERDELETE | YAMN_MSG_AUTODELETE);	//clear "new mail"
							}
							delete   MsgQueuePtr->MailData;
							delete[] MsgQueuePtr->ID;
							delete   MsgQueuePtr;
						}
						MsgQueuePtr = Temp;

						if (DataRX != nullptr)
							free(DataRX);
						DataRX = nullptr;
					}
					else
						MsgQueuePtr = MsgQueuePtr->Next;
				}
			}
			catch (...)		//if any exception in the code where we have write-access to account occured, don't forget to leave write-access
			{
#ifdef DEBUG_SYNCHRO
				DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountMsgsSO-write done\n");
#endif  	
				MsgsWriteDone(ActualAccount);
				throw;			//and go to the main exception handling
			}

			if (NewMails != nullptr)
				//	in ActualAccount->Mails we have all mails stored before calling this function
				//	in NewMails we have all mails not found in DeleteMails (in other words: we performed new ID checking and we
				//	stored all mails found on server, then we deleted the ones we wanted to delete in this function
				//	and NewMails queue now contains actual state of mails on server). But we will not use NewMails as actual state, because NewMails does not contain header data (subject, from...)
				//	We perform deleting from ActualAccount->Mails: we remove from original queue (ActualAccount->Mails) all deleted mails
				SynchroMessages(ActualAccount, (HYAMNMAIL *)&ActualAccount->Mails, nullptr, (HYAMNMAIL *)&NewMails, nullptr);
			//	Now ActualAccount->Mails contains all mails when calling this function except the ones, we wanted to delete (these are in DeleteMails)
			//	And in NewMails we have new mails (if any)
			else if (POP3_DELETEFROMCHECK != POP3PluginParam)
			{
				DeleteMIMEQueue(ActualAccount, (HYAMNMAIL)ActualAccount->Mails);
				ActualAccount->Mails = nullptr;
			}
		}
		else
		{
			DeleteMIMEQueue(ActualAccount, (HYAMNMAIL)ActualAccount->Mails);
			ActualAccount->Mails = nullptr;
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"DeleteMailsPOP3:ActualAccountMsgsSO-write done\n");
#endif
		MsgsWriteDone(ActualAccount);
#ifdef DEBUG_DECODE     	
		DebugLog(DecodeFile,"</--------Deleting requested mails-------->\n");
#endif

		//	TODO: now, we have in NewMails new mails. If NewMails is not NULL, we found some new mails, so Checking for new mail should be performed
		//	now, we do not call CheckPOP3

		//	if there is no waiting thread for internet connection close it
		//	else leave connection open
		//	if this functin was called from SynchroPOP3, then do not try to disconnect 
		if (POP3_DELETEFROMCHECK != POP3PluginParam)
		{
			YAMN_MAILBROWSERPARAM Param = { (HANDLE)nullptr, ActualAccount, ActualCopied.NFlags, YAMN_ACC_MSGP, YAMNParam };

			CallService(MS_YAMN_MAILBROWSER, (WPARAM)&Param, (LPARAM)YAMN_MAILBROWSERVERSION);

			if (0 == SCGetNumber(ActualAccount->InternetQueries))
			{
				DataRX = MyClient->Quit();
				if (DataRX != nullptr)
					free(DataRX);
				DataRX = nullptr;
				MyClient->NetClient->Disconnect();

				SetAccountStatus(ActualAccount, TranslateT("Disconnected"));
			}

			UsingInternet = FALSE;
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"DeleteMailsPOP3:InternetFreeEV-done\n");
#endif
			SetEvent(ActualAccount->UseInternetFree);
		}
		SetContactStatus(ActualAccount, ActualAccount->isCounting ? ID_STATUS_ONLINE : ID_STATUS_OFFLINE);
	}
#ifdef DEBUG_COMM
	catch(uint32_t ErrorCode)
#else
	catch (uint32_t)
#endif
	{
		if (ActualAccount->Client.POP3Error == EPOP3_STOPPED)
			ActualAccount->SystemError = EACC_STOPPED;
#ifdef DEBUG_COMM
		DebugLog(CommFile,"ERROR %x\n",ErrorCode);
#endif
		if (DataRX != nullptr)
			free(DataRX);
		switch (ActualAccount->SystemError)
		{
		case EACC_QUEUEALLOC:
		case EACC_STOPPED:
			ActualAccount->Client.NetClient->Disconnect();
			break;
		default:
			PostErrorProc(ActualAccount, YAMNParam, POP3PluginParam, MyClient->SSL);	//it closes internet connection too
		}

		if (UsingInternet && (POP3_DELETEFROMCHECK != POP3PluginParam))	//if our thread still uses internet and it is needed to release internet
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"CheckPOP3:InternetFreeEV-done\n");
#endif
			SetEvent(ActualAccount->UseInternetFree);
		}
	}

	free(ActualCopied.ServerName);
	free(ActualCopied.ServerLogin);
	free(ActualCopied.ServerPasswd);

	DeleteMIMEQueue(ActualAccount, NewMails);
	DeleteMIMEQueue(ActualAccount, DeleteMails);

#ifdef DEBUG_COMM
	DebugLog(CommFile,"</--------Communication-------->\n");
#endif
	//	WriteAccounts();
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"DeleteMailsPOP3:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
	SCDec(ActualAccount->UsingThreads);
	return;
}

void ExtractStat(char *stream, int *mboxsize, int *mails)
{
	char *finder = stream;
	while (WS(finder) || ENDLINE(finder)) finder++;
	if (ACKLINE(finder))
	{
		while (!WS(finder)) finder++;
		while (WS(finder)) finder++;
	}
	if (1 != sscanf(finder, "%d", mails))
		throw (uint32_t)EPOP3_STAT;
	while (!WS(finder)) finder++;
	while (WS(finder)) finder++;
	if (1 != sscanf(finder, "%d", mboxsize))
		throw (uint32_t)EPOP3_STAT;
}
void ExtractMail(char *stream, int len, HYAMNMAIL queue)
{
	char *finder = stream;
	char *finderend;
	int msgnr, i;
	HYAMNMAIL queueptr = queue;

	while (WS(finder) || ENDLINE(finder)) finder++;
	while (!ACKLINE(finder)) finder++;
	while (!ENDLINE(finder)) finder++;			//now we at the end of first ack line
	while (finder <= (stream + len))
	{
		while (ENDLINE(finder)) finder++;		//go to the new line
		if (DOTLINE(finder + 1))					//at the end of stream
			break;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Message>\n");
#endif
		while (WS(finder)) finder++;			//jump whitespace
		if (1 != sscanf(finder, "%d", &msgnr))
			throw (uint32_t)EPOP3_UIDL;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Nr>%d</Nr>\n",msgnr);
#endif
		//		for (i=1,queueptr=queue;(queueptr->Next != NULL) && (i<msgnr);queueptr=queueptr->Next,i++);
		//		if (i != msgnr)
		//			throw (uint32_t)EPOP3_UIDL;
		while (!WS(finder)) finder++;			//jump characters
		while (WS(finder)) finder++;			//jump whitespace
		finderend = finder + 1;
		while (!WS(finderend) && !ENDLINE(finderend)) finderend++;
		queueptr->ID = new char[finderend - finder + 1];
		for (i = 0; finder != finderend; finder++, i++)
			queueptr->MailData->Body[i] = *finder;
		queueptr->MailData->Body[i] = 0;				//ends string
		queueptr->Number = msgnr;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<ID>%s</ID>\n",queueptr->MailData->Body);
		DebugLog(DecodeFile,"</Message>\n");
#endif
		queueptr = queueptr->Next;
		while (!ENDLINE(finder)) finder++;
	}
}

void ExtractUIDL(char *stream, int len, HYAMNMAIL queue)
{
	char *finder = stream;
	char *finderend;
	int msgnr, i;
	HYAMNMAIL queueptr = queue;

	while (WS(finder) || ENDLINE(finder)) finder++;
	while (!ACKLINE(finder)) finder++;
	while (!ENDLINE(finder)) finder++;			//now we at the end of first ack line
	while (finder <= (stream + len))
	{
		while (ENDLINE(finder)) finder++;		//go to the new line
		if (DOTLINE(finder + 1))					//at the end of stream
			break;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Message>\n");
#endif
		while (WS(finder)) finder++;			//jump whitespace
		if (1 != sscanf(finder, "%d", &msgnr))
			throw (uint32_t)EPOP3_UIDL;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Nr>%d</Nr>\n",msgnr);
#endif
		//		for (i=1,queueptr=queue;(queueptr->Next != NULL) && (i<msgnr);queueptr=queueptr->Next,i++);
		//		if (i != msgnr)
		//			throw (uint32_t)EPOP3_UIDL;
		while (!WS(finder)) finder++;			//jump characters
		while (WS(finder)) finder++;			//jump whitespace
		finderend = finder + 1;
		while (!WS(finderend) && !ENDLINE(finderend)) finderend++;
		queueptr->ID = new char[finderend - finder + 1];
		for (i = 0; finder != finderend; finder++, i++)
			queueptr->ID[i] = *finder;
		queueptr->ID[i] = 0;				//ends string
		queueptr->Number = msgnr;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<ID>%s</ID>\n",queueptr->ID);
		DebugLog(DecodeFile,"</Message>\n");
#endif
		queueptr = queueptr->Next;
		while (!ENDLINE(finder)) finder++;
	}
}

void ExtractList(char *stream, int len, HYAMNMAIL queue)
{
	char *finder = stream;
	char *finderend;
	int msgnr, i;
	HYAMNMAIL queueptr;

	while (WS(finder) || ENDLINE(finder)) finder++;
	while (!ACKLINE(finder)) finder++;
	while (!ENDLINE(finder)) finder++;			//now we at the end of first ack line
	while (finder <= (stream + len))
	{
		while (ENDLINE(finder)) finder++;		//go to the new line
		if (DOTLINE(finder + 1))				//at the end of stream
			break;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Message>\n",NULL,0);
#endif
		while (WS(finder)) finder++;			//jump whitespace
		if (1 != sscanf(finder, "%d", &msgnr))		//message nr.
			throw (uint32_t)EPOP3_LIST;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Nr>%d</Nr>\n",msgnr);
#endif

		for (i = 1, queueptr = queue; (queueptr->Next != nullptr) && (i < msgnr); queueptr = queueptr->Next, i++);
		if (i != msgnr)
			throw (uint32_t)EPOP3_LIST;
		while (!WS(finder)) finder++;			//jump characters
		while (WS(finder)) finder++;			//jump whitespace
		finderend = finder + 1;
		if (1 != sscanf(finder, "%u", &queueptr->MailData->Size))
			throw (uint32_t)EPOP3_LIST;
#ifdef DEBUG_DECODE
		DebugLog(DecodeFile,"<Nr>%d</Nr>\n",queueptr->MailData->Size);
#endif
		while (!ENDLINE(finder)) finder++;
	}
}

wchar_t* WINAPI GetErrorString(DWORD Code)
{
	static wchar_t *POP3Errors[] =
	{
		LPGENW("Memory allocation error."),		//memory allocation
		LPGENW("Account is about to be stopped."),	//stop account
		LPGENW("Cannot connect to POP3 server."),
		LPGENW("Cannot allocate memory for received data."),
		LPGENW("Cannot login to POP3 server."),
		LPGENW("Bad user or password."),
		LPGENW("Server does not support APOP authorization."),
		LPGENW("Error while executing POP3 command."),
		LPGENW("Error while executing POP3 command."),
		LPGENW("Error while executing POP3 command."),
	};

	static wchar_t *NetlibErrors[] =
	{
		LPGENW("Cannot connect to server with NetLib."),
		LPGENW("Cannot send data."),
		LPGENW("Cannot receive data."),
		LPGENW("Cannot allocate memory for received data."),
	};

	static wchar_t *SSLErrors[] =
	{
		LPGENW("OpenSSL not loaded."),
		LPGENW("Windows socket 2.0 init failed."),
		LPGENW("DNS lookup error."),
		LPGENW("Error while creating base socket."),
		LPGENW("Error connecting to server with socket."),
		LPGENW("Error while creating SSL structure."),
		LPGENW("Error connecting socket with SSL."),
		LPGENW("Server rejected connection with SSL."),
		LPGENW("Cannot write SSL data."),
		LPGENW("Cannot read SSL data."),
		LPGENW("Cannot allocate memory for received data."),
	};

	wchar_t *ErrorString = new wchar_t[ERRORSTR_MAXLEN];
	POP3_ERRORCODE *ErrorCode = (POP3_ERRORCODE *)(UINT_PTR)Code;

	mir_snwprintf(ErrorString, ERRORSTR_MAXLEN, TranslateT("Error %d-%d-%d-%d:"), ErrorCode->AppError, ErrorCode->POP3Error, ErrorCode->NetError, ErrorCode->SystemError);
	if (ErrorCode->POP3Error)
		mir_snwprintf(ErrorString, ERRORSTR_MAXLEN, L"%s\n%s", ErrorString, TranslateW(POP3Errors[ErrorCode->POP3Error - 1]));
	if (ErrorCode->NetError) {
		if (ErrorCode->SSL)
			mir_snwprintf(ErrorString, ERRORSTR_MAXLEN, L"%s\n%s", ErrorString, TranslateW(SSLErrors[ErrorCode->NetError - 1]));
		else
			mir_snwprintf(ErrorString, ERRORSTR_MAXLEN, L"%s\n%s", ErrorString, TranslateW(NetlibErrors[ErrorCode->NetError - 4]));
	}

	return ErrorString;
}

void WINAPI DeleteErrorString(LPVOID String)
{
	delete (char *)String;
}
