#include "compute_program.h"

#include <stdexcept>
#include <utility>

namespace arti::rendering::detail {
namespace {

uint32_t ceilDispatch(uint32_t extent, uint32_t group_size) {
    if (group_size == 0) {
        throw std::invalid_argument("A compute dispatch group size must be non-zero.");
    }
    return extent == 0 ? 0 : 1U + (extent - 1U) / group_size;
}

} // namespace

ComputeProgram createComputeProgram(nvrhi::IDevice& device,
        const arti::renderer::CompiledComputeProgram& source, std::string_view debug_name) {
    if (source.thread_group_size_x == 0 || source.thread_group_size_y == 0 ||
            source.thread_group_size_z == 0) {
        throw std::invalid_argument("Compute thread-group dimensions must be non-zero: " +
                                   std::string{ debug_name });
    }

    const auto shaders =
            arti::renderer::vulkan::createNvrhiComputeShaderSet(device, source, debug_name);
    if (!shaders.compute_shader || shaders.binding_layouts.empty() ||
            !shaders.binding_layouts.front()) {
        throw std::runtime_error("Failed to create a compute shader set: " +
                                 std::string{ debug_name });
    }

    ComputeProgram program;
    program.reflection = source.reflection;
    program.layout = shaders.binding_layouts.front();
    program.group_x = source.thread_group_size_x;
    program.group_y = source.thread_group_size_y;
    program.group_z = source.thread_group_size_z;

    nvrhi::ComputePipelineDesc desc;
    desc.setComputeShader(shaders.compute_shader).addBindingLayout(program.layout);
    program.pipeline = device.createComputePipeline(desc);
    if (!program.pipeline) {
        throw std::runtime_error("Failed to create a compute pipeline: " +
                                 std::string{ debug_name });
    }
    return program;
}

nvrhi::BindingSetHandle createComputeBindingSet(nvrhi::IDevice& device,
        const ComputeProgram& program,
        std::span<const arti::renderer::vulkan::NvrhiBindingResource> resources) {
    if (!program.layout || !program.pipeline) {
        throw std::logic_error("Cannot create bindings for an invalid compute program.");
    }
    nvrhi::BindingSetHandle result = arti::renderer::vulkan::createNvrhiBindingSet(device,
            program.reflection, 0, *program.layout, resources);
    if (!result) {
        throw std::runtime_error("NVRHI failed to create a compute binding set.");
    }
    return result;
}

bool dispatchCompute(nvrhi::ICommandList& commands, const ComputeProgram& program,
        nvrhi::IBindingSet& bindings, const void* push_constants, size_t push_constant_size,
        uint32_t extent_x, uint32_t extent_y, uint32_t extent_z) {
    const uint32_t groups_x = ceilDispatch(extent_x, program.group_x);
    const uint32_t groups_y = ceilDispatch(extent_y, program.group_y);
    const uint32_t groups_z = ceilDispatch(extent_z, program.group_z);
    if (groups_x == 0 || groups_y == 0 || groups_z == 0) {
        return false;
    }
    if (!program.pipeline) {
        throw std::logic_error("Cannot dispatch an invalid compute program.");
    }

    // push constant 的大小对着反射校验：这里传的是裸内存，写错大小的表现是数据错位而不是崩，
    // 那种 bug 只会在画面上体现成「某个参数好像没生效」，非常难查。
    const size_t expected = program.reflection.push_constants.empty()
            ? 0U
            : program.reflection.push_constants.front().size;
    if (program.reflection.push_constants.size() > 1 || push_constant_size != expected) {
        throw std::invalid_argument("Compute push-constant data does not match the reflected ABI.");
    }
    if (push_constant_size != 0 && push_constants == nullptr) {
        throw std::invalid_argument("Compute push constants cannot be null with a non-zero size.");
    }

    nvrhi::ComputeState state;
    state.setPipeline(program.pipeline).addBindingSet(&bindings);
    commands.setComputeState(state);
    if (push_constant_size != 0) {
        commands.setPushConstants(push_constants, push_constant_size);
    }
    commands.dispatch(groups_x, groups_y, groups_z);
    return true;
}

arti::renderer::vulkan::NvrhiBindingResource makeComputeTextureBinding(std::string name,
        nvrhi::ITexture& texture, nvrhi::TextureDimension dimension,
        nvrhi::TextureSubresourceSet subresources) {
    auto result = arti::renderer::vulkan::NvrhiBindingResource::Texture(std::move(name), texture);
    result.dimension = dimension;
    result.subresources = subresources;
    return result;
}

} // namespace arti::rendering::detail
