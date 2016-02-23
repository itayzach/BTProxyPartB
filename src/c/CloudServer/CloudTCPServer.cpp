#define DEFAULT_MSGLEN 512
#define DEFAULT_IDLEN 7 // btproxy / windspc
#define DEFAULT_PORT "4020"
#define COMM_THREADS_NUM 2

#define WINDSPC 0
#define BTPROXY 1

#pragma comment(lib, "ws2_32.lib") //Winsock Library
#include<stdio.h>
#include<winsock2.h>
#include<ws2tcpip.h>
//#include <windows.h>

 DWORD WINAPI CommChannel( LPVOID lpParam );

// Sockets struct for the comm channel threads
typedef struct Sockets {
    int ID;
	int SrcSocket;
    int DstSocket;
} SocketsStruct, *pSocketsStruct;
 
 
DWORD WINAPI CommChannel( LPVOID lpParam ) {
	char *msgFromSrc = NULL;
	int recvmsglen = DEFAULT_MSGLEN;
	int iResult, iSendResult;
	// Convert to the correct data type
	pSocketsStruct pSockets = (pSocketsStruct)lpParam;
	//printf("[%s (%d->%d)] Thread created\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
	msgFromSrc = (char*)malloc(DEFAULT_MSGLEN);
	while (true) {
		// recv message from pc socket
		if (msgFromSrc == NULL) {
			printf("[%s (%d->%d)] can't allocate msgFromSrc string\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
			closesocket(pSockets->SrcSocket);
			closesocket(pSockets->DstSocket);
			WSACleanup();
			return 1;
		}
		
		printf("[%s (%d->%d)] Waiting for message from src...\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
		iResult = recv(pSockets->SrcSocket, msgFromSrc, recvmsglen, 0);
		if (iResult > 0) {
			//printf("[%s (%d->%d)] Bytes received from src: %d\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, iResult);
			msgFromSrc[iResult] = '\0';
			printf("[%s (%d->%d)] Received %s from src\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, msgFromSrc);
		} else {
			printf("[%s (%d->%d)] recv from src failed with error: %d\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, WSAGetLastError());
			closesocket(pSockets->SrcSocket);
			//closesocket(pSockets->DstSocket);
			//WSACleanup();
			return 1;
		}
		if (strcmp(msgFromSrc, "msgFromHKW_closeSockets") == 0 ) {
			if (pSockets->ID == WINDSPC) {
				printf("[%s (%d->%d)] Closing WINDSPC socket\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
				closesocket(pSockets->SrcSocket);
				printf("[%s (%d->%d)] Not breaking...\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
				printf("[%s (%d->%d)] Sending message %s to dst...\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, msgFromSrc);
				send( pSockets->DstSocket, msgFromSrc, strlen(msgFromSrc), 0 );
				break;
			}
			printf("[%s (%d->%d)] received msgFromHKW_closeSockets, but not exiting loop\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
			//break;
		}
		// send data from src to dst socket
		printf("[%s (%d->%d)] Sending message %s to dst...\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, msgFromSrc);
		iSendResult = send( pSockets->DstSocket, msgFromSrc, strlen(msgFromSrc), 0 );
		if (iSendResult == SOCKET_ERROR) {
			printf("[%s (%d->%d)] send to dst failed with error: %d\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket, WSAGetLastError());
			// Notify the Src that Dst is not connected yet:
			char* msgFromServer = "msgFromServer_sendToDstFailed";
			iSendResult = send( pSockets->SrcSocket, msgFromServer, strlen(msgFromServer), 0 );

			if (iSendResult == SOCKET_ERROR) {
				closesocket(pSockets->SrcSocket);
				WSACleanup();
				return 1;	
			}
			printf("[%s (%d->%d)] sent to SRC 'msgFromServer_sendToDstFailed'\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);			
			continue;
		}
	}
	printf("[%s (%d->%d)] Thread finished\n", pSockets->ID == WINDSPC ? "HKW" : "BTP", pSockets->SrcSocket, pSockets->DstSocket);
	free(msgFromSrc);
	return 0;
}
 // Main
int main(int argc , char *argv[])
{
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET PCClientSocket, BTProxyClientSocket;
	SOCKET TempClientSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvid[DEFAULT_MSGLEN] = "";
	
    int recvidlen = DEFAULT_IDLEN;
	
	// Two threads for communcation between sockets
	pSocketsStruct pCommThreadData[COMM_THREADS_NUM];
	DWORD          dwCommThreadId[COMM_THREADS_NUM];
	HANDLE         hCommThread[COMM_THREADS_NUM] = { NULL };
	
	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}
	printf("WSA Inited\n");

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	if ( iResult != 0 ) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}
	
	// Create a SOCKET for connecting to server
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	// Setup the TCP listening socket
	iResult = bind( ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}
	printf("Finished bind\n");
	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}
	printf("Listening on socket %d\n", ListenSocket);
	
  	while (TRUE) {
		// Initialize ClientSockets
		PCClientSocket = INVALID_SOCKET;
		BTProxyClientSocket = INVALID_SOCKET;
		// Create sockets for pc and btproxy
		//while (PCClientSocket == INVALID_SOCKET || BTProxyClientSocket == INVALID_SOCKET) {
			// Since we don't know which of the btproxy or the pc will connect first,
			// we keep in TempClientSocket the new client socket, and after we receive the id ("windspc" or "btproxy")
			// we will save at PCClientSocket the pc socket and at BTProxyClientSocket the btproxy socket.
			
			// Accept a client socket
			printf("Before accept\n");
			TempClientSocket = accept(ListenSocket, NULL, NULL);
			if (TempClientSocket == INVALID_SOCKET) {
				printf("accept failed with error: %d\n", WSAGetLastError());
				closesocket(TempClientSocket);
				WSACleanup();
				return 1;
			}
			printf("Accepted connection on TempClientSocket %d\n", TempClientSocket);
			printf("Waiting to receive the id...\n");
			
			iResult = recv(TempClientSocket, recvid, recvidlen, 0);
			if (iResult > 0) {
				printf("Bytes received: %d\n", iResult);
				
				if (strcmp(recvid, "windspc") == 0) {
					printf("Recieved windspc\n");
					if (PCClientSocket == INVALID_SOCKET) {
						PCClientSocket = TempClientSocket;
						printf("Creating thread for windspc\n");
						// Create thread for WINDSPC
						pCommThreadData[WINDSPC] = (pSocketsStruct) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
							sizeof(SocketsStruct));
						if (pCommThreadData[WINDSPC] == NULL) {
							printf("Cannot allocate data for thread %d\n", WINDSPC);
							closesocket(PCClientSocket);
							WSACleanup();
							return 1;
						}
						pCommThreadData[WINDSPC]->SrcSocket = PCClientSocket;
						if (hCommThread[BTPROXY] != NULL) {
							pCommThreadData[WINDSPC]->DstSocket = pCommThreadData[BTPROXY]->SrcSocket;
							pCommThreadData[BTPROXY]->DstSocket = pCommThreadData[WINDSPC]->SrcSocket;
						} else {
							pCommThreadData[WINDSPC]->DstSocket = INVALID_SOCKET;
						}
						pCommThreadData[WINDSPC]->ID = WINDSPC;
						
						hCommThread[WINDSPC] = CreateThread( 
							NULL,                         // default security attributes
							0,                            // use default stack size  
							CommChannel,           	      // thread function name
							pCommThreadData[WINDSPC],     // argument to thread function 
							0,                            // use default creation flags 
							&dwCommThreadId[WINDSPC]);    // returns the thread identifier 
						
						// Check the return value for success.
						// If CreateThread fails, terminate execution. 
						// This will automatically clean up threads and memory. 
						if (hCommThread[WINDSPC] == NULL) 
						{
							printf("Cannot create thread %d\n", WINDSPC);
							closesocket(PCClientSocket);
							WSACleanup();
							return 1;
						}
					}
				} else if (strcmp(recvid,"btproxy") == 0) {
					printf("Recieved btproxy\n");
					if (BTProxyClientSocket == INVALID_SOCKET) {
						BTProxyClientSocket = TempClientSocket;
						printf("Creating thread for btproxy\n");
					    // Create thread for BTPROXY
						pCommThreadData[BTPROXY] = (pSocketsStruct) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
							sizeof(SocketsStruct));
						if (pCommThreadData[BTPROXY] == NULL) {
							printf("Cannot allocate data for thread %d\n", BTPROXY);
							closesocket(PCClientSocket);
							WSACleanup();
							return 1;
						}
						pCommThreadData[BTPROXY]->SrcSocket = BTProxyClientSocket;
						if (hCommThread[WINDSPC] != NULL) {
							pCommThreadData[BTPROXY]->DstSocket = pCommThreadData[WINDSPC]->SrcSocket;
							pCommThreadData[WINDSPC]->DstSocket = pCommThreadData[BTPROXY]->SrcSocket;
						} else {
							pCommThreadData[BTPROXY]->DstSocket = INVALID_SOCKET;
						}
						pCommThreadData[BTPROXY]->ID = BTPROXY;
						
						hCommThread[BTPROXY] = CreateThread( 
							NULL,                         // default security attributes
							0,                            // use default stack size  
							CommChannel,           	      // thread function name
							pCommThreadData[BTPROXY],     // argument to thread function 
							0,                            // use default creation flags 
							&dwCommThreadId[BTPROXY]);    // returns the thread identifier 
						// Check the return value for success.
						// If CreateThread fails, terminate execution. 
						// This will automatically clean up threads and memory. 
						if (hCommThread[BTPROXY] == NULL) 
						{
							printf("Cannot create thread %d\n", BTPROXY);
							closesocket(PCClientSocket);
							WSACleanup();
							return 1;
						}
					}
				} else {
					printf("received %s which is not a correct id\n", recvid);
				}
				
			}
			else if (iResult == 0) {
				printf("Connection closing...\n");
			} else  {
				printf("recv failed with error: %d\n", WSAGetLastError());
				closesocket(TempClientSocket);
				WSACleanup();
				return 1;
			}
		//}
		if (PCClientSocket != INVALID_SOCKET && BTProxyClientSocket != INVALID_SOCKET) {
			printf("Both sides are connected\n");		
		}
	}
	printf("Waiting for both threads to finish\n");
	WaitForMultipleObjects(COMM_THREADS_NUM, hCommThread, TRUE, INFINITE);
	printf("Both threads are done\n");

	
	// Close all thread handles and free memory allocations.
	for(int i=0; i<COMM_THREADS_NUM; i++)
	{
		CloseHandle(hCommThread[i]);
		if(pCommThreadData[i] != NULL)
		{
			HeapFree(GetProcessHeap(), 0, pCommThreadData[i]);
			pCommThreadData[i] = NULL;    // Ensure address is not reused.
		}
	}
    // No longer need server socket
    closesocket(ListenSocket);

    // shutdown the connection since we're done
	iResult = shutdown(BTProxyClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(BTProxyClientSocket);
	}
	iResult = shutdown(PCClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(PCClientSocket);
		WSACleanup();
		return 1;
	}
	
    // cleanup
	closesocket(PCClientSocket);
	closesocket(BTProxyClientSocket);
    WSACleanup();
    return 0;
}
