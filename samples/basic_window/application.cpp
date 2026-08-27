#include "artichoco/core/application.h"
#include "artichoco/platform/window/window_factory.h"
#include "basic_window_layer.h"

#include <charconv>
#include <memory>
#include <string_view>

namespace arti::core {

// 由 artichoco_core 的 entry_point.cpp 调用。
// 用法: basic_window [--headless] [--frames N]
Application* createApplication(int argc, char** argv)
{
    ApplicationCreateInfo info;
    info.name = "ArtiRenderer Basic Window";
    info.log_channel = "ArtiRenderer";
    info.width = 1'280;
    info.height = 720;

    bool headless = false;
    uint32_t frame_limit = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument == "--headless") {
            headless = true;
        } else if (argument == "--frames" && (index + 1) < argc) {
            const std::string_view value{ argv[++index] };
            uint32_t parsed = 0;
            const auto result =
                    std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc{}) {
                frame_limit = parsed;
            }
        }
    }

    if (!headless) {
        info.window_factory = platform::createSDLWindow;
    }

    auto* app = new Application(info);
    app->pushLayer(std::make_unique<sample::BasicWindowLayer>(!headless, frame_limit));
    return app;
}

} // namespace arti::core
