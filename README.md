# GameSaveCloudQt

GameSaveCloudQt 是一个 Windows 游戏存档云同步工具，目标是自动识别 Steam 游戏、管理本地存档快照，并通过本地 OpenList 网关同步到夸克网盘。

## 功能

- 自动扫描 Steam 游戏库
- 获取 Steam 中文游戏资料
- 从本机 Steam appcache/appinfo.vdf 读取 ufs.savefiles获取存档位置
- 识别 PCGamingWiki 存档路径
- 支持手动指定存档目录
- 创建、恢复、删除本地存档快照
- 上传和下载云端快照
- 支持上传/下载进度显示
- 支持游戏关闭后的自动同步
- 支持配置云同步
- 内置 OpenList 网关用于夸克网盘 WebDAV 同步

## 技术栈

- C++20
- Qt 6 / QML
- CMake
- Visual Studio 2026
- OpenList

## 构建

推荐使用 Visual Studio 2026 打开项目，并选择 CMake 预设：

```text
Qt-Release
```

Qt 路径在 `CMakeUserPresets.json` 中配置。当前项目使用 Qt 6.11.1 MSVC 2022 x64。

## 发布

发布目录由 Release 构建产物和 `windeployqt` 生成，安装包脚本位于：

```text
installer/GameSaveCloudQt.iss
```

使用 Inno Setup 编译安装包：

```powershell
& "E:\Program Files (x86)\Inno Setup 6\ISCC.exe" "F:\ruanjian\yuncundang\installer\GameSaveCloudQt.iss"
```

如需中文安装界面，请确保存在：

```text
E:\Program Files (x86)\Inno Setup 6\Languages\ChineseSimplified.isl
```

## 数据目录

软件运行数据默认保存在：

```text
%APPDATA%\GameSaveCloudQt
```

OpenList 数据目录：

```text
%APPDATA%\GameSaveCloudQt\openlist
```


## 作者

baolibaobao
