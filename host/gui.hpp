// The SDL host's window: one OpenGL 3.3 core context, owned by the host, with Dear
// ImGui (SDL2 + OpenGL3 backends) on it.  The map chooser, the game's
// picture and the minimap are all drawn through ImGui.
#pragma once
#include <cstdint>
#include <SDL.h>
#include "imgui.h"

// Creates the window (resizable, OpenGL), its context and ImGui.
SDL_Window *gui_open(int w, int h);
void gui_close();
SDL_Window *gui_window();
SDL_GLContext gui_context();

// One frame: gui_begin (clears the window), then ImGui calls and any GL
// drawing, then gui_end, which draws ImGui over it and swaps.
void gui_begin();
void gui_end();

// Passes an event to ImGui.
void gui_event(const SDL_Event &e);

// A texture for ImGui::Image / draw lists, from RGBA pixels (only GL 1.1 calls).
class GuiTexture {
public:
    GuiTexture() = default;
    ~GuiTexture();
    GuiTexture(const GuiTexture &) = delete;
    GuiTexture &operator=(const GuiTexture &) = delete;

    // pixels: w * h, 0xAABBGGRR; the texture is (re)made when the size changes.
    void upload(const uint32_t *pixels, int w, int h, bool smooth);
    ImTextureID id() const { return (ImTextureID)(intptr_t)tex; }

private:
    unsigned tex = 0;
    int tw = 0, th = 0;
};
