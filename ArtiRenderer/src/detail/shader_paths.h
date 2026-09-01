#pragma once

#include <filesystem>
#include <string_view>

namespace arti::rendering::detail {

// 内建 slang 源码的路径。着色器是运行期编译的，所以这些文件必须在运行时存在。
//
// 两段查找，**整目录二选一**（不逐文件回落）：
//   1. <exe 目录>/shaders/  —— 里面至少有一个 .slang 就用它。构建时 POST_BUILD 拷进去的，
//                              也是打包产物里的那一份，所以二进制可搬移
//   2. ARTIRENDERER_SHADER_DIR —— 构建期注入的源码树绝对路径，开发期的回落。好处是改
//                              shader 不用重新编译 C++
//
// 选中哪个根只判定一次，并记一条 info 日志 —— 排查「为什么用的是旧 shader」时要靠它。
std::filesystem::path shaderPath(std::string_view name);

} // namespace arti::rendering::detail
