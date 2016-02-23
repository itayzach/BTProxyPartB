#define CXN_MAX_INQUIRY_RETRY             3
#define CXN_DELAY_NEXT_INQUIRY            5
#define CXN_SUCCESS                       0
#define CXN_ERROR                         1

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"

#include <stdio.h>
#include <WinSock2.h>
#include <ws2bth.h>
#include <bthsdpdef.h>
#include <BluetoothAPIs.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
	wchar_t* wString = new wchar_t[4096];
	MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
	return wString;
}

int NameToBthAddr(char *pszRemoteName, PSOCKADDR_BTH pRemoteBthAddr)
{
	INT             iResult = CXN_SUCCESS;
	BOOL            bContinueLookup = FALSE, bRemoteDeviceFound = FALSE;
	ULONG           ulFlags = 0, ulPQSSize = sizeof(WSAQUERYSET);
	HANDLE          hLookup = NULL;
	PWSAQUERYSET    pWSAQuerySet = NULL;

	ZeroMemory(pRemoteBthAddr, sizeof(*pRemoteBthAddr));

	pWSAQuerySet = (PWSAQUERYSET)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		ulPQSSize);
	if (NULL == pWSAQuerySet) {
		iResult = STATUS_NO_MEMORY;
		printf("[BTClient] Unable to allocate memory for WSAQUERYSET\n");
	}

	//
	// Search for the device with the correct name
	//
	printf("[BTClient] Looking for %s...\n", pszRemoteName);
	if (CXN_SUCCESS == iResult) {

		for (INT iRetryCount = 0;
			!bRemoteDeviceFound && (iRetryCount < CXN_MAX_INQUIRY_RETRY);
			iRetryCount++) {
			//
			// WSALookupService is used for both service search and device inquiry
			// LUP_CONTAINERS is the flag which signals that we're doing a device inquiry.
			//
			ulFlags = LUP_CONTAINERS;

			//
			// Friendly device name (if available) will be returned in lpszServiceInstanceName
			//
			ulFlags |= LUP_RETURN_NAME;

			//
			// BTH_ADDR will be returned in lpcsaBuffer member of WSAQUERYSET
			//
			ulFlags |= LUP_RETURN_ADDR;

			if (0 == iRetryCount) {
				printf("[BTClient] Inquiring device from cache...\n");
			}
			else {
				//
				// Flush the device cache for all inquiries, except for the first inquiry
				//
				// By setting LUP_FLUSHCACHE flag, we're asking the lookup service to do
				// a fresh lookup instead of pulling the information from device cache.
				//
				ulFlags |= LUP_FLUSHCACHE;

				//
				// Pause for some time before all the inquiries after the first inquiry
				//
				// Remote Name requests will arrive after device inquiry has
				// completed.  Without a window to receive IN_RANGE notifications,
				// we don't have a direct mechanism to determine when remote
				// name requests have completed.
				//
				printf("[BTClient] Unable to find device.  Waiting for %d seconds before re-inquiry...\n", CXN_DELAY_NEXT_INQUIRY);
				Sleep(CXN_DELAY_NEXT_INQUIRY * 1000);

				printf("[BTClient] Inquiring device ...\n");
			}

			//
			// Start the lookup service
			//
			iResult = CXN_SUCCESS;
			hLookup = 0;
			bContinueLookup = FALSE;
			ZeroMemory(pWSAQuerySet, ulPQSSize);
			pWSAQuerySet->dwNameSpace = NS_BTH;
			pWSAQuerySet->dwSize = sizeof(WSAQUERYSET);
			printf("[BTClient] before WSALookupServiceBegin\n");
			iResult = WSALookupServiceBegin(pWSAQuerySet, ulFlags, &hLookup);
			printf("[BTClient] after WSALookupServiceBegin\n");
			//
			// Even if we have an error, we want to continue until we
			// reach the CXN_MAX_INQUIRY_RETRY
			//
			if ((NO_ERROR == iResult) && (NULL != hLookup)) {
				bContinueLookup = TRUE;
			}
			else if (0 < iRetryCount) {
				printf("[BTClient] WSALookupServiceBegin() failed with error code %d, WSAGetLastError = %d\n", iResult, WSAGetLastError());
				break;
			}

			while (bContinueLookup) {
				//
				// Get information about next bluetooth device
				//
				// Note you may pass the same WSAQUERYSET from LookupBegin
				// as long as you don't need to modify any of the pointer
				// members of the structure, etc.
				//
				// ZeroMemory(pWSAQuerySet, ulPQSSize);
				// pWSAQuerySet->dwNameSpace = NS_BTH;
				// pWSAQuerySet->dwSize = sizeof(WSAQUERYSET);
				printf("[BTClient] Before WSALookupServiceNext\n");
				if (NO_ERROR == WSALookupServiceNext(hLookup,
					ulFlags,
					&ulPQSSize,
					pWSAQuerySet)) {

					//
					// Compare the name to see if this is the device we are looking for.
					//
					wchar_t* wcRemoteName = convertCharArrayToLPCWSTR(pszRemoteName);
					if ((pWSAQuerySet->lpszServiceInstanceName != NULL) &&
						(0 == wcscmp(pWSAQuerySet->lpszServiceInstanceName, wcRemoteName))) {
						delete wcRemoteName;
						//
						// Found a remote bluetooth device with matching name.
						// Get the address of the device and exit the lookup.
						//
						wprintf(L"[BTClient] Found %s!\n", pWSAQuerySet->lpszServiceInstanceName);
						//printf("[BTClient] lpszServiceInstanceName addr %d\n", (pWSAQuerySet->lpszServiceInstanceName));

						//printf("[BTClient] lpcsaBuffer address is %d\n", (pWSAQuerySet->lpcsaBuffer));
						//printf("[BTClient]\t pResults->...->btAddr: %012X\n", ((PSOCKADDR_BTH)pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr)->btAddr);

						//printf("[BTClient] RemoteAddr address is %d\n", (pWSAQuerySet->lpcsaBuffer->RemoteAddr));

						CopyMemory(pRemoteBthAddr,
							(PSOCKADDR_BTH)pWSAQuerySet->lpcsaBuffer->RemoteAddr.lpSockaddr,
							sizeof(*pRemoteBthAddr));
						bRemoteDeviceFound = TRUE;
						bContinueLookup = FALSE;
						printf("[BTClient] Finished copy\n");
					}
					else {
						wprintf(L"[BTClient] Found %s, still looking...\n", pWSAQuerySet->lpszServiceInstanceName);
					}
				}
				else {
					iResult = WSAGetLastError();
					if (WSA_E_NO_MORE == iResult) { //No more data
						printf("[BTClient] WSA_E_NO_MORE\n");
						//
						// No more devices found.  Exit the lookup.
						//
						bContinueLookup = FALSE;
					}
					else if (WSAEFAULT == iResult) {

						//
						// The buffer for QUERYSET was insufficient.
						// In such case 3rd parameter "ulPQSSize" of function "WSALookupServiceNext()" receives
						// the required size.  So we can use this parameter to reallocate memory for QUERYSET.
						//
						HeapFree(GetProcessHeap(), 0, pWSAQuerySet);

						pWSAQuerySet = (PWSAQUERYSET)HeapAlloc(GetProcessHeap(),
							HEAP_ZERO_MEMORY,
							ulPQSSize);
						printf("[BTClient] Got WSAEFAULT\n");
						if (NULL == pWSAQuerySet) {
							printf("[BTClient] Unable to allocate memory for WSAQERYSET\n");
							iResult = STATUS_NO_MEMORY;
							bContinueLookup = FALSE;
						}
					}
					else {
						printf("[BTClient] WSALookupServiceNext() failed with error code %d\n", iResult);
						bContinueLookup = FALSE;
					}
				}
			}

			//
			// End the lookup service
			//
			WSALookupServiceEnd(hLookup);
			if (STATUS_NO_MEMORY == iResult) {
				break;
			}
		}
	}

	if (NULL != pWSAQuerySet) {
		HeapFree(GetProcessHeap(), 0, pWSAQuerySet);
		pWSAQuerySet = NULL;
	}

	if (bRemoteDeviceFound) {
		iResult = CXN_SUCCESS;
	}
	else {
		iResult = CXN_ERROR;
	}
	return iResult;
}

int __cdecl main(int argc, char **argv)
{
	WORD 			wVersionRequested = 0x202;
	WSADATA 		wsaData;
	SOCKET 			LocalSocket = INVALID_SOCKET;
	SOCKADDR_BTH    SockAddrBthServer;
	char			*Faked_RemoteBTName = "faked_btdevice";
	char			*RemoteBTName = "btdevice";
	char 			*sendbuf = "Hello from DLL\n";
	int 			iResult;

	// =========================================
	// Initialize Winsock
	// =========================================
	iResult = WSAStartup(wVersionRequested, &wsaData);
	if (iResult != 0) {
		printf("[BTClient] WSAStartup failed with error: %d\n", iResult);
		return 1;
	}
	printf("[BTClient] Initialized Winsock\n");

	// =========================================
	// Look for server address using Name
	// =========================================
	printf("[BTClient] calling NameToBthAddr with %s\n", RemoteBTName);
	iResult = NameToBthAddr(RemoteBTName, &SockAddrBthServer);
	if (iResult == CXN_ERROR) {
		printf("[BTClient] NameToBthAddr failed\n");
		WSACleanup();
		return 1;
	}
	else {
		printf("[BTClient] %s's MAC is 0x%llX \n", RemoteBTName, SockAddrBthServer.btAddr);
	}


	// After the btAddr field is set (found using NameToBthAddr function),
	// set the other parameters
	SockAddrBthServer.addressFamily = AF_BTH;
	SockAddrBthServer.serviceClassId = SerialPortServiceClass_UUID;
	SockAddrBthServer.port = 0; // was BT_PORT_ANY before name lookup

	// =========================================
	// Create a SOCKET for connecting to server
	// =========================================
	LocalSocket = socket(AF_BTH,				//ai_family
		SOCK_STREAM, 		//ai_socktype
		BTHPROTO_RFCOMM);	//ai_protocol
	if (LocalSocket == INVALID_SOCKET) {
		printf("[BTClient] socket failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}
	printf("[BTClient] Created local socket = %d\n", LocalSocket);
	// =========================================
	// Connect to server
	// =========================================
	printf("[BTClient] Attemping to connect\n");
	iResult = connect(LocalSocket, (struct sockaddr *) &SockAddrBthServer, sizeof(SOCKADDR_BTH));
	if (iResult == SOCKET_ERROR) {
		printf("[BTClient] Connect failed with error: %d\n", WSAGetLastError());
		closesocket(LocalSocket);
		return 1;
	}
	printf("[BTClient] Connected to server. Sending message to server\n");

	// =========================================
	// Send an initial buffer
	// =========================================
	iResult = send(LocalSocket, sendbuf, (int)strlen(sendbuf), 0);
	if (iResult == SOCKET_ERROR) {
		printf("[BTClient] send failed with error: %d\n", WSAGetLastError());
		closesocket(LocalSocket);
		WSACleanup();
		return 1;
	}
	printf("[BTClient] Message to server was sent\n");

	// =========================================
	// cleanup
	// =========================================
	printf("[BTClient] Cleaning up\n");
	closesocket(LocalSocket);
	WSACleanup();

	return 0;
}
