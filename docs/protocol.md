# ALVR VRCFT 兼容 UDP 协议要点

来源：ALVR 服务端（`alvr/server_core/src/tracking/face.rs`，Rust，MIT 类许可）在
「Eye and face tracking → VRCFaceTracking」模式下发送到 **127.0.0.1:41463**（0xA1F7）的
UDP 数据报；接收端参考 [alvr-org/VRCFT-ALVR](https://github.com/alvr-org/VRCFT-ALVR)
（Apache-2.0）。本模块是独立 C++ 实现，未复制任何上游源码。字段索引另以
Khronos OpenXR 注册表（`XR_FB_face_tracking2` / `XR_HTC_facial_tracking` /
`XR_BD_facial_simulation` 的生成绑定）核对过。

**线格式（发送端 face.rs 确认）**：单个数据报由若干块顺序拼接，每块为
「8 字节 ASCII 前缀 + N 个 float32（小端）」。一个数据报内通常是
`(EyesQuat 或 CombQuat) + (Face2Fb 或 FacePico 或 EyesHtc 和/或 LipHtc)`，
也可只有表情块或只有注视块。

## 前缀与负载长度

| 前缀（8 字节） | float 数 | 内容 | 确认状态 |
|---|---|---|---|
| `EyesQuat` | 8 | 左眼四元数 `[x,y,z,w]` + 右眼四元数 `[x,y,z,w]` | 发送端+接收端一致 ✔ |
| `CombQuat` | 4 | 合并注视四元数 `[x,y,z,w]` | 发送端+接收端一致 ✔ |
| `Face2Fb\0` | 70 | XR_FB_face_tracking2 默认集（0-62 表情、63-69 舌部） | 发送端 `// 70 values` + 注册表 `COUNT_FB = 70` + 接收端 `Face2FbMax=70` ✔ |
| `FaceFb\0\0` | 63 | face_tracking 1 遗留集（无舌部） | 仅接收端有此消费者；当前 face.rs **不再发送**，本模块保留兼容解析 ✔ |
| `FacePico` | 52 | XR_BD_facial_simulation 表情集 | 发送端 `// 52 values` + `XR_FACE_EXPRESSION_COUNT_BD = 52`；**接收端读 72（52+20 viseme），两边不一致**，见下文 ⚠ |
| `EyesHtc\0` | 14 | HTC 眼部（blink/wide/squeeze/look 逐眼） | 发送端注释 + `FACIAL_EXPRESSION_EYE_COUNT_HTC = 14` + 接收端一致 ✔ |
| `LipHtc\0\0` | 37 | HTC 唇舌（jaw/mouth/cheek/tongue） | 发送端 `// 37 values` + `FACIAL_EXPRESSION_LIP_COUNT_HTC = 37` + 接收端一致 ✔ |

与常见二手资料（含任务简报）的差异：

- **`Face2Fb` 是 70 float，不是 63**。63 是遗留 `FaceFb\0\0`（face_tracking 1）的长度。
- **`FacePico` 按发送端是 52 float**。接收端 VRCFT-ALVR 读 72 并使用了一套
  与 BD 规范不同的枚举顺序（旧 Pico Launcher 布局、含 20 个 viseme），
  两者必有一边未跟上现行 ALVR；本模块**以发送端与 OpenXR 注册表为准**，
  只解析 52，绝不从多余的 80 字节里猜 viseme 数据。若实测发现某些 ALVR 版本
  真发 72，需要升级本表。

## 四元数与注视方向

- 排布 `[x, y, z, w]`（接收端 `Identity = {0,0,0,1}` 佐证）。
- `alvr_packets::FaceData` 注释：`eyes_combined` 是「-Z 轴沿注视方向」的
  **头局部**朝向；`eyes_social` 是逐眼头局部朝向（FB 眼动在社会存在模式下
  按 VIEW 空间取样）。坐标系与 FGB ABI 的 `gaze_dir`（HMD 局部、-Z 前、+Y 上、
  +X 右）直接一致，本模块用 `v = q·(0,0,-1)` 取注视单位向量。
- 防御处理：NaN/Inf 或模长接近 0 的四元数视为无效，**不**回退成「看正前方」。
- 未确认：个别平台（Pico 注视走了 STAGE 空间反头旋的 workaround）平滑策略与
  精度差异；ALVR 对 `eyes_social` 是否二次平滑由运行时决定。

## 表情块字段顺序（对照 OpenXR 注册表确认）

- `Face2Fb`：0 `browLowererL` … 69 `tongueRetreat`，与 VRCFT-ALVR 的 `FaceFb`
  枚举逐项一致；其中 14-21（EyesLook*）是眼睑近似，接收端不当注视用，本模块也不。
- `FacePico`：0 `browDropL` … 51 `tongueOut`，即 BD 规范顺序；语义与 ARKit 52 项
  一一对应，本模块转成 ARKit 权重后走 Kit 通用映射。
- `EyesHtc`：0 `leftBlink` 1 `leftWide` 2 `rightBlink` 3 `rightWide` 4 `leftSqueeze`
  5 `rightSqueeze` 6 `leftDown` 7 `rightDown` 8 `leftOut` 9 `rightIn` 10 `leftIn`
  11 `rightOut` 12 `leftUp` 13 `rightUp`。
- `LipHtc`：0 `jawRight` … 36 `tongueDownleftMorph`，与接收端 `LipHtc` 枚举一致。

## 睁眼度

三种来源统一沿用接收端公式：

```text
openness = 1 - clamp(blink + blink * squint, 0, 1)
```

`Face2Fb` 用 `EyesClosed* + LidTightener*`；`FacePico` 用 `eyeBlink* + eyeSquint*`；
`EyesHtc` 用 `*Blink + *Squeeze`。都没有时不写 openness 属性位。

## 近似与取舍

- FB/HTC 直写 Unified Expressions（与接收端同源口径，含 `min(1 - raiser^(1/6), suck)`
  之类的上游近似）；Pico 经 ARKit 通用表。
- 接收端把 HTC 的 `MouthStretch*` 两侧都写成 `SadRight`，属明显笔误，不复刻；
  该形状改由表情块不输出，宁缺毋假。
- HTC 无四元数时的眼睑注视近似置信度 0.4；`EyesQuat`/`CombQuat` 为 0.9。
- 本模块不写瞳孔、头姿、2D 注视点；`Capabilities` 只声明 `expression` 与 `eyes`
  （渲染级 `gaze` 通道未真机验证，暂不声明）。

## 冲突

同一端口只能有一个接收者。VRCFT 的 ALVR 模块、其它监听 41463 的程序都会抢端口；
绑定失败时模块卡片会给出原因。需要改端口就在模块目录写 `config.ini`：

```ini
port = 41464
```

并同步修改 ALVR 侧（face.rs 中端口为常量，社区版 ALVR 暂不可配；改端口通常意味着
用别的中转，文档仅作说明）。
