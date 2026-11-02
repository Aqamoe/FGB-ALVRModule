# FGB ALVRModule

[![build](https://github.com/Aqamoe/FGB-ALVRModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-ALVRModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 UDP
端到端冒烟；真机（ALVR + Quest / Pico / Vive Focus）验收待补，见 [docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 ALVR 原生模块。
直接接收 **ALVR 服务端**在「Eye and face tracking → VRCFaceTracking」模式下发到本机
41463 端口的 VRCFT 兼容 UDP 二进制包，输出 Unified Expressions 表情、睁眼度与逐眼
注视方向。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

ALVR native input module for FaceGaze Bridge. Reads the VRCFT-compatible binary
UDP stream your ALVR server already emits and outputs Unified Expressions shapes,
eye openness and per-eye gaze. No VRCFT, no process injection, no admin rights.

## 功能 / Features

- **表情**：按头显自动接收 `Face2Fb`（Meta 70 项）、`FacePico`（Pico/BD 52 项）或
  `EyesHtc` + `LipHtc`（Vive Focus 14+37 项），映射到 Unified Expressions。
- **眼睛**：`EyesQuat` / `CombQuat` 的设备级注视四元数 → 头局部单位向量（-Z 前，
  置信度 0.9）；睁眼度沿用上游公式 `1 - clamp(blink + blink * squint)`；
  Vive 无眼动四元数时退回眼睑注视近似（置信度 0.4）。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因，数据恢复后自动回到活跃。
- **可诊断**：端口冲突、未知前缀、截断块、脏值（NaN/Inf）都有明确日志，不静默造假。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 瞳孔 | 协议未携带 |
| 头姿 | 本协议只发头显内部的表情/注视流，头姿走 OpenVR 层，与本模块无关 |
| 2D 注视点 / 渲染级注视 | 线格式已确认，但注视精度未真机验证，`gaze` 通道暂不声明 |
| `FacePico` 的 viseme 段 | 上游发送端注释 52 与接收端读取 72 冲突（见 docs/protocol.md），未确认的字段不伪造 |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 串流 | ALVR 服务端，且头显支持眼动/表情追踪（Quest Pro/3、Pico 4 Pro/Enterprise、Vive Focus 等） |
| 其它 | 关闭 VRCFaceTracking：它和本模块抢同一个 UDP 41463 端口 |

## 安装

1. 在 ALVR 服务端设置里把 **Eye and face tracking** 设为 **VRCFaceTracking**。
2. 关闭 VRCFT（或任何监听 41463 的程序）。
3. 从 Releases 下载 `alvr-0.1.0.zip`。
4. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
5. 「模块」页把 **ALVR** 设为启动，角色通常给 `expression` 与 `eyes`。
6. 重启引擎；佩戴头显开始串流后模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 41463
```

改完重启引擎。注意 ALVR 侧端口是写死的 0xA1F7，改本模块端口只适用于中转场景。
未知键会被记录一条警告，不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="D:/code/FGB-Module/FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/alvr-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 UDP 冒烟（需 41463 空闲）
```

## 目录结构

```
src/main.cpp                ABI 生命周期、UDP 接收、状态上报
src/alvr_payload.*          二进制包解析与 Unified Expressions 写入
module/                     fgb-native.json 与图标
tests/                      协议单测、真实 ABI 冒烟
docs/protocol.md            线格式与映射依据（含已确认/未确认清单）
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 41463：端口已被占用」 | 关闭 VRCFT 或其它接收端 |
| 一直提示「还没收到任何数据包」 | ALVR 设置里 Eye and face tracking 是否选了 VRCFaceTracking；是否已开始串流且头显支持该功能 |
| 表情不动、眼睛正常 | 角色没分给 `expression`；或头显表情追踪未启用 |
| 日志出现「无法识别的前缀块」 | ALVR 版本改了协议；把版本号和日志一并提到 issue |
| 日志出现「尾部块不完整」且用 Pico | 你的 ALVR 版本 `FacePico` 按 72 发送（上游两边不一致），请回报版本号 |

## 致谢 / Attribution

- 线格式与映射依据参考 [ALVR](https://github.com/alvr-org/ALVR)（服务端
  `face.rs` 为发送端权威）与 [VRCFT-ALVR](https://github.com/alvr-org/VRCFT-ALVR)；
  字段顺序以 Khronos OpenXR 注册表核对。本仓库是该线格式的独立 C++ 实现，
  未包含上述项目的源码。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
