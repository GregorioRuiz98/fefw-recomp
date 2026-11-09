// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
namespace fefw::setup {
struct Controller { SDL_JoystickID id; std::string name,guid; };
class Controllers {
public:
    Controllers(); ~Controllers();
    std::vector<Controller> scan();
    bool select(SDL_JoystickID id);
    std::string sample();
    bool connected() const;
    std::string error;
private:
    bool initialized=false;
    SDL_Gamepad* pad=nullptr;
};
}
