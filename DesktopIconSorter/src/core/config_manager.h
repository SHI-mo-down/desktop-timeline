// config_manager.h : JSON config file management
#pragma once
#include <Windows.h>
#include <vector>
#include "sort_engine.h"

// App configuration
struct AppConfig {
    std::vector<SortPartition> partitions;
    bool autoApply;          // Auto-apply on system sort
    bool minimizeToTray;     // Minimize to system tray
};

// Load config from JSON file
AppConfig Config_Load(const wchar_t* filePath);

// Save config to JSON file
bool Config_Save(const wchar_t* filePath, const AppConfig& config);

// Create default config with a single full-screen partition
AppConfig Config_CreateDefault();
