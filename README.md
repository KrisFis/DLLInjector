# DLL Injector

A Windows DLL injection tool that supports both launching a new process with a DLL pre-injected, and attaching to an existing running process.

**Disclaimer**: This tool is intended for **legitimate development and debugging purposes only**, such as modding, instrumentation, and testing of software you own or have permission to modify. Do not use this tool for cheating in games, bypassing anti-cheat systems, unauthorized access, or any other malicious or illegal activity. The author is not responsible for any misuse of this software.

## Features

- Launch a new process in suspended state and inject a DLL before it runs
- Attach to an existing running process by PID
- Architecture mismatch detection (32-bit vs 64-bit)
- Configurable injection timeout
- Command-line and interactive modes

## Requirements

- Windows OS
- MSVC or compatible compiler
- Supported architectures are `x64` and `x86`
- Administrator privileges (optional)
- Injector, DLL, and target process must all match in bitness (32-bit or 64-bit)

## Building

Requires [CMake](https://cmake.org/) 3.31+ and a C++17 compatible compiler (MSVC recommended).

```bash
git clone 
cd DLLInjector
cmake -B build
cmake --build build
```

To explicitly target a specific architecture:

```bash
# 64-bit
cmake -B build -A x64
cmake --build build

# 32-bit
cmake -B build -A Win32
cmake --build build
```

The output binary will be named `DLLInjector.exe` and placed in your build directory.

## Usage

```
InjectDLL.exe [options] <dll_path> [executable_path]
```

### Options

| Option               | Description                                                               |
|----------------------|---------------------------------------------------------------------------|
| `-h, --help, -?`     | Show help message                                                         |
| `-t, --timeout <ms>` | Injection timeout in milliseconds (default: 10000). Use `0` for infinite. |

### Examples

```
# Interactive mode
InjectDLL.exe

# Inject into a new process
InjectDLL.exe C:\MyFolder\MyDll.dll C:\MyFolder\MyExecutable.exe

# Attach to an existing process (will prompt for PID)
InjectDLL.exe C:\MyFolder\MyDll.dll

# Inject with infinite timeout (useful for waiting on a debugger)
InjectDLL.exe --timeout 0 C:\MyFolder\MyDll.dll C:\MyFolder\MyExecutable.exe
InjectDLL.exe -t 0 C:\MyFolder\MyDll.dll C:\MyFolder\MyExecutable.exe

# Show help
InjectDLL.exe --help
```

### Interactive Mode

If no executable path is provided, the tool will prompt you to choose an option:

```
Available Options:

1) Start Process
2) Attach To Process

Pick Option:
```

**Option 1 - Start Process**: Launches a new executable in a suspended state, injects the DLL, then resumes the process.

**Option 2 - Attach To Process**: Lists all visible windows with their PIDs, then injects into your chosen PID.

## Architecture

The injector works by:

1. Opening the target process with the required access rights
2. Allocating memory in the target process for the DLL path
3. Writing the DLL path into the allocated memory
4. Creating a remote thread in the target process that calls `LoadLibraryA` with the DLL path
5. Waiting for the remote thread to complete (default timeout: 10 seconds)
6. Cleaning up allocated memory and handles

## Bitness

The DLL **must match the bitness of the target process**:

| Injector | Target | DLL    | Result     |
|----------|--------|--------|------------|
| 64-bit   | 64-bit | 64-bit | ✅ OK       |
| 32-bit   | 32-bit | 32-bit | ✅ OK       |
| 64-bit   | 32-bit | any    | ❌ Mismatch |
| 32-bit   | 64-bit | any    | ❌ Mismatch |

If a mismatch is detected, the injector will exit early with an error message indicating which version to use instead.

## DLL Entry Point

Your DLL should follow this pattern to work correctly with the injector:

```cpp
HMODULE GModule = nullptr;

DWORD WINAPI DllThreadUnload(LPVOID lpParameter)
{
	Sleep(100);
	FreeLibraryAndExitThread(GModule, 0);
}

DWORD WINAPI DllThreadMain(LPVOID)
{
	// Your initialization and main logic here
	CreateThread(nullptr, 0, DllThreadUnload, nullptr, 0, nullptr);
	return EXIT_SUCCESS;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved)
{
	if (reasonForCall != DLL_PROCESS_ATTACH) return TRUE;
	GModule = hModule;
	CreateThread(nullptr, 0, DllThreadMain, nullptr, 0, nullptr);
	return TRUE;
}
```

> **Note**: Blocking inside `DllMain` (e.g. waiting for a debugger) is valid, but the injector's `WaitForSingleObject` will time out if the block exceeds the timeout duration. Pass a longer `timeoutMs` value if needed, or `0` to wait indefinitely.

## Debugging

To attach a debugger to the injected DLL before it runs, add a `WaitForDebugger` call at the start of `DllThreadMain`:

```cpp
void WaitForDebugger()
{
	while (!IsDebuggerPresent())
	{
		Sleep(100);
	}
	DebugBreak(); // breaks immediately on attach
}

DWORD WINAPI DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved)
{
	WaitForDebugger(); // attach your debugger now
	// ...
}
```

Then attach via **Visual Studio** (`Debug → Attach to Process`) or **x64dbg** using the PID printed by the injector on launch.