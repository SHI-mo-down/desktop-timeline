# 阶段一：Visual Studio 2022 配置指南

## 一、Visual Studio 安装组件

### 必需工作负载
1. **使用 C++ 的桌面开发**
   - MSVC v143 编译器工具集
   - Windows SDK（最新版本，建议 10.0.22621.0 或更高）
   - C++ ATL 支持（用于 COM 开发）
   - C++ MFC 支持（可选，用于传统 UI）

2. **.NET 桌面开发**（用于配置界面）
   - .NET 6.0 或 .NET 8.0 运行时
   - WPF 开发工具

### 推荐安装的单个组件
- C++ 分析工具
- Windows 应用程序 SDK
- C++ AddressSanitizer（内存检测）

## 二、项目配置

### C++ 核心引擎项目设置

#### 1. 项目类型
- 创建 **Windows 桌面应用程序** 或 **动态链接库 (DLL)** 项目
- 平台：x64（推荐）和 x86

#### 2. 项目属性配置

**常规**
- Windows SDK 版本：10.0（最新安装版本）
- 平台工具集：Visual Studio 2022 (v143)
- C++ 语言标准：ISO C++17 或 C++20

**C/C++ → 常规**
- SDL 检查：是
- 警告等级：级别 4 (/W4)

**C/C++ → 预处理器**
- 预处理器定义添加：
  ```
  WIN32
  _WINDOWS
  UNICODE
  _UNICODE
  NOMINMAX
  ```

**C/C++ → 代码生成**
- 运行库：多线程调试 DLL (/MDd) [Debug] / 多线程 DLL (/MD) [Release]
- 启用 C++ 异常：是 (/EHsc)

**链接器 → 系统**
- 子系统：Windows (/SUBSYSTEM:WINDOWS)

**链接器 → 输入**
- 附加依赖项添加：
  ```
  comctl32.lib
  shell32.lib
  ole32.lib
  oleaut32.lib
  uuid.lib
  ```

#### 3. DPI 感知配置
在项目中添加 `app.manifest` 文件，设置 DPI 感知：
```xml
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/PM</dpiAware>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
  </windowsSettings>
</application>
```

## 三、推荐的第三方库

### JSON 解析库
- **nlohmann/json**（仅头文件，易于集成）
- 通过 NuGet 或 vcpkg 安装

### 日志库（可选）
- **spdlog**（高性能日志库）

### 安装方式
使用 vcpkg 包管理器：
```bash
vcpkg install nlohmann-json:x64-windows
vcpkg install spdlog:x64-windows
vcpkg integrate install
```

## 四、调试配置

### 1. 调试器设置
- 启用本机代码调试
- 启用 COM 调试
- 工作目录设置为 `$(OutDir)`

### 2. 性能分析工具
- 使用 Visual Studio 性能探查器
- 启用 CPU 使用率和内存使用率分析

## 五、版本控制配置

### .gitignore 文件
创建 `.gitignore` 包含：
```
# Visual Studio
.vs/
*.user
*.suo
*.userosscache
*.sln.docstates
[Dd]ebug/
[Rr]elease/
x64/
x86/
[Bb]in/
[Oo]bj/

# Build results
*.exe
*.dll
*.pdb
*.ilk
*.obj
*.log
```

## 六、第一阶段验证项目结构

```
DesktopIconSorter/
├── Core/                    # C++ 核心引擎
│   ├── IconManager.cpp      # 图标位置管理
│   ├── FolderViewHelper.cpp # IFolderView 接口封装
│   └── GridSystem.cpp       # 网格坐标系统
├── Config/                  # C# 配置工具（后续阶段）
└── Tests/                   # 测试项目
    └── PrototypeTest.cpp    # 原型验证代码
```

## 七、验证清单

- [ ] Visual Studio 2022 已安装所需组件
- [ ] Windows SDK 版本 ≥ 10.0.22621.0
- [ ] 创建 C++ 桌面项目并配置属性
- [ ] 成功编译 Hello World 程序
- [ ] COM 接口调用测试通过
- [ ] vcpkg 包管理器配置完成
- [ ] Git 仓库初始化

配置完成后，即可开始核心技术验证工作。
