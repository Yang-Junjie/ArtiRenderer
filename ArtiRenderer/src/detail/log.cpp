#include "log.h"

namespace arti::rendering {

const core::Logger::Channel& getLogChannel() {
    static const auto channel = core::Logger::registerChannel("ArtiRenderer");
    return *channel;
}

} // namespace arti::rendering
