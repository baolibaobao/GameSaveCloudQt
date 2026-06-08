# AGENTS.md - Project Context for AI Assistants

## 1. 项目概览 (Project Overview)
- **名称**: GameSaveCloud-Qt (暂定)
- **目标**: 开发一个类似 Mayfly 的 Steam 游戏存档云同步工具。
- **核心逻辑**: 自动识别 Steam 游戏 -> 监控游戏进程 -> 游戏关闭后通过 WebDAV 增量同步存档至个人网盘。

## 2. 技术栈 (Tech Stack)
- **开发语言**: C++ 20 (现代 C++ 标准)。
- **UI 框架**: Qt 6 (必须使用 QML/Qt Quick 实现现代视觉效果)。
- **构建工具**: CMake。
- **IDE**: Visual Studio 2022。
- **数据库**: SQLite 3 (通过 QtSql 模块)。
- **网络**: QNetworkAccessManager (处理 WebDAV HTTP 请求)。
- **系统 API**: Windows API (用于进程监控和注册表读取)。

## 3. UI 视觉风格指南 (UI Style Guide)
- **参考风格**: Modern Fluent / Material Design (高圆角、扁平化、卡片式布局)。
- **布局结构**: 
    - 侧边栏导航 (Sidebar Navigation)。
    - 主区域采用带柔和阴影的悬浮卡片 (Floating Cards)。
- **配色**: 亮色模式为主，支持亮暗切换；主题色为 Accent Blue (#0078D4 或类似蓝色)。
- **控件**: 使用 QML 自定义控件实现高圆角 (BorderRadius: 10px+)。

## 4. 核心逻辑实现细节 (Core Logic Details)
### 4.1 Steam 识别
- **路径寻址**: 通过 Windows 注册表 `HKEY_CURRENT_USER\Software\Valve\Steam` 获取 `SteamPath`。
- **游戏扫描**: 遍历 `steamapps` 目录下的 `appmanifest_*.acf` 文件，解析 AppID 和 Name 字段。
- **图标获取**: 拼接官方 URL `https://cdn.cloudflare.steamstatic.com/steam/apps/[AppID]/header.jpg`。

### 4.2 存档检测与寻址
- **自动检测**: 优先尝试从内置 AppID-Path 映射库匹配路径。
- **手动补全**: 若自动检测失败，UI 需提供文件夹选择器让用户手动指定路径。

### 4.3 进程监控
- **机制**: 使用后台 QThread 或 QTimer 结合 WinAPI (`EnumProcesses`) 监控目标游戏进程名。
- **触发器**: 监测到进程由“运行”变为“不存在”时，立即触发同步逻辑。

### 4.4 同步算法
- **增量同步**: 基于文件修改时间和 MD5 哈希值对比。
- **协议**: WebDAV (PUT, GET, MKCOL, PROPFIND)。
- **MANIFEST**: 在云端维护一个 `.sync-manifest.json` 文件作为同步基准。

## 5. 对 AI 助手的要求 (Instructions for AI)
- **代码规范**: 请提供符合 C++ 20 标准的代码，尽量使用 `std::filesystem` 处理路径。
- **QML 与 C++ 分离**: 逻辑放在 C++ 类中（继承 QObject），UI 放在 .qml 文件中。
- **注释要求**: 关键算法和 Windows API 调用处请提供详细的中文注释。
- **兼容性**: 确保代码能在 Visual Studio 2022 下直接编译，CMakeLists.txt 需配置好 Qt 6 依赖。

## Agent skills

### Issue tracker

Issues and PRDs are tracked as local markdown files under `.scratch/`. See `docs/agents/issue-tracker.md`.

### Triage labels

This repo uses the default mattpocock/skills triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

This repo uses a single-context domain documentation layout. See `docs/agents/domain.md`.
