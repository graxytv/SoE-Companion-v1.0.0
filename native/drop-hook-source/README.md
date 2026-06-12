# SoE ijl11 Drop Hook Source

This folder contains the recovered SoE `ijl11.dll` proxy source.

Build with MSYS2 MinGW 32-bit:

```powershell
$env:Path = "C:\msys64\mingw32\bin;$env:Path"
g++ -O2 -m32 -std=c++14 -static -static-libgcc -static-libstdc++ -shared -o build\ijl11.dll ijl11_grail.cpp ijl11.def -lkernel32 -luser32 -lshlwapi
```

After building, copy `build\ijl11.dll` to `src-tauri\resources\ijl11.dll`
before compiling the Tauri app.

The proxy preserves the existing DropIdentified and Auto Grail behavior, and now
also writes structured drop events to `C:\SoECompanion\logs\soe_companion_drops.log`. SoE
Companion consumes that JSONL file through `read_hook_drop_events` and applies
events through the same tracker path as normal scanner drops.

Unsafe `pGame` diagnostic experiments were removed after testing. `D2Game+0x2aae0` must not be called from this proxy DLL, including from hotkey threads or the SetItemFlag hook path. Future diagnostics should use debugger breakpoints or a narrower hook after the exact D2Game lifecycle context is understood.
