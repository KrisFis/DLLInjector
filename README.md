## IOTDPatch

This project is a patch for "The I of the dragon" game, aimed at improving performance and user experience. The patch addresses the following issues:

1) Improves loading times when opening any UI, reducing the time from approximately 10 seconds to less than half a second.
2) Changes sensitivity when in camera rotation mode in comparison to auto-follow mode.

The patch is implemented through DLL injection, achieved by reverse engineering the game's code and patching the InputSystem. The game uses DirectInput and dimap, which are intercepted and patched by the DLL.

Please note that there are known issues with this patch:

1) When the patch is enabled, the keybind option UI may not properly show all inputs, although you can update them (or just update them without the patch).
2) Rotation sensitivity is not exposed in the game's UI, as it cannot be added by the patch.

The project is written in pure C++ and uses CMake for build. It relies on the following libraries:

- Minhook: A library for hooking and patching code.
- ASTD: The project's standard library, which is also written by the author.

To use this patch, the DLL needs to be injected as the first DLL. The best way to do this is to start the game in a suspended state, inject the DLL, and then resume or use a launcher that includes this functionality.

If you need assistance with any aspect of this project, please feel free to ask.
