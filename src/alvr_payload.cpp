#include "alvr_payload.hpp"

#include <cmath>
#include <cstring>

namespace alvr {

using fgb::kit::Arkit;
using fgb::kit::ArkitWeights;
using fgb::kit::CleanUnit;

namespace {

// ---- 前缀常量（8 字节 ASCII，含 NUL 填充；与上游 face.rs 的字节串一致） ----
constexpr char kPrefixEyesQuat[8] = {'E', 'y', 'e', 's', 'Q', 'u', 'a', 't'};
constexpr char kPrefixCombQuat[8] = {'C', 'o', 'm', 'b', 'Q', 'u', 'a', 't'};
constexpr char kPrefixFace2Fb[8] = {'F', 'a', 'c', 'e', '2', 'F', 'b', '\0'};
constexpr char kPrefixFace1Fb[8] = {'F', 'a', 'c', 'e', 'F', 'b', '\0', '\0'};
constexpr char kPrefixFacePico[8] = {'F', 'a', 'c', 'e', 'P', 'i', 'c', 'o'};
constexpr char kPrefixEyesHtc[8] = {'E', 'y', 'e', 's', 'H', 't', 'c', '\0'};
constexpr char kPrefixLipHtc[8] = {'L', 'i', 'p', 'H', 't', 'c', '\0', '\0'};

enum BlockId {
    BlockUnknown = -1,
    BlockEyesQuat = 0,
    BlockCombQuat,
    BlockFace2Fb,
    BlockFace1Fb,
    BlockFacePico,
    BlockEyesHtc,
    BlockLipHtc,
};

struct BlockSpec {
    const char* prefix;
    std::size_t floats;
    BlockId id;
};

constexpr BlockSpec kBlocks[] = {
    {kPrefixEyesQuat, kEyesQuatFloats, BlockEyesQuat},
    {kPrefixCombQuat, kCombQuatFloats, BlockCombQuat},
    {kPrefixFace2Fb, kFace2FbFloats, BlockFace2Fb},
    {kPrefixFace1Fb, kFace1FbFloats, BlockFace1Fb},
    {kPrefixFacePico, kFacePicoFloats, BlockFacePico},
    {kPrefixEyesHtc, kEyesHtcFloats, BlockEyesHtc},
    {kPrefixLipHtc, kLipHtcFloats, BlockLipHtc},
};

constexpr std::size_t kPrefixSize = 8;

// 在 pos 处匹配已知前缀；命中时返回块描述符，否则返回 nullptr。
const BlockSpec* MatchBlock(const std::uint8_t* data, std::size_t size, std::size_t pos) {
    if (pos + kPrefixSize > size) {
        return nullptr;
    }
    for (const BlockSpec& spec : kBlocks) {
        if (std::memcmp(data + pos, spec.prefix, kPrefixSize) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

// 读一个小端 float32 并清洗 NaN/Inf；不做越界裁剪（调用方负责范围检查）。
float ReadFloat(const std::uint8_t* bytes) {
    float value = 0.0f;
    std::memcpy(&value, bytes, sizeof(value)); // x86 上即小端
    if (!std::isfinite(value)) {               // 上游偶尔给 NaN/Inf，不能污染整帧
        return 0.0f;
    }
    return value;
}

// 读 count 个 float 到目标数组，逐个清洗到 [0,1]。
void ReadWeights(const std::uint8_t* payload, float* dest, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        dest[i] = CleanUnit(ReadFloat(payload + i * 4));
    }
}

void ReadQuaternion(const std::uint8_t* payload, Quaternion& out) {
    out.x = ReadFloat(payload);
    out.y = ReadFloat(payload + 4);
    out.z = ReadFloat(payload + 8);
    out.w = ReadFloat(payload + 12);
}

// ---- Face2Fb / Face1Fb 的索引常量（XR_FB_face_tracking2 默认集，已对照注册表确认） ----
constexpr std::size_t FbBrowLowererL = 0, FbBrowLowererR = 1;
constexpr std::size_t FbCheekPuffL = 2, FbCheekPuffR = 3;
constexpr std::size_t FbCheekRaiserL = 4, FbCheekRaiserR = 5;
constexpr std::size_t FbCheekSuckL = 6, FbCheekSuckR = 7;
constexpr std::size_t FbChinRaiserB = 8, FbChinRaiserT = 9;
constexpr std::size_t FbDimplerL = 10, FbDimplerR = 11;
constexpr std::size_t FbEyesClosedL = 12, FbEyesClosedR = 13;
constexpr std::size_t FbInnerBrowRaiserL = 22, FbInnerBrowRaiserR = 23;
constexpr std::size_t FbJawDrop = 24;
constexpr std::size_t FbJawSidewaysLeft = 25, FbJawSidewaysRight = 26;
constexpr std::size_t FbJawThrust = 27;
constexpr std::size_t FbLidTightenerL = 28, FbLidTightenerR = 29;
constexpr std::size_t FbLipCornerDepressorL = 30, FbLipCornerDepressorR = 31;
constexpr std::size_t FbLipCornerPullerL = 32, FbLipCornerPullerR = 33;
constexpr std::size_t FbLipFunnelerLB = 34, FbLipFunnelerLT = 35;
constexpr std::size_t FbLipFunnelerRB = 36, FbLipFunnelerRT = 37;
constexpr std::size_t FbLipPressorL = 38, FbLipPressorR = 39;
constexpr std::size_t FbLipPuckerL = 40, FbLipPuckerR = 41;
constexpr std::size_t FbLipStretcherL = 42, FbLipStretcherR = 43;
constexpr std::size_t FbLipSuckLB = 44, FbLipSuckLT = 45;
constexpr std::size_t FbLipSuckRB = 46, FbLipSuckRT = 47;
constexpr std::size_t FbLipTightenerL = 48, FbLipTightenerR = 49;
constexpr std::size_t FbLipsToward = 50;
constexpr std::size_t FbLowerLipDepressorL = 51, FbLowerLipDepressorR = 52;
constexpr std::size_t FbMouthLeft = 53, FbMouthRight = 54;
constexpr std::size_t FbNoseWrinklerL = 55, FbNoseWrinklerR = 56;
constexpr std::size_t FbOuterBrowRaiserL = 57, FbOuterBrowRaiserR = 58;
constexpr std::size_t FbUpperLidRaiserL = 59, FbUpperLidRaiserR = 60;
constexpr std::size_t FbUpperLipRaiserL = 61, FbUpperLipRaiserR = 62;
constexpr std::size_t FbTongueOut = 68;

// ---- EyesHtc 索引（XR_HTC_facial_tracking 眼部 14 项，已对照注册表确认） ----
constexpr std::size_t HtcLeftBlink = 0, HtcLeftWide = 1;
constexpr std::size_t HtcRightBlink = 2, HtcRightWide = 3;
constexpr std::size_t HtcLeftSqueeze = 4, HtcRightSqueeze = 5;
constexpr std::size_t HtcLeftDown = 6, HtcRightDown = 7;
constexpr std::size_t HtcLeftOut = 8, HtcRightIn = 9;
constexpr std::size_t HtcLeftIn = 10, HtcRightOut = 11;
constexpr std::size_t HtcLeftUp = 12, HtcRightUp = 13;

// ---- LipHtc 索引（XR_HTC_facial_tracking 唇部 37 项，已对照注册表确认） ----
constexpr std::size_t HtcJawRight = 0, HtcJawLeft = 1, HtcJawForward = 2, HtcJawOpen = 3;
constexpr std::size_t HtcMouthApeShape = 4;
constexpr std::size_t HtcMouthUpperRight = 5, HtcMouthUpperLeft = 6;
constexpr std::size_t HtcMouthLowerRight = 7, HtcMouthLowerLeft = 8;
constexpr std::size_t HtcMouthUpperOverturn = 9, HtcMouthLowerOverturn = 10;
constexpr std::size_t HtcMouthPout = 11;
constexpr std::size_t HtcMouthSmileRight = 12, HtcMouthSmileLeft = 13;
constexpr std::size_t HtcMouthSadRight = 14, HtcMouthSadLeft = 15;
constexpr std::size_t HtcCheekPuffRight = 16, HtcCheekPuffLeft = 17;
constexpr std::size_t HtcCheekSuck = 18;
constexpr std::size_t HtcMouthUpperInside = 23, HtcMouthLowerInside = 24;
constexpr std::size_t HtcMouthLowerOverlay = 25;
constexpr std::size_t HtcTongueLongStep1 = 26, HtcTongueLongStep2 = 32;
constexpr std::size_t HtcTongueLeft = 27, HtcTongueRight = 28;
constexpr std::size_t HtcTongueUp = 29, HtcTongueDown = 30, HtcTongueRoll = 31;

// ---- FacePico 索引 → ARKit 槽位（XR_BD_facial_simulation 的 52 项规范顺序） ----
// 注意：这个顺序来自 OpenXR 注册表的 XrFaceExpressionBD；VRCFT-ALVR 接收端的
// FacePico 枚举用的是另一套（旧 Pico Launcher）顺序且读到 72 项，两者都不可信，
// 以 ALVR 发送端 + 注册表为准，详见 docs/protocol.md。
constexpr Arkit kPicoToArkit[kFacePicoFloats] = {
    Arkit::BrowDownLeft,      Arkit::BrowDownRight,    Arkit::BrowInnerUp,
    Arkit::BrowOuterUpLeft,   Arkit::BrowOuterUpRight, Arkit::EyeBlinkLeft,
    Arkit::EyeLookDownLeft,   Arkit::EyeLookInLeft,    Arkit::EyeLookOutLeft,
    Arkit::EyeLookUpLeft,     Arkit::EyeSquintLeft,    Arkit::EyeWideLeft,
    Arkit::EyeBlinkRight,     Arkit::EyeLookDownRight, Arkit::EyeLookInRight,
    Arkit::EyeLookOutRight,   Arkit::EyeLookUpRight,   Arkit::EyeSquintRight,
    Arkit::EyeWideRight,      Arkit::NoseSneerLeft,    Arkit::NoseSneerRight,
    Arkit::CheekPuff,         Arkit::CheekSquintLeft,  Arkit::CheekSquintRight,
    Arkit::MouthClose,        Arkit::MouthFunnel,      Arkit::MouthPucker,
    Arkit::MouthLeft,         Arkit::MouthRight,       Arkit::MouthSmileLeft,
    Arkit::MouthSmileRight,   Arkit::MouthFrownLeft,   Arkit::MouthFrownRight,
    Arkit::MouthDimpleLeft,   Arkit::MouthDimpleRight, Arkit::MouthStretchLeft,
    Arkit::MouthStretchRight, Arkit::MouthRollLower,   Arkit::MouthRollUpper,
    Arkit::MouthShrugLower,   Arkit::MouthShrugUpper,  Arkit::MouthPressLeft,
    Arkit::MouthPressRight,   Arkit::MouthLowerDownLeft, Arkit::MouthLowerDownRight,
    Arkit::MouthUpperUpLeft,  Arkit::MouthUpperUpRight, Arkit::JawForward,
    Arkit::JawLeft,           Arkit::JawRight,         Arkit::JawOpen,
    Arkit::TongueOut,
};

// BD 规范里没有 mouthUpperUp 与 noseSneer 的扣减关系，但 ARKit 语义表按
// 「上唇抬高不含皱鼻」处理；沿用 Kit 通用映射，不再二次加工。
void PicoToArkit(const float* pico, ArkitWeights& weights) {
    for (std::size_t i = 0; i < kFacePicoFloats; ++i) {
        weights[static_cast<std::size_t>(kPicoToArkit[i])] = pico[i];
    }
}

// 上游三套来源共用的睁眼度公式：1 - clamp(blink + blink * squint)。
float Openness(float blink, float squint) {
    return 1.0f - fgb::kit::Clamp01(blink + blink * squint);
}

// 上唇抬高与皱鼻的差值（上游用 max(0, raiser - wrinkler) 拆出上唇形状）。
float UpperLipNet(float raiser, float wrinkler) {
    const float net = raiser - wrinkler;
    return net > 0.0f ? net : 0.0f;
}

// LipSuckUpper 的上游近似：min(1 - raiser^(1/6), lipSuck)。
float LipSuckUpperApprox(float raiser, float suck) {
    const float cap = 1.0f - std::pow(raiser, 1.0f / 6.0f);
    return suck < cap ? suck : cap;
}

void WriteFbShapes(const float* w, bool is_face1, const fgb::kit::ShapeTable& table,
                   FgbFrameSlot* slot) {
    table.Write(slot, "BrowLowererLeft", w[FbBrowLowererL]);
    table.Write(slot, "BrowLowererRight", w[FbBrowLowererR]);
    table.Write(slot, "BrowPinchLeft", w[FbBrowLowererL]);
    table.Write(slot, "BrowPinchRight", w[FbBrowLowererR]);
    table.Write(slot, "CheekPuffLeft", w[FbCheekPuffL]);
    table.Write(slot, "CheekPuffRight", w[FbCheekPuffR]);
    table.Write(slot, "CheekSquintLeft", w[FbCheekRaiserL]);
    table.Write(slot, "CheekSquintRight", w[FbCheekRaiserR]);
    table.Write(slot, "CheekSuckLeft", w[FbCheekSuckL]);
    table.Write(slot, "CheekSuckRight", w[FbCheekSuckR]);
    table.Write(slot, "MouthRaiserLower", w[FbChinRaiserB]);
    table.Write(slot, "MouthRaiserUpper", w[FbChinRaiserT]);
    table.Write(slot, "MouthDimpleLeft", w[FbDimplerL]);
    table.Write(slot, "MouthDimpleRight", w[FbDimplerR]);
    table.Write(slot, "BrowInnerUpLeft", w[FbInnerBrowRaiserL]);
    table.Write(slot, "BrowInnerUpRight", w[FbInnerBrowRaiserR]);
    table.Write(slot, "JawOpen", w[FbJawDrop]);
    table.Write(slot, "JawLeft", w[FbJawSidewaysLeft]);
    table.Write(slot, "JawRight", w[FbJawSidewaysRight]);
    table.Write(slot, "JawForward", w[FbJawThrust]);
    table.Write(slot, "EyeSquintLeft", w[FbLidTightenerL]);
    table.Write(slot, "EyeSquintRight", w[FbLidTightenerR]);
    table.Write(slot, "MouthFrownLeft", w[FbLipCornerDepressorL]);
    table.Write(slot, "MouthFrownRight", w[FbLipCornerDepressorR]);
    table.Write(slot, "MouthCornerPullLeft", w[FbLipCornerPullerL]);
    table.Write(slot, "MouthCornerPullRight", w[FbLipCornerPullerR]);
    table.Write(slot, "MouthCornerSlantLeft", w[FbLipCornerPullerL]);
    table.Write(slot, "MouthCornerSlantRight", w[FbLipCornerPullerR]);
    table.Write(slot, "LipFunnelLowerLeft", w[FbLipFunnelerLB]);
    table.Write(slot, "LipFunnelUpperLeft", w[FbLipFunnelerLT]);
    table.Write(slot, "LipFunnelLowerRight", w[FbLipFunnelerRB]);
    table.Write(slot, "LipFunnelUpperRight", w[FbLipFunnelerRT]);
    table.Write(slot, "MouthPressLeft", w[FbLipPressorL]);
    table.Write(slot, "MouthPressRight", w[FbLipPressorR]);
    table.Write(slot, "LipPuckerUpperLeft", w[FbLipPuckerL]);
    table.Write(slot, "LipPuckerLowerLeft", w[FbLipPuckerL]);
    table.Write(slot, "LipPuckerUpperRight", w[FbLipPuckerR]);
    table.Write(slot, "LipPuckerLowerRight", w[FbLipPuckerR]);
    table.Write(slot, "MouthStretchLeft", w[FbLipStretcherL]);
    table.Write(slot, "MouthStretchRight", w[FbLipStretcherR]);
    table.Write(slot, "LipSuckLowerLeft", w[FbLipSuckLB]);
    table.Write(slot, "LipSuckUpperLeft", LipSuckUpperApprox(w[FbUpperLipRaiserL], w[FbLipSuckLT]));
    table.Write(slot, "LipSuckLowerRight", w[FbLipSuckRB]);
    table.Write(slot, "LipSuckUpperRight", LipSuckUpperApprox(w[FbUpperLipRaiserR], w[FbLipSuckRT]));
    table.Write(slot, "MouthTightenerLeft", w[FbLipTightenerL]);
    table.Write(slot, "MouthTightenerRight", w[FbLipTightenerR]);
    table.Write(slot, "MouthClosed", w[FbLipsToward]);
    table.Write(slot, "MouthLowerDownLeft", w[FbLowerLipDepressorL]);
    table.Write(slot, "MouthLowerDownRight", w[FbLowerLipDepressorR]);
    table.Write(slot, "MouthUpperLeft", w[FbMouthLeft]);
    table.Write(slot, "MouthLowerLeft", w[FbMouthLeft]);
    table.Write(slot, "MouthUpperRight", w[FbMouthRight]);
    table.Write(slot, "MouthLowerRight", w[FbMouthRight]);
    table.Write(slot, "NoseSneerLeft", w[FbNoseWrinklerL]);
    table.Write(slot, "NoseSneerRight", w[FbNoseWrinklerR]);
    table.Write(slot, "BrowOuterUpLeft", w[FbOuterBrowRaiserL]);
    table.Write(slot, "BrowOuterUpRight", w[FbOuterBrowRaiserR]);
    table.Write(slot, "EyeWideLeft", w[FbUpperLidRaiserL]);
    table.Write(slot, "EyeWideRight", w[FbUpperLidRaiserR]);
    table.Write(slot, "MouthUpperUpLeft", UpperLipNet(w[FbUpperLipRaiserL], w[FbNoseWrinklerL]));
    table.Write(slot, "MouthUpperDeepenLeft", UpperLipNet(w[FbUpperLipRaiserL], w[FbNoseWrinklerL]));
    table.Write(slot, "MouthUpperUpRight", UpperLipNet(w[FbUpperLipRaiserR], w[FbNoseWrinklerR]));
    table.Write(slot, "MouthUpperDeepenRight", UpperLipNet(w[FbUpperLipRaiserR], w[FbNoseWrinklerR]));
    // FB 的 EyesLook*（14-21）是眼睑近似，VRCFT 接收端也没把它们当注视用，
    // 这里保持一致：不写 gaze。舌部细分（63-67、69）在 Unified 里没有确认的
    // 对应形状，只写 TongueOut；Face1Fb 布局根本没有 63-69，跳过不伪造。
    if (!is_face1) {
        table.Write(slot, "TongueOut", w[FbTongueOut]);
    }
}

void WriteHtcEyeShapes(const float* w, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    table.Write(slot, "EyeWideLeft", w[HtcLeftWide]);
    table.Write(slot, "EyeWideRight", w[HtcRightWide]);
    table.Write(slot, "EyeSquintLeft", w[HtcLeftSqueeze]);
    table.Write(slot, "EyeSquintRight", w[HtcRightSqueeze]);
    // 上游把 Wide/Squeeze 兼作眉毛近似，保留同一口径。
    table.Write(slot, "BrowInnerUpLeft", w[HtcLeftWide]);
    table.Write(slot, "BrowOuterUpLeft", w[HtcLeftWide]);
    table.Write(slot, "BrowInnerUpRight", w[HtcRightWide]);
    table.Write(slot, "BrowOuterUpRight", w[HtcRightWide]);
    table.Write(slot, "BrowPinchLeft", w[HtcLeftSqueeze]);
    table.Write(slot, "BrowLowererLeft", w[HtcLeftSqueeze]);
    table.Write(slot, "BrowPinchRight", w[HtcRightSqueeze]);
    table.Write(slot, "BrowLowererRight", w[HtcRightSqueeze]);
}

void WriteHtcLipShapes(const float* w, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    table.Write(slot, "JawOpen", w[HtcJawOpen] + w[HtcMouthApeShape]);
    table.Write(slot, "JawLeft", w[HtcJawLeft]);
    table.Write(slot, "JawRight", w[HtcJawRight]);
    table.Write(slot, "JawForward", w[HtcJawForward]);
    table.Write(slot, "MouthClosed", w[HtcMouthApeShape]);
    table.Write(slot, "MouthUpperUpRight", w[HtcMouthUpperRight] - w[HtcMouthUpperOverturn]);
    table.Write(slot, "MouthUpperDeepenRight", w[HtcMouthUpperRight] - w[HtcMouthUpperOverturn]);
    table.Write(slot, "MouthUpperUpLeft", w[HtcMouthUpperLeft] - w[HtcMouthUpperOverturn]);
    table.Write(slot, "MouthUpperDeepenLeft", w[HtcMouthUpperLeft] - w[HtcMouthUpperOverturn]);
    table.Write(slot, "MouthLowerDownLeft", w[HtcMouthLowerLeft] - w[HtcMouthLowerOverturn]);
    table.Write(slot, "MouthLowerDownRight", w[HtcMouthLowerRight] - w[HtcMouthLowerOverturn]);
    table.Write(slot, "LipPuckerUpperLeft", w[HtcMouthPout]);
    table.Write(slot, "LipPuckerLowerLeft", w[HtcMouthPout]);
    table.Write(slot, "LipPuckerUpperRight", w[HtcMouthPout]);
    table.Write(slot, "LipPuckerLowerRight", w[HtcMouthPout]);
    table.Write(slot, "LipFunnelUpperLeft", w[HtcMouthUpperOverturn]);
    table.Write(slot, "LipFunnelUpperRight", w[HtcMouthUpperOverturn]);
    // 上游把 LipFunnelLower 也写成 UpperOverturn，是明确的近似而非笔误，保留。
    table.Write(slot, "LipFunnelLowerLeft", w[HtcMouthUpperOverturn]);
    table.Write(slot, "LipFunnelLowerRight", w[HtcMouthUpperOverturn]);
    table.Write(slot, "LipSuckUpperLeft", w[HtcMouthUpperInside]);
    table.Write(slot, "LipSuckUpperRight", w[HtcMouthUpperInside]);
    table.Write(slot, "LipSuckLowerLeft", w[HtcMouthLowerInside]);
    table.Write(slot, "LipSuckLowerRight", w[HtcMouthLowerInside]);
    table.Write(slot, "MouthUpperLeft", w[HtcMouthUpperLeft]);
    table.Write(slot, "MouthUpperRight", w[HtcMouthUpperRight]);
    table.Write(slot, "MouthLowerLeft", w[HtcMouthLowerLeft]);
    table.Write(slot, "MouthLowerRight", w[HtcMouthLowerRight]);
    table.Write(slot, "MouthCornerPullLeft", w[HtcMouthSmileLeft]);
    table.Write(slot, "MouthCornerPullRight", w[HtcMouthSmileRight]);
    table.Write(slot, "MouthCornerSlantLeft", w[HtcMouthSmileLeft]);
    table.Write(slot, "MouthCornerSlantRight", w[HtcMouthSmileRight]);
    table.Write(slot, "MouthFrownLeft", w[HtcMouthSadLeft]);
    table.Write(slot, "MouthFrownRight", w[HtcMouthSadRight]);
    table.Write(slot, "MouthRaiserUpper", w[HtcMouthLowerOverlay] - w[HtcMouthUpperInside]);
    table.Write(slot, "MouthRaiserLower", w[HtcMouthLowerOverlay]);
    table.Write(slot, "CheekPuffLeft", w[HtcCheekPuffLeft]);
    table.Write(slot, "CheekPuffRight", w[HtcCheekPuffRight]);
    table.Write(slot, "CheekSuckLeft", w[HtcCheekSuck]);
    table.Write(slot, "CheekSuckRight", w[HtcCheekSuck]);
    table.Write(slot, "TongueOut", (w[HtcTongueLongStep1] + w[HtcTongueLongStep2]) * 0.5f);
    table.Write(slot, "TongueUp", w[HtcTongueUp]);
    table.Write(slot, "TongueDown", w[HtcTongueDown]);
    table.Write(slot, "TongueLeft", w[HtcTongueLeft]);
    table.Write(slot, "TongueRight", w[HtcTongueRight]);
    table.Write(slot, "TongueRoll", w[HtcTongueRoll]);
    // 上游对 CheekSquint/MouthDimple 的 smile 近似照抄，但把 MouthStretch
    // 两侧都写成 SadRight，属明显笔误，不复刻（宁缺毋假）。
    table.Write(slot, "CheekSquintLeft", w[HtcMouthSmileLeft]);
    table.Write(slot, "CheekSquintRight", w[HtcMouthSmileRight]);
    table.Write(slot, "MouthDimpleLeft", w[HtcMouthSmileLeft] * 0.5f);
    table.Write(slot, "MouthDimpleRight", w[HtcMouthSmileRight] * 0.5f);
}

} // namespace

bool ParseDatagram(const std::uint8_t* data, std::size_t size, Packet& out, std::string& error) {
    out = Packet{};
    if (data == nullptr || size < kPrefixSize) {
        error = "数据包不足 8 字节，无法容纳任何前缀块";
        return false;
    }

    std::size_t pos = 0;
    bool any_block = false;
    while (pos + kPrefixSize <= size) {
        const BlockSpec* spec = MatchBlock(data, size, pos);
        if (spec == nullptr) {
            // 不认识的前缀无法推断长度，只能向前扫描下一个已知前缀。
            std::size_t next = pos + 1;
            while (next + kPrefixSize <= size && MatchBlock(data, size, next) == nullptr) {
                ++next;
            }
            if (next + kPrefixSize > size) {
                out.truncated = next != pos;  // 尾部只剩无法识别的字节
                break;
            }
            ++out.blocks_unknown;
            pos = next;
            continue;
        }

        const std::size_t payload_bytes = spec->floats * 4;
        if (pos + kPrefixSize + payload_bytes > size) {
            out.truncated = true;  // 半块数据：宁缺毋假，整块丢弃
            break;
        }
        const std::uint8_t* payload = data + pos + kPrefixSize;

        switch (spec->id) {
        case BlockEyesQuat:
            ReadQuaternion(payload, out.left);
            ReadQuaternion(payload + 16, out.right);
            out.has_left = true;
            out.has_right = true;
            break;
        case BlockCombQuat:
            ReadQuaternion(payload, out.combined);
            out.has_combined = true;
            break;
        case BlockFace2Fb:
            ReadWeights(payload, out.fb, kFace2FbFloats);
            out.has_fb = true;
            out.fb_is_face1 = false;
            break;
        case BlockFace1Fb:
            // 遗留 63 项：尾部（舌部 63-69）不存在，保持 zero 并标记，不写 TongueOut。
            ReadWeights(payload, out.fb, kFace1FbFloats);
            out.has_fb = true;
            out.fb_is_face1 = true;
            break;
        case BlockFacePico:
            ReadWeights(payload, out.pico, kFacePicoFloats);
            out.has_pico = true;
            break;
        case BlockEyesHtc:
            ReadWeights(payload, out.htc_eye, kEyesHtcFloats);
            out.has_htc_eye = true;
            break;
        case BlockLipHtc:
            ReadWeights(payload, out.htc_lip, kLipHtcFloats);
            out.has_htc_lip = true;
            break;
        default:
            break;
        }
        ++out.blocks_parsed;
        any_block = true;
        pos += kPrefixSize + payload_bytes;
    }

    if (!any_block) {
        error = "没有解析出任何已知前缀块";
        return false;
    }
    return true;
}

bool GazeDirFromQuat(const Quaternion& q, float (&direction)[3]) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) ||
        !std::isfinite(q.w)) {
        return false;
    }
    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(norm > 0.001f)) {
        return false;  // 零四元数没有方向语义，不能假装「看正前方」
    }
    // 先归一化再用，防止上游给的模长漂移。
    const float x = q.x / norm;
    const float y = q.y / norm;
    const float z = q.z / norm;
    const float w = q.w / norm;
    // v = q * (0,0,-1)，即旋转矩阵第三列取反；OpenXR 与 FGB ABI 同为
    // 右手系、-Z 前、+Y 上、+X 右，得到即头局部注视单位向量。
    direction[0] = -2.0f * (x * z + w * y);
    direction[1] = 2.0f * (x * w - y * z);
    direction[2] = 2.0f * (x * x + y * y) - 1.0f;
    return true;
}

void DeclareTargets(fgb::kit::ShapeTable& table) {
    // Face2Fb / LipHtc 直写、但不在 Kit 通用 ARKit 表里的目标名。
    table.Declare("CheekSuckLeft");
    table.Declare("CheekSuckRight");
    table.Declare("MouthUpperDeepenLeft");
    table.Declare("MouthUpperDeepenRight");
    table.Declare("MouthTightenerLeft");
    table.Declare("MouthTightenerRight");
    table.Declare("TongueUp");
    table.Declare("TongueDown");
    table.Declare("TongueLeft");
    table.Declare("TongueRight");
    table.Declare("TongueRoll");
    fgb::kit::DeclareUnifiedTargets(table);
}

namespace {

void WriteEyes(const Packet& packet, FgbFrameSlot* slot) {
    float direction[3] = {0.0f, 0.0f, -1.0f};

    if (packet.has_left && GazeDirFromQuat(packet.left, direction)) {
        slot->left_eye.gaze_dir[0] = direction[0];
        slot->left_eye.gaze_dir[1] = direction[1];
        slot->left_eye.gaze_dir[2] = direction[2];
        slot->left_eye.gaze_confidence = 0.9f;  // 设备级眼动，但平滑策略未知
        slot->attributes |= FGB_ATTR_LEFT_GAZE;
    }
    if (packet.has_right && GazeDirFromQuat(packet.right, direction)) {
        slot->right_eye.gaze_dir[0] = direction[0];
        slot->right_eye.gaze_dir[1] = direction[1];
        slot->right_eye.gaze_dir[2] = direction[2];
        slot->right_eye.gaze_confidence = 0.9f;
        slot->attributes |= FGB_ATTR_RIGHT_GAZE;
    }
    if (!packet.has_left && !packet.has_right && packet.has_combined &&
        GazeDirFromQuat(packet.combined, direction)) {
        // 只有合并注视时写合并通道；不冒充逐眼数据。
        slot->combined_gaze_dir[0] = direction[0];
        slot->combined_gaze_dir[1] = direction[1];
        slot->combined_gaze_dir[2] = direction[2];
        slot->attributes |= FGB_ATTR_COMBINED_GAZE;
    }

    // 睁眼度：三套来源的 blink/squint 索引都已确认，按 fb > htc_eye > pico 取用。
    if (packet.has_fb) {
        slot->left_eye.openness = Openness(packet.fb[FbEyesClosedL], packet.fb[FbLidTightenerL]);
        slot->right_eye.openness =
            Openness(packet.fb[FbEyesClosedR], packet.fb[FbLidTightenerR]);
        slot->attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS;
    } else if (packet.has_htc_eye) {
        slot->left_eye.openness =
            Openness(packet.htc_eye[HtcLeftBlink], packet.htc_eye[HtcLeftSqueeze]);
        slot->right_eye.openness =
            Openness(packet.htc_eye[HtcRightBlink], packet.htc_eye[HtcRightSqueeze]);
        slot->attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS;
    } else if (packet.has_pico) {
        slot->left_eye.openness = Openness(packet.pico[5], packet.pico[10]);
        slot->right_eye.openness = Openness(packet.pico[12], packet.pico[17]);
        slot->attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS;
    }

    if ((slot->attributes & (FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS)) != 0) {
        slot->left_eye.openness_confidence = 1.0f;
        slot->right_eye.openness_confidence = 1.0f;
    }

    // HTC 没有任何四元数注视时，退回眼睑 Look 权重近似（上游同款，置信度压低）。
    if (packet.has_htc_eye && (slot->attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                                   FGB_ATTR_COMBINED_GAZE)) == 0) {
        float left_dir[3];
        float right_dir[3];
        fgb::kit::GazeFromLookWeights(packet.htc_eye[HtcLeftIn] - packet.htc_eye[HtcLeftOut],
                                      packet.htc_eye[HtcLeftUp] - packet.htc_eye[HtcLeftDown],
                                      left_dir);
        fgb::kit::GazeFromLookWeights(packet.htc_eye[HtcRightOut] - packet.htc_eye[HtcRightIn],
                                      packet.htc_eye[HtcRightUp] - packet.htc_eye[HtcRightDown],
                                      right_dir);
        slot->left_eye.gaze_dir[0] = left_dir[0];
        slot->left_eye.gaze_dir[1] = left_dir[1];
        slot->left_eye.gaze_dir[2] = left_dir[2];
        slot->left_eye.gaze_confidence = 0.4f;  // 眼睑代理，只当趋势
        slot->right_eye.gaze_dir[0] = right_dir[0];
        slot->right_eye.gaze_dir[1] = right_dir[1];
        slot->right_eye.gaze_dir[2] = right_dir[2];
        slot->right_eye.gaze_confidence = 0.4f;
        slot->attributes |= FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE;
    }
}

} // namespace

void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    bool has_expression = false;

    if (packet.has_pico) {
        ArkitWeights weights{};
        PicoToArkit(packet.pico, weights);
        has_expression = has_expression || fgb::kit::MapArkitToUnified(weights, table, slot) > 0;
    }
    if (packet.has_fb) {
        // FB 的 70 项不是 ARKit 语义，直写 Unified（与 VRCFT 接收端同源口径）。
        // Face1Fb（63 项）没有舌部，WriteFbShapes 里按标志跳过 TongueOut。
        WriteFbShapes(packet.fb, packet.fb_is_face1, table, slot);
        has_expression = true;
    }
    if (packet.has_htc_lip) {
        WriteHtcLipShapes(packet.htc_lip, table, slot);
        has_expression = true;
    }
    if (packet.has_htc_eye) {
        WriteHtcEyeShapes(packet.htc_eye, table, slot);
        has_expression = true;
    }
    if (has_expression) {
        slot->attributes |= FGB_ATTR_EXPRESSION;
    }

    WriteEyes(packet, slot);
}

} // namespace alvr
