#include "shader_paths.h"

#include "log.h"

#include "artichoco/core/io/paths.h"

#include <system_error>

#ifndef ARTIRENDERER_SHADER_DIR
#error "ARTIRENDERER_SHADER_DIR must be provided by the build system."
#endif

namespace arti::rendering::detail {
namespace {

// exe 旁边有没有一份可用的 shader 目录。
//
// 要求「至少有一个 .slang」而不是只看目录存在：拷贝失败留下的空壳目录会让整条查找路指向
// 一个什么都没有的地方，而回落分支再也不会被走到。
bool hasStagedShaders(const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return false;
    }

    std::filesystem::directory_iterator entries{ directory, error };
    if (error) {
        return false;
    }
    for (const auto& entry: entries) {
        if (entry.path().extension() == ".slang") {
            return true;
        }
    }
    return false;
}

// 整目录二选一，**不逐文件回落**。
//
// .slang 之间有 #include（ibl_common.slang 被 irradiance / prefilter / deferred_lighting 引用），
// 而 Slang 解析 include 是相对包含它的那个文件。逐文件回落一旦出现「A 在 exe 旁边、它
// include 的 B 只在源码树」，报出来的是一个跟路径无关的编译错误 —— 比「文件找不到」难查
// 得多。整目录二选一保证 include 永远在同一个根里解析。
const std::filesystem::path& shaderRoot()
{
    static const std::filesystem::path root = []() -> std::filesystem::path {
        auto staged = core::executableDir() / "shaders";
        if (hasStagedShaders(staged)) {
            getLogChannel().info("Loading shaders from '{}' (staged next to the executable)",
                    staged.string());
            return staged;
        }

        std::filesystem::path source{ ARTIRENDERER_SHADER_DIR };
        getLogChannel().info("Loading shaders from the build-time source tree '{}' "
                             "(no staged copy next to the executable; this binary is not "
                             "relocatable)",
                source.string());
        return source;
    }();
    return root;
}

} // namespace

std::filesystem::path shaderPath(std::string_view name)
{
    return shaderRoot() / name;
}

} // namespace arti::rendering::detail
