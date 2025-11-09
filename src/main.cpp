// Copyright Alternity Arts. All Rights Reserved.

#include "ASTD/ASTD.h"

#include <complex>
#include <iostream>
#include <string>

constexpr uint8 CREATE_THREAD_ACCESS = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ; 
HANDLE GAppExitEvent = nullptr;

struct SInjectContext
{
	HANDLE Process;
	LPVOID Buffer;
	SIZE_T BufferSize;
	HANDLE Thread;
};

bool CleanupInject(const SInjectContext& ctx)
{
	if (ctx.Buffer)
	{
		VirtualFreeEx(ctx.Process, ctx.Buffer, 0, MEM_RELEASE);
	}

	if (ctx.Thread)
	{
		CloseHandle(ctx.Thread);
	}

	if (ctx.Process)
	{
		CloseHandle(ctx.Process);
	}

	return true;
}

bool InjectDLLAsync(const std::string& dllPath, DWORD pid, SInjectContext& outContext)
{
	if (!pid) return false;
	
	const HANDLE proc = OpenProcess(CREATE_THREAD_ACCESS, FALSE, pid);
	if (!proc)
	{
		std::cerr << "OpenProcess failed: " << GetLastError() << std::endl;
		return false;
	}
 
	const LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
	if (!func)
	{
		std::cerr << "GetProcAddress failed: " << GetLastError() << std::endl;
		return false;
	}

	const LPVOID buffer = VirtualAllocEx(proc, nullptr, dllPath.length() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!buffer)
	{
		std::cerr << "VirtualAllocEx failed: " << GetLastError() << std::endl;
		return false;
	}

	const bool bufWrittenSuccess = WriteProcessMemory(
		proc,
		buffer,
		dllPath.c_str(),
		dllPath.length() + 1,
		nullptr
	);
	if (!bufWrittenSuccess)
	{
		VirtualFreeEx(proc, buffer, 0, MEM_RELEASE);
		std::cerr << "WriteProcessMemory failed: " << GetLastError() << std::endl;
		return false;
	}
	
	HANDLE thread = CreateRemoteThread(
		proc,
		nullptr,
		0,
		func,
		buffer,
		0,
		nullptr
	);

	if (!thread)
	{
		VirtualFreeEx(proc, buffer, 0, MEM_RELEASE);
		std::cerr << "CreateRemoteThread failed: " << GetLastError() << std::endl;
		return false;
	}

	outContext = { proc, buffer, dllPath.length(), thread };
	return true;
}

bool InjectDLLSync(const std::string& dllPath, DWORD pid, const uint64 timeoutMs = 10000)
{
	SInjectContext ctx;
	if (!InjectDLLAsync(dllPath, pid, ctx))
	{
		std::cerr << "InjectDLL failed" << std::endl;
		return false;
	}

	bool success = true;
	const uint64 startMs = GetTickCount64();
	while (true)
	{
		if (const DWORD res = WaitForSingleObject(ctx.Thread, 100);
			res == WAIT_OBJECT_0)
		{	
			std::cout << "DLL injected" << std::endl;
			break;
		}

		if (timeoutMs > 0 &&
			GetTickCount64() - startMs > timeoutMs)
		{
			const double timeoutAsSec = (double)timeoutMs / SMisc::MS_PER_SECOND;
			std::cerr << "DLL injection timed out (" << timeoutAsSec << " seconds)" << std::endl;
			success = false;
			break;	
		}
	}

	CleanupInject(ctx);
	return success;
}

bool StartDLLInjectedProcess(const std::string& dllPath, const std::string& executablePath, const uint64 timeoutMs = 10000)
{
	STARTUPINFO si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};

	const bool createSucceed = CreateProcess(
		executablePath.c_str(),				   // Path to executable
		nullptr,                               // Command line
		nullptr,                               // Process security
		nullptr,                               // Thread security
		FALSE,                                 // Inherit handles
		CREATE_SUSPENDED,                      // Flags
		nullptr,                               // Environment
		nullptr,                               // Current directory
		&si,                                   // Startup info
		&pi                                    // Process info
	);
	if (!createSucceed)
	{
		std::cerr << "Failed to create process " << std::endl;
		return false;
	}

	std::cout << "Process created with PID: '" << pi.dwProcessId << "'" << std::endl;
	const bool injectSucceed = InjectDLLSync(dllPath, pi.dwProcessId);
	if (injectSucceed)
	{
		ResumeThread(pi.hThread);
	}
	else
	{
		TerminateProcess(pi.hProcess, 1);
		std::cerr << "Process terminated" << std::endl;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	return injectSucceed;
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	char str[255];
	if (!hWnd)
		return TRUE;        // Not a window
	if (!::IsWindowVisible(hWnd))
		return TRUE;        // Not visible
	if (!SendMessage(hWnd, WM_GETTEXT, sizeof(str), (LPARAM)str))
		return TRUE;        // No window title

	DWORD pid;
	GetWindowThreadProcessId(hWnd, &pid);
	std::cout << "PID: " << pid << '\t' << str << '\t' << std::endl;
	return TRUE;
}

bool CheckFilePath(const std::string& filePath)
{
	if (DWORD attrs = GetFileAttributesA(filePath.c_str());
		attrs == INVALID_FILE_ATTRIBUTES)
	{
		std::cerr << "File '" << filePath << "' doesn't exist or can't be accessed" << std::endl;
		return false;
	}

	return true;
}

int32 RunOption(int argc, char* argv[], int32 option)
{
	std::string dllPath;
	if (argc > 1)
	{
		dllPath = argv[1];
		std::cout << "DLL file provided: " << dllPath << std::endl;
	}
	else
	{
		std::cout << "Enter DLL file: ";
		std::getline(std::cin, dllPath);
	}

	if (!CheckFilePath(dllPath)) return 1;
	
	switch (option)
	{
		case 1:
		{
			std::string executablePath;
			if (argc > 2)
			{
				executablePath = argv[2];
				std::cout << "Executable file provided: " << executablePath << std::endl;
			}
			else
			{
				std::cout << "Enter executable file path: ";
				std::getline(std::cin, executablePath);
			}

			std::cout << std::endl;
			if (!CheckFilePath(executablePath)) return 2;

			if (!StartDLLInjectedProcess(dllPath, executablePath))
			{
				return 3;
			}
			break;
		}
		case 2:
		{
			std::cout << "\nAvailable Targets:\n" << std::endl;
			EnumWindows(EnumWindowsProc, NULL);
			std::cout << "\nPick Target PID" << std::endl;

			DWORD pid;
			std::cin >> pid;
			std::cout << std::endl;

			if (!InjectDLLSync(dllPath, pid))
			{
				return 3;
			}
			break;
		}
		default:
		{
			std::cerr << "Invalid option" << std::endl;
			return 2;
		}
	}

	return 0;
}

int main(int argc, char* argv[])
{
	if (argc > 1)
	{
		std::string firstArg = argv[1];
		if (firstArg == "--help" || firstArg == "-h" || firstArg == "?")
		{
			std::cout << "Usage: InjectDLL.exe (optional)<dll_path> (optional)<executable_path>" << std::endl;
			std::cout << "Example: " << std::endl;
			std::cout << "InjectDLL.exe" << std::endl;
			std::cout << "InjectDLL.exe (--help, -h, ?)" << std::endl;
			std::cout << "InjectDLL.exe C:\\MyFolder\\MyDll.dll" << std::endl;
			std::cout << "InjectDLL.exe C:\\MyFolder\\MyDll.dll C:\\MyFolder\\MyExecutable.exe" << std::endl;

			return 0;
		}
	}
	
	if (argc > 2)
	{
		return RunOption(argc, argv, 1);
	}

	std::cout << "Available Options:\n" << std::endl;
	std::cout << "1) Start Process\n2) Attach To Process\n" << std::endl;
	std::cout << "Pick Option: ";

	int32 option;
	std::cin >> option;
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

	return RunOption(argc, argv, option);
}