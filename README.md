# DLL Injector

A Windows DLL injection tool that supports both launching a new process with a DLL pre-injected, and attaching to an existing running process.

## Features

- Launch a new process in suspended state and inject a DLL before it runs
- Attach to an existing running process by PID
- Architecture mismatch detection (32-bit vs 64-bit)
- Configurable injection timeout
- Command-line and interactive modes

## Requirements

- Windows OS
- MSVC or compatible compiler
- Administrator privileges (recommended)
- Injector, DLL, and target process must all match in bitness (32-bit or 64-bit)

## Usage

```
InjectDLL.exe (optional)<dll_path> (optional)<executable_path>
```

### Examples

```
# Interactive mode
InjectDLL.exe

# Inject into a new process
InjectDLL.exe C:\MyFolder\MyDll.dll C:\MyFolder\MyExecutable.exe

# Show help
InjectDLL.exe --help
```

### Interactive Mode

If no arguments are provided, the tool will prompt you to choose an option:

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

DWORD WINAPI DllMain(LPVOID)
{
	WaitForDebugger(); // attach your debugger now
	// ...
}
```

Then attach via **Visual Studio** (`Debug → Attach to Process`) or **x64dbg** using the PID printed by the injector on launch.