# ADR-0080: Crash reports from the minidumps

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

The crash handler (Phase 0) writes a minidump when the process fails: an unhandled exception, `abort()`, a failed assertion, an invalid parameter to the C runtime. Nothing read those dumps but a debugger. Phase 15 lists "crash reports from the minidumps" (ADR-0041); telemetry is out (no server, no privacy policy), so a report stays on the player's machine.

When this step began, the sandbox's `crashdumps` folder already held eight dumps written during development, which nobody had read. Release builds had no PDBs, so even a debugger could only give module offsets.

## Decision

- **`platform::ReadCrashDump`** reads a minidump with Windows' dbghelp:
  - what failed (access violation reading or writing an address, stack overflow, heap corruption, "abort or failed assertion" for the code the handler raises), and where;
  - the modules loaded, with their versions;
  - the failing thread's stack, innermost first, as module and offset, with the function, file and line when the module's PDB is found.
- **Only the build that ran is trusted for symbols:** a module's link time stamp and size are compared with the image on disk. A rebuilt executable would name the wrong functions, so its frames keep their offsets only, and the report says the module is no longer the build that ran.
- **Unwinding reads the images:** a dump holds stacks, not code. The images that are still the builds that ran are mapped as the process had them. Their exception directories (`.pdata`) give StackWalk64 the unwind data, so a stack goes through system libraries: from `RaiseException`, through `abort`, down to the caller.
- **Optimized builds have symbols:** MSVC builds other than Debug compile with `/Z7` and link a PDB beside each executable (`/DEBUG:FULL /OPT:REF /OPT:ICF`). The release archive still installs executables only. The release workflow keeps the Windows PDBs as a separate artifact, for reports of a released build (`crash-report --symbols`).
- **Reports at the next start:** the logger keeps the last run's log as `<name>.previous.log` instead of overwriting it. At start, the application writes `<dump>.txt` beside each dump that has none. The report holds the end of the previous log when the dump was written within two minutes of it. `Application::CrashReports` lists what was written, and a warning names each report.
- **The game tells the player:** after a crash, the sandbox's main menu says the game stopped unexpectedly and where the report is, in the player's language.
- **`crash-report`,** a development tool, prints the reports of dumps or directories of dumps.
- **Not on Linux yet:** the crash handler writes no dumps there (core dumps are the system's), and `ReadCrashDump` says so.

Tests crash the existing helper on purpose and read its dumps back: an access violation names `main` in `CrashHelper.cpp` and the address written; an abort is walked from `RaiseException` down to `main`; a report is written once.

## Consequences

- The eight dumps found are all access violations reading address 0x60 in `sandbox.exe`, at offsets around 0xD08xx. They came from builds without PDBs, which are gone, so their functions cannot be named. A null pointer read at offset 0x60 into an object, in four different builds, is a bug left to watch for in Phase 17. The next one will have a name.
- Release builds take longer to link and leave PDBs of a few tens of megabytes beside their executables in the build tree.
- Reports name function and file paths of the build machine. They stay on the player's machine, and the player decides whether to send one.
- dbghelp is single-threaded: reports are read one at a time, behind a lock.

## Alternatives considered

- **Crashpad or Sentry:** a handler, an uploader and a server. Uploading needs the server and the privacy policy this project does not have (ADR-0041 leaves telemetry out).
- **A stack trace written by the crash handler itself, in the crashed process:** no dump reading. But symbol lookup inside a crashed process can fail or deadlock (the heap or the loader lock may be what broke), and a dump holds more.
- **Reports by hand in a debugger:** what there was. It needs Visual Studio or WinDbg and someone to run them.
