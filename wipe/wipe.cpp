// wipe.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#ifdef _DEBUG
#define DEBUG_NO_REAL_WIPE
#endif

#define BIN_LINE 16

void printBinary(const char *data, const size_t size) {
	TCHAR c[BIN_LINE + 1];
	c[BIN_LINE] = 0;
	size_t pos = 0;
	for (size_t i = 0; i < size; i++) {
		if (pos == 0) {
			_tprintf_s(_T("[%08zX] "), i);
		}
		char b = data[i];
		c[pos] = (b >= 32) ? b : _T('.');
		_tprintf_s(_T("%02X "), (unsigned char)b);
		pos++;
		if (pos >= BIN_LINE) {
			pos = 0;
			_tprintf_s(_T("%s\n"), c);
		}
	}
	if (pos < BIN_LINE) {
		for (; pos < BIN_LINE; pos++) {
			_tprintf_s(_T("   "));
			c[pos] = ' ';
		}
		_tprintf_s(_T("%s\n"), c);
	}
}

void printError(const TCHAR *format, DWORD errId) {
	_tprintf_s(_T("%s\n"), format);

	LPTSTR errorText = NULL;

	DWORD dwSuccess = FormatMessage(
		// use system message tables to retrieve error text
		FORMAT_MESSAGE_FROM_SYSTEM
		// allocate buffer on local heap for error text
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		// Important! will fail otherwise, since we're not 
		// (and CANNOT) pass insertion parameters
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
		errId,
		GetUserDefaultUILanguage(),
		(LPTSTR)&errorText,  // output 
		0, // minimum size for output buffer
		NULL);   // arguments - see note 

	if (dwSuccess && NULL != errorText) {
		_tprintf_s(_T("错误%u：%s\n"), errId, errorText);
		LocalFree(errorText);
		errorText = NULL;
	} else _tprintf_s(_T("错误%u：（无法获取错误信息）\n"), errId);
}

const ULONGLONG TICKS_PER_HOUR = 3600000ULL;
const ULONGLONG TICKS_PER_MINUTE = 60000ULL;
const ULONGLONG TICKS_PER_SECOND = 1000ULL;

void formatTime(TCHAR *out, const size_t bufSize, const ULONGLONG ullTickCount) {
	if (ullTickCount > TICKS_PER_HOUR) {
		ULONGLONG h = ullTickCount, m, s, ms;
		m = h % TICKS_PER_HOUR;
		h /= TICKS_PER_HOUR;
		s = m % TICKS_PER_MINUTE;
		m /= TICKS_PER_MINUTE;
		ms = s % TICKS_PER_SECOND;
		s /= TICKS_PER_SECOND;
		_stprintf_s(out, bufSize, _T("%llu:%02llu:%02llu.%03llu"), h, m, s, ms);
	} else if (ullTickCount > TICKS_PER_MINUTE) {
		ULONGLONG  m = ullTickCount, s, ms;
		s = m % TICKS_PER_MINUTE;
		m /= TICKS_PER_MINUTE;
		ms = s % TICKS_PER_SECOND;
		s /= TICKS_PER_SECOND;
		_stprintf_s(out, bufSize, _T("%llu:%02llu.%03llu"), m, s, ms);
	} else {
		ULONGLONG  s = ullTickCount, ms;
		ms = s % TICKS_PER_SECOND;
		s /= TICKS_PER_SECOND;
		_stprintf_s(out, bufSize, _T("%llu.%03llu"), s, ms);
	}
}

int run(DWORD drive) {
	TCHAR strPath[MAX_PATH];
	_stprintf_s(strPath, MAX_PATH, _T("\\\\.\\PhysicalDrive%u"), drive);
	_tprintf_s(_T("Path: %s\n"), strPath);
	HANDLE hDisk = CreateFile(strPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hDisk == INVALID_HANDLE_VALUE) {
		printError(L"CreateFile Error", GetLastError());
		return EXIT_FAILURE;
	}

	GET_LENGTH_INFORMATION DiskSize;
	DWORD dwReturned;
	BOOL bOK;

	bOK = DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, (LPVOID)&DiskSize, sizeof(GET_LENGTH_INFORMATION), &dwReturned, NULL);
	if (bOK != TRUE) {
		printError(_T("DeviceIoControl Error"), GetLastError());
		return EXIT_FAILURE;
	}
	if (dwReturned != sizeof(GET_LENGTH_INFORMATION)) {
		_tprintf_s(_T("WTF: DeviceIoControl returned unexpected length\n"));
		return EXIT_FAILURE;
	}
	_tprintf_s(_T("Disk size: %llu GB\n"), DiskSize.Length.QuadPart >> 30);

#ifndef DEBUG_NO_REAL_WIPE

	_tprintf_s(_T("\n"));
	_tprintf_s(_T("=== ULTIMATE WARNING ===\n\n"));
	_tprintf_s(_T("  ALL data on this physical drive will be lost.\n"));
	_tprintf_s(_T("  You may not able to boot your computer anymore once wipe STARTED.\n"));
	_tprintf_s(_T("  This operation can NOT be undone.\n"));
	_tprintf_s(_T("  Enter UPPERCASE yes if you are 100%% sure.\n"));
	_tprintf_s(_T("\n?"));
	TCHAR yes[8];
	_tscanf_s(_T("%s"), yes, 8);
	if (_tcscmp(yes, _T("YES")) != 0) {
		_tprintf_s(_T("cancelled.\n\n"));
		return EXIT_SUCCESS;
	}
	_tprintf_s(_T("\n"));

#endif // !DEBUG_NO_REAL_WIPE

	HANDLE hHeap = GetProcessHeap();
	if (NULL == hHeap) {
		printError(_T("GetProcessHeap Error"), GetLastError());
		return EXIT_FAILURE;
	}

	size_t size = DiskSize.Length.QuadPart;
	DWORD dwBufSize = 1 << 25; // 32 MiB
	DWORD dwWritten;
	LPVOID lpBuffer = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, dwBufSize);
	if (NULL == lpBuffer) {
		printError(_T("HeapAlloc Error"), GetLastError());
		return EXIT_FAILURE;
	}

	TCHAR etaBuffer[64];
	ULONGLONG ullStart = GetTickCount64();
	for (size_t offset = 0; offset < size; offset += dwWritten) {
		DWORD dwTryWrite;
		if (offset + dwBufSize <= size) dwTryWrite = dwBufSize;
		else dwTryWrite = (DWORD)(size - offset);
#ifdef DEBUG_NO_REAL_WIPE
		bOK = ReadFile(hDisk, lpBuffer, dwTryWrite, &dwWritten, NULL);
#else
		bOK = WriteFile(hDisk, lpBuffer, dwTryWrite, &dwWritten, NULL);
#endif
		if (bOK != TRUE) {
			printError(_T("WriteFile Error"), GetLastError());
			bOK = HeapFree(hHeap, 0, lpBuffer);
			if (bOK != TRUE) {
				printError(_T("HeapFree Error"), GetLastError());
			}
			return EXIT_FAILURE;
		}
		if (dwWritten < dwTryWrite) {
			_tprintf_s(_T("\nWARNING: Short read at %llX, expected %llX, actual %llX\n", offset, dwBufSize, dwWritten));
		}
		// printBinary((char*)lpBuffer, dwSize);
		double percent = (double)offset / (double)DiskSize.Length.QuadPart;
		double eta = (double)(GetTickCount64() - ullStart) / percent * (1.0 - percent);
		formatTime(etaBuffer, 64, (ULONGLONG)eta);
		_tprintf_s(_T("Now offset: %llX / %llX %.02lf%% ETA: %s        \r"), offset, DiskSize.Length.QuadPart,
			percent * 1e+2, etaBuffer);
	}

	bOK = HeapFree(hHeap, 0, lpBuffer);
	if (bOK != TRUE) {
		printError(_T("HeapFree Error"), GetLastError());
	}

	_tprintf_s(_T("\n\n"));
	_tprintf_s(_T("finished normally.\n"));
	return EXIT_SUCCESS;
}

int _tmain() {
	setlocale(LC_ALL, "");
	DWORD drive = 0;
	_tprintf_s(_T("Physical drive(e.g. 0):"));
	_tscanf_s(_T("%u"), &drive);
	int ret = run(drive);
	system("pause");
	return ret;
}
