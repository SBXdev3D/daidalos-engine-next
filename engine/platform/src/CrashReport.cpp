// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/platform/CrashReport.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// dbghelp.h must come after windows.h.
#include <dbghelp.h>

#include <mutex>
#include <utility>
#endif

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";
/// The code the crash handler raises to dump from abort() and failed assertions (CrashHandler.cpp).
constexpr std::uint32_t kDumpHereException = 0xE0000DA1;
constexpr std::size_t kMaxFrames = 64;
constexpr std::size_t kLogLines = 40;

std::string ExceptionName(std::uint32_t code) {
    switch (code) {
    case 0xC0000005:
        return "access violation";
    case 0xC00000FD:
        return "stack overflow";
    case 0xC000001D:
        return "illegal instruction";
    case 0xC0000094:
        return "integer division by zero";
    case 0xC0000409:
        return "stack buffer overrun (fast fail)";
    case 0xC0000374:
        return "heap corruption";
    case 0x80000003:
        return "breakpoint";
    case kDumpHereException:
        return "abort or failed assertion";
    default:
        return std::format("exception 0x{:08X}", code);
    }
}

} // namespace

#if defined(_WIN32)

namespace {

/// A view of the dump file, mapped into memory as dbghelp reads it.
class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path) {
        m_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) {
            return;
        }
        m_mapping = CreateFileMappingW(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (m_mapping != nullptr) {
            m_base = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0);
        }
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile() {
        if (m_base != nullptr) {
            UnmapViewOfFile(m_base);
        }
        if (m_mapping != nullptr) {
            CloseHandle(m_mapping);
        }
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
        }
    }
    [[nodiscard]] void* Base() const { return m_base; }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
    void* m_base = nullptr;
};

struct MemoryRange {
    std::uint64_t start = 0;
    std::uint64_t size = 0;
    const std::byte* data = nullptr;
};

struct Module {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::wstring path;
    std::uint32_t timeStamp = 0;
    /// The image on disk is the one that ran: its symbols are the right ones.
    bool matches = false;
};

/// Whether the image at a path is the build a dump saw, by the link time stamp and size in its PE header. A
/// rebuilt executable has other symbols, which would name the wrong functions.
bool SameImage(const std::wstring& path, std::uint32_t timeStamp, std::uint64_t size) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    IMAGE_NT_HEADERS64 nt{};
    file.seekg(dos.e_lfanew);
    if (!file.read(reinterpret_cast<char*>(&nt), sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    return nt.FileHeader.TimeDateStamp == timeStamp && nt.OptionalHeader.SizeOfImage == size;
}

// StackWalk64 reads memory through a callback without a user pointer: the ranges of the dump being read, then
// the images of the modules that are still the builds that ran, mapped as the process had them (the dump keeps
// no code, and unwinding through a system library reads its unwind data). dbghelp is single-threaded anyway, so
// reading dumps is serialized.
std::mutex s_dbghelp;
const std::vector<MemoryRange>* s_memory = nullptr;

BOOL CALLBACK ReadDumpMemory(HANDLE /*process*/, DWORD64 address, PVOID buffer, DWORD size, LPDWORD read) {
    for (const MemoryRange& range : *s_memory) {
        if (address >= range.start && address + size <= range.start + range.size) {
            std::memcpy(buffer, range.data + (address - range.start), size);
            *read = size;
            return TRUE;
        }
    }
    *read = 0;
    return FALSE;
}

/// The modules StackWalk64 unwinds through: where each sits, and its image mapped as the process had it.
struct LoadedImage {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    const std::byte* image = nullptr;
};
const std::vector<LoadedImage>* s_images = nullptr;

const LoadedImage* ImageAt(DWORD64 address) {
    for (const LoadedImage& loaded : *s_images) {
        if (address >= loaded.base && address < loaded.base + loaded.size) {
            return &loaded;
        }
    }
    return nullptr;
}

DWORD64 CALLBACK ModuleBase(HANDLE /*process*/, DWORD64 address) {
    const LoadedImage* loaded = ImageAt(address);
    return loaded != nullptr ? loaded->base : 0;
}

/// The x64 unwind entry of the function at an address, from its image's exception directory (.pdata).
PVOID CALLBACK FunctionEntry(HANDLE /*process*/, DWORD64 address) {
    const LoadedImage* loaded = ImageAt(address);
    if (loaded == nullptr || loaded->image == nullptr) {
        return nullptr;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(loaded->image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(loaded->image + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const auto* entries =
        reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(loaded->image + directory.VirtualAddress);
    const std::size_t count = directory.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
    const auto rva = static_cast<DWORD>(address - loaded->base);
    std::size_t low = 0;
    std::size_t high = count;
    while (low < high) {
        const std::size_t middle = (low + high) / 2;
        if (rva < entries[middle].BeginAddress) {
            high = middle;
        } else if (rva >= entries[middle].EndAddress) {
            low = middle + 1;
        } else {
            return const_cast<IMAGE_RUNTIME_FUNCTION_ENTRY*>(&entries[middle]);
        }
    }
    return nullptr;
}

/// A module's image mapped as an image, as data: what its code and unwind data were in the process.
class MappedImage {
public:
    explicit MappedImage(const std::wstring& path) {
        m_module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    }
    MappedImage(const MappedImage&) = delete;
    MappedImage& operator=(const MappedImage&) = delete;
    MappedImage(MappedImage&& other) noexcept : m_module(std::exchange(other.m_module, nullptr)) {}
    ~MappedImage() {
        if (m_module != nullptr) {
            FreeLibrary(m_module);
        }
    }
    /// The image's first byte: a module loaded as data has its handle's low bits set.
    [[nodiscard]] const std::byte* Base() const {
        return m_module == nullptr ? nullptr
                                   : reinterpret_cast<const std::byte*>(reinterpret_cast<std::uintptr_t>(m_module) &
                                                                        ~std::uintptr_t{3});
    }

private:
    HMODULE m_module = nullptr;
};

std::string Narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

template <class T>
const T* Stream(void* base, ULONG type) {
    PMINIDUMP_DIRECTORY directory = nullptr;
    void* stream = nullptr;
    ULONG size = 0;
    if (MiniDumpReadDumpStream(base, type, &directory, &stream, &size) == FALSE || size < sizeof(T)) {
        return nullptr;
    }
    return static_cast<const T*>(stream);
}

} // namespace

bool CrashReportsSupported() noexcept {
    return true;
}

core::Result<CrashReport> ReadCrashDump(const std::filesystem::path& dump,
                                        std::span<const std::filesystem::path> symbolPaths) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(dump, error)) {
        return core::MakeError(core::ErrorCode::NotFound, kModule, std::format("No dump at {}", dump.string()));
    }
    const MappedFile file(dump);
    void* base = file.Base();
    const auto* bytes = static_cast<const std::byte*>(base);
    const auto* exception = base != nullptr ? Stream<MINIDUMP_EXCEPTION_STREAM>(base, ExceptionStream) : nullptr;
    if (exception == nullptr) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("{} is not a minidump with an exception", dump.string()));
    }

    CrashReport report;
    report.dump = dump;
    report.threadId = exception->ThreadId;
    const MINIDUMP_EXCEPTION& record = exception->ExceptionRecord;
    report.exceptionCode = record.ExceptionCode;
    report.exception = ExceptionName(record.ExceptionCode);
    report.exceptionAddress = record.ExceptionAddress;
    if (record.ExceptionCode == 0xC0000005 && record.NumberParameters >= 2) {
        const char* kind = record.ExceptionInformation[0] == 0   ? "reading"
                           : record.ExceptionInformation[0] == 8 ? "executing"
                                                                 : "writing";
        report.detail = std::format("{} 0x{:X}", kind, record.ExceptionInformation[1]);
    }

    // What the process had in memory, and the modules it had loaded.
    std::vector<MemoryRange> memory;
    if (const auto* list = Stream<MINIDUMP_MEMORY_LIST>(base, MemoryListStream)) {
        for (ULONG index = 0; index < list->NumberOfMemoryRanges; ++index) {
            const MINIDUMP_MEMORY_DESCRIPTOR& range = list->MemoryRanges[index];
            memory.push_back({range.StartOfMemoryRange, range.Memory.DataSize, bytes + range.Memory.Rva});
        }
    }
    if (const auto* list = Stream<MINIDUMP_MEMORY64_LIST>(base, Memory64ListStream)) {
        RVA64 rva = list->BaseRva;
        for (ULONG64 index = 0; index < list->NumberOfMemoryRanges; ++index) {
            const MINIDUMP_MEMORY_DESCRIPTOR64& range = list->MemoryRanges[index];
            memory.push_back({range.StartOfMemoryRange, range.DataSize, bytes + rva});
            rva += range.DataSize;
        }
    }
    std::vector<Module> modules;
    if (const auto* list = Stream<MINIDUMP_MODULE_LIST>(base, ModuleListStream)) {
        for (ULONG index = 0; index < list->NumberOfModules; ++index) {
            const MINIDUMP_MODULE& module = list->Modules[index];
            const auto* name = reinterpret_cast<const MINIDUMP_STRING*>(bytes + module.ModuleNameRva);
            Module read{.base = module.BaseOfImage,
                        .size = module.SizeOfImage,
                        .path = std::wstring(name->Buffer, name->Length / sizeof(WCHAR)),
                        .timeStamp = module.TimeDateStamp};
            read.matches = SameImage(read.path, read.timeStamp, read.size);
            const VS_FIXEDFILEINFO& version = module.VersionInfo;
            const std::string fileName = Narrow(std::filesystem::path(read.path).filename().wstring());
            std::string line = version.dwFileVersionMS != 0
                                   ? std::format("{} {}.{}.{}.{}", fileName, HIWORD(version.dwFileVersionMS),
                                                 LOWORD(version.dwFileVersionMS), HIWORD(version.dwFileVersionLS),
                                                 LOWORD(version.dwFileVersionLS))
                                   : fileName;
            if (!read.matches) {
                line += " (no longer the build that ran: no symbols)";
            }
            report.modules.push_back(std::move(line));
            modules.push_back(std::move(read));
        }
    }
    const auto moduleOf = [&modules](std::uint64_t address) -> const Module* {
        for (const Module& module : modules) {
            if (address >= module.base && address < module.base + module.size) {
                return &module;
            }
        }
        return nullptr;
    };

    // Symbols: a handle of our own stands for the process, which is gone.
    const std::scoped_lock lock(s_dbghelp);
    const auto process = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xDA1DA105));
    std::wstring searchPath;
    for (const std::filesystem::path& path : symbolPaths) {
        searchPath += path.wstring() + L";";
    }
    for (const Module& module : modules) {
        searchPath += std::filesystem::path(module.path).parent_path().wstring() + L";";
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS);
    const bool symbols = SymInitializeW(process, searchPath.c_str(), FALSE) != FALSE;
    if (symbols) {
        for (const Module& module : modules) {
            if (module.matches) {
                SymLoadModuleExW(process, nullptr, module.path.c_str(), nullptr, module.base,
                                 static_cast<DWORD>(module.size), nullptr, 0);
            }
        }
    }

    // The images of the modules that are the builds that ran, after the dump's own memory.
    std::vector<MappedImage> images;
    std::vector<LoadedImage> loaded;
    images.reserve(modules.size());
    for (const Module& module : modules) {
        const std::byte* image = nullptr;
        if (module.matches) {
            image = images.emplace_back(module.path).Base();
            if (image != nullptr) {
                memory.push_back({module.base, module.size, image});
            }
        }
        loaded.push_back({module.base, module.size, image});
    }

    // The failing thread's stack, from the context at the exception.
    CONTEXT context{};
    std::memcpy(&context, bytes + exception->ThreadContext.Rva,
                std::min<std::size_t>(sizeof(context), exception->ThreadContext.DataSize));
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    s_memory = &memory;
    s_images = &loaded;
    for (std::size_t depth = 0; depth < kMaxFrames; ++depth) {
        if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, nullptr, &frame, &context, ReadDumpMemory, FunctionEntry,
                        ModuleBase, nullptr) == FALSE ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        CrashFrame out{.address = frame.AddrPC.Offset};
        if (const Module* module = moduleOf(out.address)) {
            out.module = Narrow(std::filesystem::path(module->path).filename().wstring());
            out.offset = out.address - module->base;
        }
        if (symbols) {
            std::array<std::byte, sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR)> storage{};
            auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(storage.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            if (SymFromAddrW(process, out.address, &displacement, symbol) != FALSE) {
                out.function = Narrow(std::wstring_view(symbol->Name, symbol->NameLen));
            }
            IMAGEHLP_LINEW64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD column = 0;
            if (SymGetLineFromAddrW64(process, out.address, &column, &line) != FALSE) {
                out.file = Narrow(line.FileName);
                out.line = line.LineNumber;
            }
        }
        report.stack.push_back(std::move(out));
    }
    s_memory = nullptr;
    s_images = nullptr;
    if (symbols) {
        SymCleanup(process);
    }
    return report;
}

#else

bool CrashReportsSupported() noexcept {
    return false;
}

core::Result<CrashReport> ReadCrashDump(const std::filesystem::path& /*dump*/,
                                        std::span<const std::filesystem::path> /*symbolPaths*/) {
    return core::MakeError(core::ErrorCode::NotSupported, kModule, "Crash dumps are read on Windows only");
}

#endif

std::string FormatCrashReport(const CrashReport& report) {
    std::string out = std::format("Crash: {}", report.exception);
    if (!report.detail.empty()) {
        out += std::format(", {}", report.detail);
    }
    out += std::format("\nAt 0x{:X}, thread {}\nDump: {}\n\nStack, innermost first:\n", report.exceptionAddress,
                       report.threadId, report.dump.filename().string());
    for (std::size_t index = 0; index < report.stack.size(); ++index) {
        const CrashFrame& frame = report.stack[index];
        out += std::format("  #{:<2} ", index);
        out += frame.module.empty() ? std::format("0x{:X}", frame.address)
                                    : std::format("{}+0x{:X}", frame.module, frame.offset);
        if (!frame.function.empty()) {
            out += "  " + frame.function;
        }
        if (!frame.file.empty()) {
            out += std::format("  ({}:{})", std::filesystem::path(frame.file).filename().string(), frame.line);
        }
        out += '\n';
    }
    if (!report.modules.empty()) {
        out += "\nModules:";
        for (const std::string& module : report.modules) {
            out += "\n  " + module;
        }
        out += '\n';
    }
    return out;
}

std::vector<std::filesystem::path> ReportNewCrashes(const std::filesystem::path& directory,
                                                    const std::filesystem::path& previousLog) {
    std::vector<std::filesystem::path> written;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return written;
    }
    std::vector<std::filesystem::path> dumps;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        std::filesystem::path text = entry.path();
        text += ".txt";
        if (entry.path().extension() == ".dmp" && !std::filesystem::exists(text, error)) {
            dumps.push_back(entry.path());
        }
    }
    std::ranges::sort(dumps);
    for (const std::filesystem::path& dump : dumps) {
        std::string text;
        if (const core::Result<CrashReport> report = ReadCrashDump(dump)) {
            text = FormatCrashReport(*report);
        } else {
            text = std::format("The dump could not be read: {}\n", core::Format(report.error()));
        }
        // The log of the run that crashed: the previous run's, if it was last written when the dump was.
        const auto sameTime = [&error](const std::filesystem::path& a, const std::filesystem::path& b) {
            const auto first = std::filesystem::last_write_time(a, error);
            const auto second = std::filesystem::last_write_time(b, error);
            return !error && std::chrono::abs(first - second) < std::chrono::minutes(2);
        };
        if (!previousLog.empty() && std::filesystem::is_regular_file(previousLog, error) &&
            sameTime(dump, previousLog)) {
            std::ifstream log(previousLog);
            std::deque<std::string> lines;
            for (std::string line; std::getline(log, line);) {
                lines.push_back(std::move(line));
                if (lines.size() > kLogLines) {
                    lines.pop_front();
                }
            }
            text += std::format("\nThe end of {}:\n", previousLog.filename().string());
            for (const std::string& line : lines) {
                text += "  " + line + "\n";
            }
        }
        std::filesystem::path out = dump;
        out += ".txt";
        std::ofstream stream(out, std::ios::binary | std::ios::trunc);
        stream << text;
        if (stream) {
            written.push_back(out);
        }
    }
    return written;
}

} // namespace daidalos::platform
