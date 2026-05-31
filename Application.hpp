#pragma once

#include <imgui.h>
#include <string>
#define IMREFL_GLM
#include "imrefl.hpp"

enum class weapon
{
    none,
    sword,
    bow,
    staff,
    wand,
    axe
};

class custom_type
{
    int data;
public:
    custom_type(int d) : data{d} {}
    int get() const { return data; }
    void set(int x) { data = x; }
};

template <ImRefl::Config config>
struct ImRefl::Renderer<config, custom_type>
{
    static bool Render(const char* name, custom_type& value)
    {
        int inner = value.get();
        if (Renderer<config, int>::Render(name, inner)) {
            value.set(inner);
            return true;
        }
        return false;
    }
    static bool Render(const char* name, const custom_type& value)
    {
        return ImRefl::DelegateToNonConst<config>(name, value);
    }
};

struct player
{
    std::string name         = "Link";
    bool        invulnerable = false;
    int         health       = 100;

    [[=ImRefl::string]]
    char buffer[64] = {};

    [[=ImRefl::slider(1, 50)]]
    int level = 14;

    [[=ImRefl::ignore]]
    double secret_information = 3.14159;

    [[=ImRefl::readonly]]
    float attack_modifier = 3.5f;

    weapon current_weapon = weapon::sword;

    [[=ImRefl::color]]
    glm::vec3 color = {1, 1, 0};

    const custom_type value = {5};

    std::vector<int> data = {1, 2, 3, 4, 5};
};


static void MainEntry() {
    static player p;
    ImGui::Begin("Debug");
    ImRefl::Input("Player", p);
    ImGui::End();
}