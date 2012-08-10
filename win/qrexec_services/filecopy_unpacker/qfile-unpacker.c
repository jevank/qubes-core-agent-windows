#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <Strsafe.h>
#include <Shlwapi.h>
#include <shlobj.h>
#include <Shellapi.h>
#include <ioall.h>
#include <gui-fatal.h>
#include "wdk.h"
#include "linux.h"
#include "filecopy.h"
#include "crc32.h"


HANDLE STDIN = INVALID_HANDLE_VALUE;
HANDLE STDOUT = INVALID_HANDLE_VALUE;
HANDLE STDERR = INVALID_HANDLE_VALUE;

#define INCOMING_DIR_ROOT L"incoming"

#ifdef DBG
#define internal_fatal gui_fatal
#else
static __inline void internal_fatal(const PWCHAR fmt, ...) {
	gui_fatal(L"Internal error");
}
#endif

void do_unpack(HANDLE fd);

WCHAR	g_wcMappedDriveLetter = L'\0';


/*
void wait_for_child(HANDLE statusfd)
{
	int status;
	if (read_all(statusfd, &status, sizeof status)!=sizeof status)
		gui_fatal(L"File copy error: Internal error reading status from unpacker");
	errno = status;
	switch (status) {
	case LEGAL_EOF: break;
	case 0: gui_fatal(L"File copy: Connection terminated unexpectedly"); break;
	case EINVAL: gui_fatal(L"File copy: Corrupted data from packer"); break;
	case EEXIST: gui_fatal(L"File copy: not overwriting existing file. Clean ~/incoming, and retry copy"); break;
	default: gui_fatal(L"File copy"); 
	}
}
*/

ULONG CreateLink(PWCHAR pwszTargetDirectory, PWCHAR pwcMappedDriveLetter)
{
	UNICODE_STRING	DirectoryObjectName;
	UNICODE_STRING	MappedDriveLetter;
	UNICODE_STRING	TargetDirectory;
	OBJECT_ATTRIBUTES	oa;
	HANDLE	hDirectoryObject;
	HANDLE	hLinkObject;
	NTSTATUS	Status;
	WCHAR	wszMappedDriveLetter[3];
	WCHAR	wszDirectoryObjectName[100];
	WCHAR	wszDevicePath[MAX_PATH];
	WCHAR	wszTargetDirectoryPath[MAX_PATH * 2];	// may be longer than MAX_PATH, but not much
	WCHAR	wszTargetDirectoryDriveLetter[3];
	HRESULT	hResult;
	ULONG	uResult;
	DWORD	dwLogicalDrives;
	UCHAR	i;


	if (!pwszTargetDirectory || !pwcMappedDriveLetter)
		return ERROR_INVALID_PARAMETER;

	hResult = StringCchPrintfW(
			wszDirectoryObjectName, 
			RTL_NUMBER_OF(wszDirectoryObjectName), 
			L"\\BaseNamedObjects\\qfile-unpacker-%d", 
			GetCurrentProcessId());
	if (FAILED(hResult)) {
		internal_fatal(L"CreateLink(): StringCchPrintfW() failed with error %d\n", hResult);
		return hResult;
	}


	hResult = StringCchCopyN(wszTargetDirectoryDriveLetter, RTL_NUMBER_OF(wszTargetDirectoryDriveLetter), pwszTargetDirectory, 2);
	if (FAILED(hResult)) {
		internal_fatal(L"CreateLink(): StringCchCopyN() failed with error %d\n", hResult);
		return hResult;
	}

	memset(&wszDevicePath, 0, sizeof(wszDevicePath));
	if (!QueryDosDevice(wszTargetDirectoryDriveLetter, wszDevicePath, RTL_NUMBER_OF(wszDevicePath))) {
		uResult = GetLastError();
		internal_fatal(L"CreateLink(): QueryDosDevice() failed with error %d\n", uResult);
		return uResult;
	}

	// Translate the directory path to a form of \Device\HarddiskVolumeN\path
	hResult = StringCchPrintfW(
			wszTargetDirectoryPath, 
			RTL_NUMBER_OF(wszTargetDirectoryPath), 
			L"%s%s", 
			wszDevicePath,
			(PWCHAR)&pwszTargetDirectory[2]);
	if (FAILED(hResult)) {
		internal_fatal(L"CreateLink(): StringCchPrintfW() failed with error %d\n", hResult);
		return hResult;
	}


	dwLogicalDrives = GetLogicalDrives();
	i = 'Z';
	while ((dwLogicalDrives & (1 << (i - 'A'))) && i)
		i--;
	if (!i) {
		internal_fatal(L"CreateLink(): Could not find a spare drive letter\n");
		return ERROR_ALREADY_EXISTS;
	}

	memset(&wszMappedDriveLetter, 0, sizeof(wszMappedDriveLetter));
	wszMappedDriveLetter[0] = i;
	wszMappedDriveLetter[1] = L':';


	RtlInitUnicodeString(&DirectoryObjectName, wszDirectoryObjectName);
	InitializeObjectAttributes(&oa, &DirectoryObjectName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	Status = ZwCreateDirectoryObject(&hDirectoryObject, DIRECTORY_ALL_ACCESS, &oa);
	if (!NT_SUCCESS(Status)) {
		internal_fatal(L"CreateLink(): ZwCreateDirectoryObject() failed with status 0x%08X\n", Status);
		return Status;
	}

	RtlInitUnicodeString(&MappedDriveLetter, wszMappedDriveLetter);
	RtlInitUnicodeString(&TargetDirectory, wszTargetDirectoryPath);
	InitializeObjectAttributes(&oa, &MappedDriveLetter, OBJ_CASE_INSENSITIVE, hDirectoryObject, NULL);

	Status = ZwCreateSymbolicLinkObject(&hLinkObject, 0, &oa, &TargetDirectory);
	if (!NT_SUCCESS(Status)) {
		ZwClose(hDirectoryObject);
		internal_fatal(L"CreateLink(): ZwCreateSymbolicLinkObject() failed with status 0x%08X\n", Status);
		return Status;
	}


#pragma prefast(suppress:28132, "sizeof(hDirectoryObject) is the correct size of a HANDLE")
	Status = ZwSetInformationProcess(GetCurrentProcess(), ProcessDeviceMap, &hDirectoryObject, sizeof(hDirectoryObject));
	if (!NT_SUCCESS(Status)) {
		internal_fatal(L"CreateLink(): ZwSetInformationProcess() failed with status 0x%08X\n", Status);
		ZwClose(hLinkObject);
		ZwClose(hDirectoryObject);
		return Status;
	}

	*pwcMappedDriveLetter = wszMappedDriveLetter[0];

//	ZwClose(hLinkObject);
//	ZwClose(hDirectoryObject);
	return ERROR_SUCCESS;
}

int __cdecl _tmain(ULONG argc, PTCHAR argv[])
{
	WCHAR	wszIncomingDir[MAX_PATH + 1];
	ULONG	nWritten;
	WCHAR	wcMappedDriveLetter;
	PWCHAR	pwszDocuments = NULL;
	HRESULT	hResult;
	ULONG	uResult;


//	if (argc < 2)
//		return;

	STDERR = GetStdHandle(STD_ERROR_HANDLE);
	if (STDERR == NULL || STDERR == INVALID_HANDLE_VALUE) {
		internal_fatal(L"Failed to get STDERR handle");
		exit(1);
	}
	STDIN = GetStdHandle(STD_INPUT_HANDLE);
	if (STDIN == NULL || STDIN == INVALID_HANDLE_VALUE) {
		internal_fatal(L"Failed to get STDIN handle");
		exit(1);
	}
	STDOUT = GetStdHandle(STD_OUTPUT_HANDLE);
	if (STDOUT == NULL || STDOUT == INVALID_HANDLE_VALUE) {
		internal_fatal(L"Failed to get STDOUT handle");
		exit(1);
	}

	hResult = SHGetKnownFolderPath(&FOLDERID_Documents, KF_FLAG_CREATE, NULL, &pwszDocuments);
	if (FAILED(hResult)) {
		internal_fatal(L"Failed to get a path to My Documents, SHGetKnownFolderPath() failed with error 0x%x\n", hResult);
		exit(1);
	}


	hResult = StringCchPrintf(wszIncomingDir, RTL_NUMBER_OF(wszIncomingDir), L"%s\\%s\\from-%s", pwszDocuments, INCOMING_DIR_ROOT, argv[1]);
	CoTaskMemFree(pwszDocuments);

	if (FAILED(hResult)) {
		internal_fatal(L"Failed to print an incoming directory path, StringCchPrintf() failed with error %d\n", hResult);
		exit(1);
	}

	uResult = SHCreateDirectoryEx(NULL, wszIncomingDir, NULL);
	if (ERROR_SUCCESS != uResult && ERROR_ALREADY_EXISTS != uResult) {
		internal_fatal(L"Failed to create an incoming directory path, SHCreateDirectoryEx() failed with error %d\n", uResult);
		exit(1);
	}

	uResult = CreateLink(wszIncomingDir, &g_wcMappedDriveLetter);
	if (ERROR_SUCCESS != uResult) {
		internal_fatal(L"Failed to map a drive letter to the incoming directory path, CreateLink() failed with error %d\n", uResult);
		exit(1);
	}

	do_unpack(STDERR);
}