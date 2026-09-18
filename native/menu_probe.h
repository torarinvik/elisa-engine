#pragma once
// Native consumption of the Elisa menu state. The Godot host binds the same
// fixture fields to real UI controls; the native host validates them and
// reports the layout it would draw. This is data-level consumption, not a
// native widget toolkit, and it is labelled as such.
#include "probe_core.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline std::vector<int> parse_int_list(const std::string& spec) {
    std::vector<int> values;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(',', start);
        if (end == std::string::npos) {
            end = spec.size();
        }
        values.push_back(std::stoi(spec.substr(start, end - start)));
        start = end + 1;
    }
    return values;
}

inline bool probe_menu(const std::map<std::string, std::string>& manifest) {
    const auto actions_it = manifest.find("menu_actions");
    const auto enabled_it = manifest.find("menu_enabled");
    const auto focus_it = manifest.find("menu_focus");
    if (actions_it == manifest.end() || enabled_it == manifest.end() || focus_it == manifest.end()) {
        return true;
    }
    const int actions = std::stoi(actions_it->second);
    const int focus = std::stoi(focus_it->second);
    const std::vector<int> enabled = parse_int_list(enabled_it->second);
    if (!check(actions > 0 && (int)enabled.size() == actions, "menu action and enable counts agree")) {
        return false;
    }
    if (!check(focus >= 0 && focus < actions && enabled[focus] == 1, "menu focus is an enabled row")) {
        return false;
    }
    int disabled = 0;
    for (int flag : enabled) {
        disabled += (flag == 0) ? 1 : 0;
    }
    int visible = actions;
    const auto visible_it = manifest.find("menu_visible");
    if (visible_it != manifest.end()) {
        visible = std::stoi(visible_it->second);
        if (!check(visible > 0 && visible <= actions && focus < visible,
                "menu focus is inside the visible window")) {
            return false;
        }
    }
    std::fprintf(stdout, "menu: actions=%d focus=%d visible=%d disabled=%d\n",
        actions, focus, visible, disabled);
    return true;
}

} // namespace probe
