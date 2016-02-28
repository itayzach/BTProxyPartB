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
// 1. Delete unwanted comments in the code
// 2. Check if there are redundent flags (like TCPSocketClosed)
// 3. Retreive IP address of CloudServer from URL

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
bool TCPSocketClosed = false;
char* CloudServerAddr = "104.45.149.160"; //"132.68.60.117";
char* BTProxyIpAddr = "132.68.50.55"; 
int BTProxyPort = 4020;
char* BTDeviceName = "btdevice"; // TODO - in BT app, the pQuerySet should be initiated with the BT device name
bool workWithCloudServer = true; // if true, using TCP connection. as default, connecting to CloudServer. 
                         // if server has no BT device connected, this flag will turn false
int remoteLookupsFails = 0;
bool BTdeviceIsRemote = true;
int lastError = 0;
bool setLastErrorToWSA_E_NO_MORE = false;
// =============================================================================
// Hooked functions
// =============================================================================
int WINAPI MyWSAGetLastError() {
	int realLastError;
	//FILE* pLogFile = NULL;
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//fprintf(pLogFile, "[MyWSAGetLastError]\t Entered\n");
	//fclose(pLogFile);
	//
	realLastError = pWSAGetLastError();
	//
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	if (realLastError == ERROR_ALREADY_EXISTS) {
	//	fprintf(pLogFile, "[MyWSAGetLastError]\t Got ERROR_ALREADY_EXISTS\n");
	//    fclose(pLogFile);
		return lastError;
	}
	lastError = pWSAGetLastError();
	//fprintf(pLogFile, "[MyWSAGetLastError]\t Got %d\n", lastError);
	//fclose(pLogFile);
	//return lastError;

	if (setLastErrorToWSA_E_NO_MORE) {
		// in case recv from BT Proxy for more devices was failed, 
		// it means there are no more devices in the BT Proxy discoverable devices database
		lastError = WSA_E_NO_MORE;
		setLastErrorToWSA_E_NO_MORE = false;
	}

	return lastError;
}

INT WINAPI MyWSALookupServiceBegin(_In_ LPWSAQUERYSET pQuerySet, _In_ DWORD dwFlags, _Out_ LPHANDLE lphLookup)
{
	INT iResult;
	int connectRes = 0;
	FILE* pLogFile = NULL;
	struct sockaddr_in server;

	// Since this function initiates the query and allocates memory, it is called as is
	iResult = pWSALookupServiceBegin(pQuerySet, dwFlags, lphLookup);

	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceBegin]\t Entered\n");
	fprintf(pLogFile, "[MyWSALookupServiceBegin]\t iResult = %d\n", iResult);
	fclose(pLogFile);

	// initiate a TCP connection 
	if (workWithCloudServer && TCPSocket == INVALID_SOCKET) { // meaning TCP socket to BTProxy wasn't opened yet
		TCPSocket = pSocket(AF_INET, SOCK_STREAM, 0);
		if (TCPSocket == INVALID_SOCKET) {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceBegin]\t TCP socket failed with error\n");
			fclose(pLogFile);
			WSACleanup();
			return -1;
		}
		else {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceBegin]\t opened TCP socket number %d\n", TCPSocket);
			fclose(pLogFile);

		}

		//server.sin_addr.s_addr = inet_addr(BTProxyIpAddr);
		server.sin_addr.s_addr = inet_addr(CloudServerAddr);
		server.sin_family = AF_INET;
		server.sin_port = htons(BTProxyPort);
		connectRes = pConnect(TCPSocket, (struct sockaddr *)&server, sizeof(server));
		if (connectRes < 0) {
			// if connect to BTProxy failed, run as a regular BT program.
			printf("[MyWSALookupServiceBegin]\t TCP Connect failed with error %d\n", pWSAGetLastError());
			workWithCloudServer = false;
			pClosesocket(TCPSocket);
			TCPSocketClosed = true;
			
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceBegin]\t TCP Connect failed\n");
			fclose(pLogFile);

			// return the pWSALookupServiceBegin result
			return iResult;
		}
		else {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceBegin]\t TCP Connect succeded\n");
			fclose(pLogFile);
			
			// send the id to cloud server
			const char* id = "windspc";
			connectRes = pSend(TCPSocket, id, strlen(id), 0);

			if (connectRes == SOCKET_ERROR) {
				//MsgBox("send failed with error: \n");
				fprintf(pLogFile, "[MyWSALookupServiceBegin]\t TCP send of the id failed with error %d\n", connectRes);
				TCPSocketClosed = 1;
				pClosesocket(TCPSocket);
				WSACleanup();
				return -1;
			}
			else {
				//MsgBox("send succeded");
				fprintf(pLogFile, "[MyWSALookupServiceBegin]\t TCP send of the id succeeded. id = %s, connectRes =  %d\n", id, connectRes);
			}
		}
	}
	return iResult;
}
INT WINAPI MyWSALookupServiceNext(_In_ HANDLE hLookup, _In_ DWORD dwFlags, _Inout_ LPDWORD lpdwBufferLength, _Out_ LPWSAQUERYSET pResults)
{
	INT iResult = 0;
	FILE* pLogFile = NULL;
	PSOCKADDR_BTH pDummyBtAddr = NULL;
	LPCSADDR_INFO localLpcsaBuffer = NULL;
	char recvBuf[1024] = { 0 };
	
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fwprintf(pLogFile, L"[MyWSALookupServiceNext]\t Entered with dwFlags = %X\n", dwFlags);
	fclose(pLogFile);

	if (workWithCloudServer && BTdeviceIsRemote) {
		// send a request for a device name from BT proxy
		char *msgToBTproxy = "msgFromHKW_queryDevice";
		iResult = pSend(TCPSocket, msgToBTproxy, strlen(msgToBTproxy), 0);

		if (iResult) {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t Sent command : %s\n", msgToBTproxy);
			fclose(pLogFile);
			iResult = recv(TCPSocket, recvBuf, 1024, 0);
			if (iResult <= 0) {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t recv failed\n");
				fclose(pLogFile);
				return -1;
			} else {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MyWSALookupServiceNext]\t Received %s (%d Bytes)\n", recvBuf, iResult);
				fclose(pLogFile);
				if (strcmp(recvBuf, "msgFromCS_sendToDstFailed") == 0) {
					// meaning, btproxy isn't connected yet. Try to look locally
					workWithCloudServer = false;
					fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
					fprintf(pLogFile, "[MyWSALookupServiceNext]\t recevied msgFromCS_sendToDstFailed, trying to look locally \n");
					fclose(pLogFile);
					iResult = pWSALookupServiceNext(hLookup, dwFlags, lpdwBufferLength, pResults);
					BTdeviceIsRemote = false;
					lastError = pWSAGetLastError();
					return iResult;

				}
				if (strcmp(recvBuf, "msgFromBTP_WSA_E_NO_MORE") == 0) {
					setLastErrorToWSA_E_NO_MORE = true; // will set WSA_E_NO_MORE as a return value from MyWSAGetLastError()
					remoteLookupsFails++;
					if (remoteLookupsFails == 2) {
						fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
						fprintf(pLogFile, "[MyWSALookupServiceNext]\t Setting BTdeviceIsRemote = false\n");
						fclose(pLogFile);
						BTdeviceIsRemote = false; // meaning, try looking locally
						remoteLookupsFails = 0;
					}
					fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
					fprintf(pLogFile, "[MyWSALookupServiceNext]\t setLastErrorToWSA_E_NO_MORE\n");
					fclose(pLogFile);
					return -1;
				}
				// return a dummy structure to BT windows app:
				// 1. in name field we should return the name we got from BT Proxy
				// 2. set a dummy MAC address (0xAABBCCDDEEFF)

				//DBG begin
				//pResults->lpszServiceInstanceName = convertCharArrayToLPCWSTR("faked_btdevice");
				pResults->lpszServiceInstanceName = convertCharArrayToLPCWSTR(recvBuf);
				//DBG end
				
				// allocate the dummy MAC
				pDummyBtAddr = new SOCKADDR_BTH;
				pDummyBtAddr->btAddr = 0xAABBCCDDEEFF;
				pDummyBtAddr->addressFamily = AF_BTH;
				pDummyBtAddr->port = BT_PORT_ANY;
				pDummyBtAddr->serviceClassId = SerialPortServiceClass_UUID;

				// allocate the localLpcsaBuffer to return to user
				localLpcsaBuffer = new CSADDR_INFO;
				localLpcsaBuffer->RemoteAddr.lpSockaddr = (PSOCKADDR)pDummyBtAddr;
				localLpcsaBuffer->RemoteAddr.iSockaddrLength = sizeof(SOCKADDR_BTH);

				// allocate the lpcsaBuffer to return to user
				pResults->lpcsaBuffer = new CSADDR_INFO;
				CopyMemory(pResults->lpcsaBuffer, localLpcsaBuffer, sizeof(*localLpcsaBuffer));

				return NO_ERROR;
			}
		}
		else {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyWSALookupServiceNext]\t pWSALookupServiceNext failed. iResult = %d, lastError = %d \n", iResult, WSAGetLastError());
			fclose(pLogFile);
			return iResult;
		}
	}
	else {
		// tcp is not needed - call pWSALookupServiceNext
		fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
		fprintf(pLogFile, "[MyWSALookupServiceNext]\t Calling original pWSALookupServiceNext\n");
		fclose(pLogFile);
		iResult = pWSALookupServiceNext(hLookup, dwFlags, lpdwBufferLength, pResults);
		lastError = pWSAGetLastError();
		return iResult;
	}
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceNext]\t end of func \n");
	fclose(pLogFile);
	return NO_ERROR;


}
INT WINAPI MyWSALookupServiceEnd(_In_ HANDLE hLookup)
{
	FILE* pLogFile = NULL;
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyWSALookupServiceEnd]\t Entered function\n");
	fclose(pLogFile);
	return pWSALookupServiceEnd(hLookup);
}

SOCKET WSAAPI MySocket(_In_ int af, _In_ int type, _In_ int protocol)
{
	//MsgBox("HookDll : Entered MySocket");
	FILE* pLogFile = NULL;
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MySocket]\t Entered\n");
	fclose(pLogFile);
	
	if (workWithCloudServer && BTdeviceIsRemote) {
		fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
		fprintf(pLogFile, "[MySocket]\t No need for BT socket\n");
		fclose(pLogFile);
		BTSocket = 100; // some random number that will never be used
	} else {
		BTSocket = pSocket(af, type, protocol);
		if (BTSocket == INVALID_SOCKET) {
			//MsgBox("BT socket failed with error\n");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySocket]\t BT socket failed with error\n");
			fclose(pLogFile);
			WSACleanup();
			return -1;
		}
		else {
			//MsgBox("BT socket succeded");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySocket]\t opened BT socket number %d\n", BTSocket);
			fclose(pLogFile);
		}
	}

	return BTSocket;
}

int WINAPI MyConnect(_In_ SOCKET s, _In_ const struct sockaddr *name, _In_ int namelen)
{
	char recvBuf[1024] = { 0 };

	// if tcp is not needed, we will connect to BTSocket. 
	// if tcp is needed, we already opened a socket in MyWSALookupServiceNext and connected to it.
	int iResult = CXN_SUCCESS; 
	FILE* pLogFile = NULL;
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyConnect]\t Entered\n");
	fclose(pLogFile);

	//MsgBox("HookDll : Entered MyConnect");
	if (workWithCloudServer && BTdeviceIsRemote) {
		// send a request for name from BT proxy
		char *msgToBTproxy = "msgFromHKW_connect";
		iResult = pSend(TCPSocket, msgToBTproxy, strlen(msgToBTproxy), 0);
		if (iResult) {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyConnect]\t Sent command : %s", msgToBTproxy);
			fclose(pLogFile);
			iResult = recv(TCPSocket, recvBuf, 1024, 0);
			if (iResult > 0) {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MyConnect]\t Received %s (%d Bytes)\n", recvBuf, iResult);
				fclose(pLogFile);
				return 0;
			}
		}
	} else {
		iResult = pConnect(BTSocket, name, namelen);
		if (iResult < 0) {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyConnect]\t BT connect failed\n");
			fclose(pLogFile);
		}
		else {
			//MsgBox("connect succeded");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyConnect]\t BT Connect succeded to socket %d\n", s);
			fclose(pLogFile);
		}
	}
	

	return iResult;
}


int WINAPI MySend(SOCKET s, const char* buf, int len, int flags)
{
	int iResult;
	FILE* pLogFile = NULL;
	char recvBuf[1024] = { 0 };
	//MsgBox("HookDll : Entered MySend");
	//char* msg = "Message from HookDLL  ";
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MySend]\t Entered\n");
	fclose(pLogFile);
	if (workWithCloudServer && BTdeviceIsRemote) {
		char *msgToBTproxy = "msgFromHKW_send";
		iResult = pSend(TCPSocket, msgToBTproxy, strlen(msgToBTproxy), 0);
		fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
		fprintf(pLogFile, "[MySend]\t Sent command : %s", msgToBTproxy);
		fclose(pLogFile);

		if (iResult) {
			// msgFromHKW_send was sent successfully. send actual message
			iResult = pSend(TCPSocket, buf, strlen(buf), flags);
			if (iResult == SOCKET_ERROR) {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MySend]\t TCP send failed with error %d\n", iResult);
				fclose(pLogFile);

				TCPSocketClosed = true;
				pClosesocket(TCPSocket);
				WSACleanup();
				return -1;
			}
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySend]\t Sent actual message : %s\n", buf);
			fprintf(pLogFile, "[MySend]\t Waiting before recv...\n");
			fclose(pLogFile);
			iResult = recv(TCPSocket, recvBuf, 1024, 0);
			if (iResult > 0) {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MySend]\t Received %s (%d Bytes)\n", recvBuf, iResult);
				fclose(pLogFile);
				return 0;
			}
			// if recv failed, MySend was failed sending the actual message
			return -1;
		} else if (iResult == SOCKET_ERROR) {
			//MsgBox("send failed with error: \n");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySend]\t TCP send failed with error %d\n", iResult);
			fclose(pLogFile);

			TCPSocketClosed = 1;
			pClosesocket(TCPSocket);
			WSACleanup();
			return -1;
		}
		else {
			//MsgBox("send succeded");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySend]\t TCP send succeeded. Message = %s, iResult =  %d\n", buf, iResult);
			fclose(pLogFile);
		}
	}
	else {
		iResult = pSend(BTSocket, buf, len, flags);
		if (iResult == SOCKET_ERROR) {
			//MsgBox("send failed with error: \n");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySend]\t BT send failed with error %d\n", iResult);
			fclose(pLogFile);

			TCPSocketClosed = 1;
			pClosesocket(TCPSocket);
			WSACleanup();
			
			return -1;
		}
		else {
			//MsgBox("send succeded");
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MySend]\t BT send succeeded - %s (%d Bytes)\n", buf, iResult);
			fclose(pLogFile);
		}
	}
	
	return iResult;
}

int WINAPI MyClosesocket(_In_ SOCKET s)
{
	int iResult;
	FILE* pLogFile = NULL;
	//MsgBox("HookDll : Entered MyClosesocket");
	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyClosesocket]\t Entered\n");
	fclose(pLogFile);
	if (!TCPSocketClosed && BTdeviceIsRemote) {
		if (TCPSocket != INVALID_SOCKET) {
			fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
			fprintf(pLogFile, "[MyClosesocket]\t TCPSocket is alive\n");
			fclose(pLogFile);
			char *msgToBTproxy = "msgFromHKW_closeSockets";
			iResult = pSend(TCPSocket, msgToBTproxy, strlen(msgToBTproxy), 0);
			if (iResult) {
				fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
				fprintf(pLogFile, "[MyClosesocket]\t Sent request to BTProxy to close sockets\n");
				fclose(pLogFile);
			}
		}
	}

	int res1;
	res1 = TCPSocketClosed ? 0 : pClosesocket(TCPSocket); // close socket only if it wasn't closed already
	int res2;
	res2 = (BTdeviceIsRemote) ? 0 : pClosesocket(BTSocket);

	fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	fprintf(pLogFile, "[MyClosesocket]\t Sockets closed - %d, %d (zeros are successes)\n", res1, res2);
	fclose(pLogFile);

	iResult = shutdown(TCPSocket, SD_SEND);
	
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
	FILE* pLogFile = NULL;
	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//fprintf(pLogFile, "[DllMain]\t Entered with dwReason = %d\n", dwReason);
	//fclose(pLogFile);

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

	//fopen_s(&pLogFile, "C:\\Users\\Itay\\Documents\\Log.txt", "a+");
	//fprintf(pLogFile, "[DllMain]\t Done\n");
	//fclose(pLogFile);
	return TRUE;
}


