![Project badge](assets/readme-badge.png)

# vdsrcswitch

> 通过 DDC/CI 协议，用键盘快捷键在多台电脑间瞬间切换显示器输入源的后台守护程序。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010%2B-blue.svg)](https://www.microsoft.com/windows)
[![Platform: macOS](https://img.shields.io/badge/Platform-macOS%20(Apple%20Silicon)-lightgrey.svg)](https://www.apple.com/macos)
[![Language: C/C++/Swift](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B%20%2F%20Swift-orange.svg)](src/)

---

## 简介

`vdsrcswitch` 是一款极简的后台工具，让你在**不碰 KVM 切换器、不动显示器按键**的情况下，只需长按 `Shift + V`（松开后按 `Tab` 循环）即可瞬间切换显示器的输入源，并通过屏幕悬浮提示框实时反馈当前切换状态。

项目同时提供：
- **Windows 版**（C / C++，Visual Studio 2022，基于 WinAPI DDC/CI + GDI+）
- **macOS 版**（Swift + Cocoa，基于 AppleSilicon 私有 `IOAVService` DDC 实现，需辅助功能权限）

---

## 功能特性

- **一键切换** — 长按 `Shift + V` → 弹出悬浮窗 → 按 `Tab` 循环目标输入源 → 松手即提交
- **DDC/CI 直驱** — 无需任何外设硬件，直接通过显示器 I²C 总线发送切换指令
- **GDI+ 圆角悬浮窗**（Windows）— 高 DPI 自适应，抗锯齿渲染，支持 Per-Monitor V2 缩放
- **WebSocket 控制接口**（Windows）— 内建 HTTP + WebSocket 服务器，支持通过局域网网页远程切换输入源
- **INI 配置文件** — 零依赖，运行时读取 `vdsrcswitch.ini`，支持多显示器配置
- **开机自启**（Windows）— 自动写入任务计划程序，以最高权限随登录无提示启动
- **LaunchAgent 支持**（macOS）— 通过 `launchctl` 随系统登录后台常驻

---

## 快速上手

### Windows

#### 前置要求

- Windows 10 / 11 x64
- Visual Studio 2022（含 C++ 桌面开发负载）
- 显示器须支持 DDC/CI（在显示器 OSD 菜单中开启）

#### 构建

1. 打开 `vdsrcswitch.sln`
2. 选择 `Release | x64` 配置并生成
3. 产物位于 `bin\Release\vdsrcswitch.exe`

#### 配置

在 `vdsrcswitch.exe` 同目录下放置（或编辑） `vdsrcswitch.ini`：

```ini
[settings]
activation_hold_ms=300   ; 长按激活阈值（毫秒）
overlay_opacity=230       ; 悬浮窗透明度 (0–255)
monitor_count=1

[monitor_0]
monitor_name=HDMI2
input_count=2
input_0_value=17
input_0_name=DisplayPort
input_1_value=15
input_1_name=HDMI
```

> [!TIP]
> `input_N_value` 对应 DDC/CI VCP 0x60 的十进制输入源编号，常见值：`15` = HDMI 1，`17` = DisplayPort 1。请参考你的显示器手册。

#### 运行

直接双击 `vdsrcswitch.exe`（首次建议以管理员身份运行），程序会静默驻留后台并注册开机任务。

卸载开机自启：

```
vdsrcswitch.exe --uninstall
```

---

### macOS（Apple Silicon）

> [!WARNING]
> **M1 芯片 HDMI 接口硬件限制**：搭载 **M1（基础款，不含 M1 Pro / M1 Max / M1 Ultra）** 芯片的 Mac 通过 HDMI 接口连接的显示器，**受芯片硬件限制，DDC/CI 通道不可用**，vdsrcswitch 无法控制该接口的输入源切换。

#### 转接线兼容性

并非所有转接线都能保留 DDC/CI 信号通道，请根据下表确认你的连接方式：

| 类型 | 示例 | 是否支持 |
|---|---|:---:|
| USB-C → DisplayPort | 直连线缆 / 无源转接头 | ✅ 支持 |
| HDMI → DVI | 无芯片无源转换头 | ✅ 支持 |
| 其他无芯片无源转接线 | 纯物理针脚映射 | ✅ 支持 |
| DP → USB-C | 主动式适配器 | ❌ 不支持 |
| USB-C → HDMI | 含芯片主动适配器 | ❌ 不支持 |
| 各类模拟线缆 | VGA 转接等 | ❌ 不支持 |
| 各类含芯片有源转接线 | 带独立芯片的扩展坞 | ❌ 不支持 |

> [!NOTE]
> 判断准则：**无源转接线**（纯针脚映射，无信号转换芯片）通常保留 DDC/CI；**有源转接线**（内含信号转换芯片）通常会截断 DDC/CI 通道。

#### 前置要求

- macOS 12 Monterey 或更高版本（Apple Silicon）
- Xcode Command Line Tools（`xcode-select --install`）
- 在 **系统设置 → 隐私与安全性 → 辅助功能** 中授权终端或程序

#### 构建 & 安装

```zsh
make install
```

这将编译 Swift 可执行文件并注册 `LaunchAgent`，随后用户登录即自动启动。

#### 卸载

```zsh
make uninstall
```

或直接运行：

```zsh
/usr/local/bin/vdsrcswitch_macos --uninstall
```

---

## WebSocket 控制接口（Windows）

程序启动后在 `http://localhost:PORT/` 提供一个响应式深色主题网页控制器，可通过局域网在手机/平板上直接点击切换输入源。

WebSocket 消息格式（JSON）：

```json
{ "action": "set_input", "monitor": 0, "value": 17 }
```

程序会在切换后广播当前状态给所有已连接客户端。

---

## 项目结构

```
vdsrcswitch/
├── src/                     # Windows 源码（C / C++）
│   ├── main.c               # 入口、消息循环、自启注册
│   ├── config.c / .h        # INI 配置读写
│   ├── monitor.c / .h       # DDC/CI 显示器枚举与输入切换
│   ├── hook.c / .h          # WH_KEYBOARD_LL 全局键盘钩子
│   ├── overlay.cpp / .h     # GDI+ 圆角悬浮提示窗
│   └── wsserver.cpp / .h    # 内建 HTTP + WebSocket 服务器
├── src_macos/               # macOS 源码（Swift）
│   ├── main.swift           # 入口、AppDelegate
│   ├── DDC.swift            # AppleSilicon IOAVService DDC 实现
│   ├── KeyboardHook.swift   # CGEvent 全局键盘监听
│   ├── Config.swift         # 配置管理
│   ├── Overlay.swift        # NSWindow 悬浮提示
│   └── com.vdsrcswitch.daemon.plist  # LaunchAgent plist
├── bin/                     # 编译产物 & 配置文件
│   └── vdsrcswitch.ini      # 运行时配置（示例）
├── assets/                  # 图片资源
├── vdsrcswitch.sln          # Visual Studio 解决方案
├── vdsrcswitch.vcxproj      # Visual Studio 项目文件
├── Makefile                 # macOS 构建脚本
└── README.md
```

---

## 使用交互流程

```
长按 Shift + V（超过 300ms）
        │
        ▼
  悬浮窗弹出，显示当前输入源
        │
        ▼
  按 Tab → 循环到下一个输入源（悬浮窗实时更新）
        │
        ├── 松开所有按键 → 提交切换，DDC/CI 写入生效
        │
        └── 按 Esc / 中途松开 Shift 或 V → 取消，恢复原输入源
```

---

## 常见问题

**Q: 切换没有反应怎么办？**  
A: 确认显示器 OSD 菜单中已开启 DDC/CI，且程序以管理员权限运行。

**Q: 如何查看调试日志？**  
A: Windows 版日志写入 `%TEMP%\vdsrcswitch_debug.log`；macOS 版写入 `/tmp/vdsrcswitch_debug.log`。

**Q: 支持多台显示器同步切换吗？**  
A: 支持，在 `vdsrcswitch.ini` 中配置多个 `[monitor_N]` 节，按 `Tab` 时所有显示器同步循环切换。

**Q: macOS 版是否支持 Intel Mac？**  
A: 当前 DDC 实现基于 Apple Silicon 的 `IOAVService` 私有 API，**不支持** Intel Mac。

---

## 贡献

欢迎提交 Issue 和 Pull Request。  
macOS DDC 实现部分参考自 [waydabber/BetterDisplay](https://github.com/waydabber/BetterDisplay) 的 AppleSiliconDDC 逻辑，在此致谢。

---

## 许可证

本项目使用 [MIT 许可证](LICENSE) 开源。
