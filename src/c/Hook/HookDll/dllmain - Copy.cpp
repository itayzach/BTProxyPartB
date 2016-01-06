#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <bthsdpdef.h>
#include <ws2bth.h>

#include "C:\Detours\include\detours.h"

#define CXN_SUCCESS 0
#define CXN_ERROR 1

#pragma comment(lib,"detours.lib")
#pragma comment(lib,"ws2_32.lib")

// TODO
// 1. find out how to send the "btdevice" name from BT windows app to this dll as pWSALookupServiceBegin argument or something like that
// 2. CopyMemory to create a dummy structure for return to BT windows app fails 
// 3. in BT windows app the device we are looking for is set to "btdeviceaaaa" instead of "btdevice"
// 4. Important! there's a case in which the device is not near but exists in the paired list (so WSA_E_NO_MORE will not be returned as error)
//    this means we should open a TCP socket anyway and use it in case BT connect fails (in MyConnect)

// =============================================================================
// Define hooking pointers
// For each function, save its' address in p<FuncName> in order to call the 
// original function using the pointer.
// =============================================================================
int(WINAPI *pWSAGetLastError)() = WSAGetLastError;
int WINAPI MyWSAGetLastError();

INT(WINAPI *pWSALookupServiceBegin)(_In_ LPWSAQUERYSET pQuerySet, _In_ DWORD dwFlags, _Out_ LPHANDLE lphLookup) = WSALookupServiceBegin;
INT WINAPI MyWSALookupServiceBegin(_In_ LPWSAQUERYSET pQuerySet, _In_ DWORD dwFlags, _Out_ LPHANDLE lphLookup);

INT(WINAPI *pWSALookupServiceNext)(_In_ HANDLE hLookup, _In_ DWORD dwFlags, _Inout_ LPDWORD lpdwBufferLength, _Out_ LPWSAQUERYSET pResults) = WSALookupServiceNext;
INT WINAPI MyWSALookupServiceNext(_In_ HANDLE hLookup, _In_ DWORD dwFlags, _Inout_ LPDWORD lpdwBufferLength, _Out_ LPWSAQUERYSET pResults);

INT(WINAPI *pWSALookupServiceEnd)(_In_ HANDLE hLookup) = WSALookupServiceEnd;
INT WINAPI MyWSALookupServiceEnd(_In_ HANDLE hLookup);

SOCKET(WSAAPI *pSocket)(_In_ int af, _In_ int type, _In_ int protocol) = socket;
SOCKET WSAAPI MySocket(_In_ int af, _In_ int type, _In_ int protocol);

int (WINAPI *pConnect)(_In_ SOCKET s, _In_ const struct sockaddr *name, _In_ int namelen) = connect;
int WINAPI MyConnect(_In_ SOCKET s, _In_ const struct sockaddr *name, _In_ int namelen);

int (WINAPI *pSend)(_In_ SOCKET s, _In_ const char* buf, _In_ int len, _In_ int flags) = send;
int WINAPI MySend(_In_ SOCKET s, _In_ const char* buf, _In_ int len, _In_ int flags);

int (WINAPI *pClosesocket)(_In_ SOCKET s) = closesocket;
int WINAPI MyClosesocket(_In_ SOCKET s);

// =============================================================================
// convert const char* to LPCWSTR
// =============================================================================
wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
	wchar_t* wString = new wchar_t[4096];
	MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
	return wString;
}

void MsgBox(char *str) {
	MessageBoxW(NULL, convertCharArrayToLPCWSTR(str), NULL, MB_OK);
}


// =============================================================================
// Global variables
// =============================================================================
SOCKET BTSocket = INVALID_SOCKET;
SOCKET TCPSocket = INVALID_SOCKET;
struct addrinfo *result = NULL;
FILE* pLogFile = NULL;
bool TCPSocketClosed = false;
char* BTProxyIpAddr = "132.68.53.93";
int BTProxyPort = 4020;
char* BTDeviceName = "btdevice"; // TODO - in BT app, the pQuerySet should be initiated with the BT device name
bool tcpIsNeeded = false; // if true, using TCP connection
int lastError = 0;
// =============================================================================
// Hooked functions
// =============================================================================
int WINAPI MyWSAGetLastError() {
	//int realLastError;
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//fprintf(pLogFile, "[MyWSAGetLastError]\t Entered\n");
	//fclose(pLogFile);
	//
	//realLastError = pWSAGetLastError();
	//
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//if (realLastError == ERROR_ALREADY_EXISTS) {
	//	fprintf(pLogFile, "[MyWSAGetLastError]\t Got ERROR_ALREADY_EXISTS\n");
	//    fclose(pLogFile);
	//	return lastError;
	//}
	//lastError = pWSAGetLastError();
	//fprintf(pLogFile, "[MyWSAGetLastError]\t Got %d\n", lastError);
	//fclose(pLogFile);
	//return lastError;

	return pWSAGetLastError();
}

INT WINAPI MyWSALookupServiceBegin(_In_ LPWSAQUERYSET pQuerySet, _In_ DWORD dwFlags, _Out_ LPHANDLE lphLookup)
{
	INT iResult;
	// Since this function initiates the query and allocates memory, it is called as is
	iResult = pWSALookupServiceBegin(pQuerySet, dwFlags, lphLookup);

	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceBegin]\t Entered\n");

	fprintf(pLogFile, "[MyWSALookupServiceBegin]\t iResult = %d\n", iResult);

	if (dwFlags & LUP_RETURN_NAME) {
		fprintf(pLogFile, "[MyWSALookupServiceBegin]\t device name is %s\n", pQuerySet->lpszServiceInstanceName);
		// TODO - in BT app, the pQuerySet should be initiated with the BT device name
		//wcscpy_s(convertCharArrayToLPCWSTR(BTDeviceName), 2, pQuerySet->lpszServiceInstanceName);
		fprintf(pLogFile, "[MyWSALookupServiceBegin]\t Looking for this device : %s\n", BTDeviceName);
	}
	else {
		fprintf(pLogFile, "[MyWSALookupServiceBegin]\t BT app didn't request to look for a name\n");
	}

	fclose(pLogFile);
	return iResult;
}
INT WINAPI MyWSALookupServiceNext(_In_ HANDLE hLookup, _In_ DWORD dwFlags, _Inout_ LPDWORD lpdwBufferLength, _Out_ LPWSAQUERYSET pResults)
{
	INT iResult;
	struct sockaddr_in server;
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceNext]\t Entered\n");
	fclose(pLogFile);
	iResult = pWSALookupServiceNext(hLookup, dwFlags, lpdwBufferLength, pResults);
	lastError = pWSAGetLastError();

	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceNext]\t iResult = %d, lastError = %d\n", iResult, lastError);

	if (iResult == NO_ERROR) {
		if ((dwFlags & LUP_RETURN_NAME) && // BT app requested to look for a name
			CXN_SUCCESS == wcscmp(pResults->lpszServiceInstanceName, convertCharArrayToLPCWSTR(BTDeviceName))) {
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t Found %s, no need for TCP connection\n", BTDeviceName);
			fclose(pLogFile);
			return iResult;
		}
		else {
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t Found %s, no match... lastError = %d\n", pResults->lpszServiceInstanceName, WSAGetLastError());
			fclose(pLogFile);
			return iResult;
		}
	}
	else {
		if (lastError == WSA_E_NO_MORE) {
			// Meaning no more devices were found. 
			// We should now initiate a TCP connection and send the parameters
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t iResult is WSA_E_NO_MORE (no more devices were found) initiating TCP connection\n");
			tcpIsNeeded = true;
			TCPSocket = pSocket(AF_INET, SOCK_STREAM, 0);
			if (TCPSocket == INVALID_SOCKET) {
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t TCP socket failed with error\n");
				WSACleanup();
				fclose(pLogFile);
				return -1;
			}
			else {
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t opened TCP socket number %d\n", TCPSocket);
			}

			server.sin_addr.s_addr = inet_addr(BTProxyIpAddr);
			server.sin_family = AF_INET;
			server.sin_port = htons(BTProxyPort);
			iResult = pConnect(TCPSocket, (struct sockaddr *)&server, sizeof(server));
			if (iResult < 0) {
				pClosesocket(TCPSocket);
				TCPSocketClosed = true;
				//MsgBox("Connect failed with error \n");
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t TCP Connect failed\n");
				WSACleanup();
				fclose(pLogFile);
				return -1;
			}
			else {
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t TCP Connect succeded\n");
				// TODO send request for android to search for BT device using psend(...)



				// return a dummy structure to BT windows app
				CopyMemory((LPWSTR)(pResults->lpszServiceInstanceName), BTDeviceName, sizeof(BTDeviceName));
				//wcscpy_s((pResults->lpszServiceInstanceName), 2, convertCharArrayToLPCWSTR(BTDeviceName));
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t Copied dummy name\n");
				BTH_ADDR btAddr = 0xD0C1B14BEB23;
				CopyMemory((PSOCKADDR_BTH)pResults->lpcsaBuffer->RemoteAddr.lpSockaddr, (PSOCKADDR_BTH)&btAddr, sizeof(btAddr));
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t Copied dummy addr\n");
				fclose(pLogFile);
				return NO_ERROR;

			}
		}
		else {
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t pWSALookupServiceNext failed. iResult = %d, lastError = %d \n", iResult, WSAGetLastError());
			fclose(pLogFile);
			return iResult;
		}
	}

	fprintf(pLogFile, "[MyWSALookupServiceNext]\t end of func \n");
	fclose(pLogFile);
	return NO_ERROR;


}
INT WINAPI MyWSALookupServiceEnd(_In_ HANDLE hLookup)
{
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//fprintf(pLogFile, "[MyWSALookupServiceEnd]\t Entered function\n");
	//fclose(pLogFile);
	return pWSALookupServiceEnd(hLookup);
}

SOCKET WSAAPI MySocket(_In_ int af, _In_ int type, _In_ int protocol)
{
	//MsgBox("HookDll : Entered MySocket");
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");

	if (!tcpIsNeeded) {
		BTSocket = pSocket(af, type, protocol);
		if (BTSocket == INVALID_SOCKET) {
			//MsgBox("BT socket failed with error\n");
			fprintf(pLogFile, "[MySocket]\t BT socket failed with error\n");
			WSACleanup();
			fclose(pLogFile);
			return -1;
		}
		else
			//MsgBox("BT socket succeded");
			fprintf(pLogFile, "[MySocket]\t opened BT socket number %d\n", BTSocket);
	}
	else {
		fprintf(pLogFile, "[MySocket]\t No need for BT socket\n");
		BTSocket = 100; // some random number that will never be used
	}

	fclose(pLogFile);
	return BTSocket;
}

int WINAPI MyConnect(_In_ SOCKET s, _In_ const struct sockaddr *name, _In_ int namelen)
{
	int iResult;
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");

	//MsgBox("HookDll : Entered MyConnect");
	if (!tcpIsNeeded) {
		iResult = pConnect(BTSocket, name, namelen);
		if (iResult < 0) {
			fprintf(pLogFile, "[MyConnect]\t BT connect failed\n");
		}
		else {
			//MsgBox("connect succeded");
			fprintf(pLogFile, "[MyConnect]\t BT Connect succeded\n");
		}
	}
	fclose(pLogFile);

	return iResult;
}


int WINAPI MySend(SOCKET s, const char* buf, int len, int flags)
{
	int iResult;
	//MsgBox("HookDll : Entered MySend");
	//char* msg = "Message from HookDLL  ";
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");

	if (tcpIsNeeded) {
		iResult = pSend(TCPSocket, buf, strlen(buf), flags);
		// DEBUG - iResult = pSend(TCPSocket, msg, strlen(msg), flags);
		if (iResult == SOCKET_ERROR) {
			//MsgBox("send failed with error: \n");
			fprintf(pLogFile, "[MySend]\t TCP send failed with error %d\n", iResult);
			TCPSocketClosed = 1;
			pClosesocket(TCPSocket);
			WSACleanup();
			fclose(pLogFile);
			return -1;
		}
		else
			//MsgBox("send succeded");
			fprintf(pLogFile, "[MySend]\t TCP send succeeded. Message = %s, iResult =  %d\n", buf, iResult);
	}
	else {
		iResult = pSend(BTSocket, buf, len, flags);
		if (iResult == SOCKET_ERROR) {
			//MsgBox("send failed with error: \n");
			fprintf(pLogFile, "[MySend]\t BT send failed with error %d\n", iResult);
			TCPSocketClosed = 1;
			pClosesocket(TCPSocket);
			WSACleanup();
			fclose(pLogFile);
			return -1;
		}
		else
			//MsgBox("send succeded");
			fprintf(pLogFile, "[MySend]\t BT send succeeded - %d\n", iResult);
	}
	fclose(pLogFile);
	return iResult;
}

int WINAPI MyClosesocket(_In_ SOCKET s)
{
	int iResult;
	//MsgBox("HookDll : Entered MyClosesocket");
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");

	int res1;
	res1 = TCPSocketClosed ? 0 : pClosesocket(TCPSocket); // close socket only if it wasn't closed already
	int res2;
	res2 = pClosesocket(BTSocket);

	fprintf(pLogFile, "[MyClosesocket]\t Sockets closed - %d, %d (zeros are successes)\n", res1, res2);
	iResult = shutdown(TCPSocket, SD_SEND);
	fclose(pLogFile);
	return res2;
}

extern "C" __declspec(dllexport) void dummy(void){
	return;
}


// =============================================================================
// DllMain
// =============================================================================
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
	if (DetourIsHelperProcess()) {
		return TRUE;
	}

	if (dwReason == DLL_PROCESS_ATTACH) {

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)pWSAGetLastError, MyWSAGetLastError);
		DetourAttach(&(PVOID&)pWSALookupServiceBegin, MyWSALookupServiceBegin);
		DetourAttach(&(PVOID&)pWSALookupServiceNext, MyWSALookupServiceNext);
		DetourAttach(&(PVOID&)pWSALookupServiceEnd, MyWSALookupServiceEnd);
		DetourAttach(&(PVOID&)pSocket, MySocket);
		DetourAttach(&(PVOID&)pConnect, MyConnect);
		DetourAttach(&(PVOID&)pSend, MySend);
		DetourAttach(&(PVOID&)pClosesocket, MyClosesocket);
		DetourTransactionCommit();
	}
	else if (dwReason == DLL_PROCESS_DETACH) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&(PVOID&)pWSAGetLastError, MyWSAGetLastError);
		DetourDetach(&(PVOID&)pWSALookupServiceBegin, MyWSALookupServiceBegin);
		DetourDetach(&(PVOID&)pWSALookupServiceNext, MyWSALookupServiceNext);
		DetourDetach(&(PVOID&)pWSALookupServiceEnd, MyWSALookupServiceEnd);
		DetourDetach(&(PVOID&)pSocket, MySocket);
		DetourDetach(&(PVOID&)pConnect, MyConnect);
		DetourDetach(&(PVOID&)pSend, MySend);
		DetourDetach(&(PVOID&)pClosesocket, MyClosesocket);
		DetourTransactionCommit();
	}

	return TRUE;
}


