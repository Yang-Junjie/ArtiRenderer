#include "shadow_pass.h"

namespace arti::rendering {

struct ShadowPass::Impl {};

ShadowPass::ShadowPass() : m_impl(std::make_unique<Impl>()) {}

ShadowPass::~ShadowPass() = default;

bool ShadowPass::isEnabled(const FrameContext&) const {
    // 阶段 1 只装接缝，不产生画面变化。阶段 3 改成「场景里有 casts_shadow 的方向光且有 draw」。
    return false;
}

void ShadowPass::prepare(PassPrepareContext&) {}

void ShadowPass::record(PassRecordContext&) {}

} // namespace arti::rendering
