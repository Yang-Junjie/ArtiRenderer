#include "environment_bake_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "compute_program.h"
#include "log.h"
#include "shader_paths.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace arti::rendering {
namespace {

// 尺寸沿用 ArtiRenderer-old 的取值。它们的依据：
//   environment 512² —— 天空背景的清晰度上限，再高对 IBL 没有帮助（后面都要卷积掉）
//   irradiance  32²  —— 余弦卷积的结果只剩低频，再大是浪费
//   prefiltered 128² / 8 mips —— 8 级正好把 roughness 0..1 分得够细
constexpr uint32_t kEnvironmentSize = 512;
constexpr uint32_t kEnvironmentMips = 10;
constexpr uint32_t kIrradianceSize = 32;
constexpr uint32_t kPrefilteredSize = 128;
constexpr uint32_t kPrefilteredMips = 8;
constexpr uint32_t kBrdfSize = 256;
constexpr uint32_t kIrradianceSamples = 256;
constexpr uint32_t kPrefilteredSamples = 256;
constexpr uint32_t kBrdfSamples = 512;
constexpr uint32_t kCubeFaces = 6;

struct EquirectPush {
    uint32_t cube_size;
    uint32_t pad[3];
};
struct IrradiancePush {
    uint32_t cube_size;
    uint32_t sample_count;
    uint32_t env_size;
    uint32_t env_mip_count;
};
struct PrefilterPush {
    uint32_t cube_size;
    uint32_t mip_level;
    uint32_t mip_count;
    uint32_t sample_count;
    uint32_t env_size;
    uint32_t env_mip_count;
    uint32_t pad[2];
};
struct BrdfPush {
    uint32_t lut_size;
    uint32_t sample_count;
    uint32_t pad[2];
};
struct MipmapPush {
    uint32_t width;
    uint32_t height;
};

static_assert(sizeof(EquirectPush) == 16);
static_assert(sizeof(IrradiancePush) == 16);
static_assert(sizeof(PrefilterPush) == 32);
static_assert(sizeof(BrdfPush) == 16);
static_assert(sizeof(MipmapPush) == 8);

// cube 的 mip 链下采样。就是个 2x2 盒式滤波，不值得单独开一个 .slang 文件。
constexpr char kMipmapSource[] = R"slang(
[[vk::binding(0, 0)]] Texture2DArray<float4> source_texture;
[[vk::binding(1, 0)]] SamplerState source_sampler;
[[vk::binding(2, 0)]] RWTexture2DArray<float4> output_texture;
struct PushConstants { uint width; uint height; };
[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= push_constants.width || id.y >= push_constants.height || id.z >= 6) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(push_constants.width, push_constants.height);
    output_texture[id] = source_texture.SampleLevel(source_sampler, float3(uv, id.z), 0.0);
}
)slang";

nvrhi::TextureHandle makeCube(nvrhi::IDevice& device, uint32_t size, uint32_t mips,
        const char* debug_name) {
    nvrhi::TextureDesc desc;
    desc.setWidth(size)
            .setHeight(size)
            .setArraySize(kCubeFaces)
            .setMipLevels(mips)
            .setFormat(nvrhi::Format::RGBA16_FLOAT)
            .setDimension(nvrhi::TextureDimension::TextureCube)
            // 必须开 UAV：compute 要往里写。这个标志是建纹理时定的、事后加不了。
            .setIsUAV(true)
            .setDebugName(debug_name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device.createTexture(desc);
}

nvrhi::TextureHandle makeLut(nvrhi::IDevice& device, uint32_t size, const char* debug_name) {
    nvrhi::TextureDesc desc;
    desc.setWidth(size)
            .setHeight(size)
            .setFormat(nvrhi::Format::RGBA16_FLOAT)
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setIsUAV(true)
            .setDebugName(debug_name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device.createTexture(desc);
}

} // namespace

struct EnvironmentBakePass::Impl {
    detail::ComputeProgram equirect;
    detail::ComputeProgram mipmap;
    detail::ComputeProgram irradiance;
    detail::ComputeProgram prefilter;
    detail::ComputeProgram brdf;
    bool programs_ready{ false };

    // equirect 那张是 2D 图，U 方向要 repeat（方位角在 ±π 处环回）；cube 的采样一律 clamp。
    nvrhi::SamplerHandle equirect_sampler;
    nvrhi::SamplerHandle cube_sampler;

    nvrhi::TextureHandle brdf_lut;
    bool brdf_baked{ false };

    // 1×1 全黑的兜底。环境关掉时也得给 binding set 提供有效纹理 ——
    // 空句柄会让 createNvrhiBindingSet 报 "no matching resource"。
    nvrhi::TextureHandle fallback_cube;
    nvrhi::TextureHandle fallback_lut;
    bool fallback_cleared{ false };

    struct Baked {
        nvrhi::TextureHandle environment;
        nvrhi::TextureHandle irradiance;
        nvrhi::TextureHandle prefiltered;
    };
    std::unordered_map<TextureHandle, Baked> cache;

    // 当前发布给下游的是哪一份。换了才自增 revision，下游据此重建 binding set。
    TextureHandle published;
    bool published_ready{ false };
    uint64_t revision{ 0 };

    void publish(EnvironmentResources& out, TextureHandle key, const Baked* baked) {
        const bool ready = baked != nullptr;
        if (published != key || published_ready != ready) {
            ++revision;
            published = key;
            published_ready = ready;
        }
        out.environment = ready ? baked->environment : fallback_cube;
        out.irradiance = ready ? baked->irradiance : fallback_cube;
        out.prefiltered = ready ? baked->prefiltered : fallback_cube;
        out.brdf_lut = ready ? brdf_lut : fallback_lut;
        out.sampler = cube_sampler;
        out.prefiltered_mips = ready ? kPrefilteredMips : 1;
        out.ready = ready;
        out.revision = revision;
    }
};

EnvironmentBakePass::EnvironmentBakePass()
        : m_impl(std::make_unique<Impl>()) {}

EnvironmentBakePass::~EnvironmentBakePass() = default;

void EnvironmentBakePass::prepare(PassPrepareContext& context) {
    if (m_impl->programs_ready) {
        return;
    }
    auto& device = context.device();

    m_impl->equirect = detail::createComputeProgram(device,
            arti::renderer::SlangCompiler::compileCompute(
                    { detail::shaderPath("equirect_to_cube.slang") }),
            "ArtiRenderer equirect to cube");
    m_impl->mipmap = detail::createComputeProgram(device,
            arti::renderer::SlangCompiler::compileComputeSource(kMipmapSource,
                    "arti_cube_mipmap.slang"),
            "ArtiRenderer cube mipmap");
    m_impl->irradiance = detail::createComputeProgram(device,
            arti::renderer::SlangCompiler::compileCompute(
                    { detail::shaderPath("irradiance.slang") }),
            "ArtiRenderer irradiance");
    m_impl->prefilter = detail::createComputeProgram(device,
            arti::renderer::SlangCompiler::compileCompute(
                    { detail::shaderPath("prefilter.slang") }),
            "ArtiRenderer prefilter");
    m_impl->brdf = detail::createComputeProgram(device,
            arti::renderer::SlangCompiler::compileCompute(
                    { detail::shaderPath("brdf_lut.slang") }),
            "ArtiRenderer BRDF LUT");

    nvrhi::SamplerDesc cube_desc;
    cube_desc.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
    m_impl->cube_sampler = device.createSampler(cube_desc);
    nvrhi::SamplerDesc equirect_desc = cube_desc;
    equirect_desc.setAddressU(nvrhi::SamplerAddressMode::Repeat);
    m_impl->equirect_sampler = device.createSampler(equirect_desc);
    if (!m_impl->cube_sampler || !m_impl->equirect_sampler) {
        throw std::runtime_error("NVRHI failed to create the environment samplers.");
    }

    m_impl->brdf_lut = makeLut(device, kBrdfSize, "ArtiRenderer BRDF LUT");
    m_impl->fallback_cube = makeCube(device, 1, 1, "ArtiRenderer fallback IBL cube");
    m_impl->fallback_lut = makeLut(device, 1, "ArtiRenderer fallback BRDF LUT");
    if (!m_impl->brdf_lut || !m_impl->fallback_cube || !m_impl->fallback_lut) {
        throw std::runtime_error("NVRHI failed to create the environment bake resources.");
    }

    m_impl->programs_ready = true;
    getLogChannel().debug("Compiled the environment bake compute programs");
}

void EnvironmentBakePass::record(PassRecordContext& context) {
    auto& commands = context.commands();
    auto& device = context.device();
    auto& out = context.environment();

    if (!m_impl->fallback_cleared) {
        const nvrhi::Color black{ 0.0f, 0.0f, 0.0f, 0.0f };
        commands.clearTextureFloat(m_impl->fallback_cube, nvrhi::AllSubresources, black);
        commands.clearTextureFloat(m_impl->fallback_lut, nvrhi::AllSubresources, black);
        commands.setPermanentTextureState(m_impl->fallback_cube,
                nvrhi::ResourceStates::ShaderResource);
        commands.setPermanentTextureState(m_impl->fallback_lut,
                nvrhi::ResourceStates::ShaderResource);
        m_impl->fallback_cleared = true;
    }

    const auto& environment = context.frame().scene().environment;
    const TextureHandle source = environment.equirectangular_texture;
    if (!environment.enabled || !source.isValid()) {
        m_impl->publish(out, TextureHandle{}, nullptr);
        return;
    }

    if (const auto cached = m_impl->cache.find(source); cached != m_impl->cache.end()) {
        m_impl->publish(out, source, &cached->second);
        return;
    }

    Impl::Baked baked;
    baked.environment =
            makeCube(device, kEnvironmentSize, kEnvironmentMips, "ArtiRenderer environment cube");
    baked.irradiance = makeCube(device, kIrradianceSize, 1, "ArtiRenderer irradiance cube");
    baked.prefiltered =
            makeCube(device, kPrefilteredSize, kPrefilteredMips, "ArtiRenderer prefiltered cube");
    if (!baked.environment || !baked.irradiance || !baked.prefiltered) {
        throw std::runtime_error("NVRHI failed to create the environment cubes.");
    }

    const auto dispatch = [&](const detail::ComputeProgram& program,
                                  std::span<const arti::renderer::vulkan::NvrhiBindingResource>
                                          resources,
                                  const void* push, size_t push_size, uint32_t x, uint32_t y,
                                  uint32_t z) {
        const auto bindings = detail::createComputeBindingSet(device, program, resources);
        detail::dispatchCompute(commands, program, *bindings, push, push_size, x, y, z);
    };

    const auto face_slice = [](uint32_t mip) {
        return nvrhi::TextureSubresourceSet{ mip, 1, 0, kCubeFaces };
    };
    const auto all_mips = nvrhi::TextureSubresourceSet{ 0, kEnvironmentMips, 0, kCubeFaces };
    nvrhi::ITexture& env = *baked.environment;

    // 1. 等距柱状 → cube mip 0。
    commands.setTextureState(&env, face_slice(0), nvrhi::ResourceStates::UnorderedAccess);
    const std::array equirect_resources = {
        detail::makeComputeTextureBinding("equirect_texture", context.texture(source),
                nvrhi::TextureDimension::Texture2D, nvrhi::AllSubresources),
        arti::renderer::vulkan::NvrhiBindingResource::Sampler("equirect_sampler",
                *m_impl->equirect_sampler),
        detail::makeComputeTextureBinding("cube_output", env,
                nvrhi::TextureDimension::Texture2DArray, face_slice(0)),
    };
    const EquirectPush equirect_push{ kEnvironmentSize, { 0, 0, 0 } };
    dispatch(m_impl->equirect, equirect_resources, &equirect_push, sizeof(equirect_push),
            kEnvironmentSize, kEnvironmentSize, kCubeFaces);
    commands.setTextureState(&env, face_slice(0), nvrhi::ResourceStates::ShaderResource);

    // 2. 逐级下采样出 mip 链。prefilter / irradiance 按样本覆盖的立体角选 mip，所以这条链是
    //    它们压噪声的前提，不是可选项。
    uint32_t source_size = kEnvironmentSize;
    for (uint32_t mip = 1; mip < kEnvironmentMips; ++mip) {
        const uint32_t size = std::max(1U, source_size / 2U);
        const std::array resources = {
            detail::makeComputeTextureBinding("source_texture", env,
                    nvrhi::TextureDimension::Texture2DArray, face_slice(mip - 1)),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("source_sampler",
                    *m_impl->cube_sampler),
            detail::makeComputeTextureBinding("output_texture", env,
                    nvrhi::TextureDimension::Texture2DArray, face_slice(mip)),
        };
        commands.setTextureState(&env, face_slice(mip), nvrhi::ResourceStates::UnorderedAccess);
        const MipmapPush push{ size, size };
        dispatch(m_impl->mipmap, resources, &push, sizeof(push), size, size, kCubeFaces);
        commands.setTextureState(&env, face_slice(mip), nvrhi::ResourceStates::ShaderResource);
        source_size = size;
    }
    commands.setPermanentTextureState(&env, nvrhi::ResourceStates::ShaderResource);

    // 3. 漫反射辐照度。
    nvrhi::ITexture& irradiance = *baked.irradiance;
    commands.setTextureState(&irradiance, nvrhi::AllSubresources,
            nvrhi::ResourceStates::UnorderedAccess);
    const std::array irradiance_resources = {
        detail::makeComputeTextureBinding("environment_map", env,
                nvrhi::TextureDimension::TextureCube, all_mips),
        arti::renderer::vulkan::NvrhiBindingResource::Sampler("environment_sampler",
                *m_impl->cube_sampler),
        detail::makeComputeTextureBinding("irradiance_output", irradiance,
                nvrhi::TextureDimension::Texture2DArray, nvrhi::AllSubresources),
    };
    const IrradiancePush irradiance_push{ kIrradianceSize, kIrradianceSamples, kEnvironmentSize,
        kEnvironmentMips };
    dispatch(m_impl->irradiance, irradiance_resources, &irradiance_push, sizeof(irradiance_push),
            kIrradianceSize, kIrradianceSize, kCubeFaces);
    commands.setPermanentTextureState(&irradiance, nvrhi::ResourceStates::ShaderResource);

    // 4. 镜面预滤波，一个 mip 一个 roughness。
    nvrhi::ITexture& prefiltered = *baked.prefiltered;
    for (uint32_t mip = 0; mip < kPrefilteredMips; ++mip) {
        const uint32_t size = std::max(1U, kPrefilteredSize >> mip);
        commands.setTextureState(&prefiltered, face_slice(mip),
                nvrhi::ResourceStates::UnorderedAccess);
        const std::array resources = {
            detail::makeComputeTextureBinding("environment_map", env,
                    nvrhi::TextureDimension::TextureCube, all_mips),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("environment_sampler",
                    *m_impl->cube_sampler),
            detail::makeComputeTextureBinding("prefilter_output", prefiltered,
                    nvrhi::TextureDimension::Texture2DArray, face_slice(mip)),
        };
        const PrefilterPush push{ size, mip, kPrefilteredMips, kPrefilteredSamples,
            kEnvironmentSize, kEnvironmentMips, { 0, 0 } };
        dispatch(m_impl->prefilter, resources, &push, sizeof(push), size, size, kCubeFaces);
    }
    commands.setPermanentTextureState(&prefiltered, nvrhi::ResourceStates::ShaderResource);

    // 5. BRDF LUT 和环境贴图无关，全局烘一次就够。
    if (!m_impl->brdf_baked) {
        nvrhi::ITexture& lut = *m_impl->brdf_lut;
        commands.setTextureState(&lut, nvrhi::AllSubresources,
                nvrhi::ResourceStates::UnorderedAccess);
        const std::array resources = { detail::makeComputeTextureBinding("brdf_output", lut,
                nvrhi::TextureDimension::Texture2D, nvrhi::AllSubresources) };
        const BrdfPush push{ kBrdfSize, kBrdfSamples, { 0, 0 } };
        dispatch(m_impl->brdf, resources, &push, sizeof(push), kBrdfSize, kBrdfSize, 1);
        commands.setPermanentTextureState(&lut, nvrhi::ResourceStates::ShaderResource);
        m_impl->brdf_baked = true;
    }

    getLogChannel().info("Baked the IBL environment for texture {}", source.toString());
    const auto inserted = m_impl->cache.emplace(source, std::move(baked));
    m_impl->publish(out, source, &inserted.first->second);
}

} // namespace arti::rendering
