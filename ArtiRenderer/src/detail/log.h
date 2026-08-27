#pragma once
#include "artichoco/core/log.h"

namespace arti::rendering {

// ArtiRenderer 自己的日志频道。
// 注意 arti::renderer（ArtiChoco 的 RHI 层）有一个同名函数，频道叫 "ArtiRHI"。
const core::Logger::Channel& getLogChannel();

} // namespace arti::rendering
