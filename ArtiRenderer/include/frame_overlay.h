#pragma once
#include "handle.h"

#include <cstdint>

// 前向声明而不是 include imgui.h：不用 UI 的调用方不该被拖上 imgui 的头。
// renderer 内部实现 pass 时才真的 include。
struct ImDrawData;

namespace arti::rendering {

// 一帧里画在场景之上的东西。和 RenderScene 分开：那个是「场景是什么」，这个是「场景之上还盖了
// 什么」，语义不同，混在一起以后加别的 overlay（调试线框、gizmo）会越来越含混。
//
// 谁拥有 ImGui context 是宿主的事 —— renderer 不链窗口层，也就没法装 ImGui 的平台后端。宿主自己
// NewFrame/Render，把 GetDrawData() 的结果放进来。为空表示这一帧没有 UI，ImGuiPass 会整体跳过，
// 连 shader 都不编译。
struct FrameOverlay {
    ImDrawData* imgui_draw_data{ nullptr };
};

// TextureHandle 和 ImTextureID 都是 uint64_t，所以直接一对一，不需要一层 UI 专用的纹理注册表。
// 副作用是 renderer 里任何一张纹理都能直接喂给 ImGui::Image()。
//
// 返回类型写 uint64_t 而不是 ImTextureID，这样这个头不用 include imgui.h。imgui 默认配置下
// 两者是同一个类型（typedef ImU64 ImTextureID），空句柄也正好对上 ImTextureID_Invalid。
constexpr uint64_t imguiTextureId(TextureHandle handle) noexcept { return handle.value(); }

} // namespace arti::rendering
