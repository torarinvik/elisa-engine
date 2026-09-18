#pragma once
// Native UI: build real Wicked GUI buttons from the Elisa menu state, verify
// their enabled states, then remove them again so the captured frame is
// unchanged. This is the native counterpart to the Godot host binding the same
// data to Control nodes; the widgets are created and destroyed without being
// rendered.
#include "probe_core.h"
#include "menu_probe.h"
#include "wiGUI.h"

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
