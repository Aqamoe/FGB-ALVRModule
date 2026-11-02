# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- ALVR 服务端 VRCFT 兼容 UDP 二进制包（41463 端口）→ Unified Expressions 表情、
  睁眼度与逐眼注视方向；支持 `EyesQuat`/`CombQuat`/`Face2Fb`/`FacePico`/
  `EyesHtc`/`LipHtc` 六种块与遗留 `FaceFb`。
- 表情映射：FB 70 项与 HTC 唇舌 37 项按 VRCFT-ALVR 口径直写 Unified；
  Pico/BD 52 项经 Kit 通用 ARKit 表。
- 注视：单位四元数 → 头局部单位向量（-Z 前）；NaN/零四元数拒绝，不伪造方向。
- `config.ini` 支持覆盖端口。
- 协议单测（合成包、长度/前缀异常、四元数方向、清洗、未确认字段不伪造）
  + 通过真实 ABI 的端到端冒烟测试。

### 已知限制
- 未在任何真机（ALVR + 头显）验证，状态为未验证。
- `FacePico` 负载长度上游发送端/接收端不一致（52 vs 72），本模块按发送端 52 解析。
- 不输出瞳孔、头姿、2D 注视点；渲染级 `gaze` 通道未声明。
