// main_gui.cpp - opens the window and runs the event loop.
//
// SDL2 gives us a window + input events, OpenGL draws into it, and Dear ImGui
// turns App::frame()'s description of the UI into triangles for OpenGL.
// This file is standard boilerplate adapted from Dear ImGui's SDL2+OpenGL3 example.

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <string>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace {

// A soft light theme so it looks like a Mac app rather than a debugger.
void applyTheme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.PopupRounding = 6.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowPadding = ImVec2(16, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.CellPadding = ImVec2(8, 5);
    s.WindowBorderSize = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.97f, 0.97f, 0.98f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.93f, 0.95f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.88f, 0.92f, 1.0f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.85f, 0.86f, 0.89f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.90f, 0.91f, 0.94f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.84f, 0.87f, 0.94f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.76f, 0.81f, 0.92f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.85f, 0.90f, 1.0f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.90f, 0.93f, 1.0f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.80f, 0.87f, 1.0f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.88f, 0.89f, 0.92f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.84f, 0.88f, 0.98f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.96f, 0.97f, 0.99f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.16f, 0.45f, 0.85f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.16f, 0.45f, 0.85f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
}

}  // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL 3.2 core profile - the newest OpenGL macOS supports.
    const char* glslVersion = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("InternScout", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1240, 820, flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 900, 600);
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't write imgui.ini next to the app
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    applyTheme();

    // Retina: the drawable is bigger than the window in points. Load fonts at the
    // pixel size and scale them back down so text is crisp instead of blurry.
    int winW = 0, drawW = 0, h = 0;
    SDL_GetWindowSize(window, &winW, &h);
    SDL_GL_GetDrawableSize(window, &drawW, &h);
    float dpiScale = (winW > 0) ? static_cast<float>(drawW) / winW : 1.0f;
    if (dpiScale < 1.0f) dpiScale = 1.0f;

    App app;
    const char* fontPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
    app.bodyFont = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f * dpiScale);
    app.headingFont = io.Fonts->AddFontFromFileTTF(fontPath, 22.0f * dpiScale);
    if (!app.bodyFont) { app.bodyFont = io.Fonts->AddFontDefault(); app.headingFont = nullptr; }
    io.FontGlobalScale = 1.0f / dpiScale;
    io.FontDefault = app.bodyFont;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glslVersion);

    bool running = true;
    while (running) {
        // Wait up to 100 ms for input so the app idles cheaply, but still
        // redraws regularly for the background fetch status.
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 100)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(window))
                    running = false;
            } while (SDL_PollEvent(&event));
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(10); continue; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.frame();

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(0.97f, 0.97f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
