#pragma once
#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace arti::rendering::detail {

// 一个编译好的 compute 程序：PSO + 绑定布局 + 线程组大小。
//
// 线程组大小从反射来（着色器里的 [numthreads]），所以调用方按**元素数量**发起 dispatch，
// 不用自己除以组大小 —— 那个除法写错一次就是边缘像素没被覆盖，而且不会报错，只是画面缺一条。
struct ComputeProgram {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    uint32_t group_x{ 1 };
    uint32_t group_y{ 1 };
    uint32_t group_z{ 1 };
};

ComputeProgram createComputeProgram(nvrhi::IDevice& device,
        const arti::renderer::CompiledComputeProgram& source, std::string_view debug_name);

nvrhi::BindingSetHandle createComputeBindingSet(nvrhi::IDevice& device,
        const ComputeProgram& program,
        std::span<const arti::renderer::vulkan::NvrhiBindingResource> resources);

// extent_* 是**元素数量**（比如 cube 的 512x512x6），内部按线程组大小向上取整。
// 任一维为 0 时不发起 dispatch，返回 false。
bool dispatchCompute(nvrhi::ICommandList& commands, const ComputeProgram& program,
        nvrhi::IBindingSet& bindings, const void* push_constants, size_t push_constant_size,
        uint32_t extent_x, uint32_t extent_y, uint32_t extent_z);

// 把纹理的某个 mip / 数组切片按指定维度绑定。cubemap 要当 RWTexture2DArray 写入时，
// dimension 必须显式给 Texture2DArray —— 不给就按纹理自身的 TextureCube 推断，写入会类型不符。
arti::renderer::vulkan::NvrhiBindingResource makeComputeTextureBinding(std::string name,
        nvrhi::ITexture& texture, nvrhi::TextureDimension dimension,
        nvrhi::TextureSubresourceSet subresources);

} // namespace arti::rendering::detail
