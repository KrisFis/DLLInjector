// Copyright Alternity Arts. All Rights Reserved.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Windows.h"

#if defined(_WIN64)
	#define ARCHITECTURE_32 0
#else
	#define ARCHITECTURE_32 1
#endif

#include <fstream>
#include <iostream>
#include <string>
#include <limits>

constexpr DWORD CREATE_THREAD_ACCESS = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
constexpr UINT16 MS_PER_SECOND = 1000;

bool GetDLLBitness(const std::string& dllPath, bool& out32bit)
{
	std::ifstream file(dllPath, std::ios::binary);
	if (!file.is_open())
	{
		std::cerr << "Failed to open DLL: " << dllPath << std::endl;
		return false;
	}

	IMAGE_DOS_HEADER dosHeader;
	file.read((char*)&dosHeader, sizeof(dosHeader));
	if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) // MZ
	{
		std::cerr << "Not a valid PE file in DLL: " << dllPath << std::endl;
		return false;
	}

	file.seekg(dosHeader.e_lfanew, std::ios::beg);

	DWORD peSignature;
	file.read((char*)&peSignature, sizeof(peSignature));
	if (peSignature != IMAGE_NT_SIGNATURE)
	{
		std::cerr << "Not a valid PE signature in DLL: " << dllPath << std::endl;
		return false;
	}

	IMAGE_FILE_HEADER fileHeader;
	file.read((char*)&fileHeader, sizeof(fileHeader));

	switch (fileHeader.Machine)
	{
		case IMAGE_FILE_MACHINE_AMD64:  // 0x8664
		case IMAGE_FILE_MACHINE_IA64:
			out32bit = false;
			return true;
		case IMAGE_FILE_MACHINE_I386:   // 0x014c
			out32bit = true;
			return true;
		default:
			std::cerr << "Unknown machine type: 0x" << std::hex << fileHeader.Machine << " in DLL: " << dllPath << std::endl;
			return false;
	}
}

bool CheckFilePath(const std::string& path, const char* label)
{
	if (path.empty())
	{
		std::cerr << label << " path is empty" << std::endl;
		return false;
	}
	if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		std::cerr << label << " file '" << path << "' doesn't exist or can't be accessed" << std::endl;
		return false;
	}
	return true;
}

bool CheckProcessCompatibility(HANDLE proc)
{
	BOOL proc32 = FALSE;
	IsWow64Process(proc, &proc32);
	if (proc32 != ARCHITECTURE_32)
	{
		std::cerr << "Incompatible architecture between Process and Injector - please use "
				  << (proc32 ? "32-bit" : "64-bit")
				  << " version of injector" << std::endl;
		return false;
	}

	return true;
}

struct SInjectContext
{
	HANDLE Process = nullptr;
	LPVOID Buffer = nullptr;
	SIZE_T BufferSize = 0;
	HANDLE Thread = nullptr;
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

	if (!CheckProcessCompatibility(proc))
	{
		return false;
	}
 
	const LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
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

bool InjectDLLSync(const std::string& dllPath, DWORD pid, const UINT64 timeoutMs)
{
	SInjectContext ctx;
	if (!InjectDLLAsync(dllPath, pid, ctx)) return false;

	bool success = true;
	const UINT64 startMs = GetTickCount64();
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
			const double timeoutAsSec = (double)timeoutMs / MS_PER_SECOND;
			std::cerr << "DLL injection timed out (" << timeoutAsSec << " seconds)" << std::endl;
			success = false;
			break;	
		}
	}

	CleanupInject(ctx);
	return success;
}

bool StartDLLInjectedProcess(const std::string& dllPath, const std::string& executablePath, const UINT64 timeoutMs)
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
	const bool injectSucceed = InjectDLLSync(dllPath, pi.dwProcessId, timeoutMs);
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

struct SLaunchArgs
{
	SLaunchArgs() = default;
	SLaunchArgs(int argc, char* argv[])
	{
		for (INT32 i = 1; i < argc; i++)
		{
			std::string arg = argv[i];
			if (arg == "--help" || arg == "-h" || arg == "-?")
			{
				shouldPrintHelp = true;
				return; // no point parsing further
			}

			if ((arg == "--timeout" || arg == "-t") && i + 1 < argc)
			{
				timeoutMs = std::stoull(argv[++i]);
			}
			else if (dllPath.empty())
			{
				dllPath = std::move(arg);
			}
			else if (execPath.empty())
			{
				execPath = std::move(arg);
			}
		}
	}

	bool CheckDLL() const
	{
		if (!CheckFilePath(dllPath, "DLL")) return false;

		bool dll32 = false;
		if (!GetDLLBitness(dllPath, dll32)) return false;

		if (dll32 != (ARCHITECTURE_32 == 1))
		{
			std::cerr << "Architecture mismatch between DLL and injector. Please use the "
					  << (dll32 ? "32-bit" : "64-bit")
					  << " injector binary" << std::endl;
			return false;
		}

		return true;
	}

	bool CheckExec() const
	{
		return CheckFilePath(execPath, "Executable");
	}

	UINT64 timeoutMs = 10000;
	std::string dllPath;
	std::string execPath;
	bool shouldPrintHelp = false;
};

void PrintHelp(const char* prgName)
{
	std::cout << "Usage: " << prgName << " [options] <dll_path> [executable_path]\n\n";
	std::cout << "Options:\n";
	std::cout << "  -h, --help, -?          Show this help message\n";
	std::cout << "  -t, --timeout <ms>      Injection timeout in milliseconds (default: 10000)\n";
	std::cout << "                          Use 0 for infinite timeout\n\n";
	std::cout << "Arguments:\n";
	std::cout << "  <dll_path>              Path to the DLL to inject\n";
	std::cout << "  [executable_path]       Path to executable (starts a new process)\n";
	std::cout << "                          If omitted, attaches to an existing process\n\n";
	std::cout << "Examples:\n";
	std::cout << "  " << prgName << " --help\n";
	std::cout << "  " << prgName << " C:\\MyFolder\\MyDll.dll\n";
	std::cout << "  " << prgName << " C:\\MyFolder\\MyDll.dll C:\\MyFolder\\MyExecutable.exe\n";
	std::cout << "  " << prgName << " --timeout 0 C:\\MyFolder\\MyDll.dll C:\\MyFolder\\MyExecutable.exe\n";
	std::cout << "  " << prgName << " -t 0 C:\\MyFolder\\MyDll.dll C:\\MyFolder\\MyExecutable.exe\n";
}

int main(int argc, char* argv[])
{
	SLaunchArgs args(argc, argv);

	if (args.shouldPrintHelp)
	{
		PrintHelp(argv[0]);
		return 0;
	}

	// Resolve exec path if provided — skip menu entirely
	if (!args.dllPath.empty() && !args.execPath.empty())
	{
		std::cout << "Executable file provided: " << args.execPath << std::endl;
		if (!args.CheckExec()) return 2;
		if (!StartDLLInjectedProcess(args.dllPath, args.execPath, args.timeoutMs)) return 3;
		return 0;
	}

	// Interactive menu
	std::cout << "\nAvailable Options:\n" << std::endl;
	std::cout << "1) Start Process\n2) Attach To Process\n" << std::endl;
	std::cout << "Pick Option: ";

	INT32 option;
	std::cin >> option;
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

	// Resolve DLL path
	if (!args.dllPath.empty())
	{
		std::cout << "DLL file provided: " << args.dllPath << std::endl;
	}
	else
	{
		std::cout << "Enter DLL file: ";
		std::getline(std::cin, args.dllPath);
	}

	if (!args.CheckDLL()) return 1;

	switch (option)
	{
		case 1:
		{
			std::cout << "Enter executable file: ";
			std::getline(std::cin, args.execPath);

			if (!args.CheckExec()) return 2;
			if (!StartDLLInjectedProcess(args.dllPath, args.execPath, args.timeoutMs)) return 3;
			break;
		}
		case 2:
		{
			std::cout << "\nAvailable Targets:\n" << std::endl;
			EnumWindows(EnumWindowsProc, NULL);
			std::cout << "\nPick Target PID: ";

			DWORD pid;
			std::cin >> pid;

			if (!InjectDLLSync(args.dllPath, pid, args.timeoutMs)) return 3;
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