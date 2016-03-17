#include <winsock2.h>
#include <ws2bth.h>
#include <BluetoothAPIs.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
//#include <windows.h>

#include "C:\Detours\include\detours.h"
#pragma comment(lib,"detours.lib")

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "irprops.lib")
// =============================================================================
// Helper functions
// =============================================================================
// convert const char* to LPCWSTR
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
// Main
// =============================================================================
int main(int argc, char *argv[])
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	//char* ExePath = "C:\\Users\\Itay\\Documents\\GitHub\\BTProxyPartB\\src\\c\\BTClient\\BTClient.exe";
	char* ExePath = "C:\\Users\\itayz\\Documents\\GitHub\\BTProxyPartB\\src\\c\\BTClientProj\\Debug\\BTClientProj.exe";
	//char* ExePath = "C:\\Program Files (x86)\\Phonak\\Phonak Target\\Target.exe";
	char* DllPath = "C:\\Users\\itayz\\Documents\\GitHub\\BTProxyPartB\\src\\c\\Hook\\HookDll\\Debug\\HookDll.dll";
	char* logFilePath = "C:\\Users\\itayz\\Documents\\Log.txt";
	
	FILE*  pLogFile;
	time_t rawtime;
	struct tm* timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fopen_s(&pLogFile, logFilePath, "w+");
	fprintf(pLogFile, "*******************************************\n");
	fprintf(pLogFile, "Ran at: %s", asctime(timeinfo));
	fprintf(pLogFile, "*******************************************\n");	
	fclose(pLogFile);

	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOW;

	if (!DetourCreateProcessWithDllEx(NULL,
		convertCharArrayToLPCWSTR(ExePath), NULL, NULL, TRUE,
		CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED,
		NULL, NULL, &si, &pi,
		DllPath, NULL))
		MsgBox("Detour failed");
	else
		MsgBox("Detour succeeded");

	ResumeThread(pi.hThread);

	WaitForSingleObject(pi.hProcess, INFINITE);

	CloseHandle(&si);
	CloseHandle(&pi);
	return EXIT_SUCCESS;
}