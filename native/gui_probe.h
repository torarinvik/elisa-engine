#pragma once
// Native UI: build real Wicked GUI buttons from the Elisa menu state, verify
// their enabled states, then remove them again so the captured frame is
// unchanged. This is the native counterpart to the Godot host binding the same
// data to Control nodes; the widgets are created and destroyed without being
// rendered.
#include "probe_core.h"
#include "menu_probe.h"
#include "wiGUI.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline bool probe_wicked_gui(wi::gui::GUI& gui, const std::map<std::string, std::string>& manifest) {
    const auto enabled_it = manifest.find("menu_enabled");
    if (enabled_it == manifest.end()) {
        return true;
    }
    const std::vector<int> enabled = parse_int_list(enabled_it->second);
    std::vector<wi::gui::Button*> buttons;
    int disabled = 0;
    for (size_t index = 0; index < enabled.size(); ++index) {
        auto* button = new wi::gui::Button();
        button->Create("ElisaMenuAction_" + std::to_string(index));
        button->SetEnabled(enabled[index] != 0);
        gui.AddWidget(button);
        buttons.push_back(button);
        if (enabled[index] == 0) {
            ++disabled;
        }
    }
    int enabled_count = 0;
    for (auto* button : buttons) {
        if (button->IsEnabled()) {
            ++enabled_count;
        }
    }
    const int expected_enabled = (int)enabled.size() - disabled;
    // Engine theme values (UiStyle::theme_default) reach this host as data: the
    // background tint is applied to the real widgets, and the focused row takes
    // the focus color. The widgets are removed before capture, so Godot remains
    // the host that verifies styled appearance; this is data-level consumption.
    const auto background_it = manifest.find("menu_style_background");
    if (background_it != manifest.end()) {
        const std::vector<int> background = parse_int_list(background_it->second);
        const auto focus_it = manifest.find("menu_style_focus");
        const std::vector<int> focus_color = focus_it == manifest.end() ? background : parse_int_list(focus_it->second);
        const auto focus_row_it = manifest.find("menu_focus");
        const int focus_row = focus_row_it == manifest.end() ? -1 : std::stoi(focus_row_it->second);
        const bool in_range = background.size() == 4 and focus_color.size() == 4 and
            std::all_of(background.begin(), background.end(), [](int value) { return value >= 0 and value <= 255; }) and
            std::all_of(focus_color.begin(), focus_color.end(), [](int value) { return value >= 0 and value <= 255; });
        if (!check(in_range, "wicked gui style colors are RGBA in range")) {
            return false;
        }
        for (size_t index = 0; index < buttons.size(); ++index) {
            const std::vector<int>& color = (int)index == focus_row ? focus_color : background;
            buttons[index]->SetColor(wi::Color((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2], (uint8_t)color[3]));
        }
        int inset = 0;
        const auto padding_it = manifest.find("menu_style_padding");
        if (padding_it != manifest.end()) {
            inset += std::stoi(padding_it->second);
        }
        const auto border_it = manifest.find("menu_style_border");
        if (border_it != manifest.end()) {
            inset += std::stoi(border_it->second);
        }
        std::fprintf(stdout, "wicked style: background=%s focus=%s inset=%d\n",
            background_it->second.c_str(), focus_it == manifest.end() ? background_it->second.c_str() : focus_it->second.c_str(), inset);
    }
    // Remove and delete so nothing is left to render or leak into the frame.
    for (auto* button : buttons) {
        gui.RemoveWidget(button);
        delete button;
    }
    if (!check(enabled_count == expected_enabled, "wicked gui enabled states match the fixture")) {
        return false;
    }
    std::fprintf(stdout, "wicked gui: actions=%u enabled=%d disabled=%d\n",
        (unsigned)enabled.size(), enabled_count, disabled);
    return true;
}

} // namespace probe
