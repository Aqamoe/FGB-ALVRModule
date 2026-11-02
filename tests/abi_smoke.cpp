// 走真实 ABI 的端到端冒烟：加载 DLL → query → initialize → 喂合成 UDP → update → 检查提交帧。
//
// 它会绑定 41463 端口，运行前关掉 VRCFT 和其它 ALVR 接收端。
// 因此不挂进 ctest，只由 `cmake --build ... --target smoke-test` 触发。

#include "alvr_payload.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"
#include "fgb_module_api.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint16_t kPort = 41463;

void AppendPrefix(std::vector<std::uint8_t>& buf, const char (&prefix)[8]) {
    buf.insert(buf.end(), prefix, prefix + 8);
}

void AppendFloat(std::vector<std::uint8_t>& buf, float value) {
    const std::size_t base = buf.size();
    buf.resize(base + 4);
    std::memcpy(buf.data() + base, &value, 4);
}

// EyesQuat（左眼 identity）+ Face2Fb（jawDrop 0.62、eyesClosedL 0.3、lidTightenerL 0.2）。
std::vector<std::uint8_t> BuildSyntheticPacket() {
    std::vector<std::uint8_t> buf;
    constexpr char kEyesQuat[8] = {'E', 'y', 'e', 's', 'Q', 'u', 'a', 't'};
    constexpr char kFace2Fb[8] = {'F', 'a', 'c', 'e', '2', 'F', 'b', '\0'};
    AppendPrefix(buf, kEyesQuat);
    const float quats[8] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    for (const float value : quats) {
        AppendFloat(buf, value);
    }
    AppendPrefix(buf, kFace2Fb);
    float fb[70]{};
    fb[24] = 0.62f;  // jawDrop
    fb[12] = 0.3f;   // eyesClosedL
    fb[28] = 0.2f;   // lidTightenerL
    for (const float value : fb) {
        AppendFloat(buf, value);
    }
    return buf;
}

bool SendToSelf(const std::vector<std::uint8_t>& bytes) {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPort);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(handle, reinterpret_cast<const char*>(bytes.data()),
                              static_cast<int>(bytes.size()), 0,
                              reinterpret_cast<sockaddr*>(&target), sizeof(target));
    ::closesocket(handle);
    ::WSACleanup();
    return sent != SOCKET_ERROR;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法：%s <alvr.dll 路径>\n", argv[0]);
        return 2;
    }

    HMODULE library = ::LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(library != nullptr);
    if (library == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu（DLL 是否已构建？依赖是否在同目录？）\n",
                    ::GetLastError());
        return CheckSummary("alvr-abi-smoke");
    }

    const auto query = reinterpret_cast<decltype(&fgb_module_query_v2)>(
        ::GetProcAddress(library, "fgb_module_query_v2"));
    CHECK(query != nullptr);

    fgb::kit::testing::HostMock host;
    // Kit 的 MakeParams 会把 module_dir 指针存进返回的拷贝里；用一个明确长寿的
    // 具名串，避免默认参数临时对象在调用结束时销毁（悬垂读取会随机崩溃）。
    const std::string module_dir = ".";
    FgbInitParams params = host.MakeParams(module_dir);

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    CHECK(module != nullptr);
    if (module == nullptr) {
        ::FreeLibrary(library);
        return CheckSummary("alvr-abi-smoke");
    }
    CHECK(module->id == std::string{"com.fgbbridge.alvr"});
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    CHECK_EQ(module->capabilities,
             static_cast<std::uint32_t>(FGB_CAP_EXPRESSION | FGB_CAP_EYES));
    CHECK(query(FGB_MODULE_ABI_V2 + 1) == nullptr);  // ABI 不匹配必须拒绝

    void* self = nullptr;
    CHECK_EQ(module->initialize(&self, host.services(), &params), 1);
    if (self == nullptr) {
        std::printf("initialize 失败：%s\n", host.LastStatusDetail().c_str());
        ::FreeLibrary(library);
        return CheckSummary("alvr-abi-smoke");
    }

    // 没有数据时不应该提交帧
    module->update(self);
    CHECK(host.committed().empty());

    CHECK(SendToSelf(BuildSyntheticPacket()));
    for (int attempt = 0; attempt < 200 && host.committed().empty(); ++attempt) {
        module->update(self);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!host.committed().empty());

    if (!host.committed().empty()) {
        const FgbFrameSlot& slot = host.committed().back();
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.62, 1e-5);
        // 睁眼度：1 - (0.3 + 0.3 * 0.2) = 0.64
        CHECK_NEAR(slot.left_eye.openness, 0.64, 1e-4);
        // identity 四元数 → 注视正前方 (0,0,-1)
        CHECK_NEAR(slot.left_eye.gaze_dir[0], 0.0, 1e-4);
        CHECK_NEAR(slot.left_eye.gaze_dir[2], -1.0, 1e-4);
        CHECK(host.HasStatus(FGB_STATUS_STARTING) || host.HasStatus(FGB_STATUS_ACTIVE));
    }

    module->teardown(self);
    ::FreeLibrary(library);
    return CheckSummary("alvr-abi-smoke");
}
