#pragma once
// ALVR 服务端 VRCFT 兼容 UDP 二进制包的解析与帧写入。
//
// 上游线格式（ALVR server_core/src/tracking/face.rs，发送端为权威）：
//   目标端口 41463（0xA1F7），单个数据报由若干「8 字节 ASCII 前缀 + float32 小端数组」
//   块顺序拼接而成。已知前缀与负载（均已对照上游源码确认，详见 docs/protocol.md）：
//     "EyesQuat"  8 float：左眼四元数 [x,y,z,w] + 右眼四元数 [x,y,z,w]（头局部，-Z 前）
//     "CombQuat"  4 float：合并注视四元数 [x,y,z,w]
//     "Face2Fb\0" 70 float：Meta face_tracking2 默认集（含舌部 63-69）
//     "FaceFb\0\0" 63 float：face_tracking 1 遗留集（当前 ALVR 不再发送，保留兼容）
//     "FacePico"  52 float：XR_BD_facial_simulation 表情集（OpenXR 规范的 52 项顺序）
//     "EyesHtc\0" 14 float：Vive 眼部混合形状
//     "LipHtc\0\0" 37 float：Vive 唇部/舌部混合形状
// 注意：VRCFT-ALVR 接收端把 FacePico 当 72 float 读（52 表情 + 20 viseme），与
// 发送端 52 float 不一致；本模块以发送端为准，只解析 52，不伪造 viseme 数据。

#include "fgb/kit/arkit.hpp"
#include "fgb/kit/host.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace alvr {

// 单个前缀块的负载长度（float 个数），全部来自上游源码常量。
constexpr std::size_t kEyesQuatFloats = 8;
constexpr std::size_t kCombQuatFloats = 4;
constexpr std::size_t kFace2FbFloats = 70;
constexpr std::size_t kFace1FbFloats = 63;
constexpr std::size_t kFacePicoFloats = 52;
constexpr std::size_t kEyesHtcFloats = 14;
constexpr std::size_t kLipHtcFloats = 37;

struct Quaternion {
    // glam / OpenXR 约定：[x, y, z, w]，单位四元数，头局部（-Z 前、+Y 上、+X 右）。
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Packet {
    // ---- 注视（来自 EyesQuat / CombQuat，设备级眼动） ----
    Quaternion left{};
    Quaternion right{};
    Quaternion combined{};
    bool has_left = false;
    bool has_right = false;
    bool has_combined = false;

    // ---- 表情块（原始权重 + 存在标志，语义映射在 WriteFrame 里做） ----
    float fb[kFace2FbFloats]{};   // Face2Fb 或 Face1Fb（后者尾部补零、标志相同）
    bool has_fb = false;
    bool fb_is_face1 = false;     // 63 项遗留布局：舌部不可信，不写

    float pico[kFacePicoFloats]{}; // XR_BD_facial_simulation 52 项
    bool has_pico = false;

    float htc_eye[kEyesHtcFloats]{};
    bool has_htc_eye = false;

    float htc_lip[kLipHtcFloats]{};
    bool has_htc_lip = false;

    // ---- 统计与诊断 ----
    std::size_t blocks_parsed = 0;
    std::size_t blocks_unknown = 0;  // 前缀不认识（无法安全跳过，其后数据作废）
    bool truncated = false;          // 尾部块不完整，按未确认数据丢弃
};

// 解析一个完整 UDP 数据报。返回 false 表示整包不可信（太短/前缀全不认识）。
// 解析失败时 out 的内容不保证有效，调用方必须丢弃。
bool ParseDatagram(const std::uint8_t* data, std::size_t size, Packet& out, std::string& error);

// 把单位四元数转成注视方向（头局部单位向量，-Z 前）。
// 四元数含 NaN/Inf 或模长过短时返回 false——不能伪造「看正前方」当有效数据。
bool GazeDirFromQuat(const Quaternion& q, float (&direction)[3]);

// 登记本模块会写的全部 Unified Expressions 目标名。
void DeclareTargets(fgb::kit::ShapeTable& table);

// 写表情 + 双眼属性，并置上对应的 attributes 位。
void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot);

} // namespace alvr
