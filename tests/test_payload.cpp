#include "alvr_payload.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace fgb::kit;
using alvr::Packet;
using alvr::ParseDatagram;

namespace {

// ---- 合成包小工具：按上游格式「8 字节前缀 + float32 小端数组」拼字节 ----

void AppendPrefix(std::vector<std::uint8_t>& buf, const char (&prefix)[8]) {
    buf.insert(buf.end(), prefix, prefix + 8);
}

void AppendFloat(std::vector<std::uint8_t>& buf, float value) {
    const std::size_t base = buf.size();
    buf.resize(base + 4);
    std::memcpy(buf.data() + base, &value, 4);  // x86 上即小端
}

void AppendFloats(std::vector<std::uint8_t>& buf, const float* values, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        AppendFloat(buf, values[i]);
    }
}

void AppendFloats(std::vector<std::uint8_t>& buf, std::initializer_list<float> values) {
    for (const float value : values) {
        AppendFloat(buf, value);
    }
}

constexpr char kEyesQuat[8] = {'E', 'y', 'e', 's', 'Q', 'u', 'a', 't'};
constexpr char kCombQuat[8] = {'C', 'o', 'm', 'b', 'Q', 'u', 'a', 't'};
constexpr char kFace2Fb[8] = {'F', 'a', 'c', 'e', '2', 'F', 'b', '\0'};
constexpr char kFace1Fb[8] = {'F', 'a', 'c', 'e', 'F', 'b', '\0', '\0'};
constexpr char kFacePico[8] = {'F', 'a', 'c', 'e', 'P', 'i', 'c', 'o'};
constexpr char kEyesHtc[8] = {'E', 'y', 'e', 's', 'H', 't', 'c', '\0'};
constexpr char kLipHtc[8] = {'L', 'i', 'p', 'H', 't', 'c', '\0', '\0'};
constexpr char kGarbage[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

float FirstNaN() {
    float value = 0.0f;
    const std::uint32_t bits = 0x7FC00000u;  // quiet NaN
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float FirstInf() {
    float value = 0.0f;
    const std::uint32_t bits = 0x7F800000u;  // +Inf
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

int main() {
    std::string error;

    // ---- EyesQuat：已知四元数 → 注视方向 ----
    {
        // 左眼 identity → (0,0,-1)；右眼绕 Y +90° → (-1,0,0)。
        const float kS = 0.70710678f;
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kEyesQuat);
        AppendFloats(buf, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, kS, 0.0f, kS});
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_left && packet.has_right);
        CHECK(!packet.has_combined);
        CHECK_NEAR(packet.left.w, 1.0, 1e-6);
        CHECK_NEAR(packet.right.y, kS, 1e-6);

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0);
        CHECK_NEAR(slot.left_eye.gaze_dir[0], 0.0, 1e-5);
        CHECK_NEAR(slot.left_eye.gaze_dir[1], 0.0, 1e-5);
        CHECK_NEAR(slot.left_eye.gaze_dir[2], -1.0, 1e-5);
        CHECK_NEAR(slot.right_eye.gaze_dir[0], -1.0, 1e-5);
        CHECK_NEAR(slot.right_eye.gaze_dir[2], 0.0, 1e-5);
        // 没有表情块，不应伪造表情属性。
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) == 0);
    }

    // ---- CombQuat：绕 X +90° → (0, 1, 0) 的归一化近似 (0, 0.9999879, -0.0) ----
    {
        const float kS = 0.70710678f;
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kCombQuat);
        AppendFloats(buf, {kS, 0.0f, 0.0f, kS});
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_combined);
        float dir[3];
        CHECK(alvr::GazeDirFromQuat(packet.combined, dir));
        CHECK_NEAR(dir[0], 0.0, 1e-5);
        CHECK_NEAR(dir[1], 1.0, 1e-4);
        CHECK_NEAR(dir[2], 0.0, 1e-4);

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        // 只有合并注视时：写合并通道，不冒充逐眼。
        CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) == 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) == 0);
    }

    // ---- 结构异常必须拒绝或降级 ----
    {
        Packet broken{};
        CHECK(!ParseDatagram(nullptr, 0, broken, error));
        const std::vector<std::uint8_t> tiny = {'E', 'y', 'e'};
        CHECK(!ParseDatagram(tiny.data(), tiny.size(), broken, error));
        // 纯未知前缀：一个块都解析不出来 → 整包丢弃。
        std::vector<std::uint8_t> junk;
        AppendPrefix(junk, kGarbage);
        AppendFloats(junk, {1.0f, 2.0f, 3.0f, 4.0f});
        CHECK(!ParseDatagram(junk.data(), junk.size(), broken, error));
        // EyesQuat 只带半块（4 float）→ truncated，且不产生任何「有效的 8 float」。
        std::vector<std::uint8_t> half;
        AppendPrefix(half, kEyesQuat);
        AppendFloats(half, {0.0f, 0.0f, 0.0f, 1.0f});
        Packet truncated{};
        CHECK(!ParseDatagram(half.data(), half.size(), truncated, error));
        CHECK(truncated.truncated);
        CHECK_EQ(truncated.blocks_parsed, static_cast<std::size_t>(0));
    }

    // ---- 未知前缀夹在合法块之间：前块有效、后块忽略，不越界猜长度 ----
    {
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kEyesHtc);
        AppendFloats(buf, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        AppendPrefix(buf, kGarbage);
        AppendFloats(buf, {0.5f, 0.5f});
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK_EQ(packet.blocks_parsed, static_cast<std::size_t>(1));
        CHECK(packet.has_htc_eye);
    }

    // ---- Face2Fb：70 float，按注册表顺序的索引映射 ----
    {
        float fb[70]{};
        fb[24] = 0.5f;    // jawDrop → JawOpen
        fb[12] = 0.4f;    // eyesClosedL
        fb[28] = 0.2f;    // lidTightenerL → 睁眼度公式里的 squint
        fb[61] = 0.9f;    // upperLipRaiserL
        fb[55] = 0.3f;    // noseWrinklerL
        fb[45] = 0.5f;    // lipSuckLT
        fb[68] = 0.7f;    // tongueOutFb
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kFace2Fb);
        AppendFloats(buf, fb, 70);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_fb);
        CHECK(!packet.fb_is_face1);
        CHECK_NEAR(packet.fb[52], 0.0, 1e-6);  // 未写的下标保持 0

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        CHECK_EQ(table.ResolvedCount(), table.Size());  // 目标名必须都是真实形状

        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.5, 1e-5);
        // 睁眼度沿用上游公式：1 - (0.4 + 0.4 * 0.2)
        CHECK_NEAR(slot.left_eye.openness, 0.52, 1e-5);
        CHECK_NEAR(slot.right_eye.openness, 1.0, 1e-6);
        // MouthUpperUp = max(0, raiser - wrinkler) = 0.6
        CHECK_NEAR(slot.shapes[host.IndexOf("MouthUpperUpLeft")], 0.6, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("MouthUpperDeepenLeft")], 0.6, 1e-5);
        // LipSuckUpper = min(1 - raiser^(1/6), suck)，raiser=0.9 时上限约 0.0174
        const float cap = 1.0f - std::pow(0.9f, 1.0f / 6.0f);
        CHECK_NEAR(slot.shapes[host.IndexOf("LipSuckUpperLeft")], cap, 1e-4);
        CHECK_NEAR(slot.shapes[host.IndexOf("TongueOut")], 0.7, 1e-5);
        // 不声明的能力绝不能出现在属性位上。
        CHECK((slot.attributes & (FGB_ATTR_LEFT_PUPIL | FGB_ATTR_RIGHT_PUPIL | FGB_ATTR_HEAD)) == 0);
    }

    // ---- Face1Fb：63 float 遗留布局，没有舌部 → 不伪造 TongueOut ----
    {
        float fb[63]{};
        fb[24] = 0.5f;   // jawDrop
        fb[62] = 1.0f;   // 最后一个合法下标
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kFace1Fb);
        AppendFloats(buf, fb, 63);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_fb);
        CHECK(packet.fb_is_face1);

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.5, 1e-5);
        CHECK(slot.shape_valid[host.IndexOf("TongueOut")] == 0);  // 未确认来源不给有效值
    }

    // ---- FacePico：52 float（BD 规范的 ARKit 同名集） ----
    {
        float pico[52]{};
        pico[50] = 0.7f;  // JAW_OPEN_BD
        pico[5] = 0.3f;   // EYE_BLINK_L_BD
        pico[10] = 0.1f;  // EYE_LOOK_SQUINT_L_BD
        pico[21] = 0.5f;  // CHEEK_PUFF_BD（单项 → 左右 CheekPuff 通用映射）
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kFacePico);
        AppendFloats(buf, pico, 52);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_pico);

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.7, 1e-5);
        CHECK_NEAR(slot.left_eye.openness, 0.67, 1e-5);  // 1 - (0.3 + 0.3*0.1)
        CHECK_NEAR(slot.shapes[host.IndexOf("CheekPuffLeft")], 0.5, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("CheekPuffRight")], 0.5, 1e-5);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    }

    // ---- FacePico 后面多出 80 字节（VRCFT 接收端按 72 读的那套尾部）：
    //      本模块只认发送端格式的 52，尾部按未确认数据丢弃，不产生形状 ----
    {
        float pico[52]{};
        pico[50] = 0.4f;
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kFacePico);
        AppendFloats(buf, pico, 52);
        for (int i = 0; i < 20; ++i) {
            AppendFloat(buf, 0.9f);  // 若被误读成 Tongue/viseme，就会造出假形状
        }
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_pico);
        CHECK_EQ(packet.blocks_parsed, static_cast<std::size_t>(1));
        CHECK(packet.truncated);  // 尾部无法识别的字节要报告，而不是静默吃掉

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK(slot.shape_valid[host.IndexOf("TongueOut")] == 0);
        CHECK(slot.shape_valid[host.IndexOf("TongueRoll")] == 0);
    }

    // ---- EyesHtc + LipHtc：睁眼、眼睑注视近似、唇部映射 ----
    {
        float eye[14]{};
        eye[0] = 0.5f;   // LeftBlink
        eye[4] = 0.5f;   // LeftSqueeze
        eye[10] = 0.8f;  // LeftIn
        eye[8] = 0.2f;   // LeftOut
        eye[12] = 0.1f;  // LeftUp
        eye[6] = 0.3f;   // LeftDown
        float lip[37]{};
        lip[3] = 0.2f;   // JawOpen
        lip[4] = 0.1f;   // MouthApeShape
        lip[26] = 0.6f;  // TongueLongStep1
        lip[32] = 0.2f;  // TongueLongStep2
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kEyesHtc);
        AppendFloats(buf, eye, 14);
        AppendPrefix(buf, kLipHtc);
        AppendFloats(buf, lip, 37);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_htc_eye && packet.has_htc_lip);

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        // 睁眼度：1 - clamp(0.5 + 0.5*0.5) = 0.25
        CHECK_NEAR(slot.left_eye.openness, 0.25, 1e-5);
        // 没有四元数时退回眼睑注视近似，置信度低但方向是单位向量。
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK_NEAR(slot.left_eye.gaze_confidence, 0.4, 1e-6);
        const float h = 0.8f - 0.2f;
        const float v = 0.1f - 0.3f;
        const float len = std::sqrt(h * h + v * v + 1.0f);
        CHECK_NEAR(slot.left_eye.gaze_dir[0], h / len, 1e-5);
        CHECK_NEAR(slot.left_eye.gaze_dir[1], v / len, 1e-5);
        CHECK_NEAR(slot.left_eye.gaze_dir[2], -1.0f / len, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.3, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("TongueOut")], 0.4, 1e-5);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
    }

    // ---- NaN/Inf/越界清洗：脏值不得污染整帧 ----
    {
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kFace2Fb);
        float fb[70]{};
        fb[24] = FirstNaN();
        fb[32] = FirstInf();
        fb[40] = 2.5f;  // 越界
        fb[41] = -1.0f;
        AppendFloats(buf, fb, 70);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK_NEAR(packet.fb[24], 0.0, 1e-6);
        CHECK_NEAR(packet.fb[32], 0.0, 1e-6);
        CHECK_NEAR(packet.fb[40], 1.0, 1e-6);
        CHECK_NEAR(packet.fb[41], 0.0, 1e-6);
    }

    // ---- 全 NaN 四元数：注视无效，不得伪造「看正前方」 ----
    {
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kEyesQuat);
        AppendFloats(buf, {FirstNaN(), FirstNaN(), FirstNaN(), FirstNaN(),
                          0.0f, 0.0f, 0.0f, 0.0f});
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK(packet.has_left && packet.has_right);
        float dir[3];
        CHECK(!alvr::GazeDirFromQuat(packet.left, dir));   // NaN → 拒绝
        CHECK(!alvr::GazeDirFromQuat(packet.right, dir));  // 零四元数 → 拒绝

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK((slot.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                  FGB_ATTR_COMBINED_GAZE)) == 0);
    }

    // ---- 组合包：EyesQuat + Face2Fb 同报（真实发送端的常态） ----
    {
        std::vector<std::uint8_t> buf;
        AppendPrefix(buf, kEyesQuat);
        AppendFloats(buf, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f});
        AppendPrefix(buf, kFace2Fb);
        float fb[70]{};
        fb[12] = 1.0f;  // 左眼闭合
        fb[24] = 0.25f;
        AppendFloats(buf, fb, 70);
        Packet packet{};
        CHECK(ParseDatagram(buf.data(), buf.size(), packet, error));
        CHECK_EQ(packet.blocks_parsed, static_cast<std::size_t>(2));

        testing::HostMock host;
        Host logger{host.services()};
        ShapeTable table;
        alvr::DeclareTargets(table);
        CHECK(table.Build(logger));
        FgbFrameSlot slot = FreshSlot();
        alvr::WriteFrame(packet, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK_NEAR(slot.left_eye.openness, 0.0, 1e-5);
        CHECK_NEAR(slot.right_eye.openness, 1.0, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.25, 1e-5);
    }

    return CheckSummary("alvr-payload");
}
