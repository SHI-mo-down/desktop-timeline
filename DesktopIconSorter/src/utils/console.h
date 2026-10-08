// console.h : Unicode-safe stdout printing
// WriteConsoleW on a real console, UTF-8 bytes when piped/redirected.
// Avoids mixing wprintf with the narrow printf used elsewhere (stream
// orientation conflict makes one of them fail silently).
#pragma once
#include <Windows.h>
#include <string>
#include <stdio.h>

inline void Console_PrintW(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (GetFileType(h) == FILE_TYPE_CHAR &&
        WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr))
        return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(bytes - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                            &utf8[0], bytes, nullptr, nullptr);
        fwrite(utf8.data(), 1, utf8.size(), stdout);
    }
    fflush(stdout);
}
