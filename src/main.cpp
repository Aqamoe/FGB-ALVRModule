// ALVR → FaceGaze Bridge 原生模块。
//
// 监听 ALVR 服务端发到本机 41463 端口的 VRCFT 兼容 UDP 二进制包，
// 输出 Unified Expressions 表情、睁眼度与逐眼注视方向。
// 不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "alvr_payload.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 41463;  // 0xA1F7，上游 face.rs 的 VRCFT_PORT
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class AlvrSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        if (config.Load(directory, "config.ini", config_error)) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        fgb::kit::ShapeTable table;
        alvr::DeclareTargets(table);
        if (!table.Build(host)) {
            error = "宿主没有提供 shape_index，无法映射 Unified Expressions";
            return false;
        }
        if (table.ResolvedCount() < table.Size()) {
            host.Warn("部分 Unified Expressions 形状未被宿主识别，已跳过 " +
                      std::to_string(table.Size() - table.ResolvedCount()) + " 个");
        }
        table_ = std::move(table);

        if (!endpoint_.Bind(port_, error)) {
            return false;
        }
        host.Info("ALVR 监听 UDP " + std::to_string(port_) + "，等待 ALVR 服务端发送");
        host.StatusStarting("等待 ALVR 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        alvr::Packet packet;
        bool parsed = false;

        fgb::kit::UdpPacket datagram;
        // ALVR 按串流帧率推送，只处理最新一包；积压的旧包丢掉比逐帧回放更准。
        if (endpoint_.PollLatest(datagram)) {
            std::string error;
            if (alvr::ParseDatagram(datagram.data.data(), datagram.data.size(), packet, error)) {
                parsed = true;
                if (packet.truncated && packet.truncated != reported_truncated_) {
                    reported_truncated_ = true;
                    host.Warn("ALVR 数据包尾部块不完整（协议版本不匹配？），已按缺失处理");
                }
                if (packet.blocks_unknown > 0 && packet.blocks_unknown != reported_unknown_) {
                    reported_unknown_ = packet.blocks_unknown;
                    host.Warn("ALVR 发送了 " + std::to_string(packet.blocks_unknown) +
                              " 个无法识别的前缀块（本包其后数据一并忽略）");
                }
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包：" + error);
            }
        }

        if (!staleness_.Check(host, endpoint_.LastPacketNs(), now_ns) || !parsed) {
            return false;
        }

        alvr::WriteFrame(packet, table_, &slot);
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("ALVR 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    fgb::kit::ShapeTable table_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "ALVR"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::size_t reported_unknown_ = 0;
    bool reported_truncated_ = false;
};

} // namespace

FGB_KIT_EXPORT(AlvrSource, "com.fgbbridge.alvr", "ALVR",
               FGB_CAP_EXPRESSION | FGB_CAP_EYES)
