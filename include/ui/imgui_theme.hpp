#pragma once

/**
 * imgui_theme.hpp - the look this client's own panels are drawn in.
 *
 * Lives here rather than in the UI manager because it is not only the game that
 * draws ImGui: the asset manager is a separate program with its own window, and
 * a tool that installs the game's assets looking nothing like the game is a tool
 * that looks like it came from somewhere else. One definition, so the two cannot
 * drift into two different dark blues.
 *
 * Colours only, and the rounding that goes with them. Scale belongs to whoever
 * owns the window: the game sizes its interface against the display and the
 * asset manager against its renderer, and neither answer suits the other.
 */

#include "imgui.h"

namespace wowee {
namespace ui {

/// The client's palette: dark blue-black panels, blue-grey controls, softened
/// corners.
inline void applyWoweeStyle(ImGuiStyle& style) {
    ImGui::StyleColorsDark();

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    // ImGui uses PopupBg for hover tooltips. Keep their text and item details
    // fully legible over the 3D scene.
    colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.40f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.50f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.25f, 0.45f, 1.00f);
}

}  // namespace ui
}  // namespace wowee
