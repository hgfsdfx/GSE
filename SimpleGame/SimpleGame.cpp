/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software under the What The Hell License.
Prototype extensions: procedural night city and simulated hacking.
*/
#include "stdafx.h"
#include "Renderer.h"
#include "Prototype.h"
#include "Dependencies/freeglut.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <Windows.h>

namespace {
std::unique_ptr<Renderer> renderer;
std::unique_ptr<Prototype> game;
auto previousTick = std::chrono::steady_clock::now();
bool closing = false;

void Close() {
    if (closing) return;
    closing = true;
    if (game) game->Save();
    game.reset();
    // freeglut invokes this callback while the window context still exists.
    renderer.reset();
}
void Display() {
    if (!game || closing) return;
    game->Draw();
    glutSwapBuffers();
}
void Resize(int width, int height) {
    if (renderer) renderer->Resize(width, height);
}
void KeyDown(unsigned char key, int, int) {
    if (key == 27) { Close(); glutLeaveMainLoop(); return; }
    if (game) game->Key(key, true);
}
void KeyUp(unsigned char key, int, int) {
    if (game) game->Key(key, false);
}
void Visibility(int visible) {
    if (game && visible != GLUT_VISIBLE) game->ReleaseKeys();
}
void Tick(int) {
    if (!game || closing) return;
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - previousTick).count();
    previousTick = now;
    DWORD foregroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    if (foregroundProcess != GetCurrentProcessId()) game->ReleaseKeys();
    game->Update(std::min(dt, .05f));
    glutPostRedisplay();
    glutTimerFunc(16, Tick, 0);
}
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitContextVersion(3, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1440, 900);
    glutInitWindowPosition(70, 40);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    const int window = glutCreateWindow("NIGHT / LINK - GSE Prototype");
    if (!window) { std::cerr << "Cannot create OpenGL window.\n"; return 1; }
    glewExperimental = GL_TRUE;
    const GLenum glewResult = glewInit();
    if (glewResult != GLEW_OK || !GLEW_VERSION_3_3) {
        std::cerr << "OpenGL 3.3 is required. GLEW: " << glewGetErrorString(glewResult) << '\n';
        glutDestroyWindow(window); return 1;
    }
    glGetError(); // Some GLEW versions probe an unsupported legacy enum in core contexts.
    renderer = std::make_unique<Renderer>(1440, 900);
    if (!renderer->IsInitialized()) {
        std::cerr << "Renderer initialization failed. Check the Shaders folder beside the executable.\n";
        renderer.reset(); glutDestroyWindow(window); return 1;
    }
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) { renderer.reset(); glutDestroyWindow(window); return 1; }
    const auto savePath = std::filesystem::path(executable).parent_path() / L"night_city.save";
    game = std::make_unique<Prototype>(*renderer, savePath);
    glutDisplayFunc(Display);
    glutReshapeFunc(Resize);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutVisibilityFunc(Visibility);
    glutCloseFunc(Close);
    glutIgnoreKeyRepeat(1);
    previousTick = std::chrono::steady_clock::now();
    glutTimerFunc(16, Tick, 0);
    std::cout << "NIGHT / LINK prototype\nWASD move | Space run | Tab scan | Hold E hack\n"
        << "+/- zoom | K save | P pause | Esc save and exit\n"
        << "O post FX | B bloom | V vignette | F edge blur | [ ] exposure\n"
        << ", . bloom strength | 1 2 vignette strength | 3 4 edge blur strength\n"
        << "Save: " << savePath << '\n';
    glutMainLoop();
    Close();
    return 0;
}
