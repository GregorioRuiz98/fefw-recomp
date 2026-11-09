// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_controllers.h"
#include <cstdio>
namespace fefw::setup {
namespace { void pump() { SDL_Event event; while(SDL_PollEvent(&event)) {} } }
Controllers::Controllers() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    initialized=SDL_InitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS);
    if(!initialized) error=SDL_GetError();
}
Controllers::~Controllers() { if(pad) SDL_CloseGamepad(pad); if(initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS); }
std::vector<Controller> Controllers::scan() {
    std::vector<Controller> out; if(!initialized) return out;
    pump(); int count=0; auto* ids=SDL_GetGamepads(&count);
    for(int i=0;i<count;++i) {
        char guid[33]{}; SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]),guid,sizeof guid);
        const char* name=SDL_GetGamepadNameForID(ids[i]); out.push_back({ids[i],name?name:"Gamepad",guid});
    }
    SDL_free(ids); return out;
}
bool Controllers::select(SDL_JoystickID id) {
    if(pad&&SDL_GetGamepadID(pad)==id&&connected()) return true;
    if(pad) SDL_CloseGamepad(pad); pad=id?SDL_OpenGamepad(id):nullptr;
    if(id&&!pad) error=SDL_GetError(); return pad!=nullptr;
}
bool Controllers::connected() const { return pad&&SDL_GamepadConnected(pad); }
std::string Controllers::sample() {
    pump(); if(!connected()) return "Disconnected";
    char axes[180]{};
    std::snprintf(axes,sizeof axes,"L: %+.2f, %+.2f     R: %+.2f, %+.2f\nLT: %.2f     RT: %.2f",
        SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX)/32768.0,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY)/32768.0,
        SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX)/32768.0,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTY)/32768.0,
        SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.0,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.0);
    std::string out=axes; out+="\nButtons: "; bool any=false;
    for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i) if(SDL_GetGamepadButton(pad,static_cast<SDL_GamepadButton>(i))) {
        if(any) out+=", "; out+=SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(i)); any=true;
    }
    if(!any) out+="-"; return out;
}
}
