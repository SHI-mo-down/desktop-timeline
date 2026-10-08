// json_extract.h : Tolerant JSON value extraction (hand-rolled, zero deps)
// Deliberately lenient: ignores whitespace/formatting differences so
// LLM-generated layout files parse. Does not handle escaped quotes inside
// strings — icon names with \" are rejected rather than misparsed.
#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <stdio.h>

inline std::wstring Json_ReadFileUtf8(const wchar_t* path) {
    std::wstring result;
    FILE* fp = nullptr;
    _wfopen_s(&fp, path, L"rb");
    if (!fp) return result;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 16 * 1024 * 1024) { fclose(fp); return result; }
    std::string buf(size, 0);
    fread(&buf[0], 1, size, fp);
    fclose(fp);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), (int)size, nullptr, 0);
    if (wlen > 0) {
        result.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), (int)size, &result[0], wlen);
    }
    return result;
}

inline std::wstring Json_ExtractString(const std::wstring& s, const wchar_t* key) {
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return L"";
    pos = s.find(L'"', pos + search.length());
    if (pos == std::wstring::npos) return L"";
    pos++;
    size_t end = s.find(L'"', pos);
    if (end == std::wstring::npos) return L"";
    return s.substr(pos, end - pos);
}

inline int Json_ExtractInt(const std::wstring& s, const wchar_t* key, int defaultVal = 0) {
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return defaultVal;
    pos = s.find(L':', pos);
    if (pos == std::wstring::npos) return defaultVal;
    pos++;
    while (pos < s.length() && (s[pos] == L' ' || s[pos] == L'\t' ||
                                s[pos] == L'\n' || s[pos] == L'\r'))
        pos++;
    return _wtoi(s.c_str() + pos);
}

inline std::vector<std::wstring> Json_ExtractStringArray(const std::wstring& s, const wchar_t* key) {
    std::vector<std::wstring> result;
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return result;
    pos = s.find(L'[', pos);
    if (pos == std::wstring::npos) return result;
    size_t end = s.find(L']', pos);
    if (end == std::wstring::npos) return result;
    pos++;
    while (pos < end) {
        size_t q1 = s.find(L'"', pos);
        if (q1 == std::wstring::npos || q1 >= end) break;
        size_t q2 = s.find(L'"', q1 + 1);
        if (q2 == std::wstring::npos || q2 >= end) break;
        result.push_back(s.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
    return result;
}

// Find the [ ... ] block following "label": returns its inner range [start, end)
inline bool Json_FindArrayBlock(const std::wstring& s, const wchar_t* label,
                                size_t* outStart, size_t* outEnd) {
    std::wstring search = std::wstring(L"\"") + label + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return false;
    pos = s.find(L'[', pos);
    if (pos == std::wstring::npos) return false;
    *outStart = pos + 1;
    int depth = 1;
    size_t i = *outStart;
    while (i < s.length() && depth > 0) {
        if (s[i] == L'[') depth++;
        else if (s[i] == L']') depth--;
        if (depth > 0) i++;
    }
    *outEnd = i;
    return depth == 0;
}

// Split a block by top-level { ... } objects, returns inner ranges
inline std::vector<std::pair<size_t, size_t>> Json_SplitObjects(
    const std::wstring& s, size_t start, size_t end) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t i = start;
    while (i < end) {
        size_t o = s.find(L'{', i);
        if (o == std::wstring::npos || o >= end) break;
        int depth = 1;
        size_t j = o + 1;
        while (j < end && depth > 0) {
            if (s[j] == L'{') depth++;
            else if (s[j] == L'}') depth--;
            j++;
        }
        if (depth != 0) break;
        out.push_back({o + 1, j - 1});
        i = j;
    }
    return out;
}
