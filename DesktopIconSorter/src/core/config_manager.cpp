// config_manager.cpp : Simple JSON config management
#include "config_manager.h"
#include <stdio.h>
#include <string>

static std::wstring EscapeJson(const std::wstring& s) {
    std::wstring result;
    for (wchar_t c : s) {
        if (c == L'"' || c == L'\\') result += L'\\';
        result += c;
    }
    return result;
}

AppConfig Config_Load(const wchar_t* filePath) {
    AppConfig config;

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"r, ccs=UTF-8");
    if (!fp) return Config_CreateDefault();

    // Simple line-based parser (avoids JSON lib dependency for now)
    SortPartition part = {};
    wchar_t line[1024];
    bool inPartition = false;

    while (fgetws(line, 1024, fp)) {
        // Very simple JSON value extraction
        std::wstring s(line);

        if (s.find(L"\"autoApply\"") != std::wstring::npos) {
            config.autoApply = s.find(L"true") != std::wstring::npos;
        }
        if (s.find(L"\"minimizeToTray\"") != std::wstring::npos) {
            config.minimizeToTray = s.find(L"true") != std::wstring::npos;
        }

        // Extract name
        auto namePos = s.find(L"\"name\"");
        if (namePos != std::wstring::npos) {
            auto start = s.find(L'"', namePos + 6);
            auto end = s.find(L'"', start + 1);
            if (start != std::wstring::npos && end != std::wstring::npos) {
                part.name = s.substr(start + 1, end - start - 1);
            }
        }

        // Extract numeric values
        if (s.find(L"\"left\"") != std::wstring::npos) {
            wchar_t* colon = wcschr(line, L':');
            if (colon) part.left = _wtoi(colon + 1);
        }
        if (s.find(L"\"top\"") != std::wstring::npos) {
            wchar_t* colon = wcschr(line, L':');
            if (colon) part.top = _wtoi(colon + 1);
        }
        if (s.find(L"\"right\"") != std::wstring::npos) {
            wchar_t* colon = wcschr(line, L':');
            if (colon) part.right = _wtoi(colon + 1);
        }
        if (s.find(L"\"bottom\"") != std::wstring::npos) {
            wchar_t* colon = wcschr(line, L':');
            if (colon) part.bottom = _wtoi(colon + 1);
        }
        if (s.find(L"\"sortMode\"") != std::wstring::npos) {
            wchar_t* colon = wcschr(line, L':');
            if (colon) part.sortMode = _wtoi(colon + 1);
        }

        // End of partition object
        if (s.find(L'}') != std::wstring::npos && !part.name.empty()) {
            config.partitions.push_back(part);
            part = SortPartition{};
        }
    }

    fclose(fp);

    if (config.partitions.empty()) {
        config = Config_CreateDefault();
    }

    return config;
}

bool Config_Save(const wchar_t* filePath, const AppConfig& config) {
    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"w, ccs=UTF-8");
    if (!fp) return false;

    fwprintf(fp, L"{\n");
    fwprintf(fp, L"  \"autoApply\": %s,\n", config.autoApply ? L"true" : L"false");
    fwprintf(fp, L"  \"minimizeToTray\": %s,\n", config.minimizeToTray ? L"true" : L"false");
    fwprintf(fp, L"  \"partitions\": [\n");

    for (size_t i = 0; i < config.partitions.size(); i++) {
        const auto& p = config.partitions[i];
        fwprintf(fp, L"    {\n");
        fwprintf(fp, L"      \"name\": \"%s\",\n", EscapeJson(p.name).c_str());
        fwprintf(fp, L"      \"left\": %d,\n", p.left);
        fwprintf(fp, L"      \"top\": %d,\n", p.top);
        fwprintf(fp, L"      \"right\": %d,\n", p.right);
        fwprintf(fp, L"      \"bottom\": %d,\n", p.bottom);
        fwprintf(fp, L"      \"sortMode\": %d,\n", p.sortMode);
        fwprintf(fp, L"      \"ascending\": %s,\n", p.ascending ? L"true" : L"false");
        fwprintf(fp, L"      \"locked\": %s\n", p.locked ? L"true" : L"false");
        fwprintf(fp, L"    }%s\n", (i < config.partitions.size() - 1) ? L"," : L"");
    }

    fwprintf(fp, L"  ]\n");
    fwprintf(fp, L"}\n");
    fclose(fp);
    return true;
}

AppConfig Config_CreateDefault() {
    AppConfig config;
    config.autoApply = true;
    config.minimizeToTray = true;

    SortPartition defaultPart;
    defaultPart.name = L"Default";
    defaultPart.left = 0;
    defaultPart.top = 0;
    defaultPart.right = 100;
    defaultPart.bottom = 100;
    defaultPart.sortMode = 0; // Name
    defaultPart.ascending = true;
    defaultPart.locked = false;

    config.partitions.push_back(defaultPart);
    return config;
}
