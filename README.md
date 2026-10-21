# CPAMP TrafficMonitor Plugin

将 [cpamp_monitor](https://github.com/shenyebuwu/cpamp_monitor) 的配额显示移入 TrafficMonitor，不再单独创建任务栏窗口。

## 已实现

- `CPAMP 5H Remaining`：Codex 5 小时剩余百分比，如 `5H: 60%`。
- `CPAMP Reset Time`：最早重置时间，使用 Windows 本地时区，如 `Reset: 01:00`。
- 多个 Manager Server；按有效账号数量加权平均剩余配额，取最早重置时间。
- 当前接口 `v0/management/auth-files` 的健康检查数据；无健康数据时 POST `v0/management/quota-snapshots/query`。
- 兼容 `api/admin/manager-data` 和 `api/v1/admin/manager-data`。
- 后台请求、15–3600 秒刷新、立即刷新、离线占位、低于 20% 告警颜色、悬浮连接状态。
- Win32 配置窗口，管理员 Key 使用当前用户的 Windows DPAPI 加密保存。

任务栏布局、字体、位置和开机启动由 TrafficMonitor 管理。插件不修改这些全局选项，也不创建独立托盘图标。

## 安装和使用

发布包为 **Windows x64 DLL**，使用官方 **TrafficMonitor 1.85.1 / API 6** 接口，也可由支持旧接口的更新版本加载。不能放入 x86 主程序中使用。

1. 退出 TrafficMonitor。
2. 从本仓库 Releases 下载 `CPAMPMonitor-x64.zip`，将其中的 `plugins/CPAMPMonitor.dll` 放到 `TrafficMonitor.exe` 同目录的 `plugins` 文件夹。从源码编译后也可以运行（路径替换为自己的安装目录）：

   ```powershell
   ./scripts/install.ps1 -TrafficMonitorDir 'C:\Apps\TrafficMonitor'
   ```

3. 重新启动 TrafficMonitor，在「其他功能 → 插件管理」中选择 `CPAMP Quota Monitor`，打开「选项」。
4. 填写 Name、Server URL、Admin Key，点击 **Apply connection**，再点击 **Save**。多个连接逐个添加。Server URL 支持管理页面地址，例如 `https://server.example/management.html`，也支持反向代理路径前缀，例如 `https://server.example/cpamp`。
5. 在任务栏窗口右键「显示项目」中勾选 `CPAMP 5H Remaining` 和 `CPAMP Reset Time`。

双击插件显示项可打开设置；TrafficMonitor 插件命令中提供 `Refresh CPAMP now` 和 `CPAMP settings...`。

没有有效数据时显示 `--%` / `--:--`。某连接离线时，其他在线连接继续聚合，离线信息保留在悬浮提示中。旧数据不会作为成功数据无限保留。

## 配置与安全

配置位置：`%APPDATA%\CPAMPTrafficMonitor\settings.json`。

文件包含连接名称、URL、刷新间隔及 DPAPI 密文 `key_dpapi`，不包含明文 Key。密文只能由保存它的 Windows 用户解密。设置损坏或无法解密时，插件显示配置错误，不会静默覆盖原配置；可在选项中重新配置。

HTTPS 证书按 Windows 默认规则验证，禁止携带管理员密钥跟随重定向。不支持通过忽略 TLS 证书错误连接。HTTP 可以用于本地或可信网络，但不适合经公网传送管理员密钥。

## 编译

无需 Python 运行时，也无需安装 MFC；DLL 使用原生 C++17、WinHTTP、DPAPI、Win32 UI 和静态 MSVC 运行库。

安装 Visual Studio 2022「使用 C++ 的桌面开发」工作负载（包含 Windows SDK）及 CMake，然后执行：

```powershell
cmake -S . -B build-cmake -A x64
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

DLL 输出到 `build-cmake/Release/CPAMPMonitor.dll`。如需使用打包脚本，先将 DLL 复制到 `dist`：

```powershell
New-Item -ItemType Directory -Force dist
Copy-Item build-cmake/Release/CPAMPMonitor.dll dist/CPAMPMonitor.dll
```

也可在 Visual Studio 的 **x64 Native Tools Command Prompt** 中运行 PowerShell 构建脚本：

```powershell
./build.ps1 -Tests
```

## 测试

```powershell
./scripts/test.ps1
```

包含解析/时间/聚合/DPAPI 单元测试，真实 WinHTTP 对本地模拟服务器的接口兼容测试，以及 LoadLibrary → 导出入口 → 插件项目 → 异步刷新 → GDI 绘制的 DLL 端到端测试。测试中的 `60%` 是明确的本地模拟数据，不是用户真实配额；测试脚本会恢复先前配置。

验证真实 API 仍需在插件选项中填写自己的 CPAMP Server URL 和 Admin Key。不要将 Key 发送到聊天或提交到 Git。

源项目版本和第三方许可说明见 `THIRD_PARTY_NOTICES.md`。

## 打包与发布

执行 `./scripts/package.ps1`，生成 `dist/CPAMPMonitor-x64.zip` 和 `dist/SHA256SUMS.txt`。加 `-Rebuild` 可先重新编译及运行单元测试；TrafficMonitor 正在加载 DLL 时，请先退出再重新编译。

ZIP 只包含插件 DLL、说明和第三方许可证，不包含用户配置、Key、测试结果或 MSVC 工具链。用户将 ZIP 中的 `plugins/CPAMPMonitor.dll` 复制到 TrafficMonitor 的 `plugins` 目录，再重启主程序即可使用。

GitHub 仓库用于保存源码，GitHub Releases 用于分发 ZIP 和校验文件。`dist`、`build`、下载的上游仓库和工具链已加入 `.gitignore`；不要用 `git add -f` 强制提交这些目录。公开发布前请核实上游与第三方许可，并为自己编写的代码选择明确的许可证。

GitHub Actions 会在推送后执行 Windows x64 编译及单元/插件 ABI/HTTP 接口测试，并生成可下载的发布包构建产物。插件不需要 Python；仅测试脚本使用 Python 标准库。
