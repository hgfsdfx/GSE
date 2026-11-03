/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software under the What The Hell License.
Prototype extensions: procedural night city and simulated hacking.
*/
#include "stdafx.h"
#include "Renderer.h"
#include "Prototype.h"
#include "PerformanceLog.h"
#include <Windows.h>
#define GLFW_INCLUDE_NONE
#include "Dependencies/GLFW/include/GLFW/glfw3.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Prototype> game;
    PerformanceLog profiler;
    GLFWwindow* window = nullptr;
    bool closing = false, iconified = false, focused = true, resizePending = false;
    int framebufferWidth = 1440, framebufferHeight = 900;

    void Close()
    {
        if (closing)
        {
            return;
        }
        closing = true;
        if (game)
        {
            game->Save();
        }
        profiler.Stop(renderer.get());
        game.reset();
        // GPU queries, buffers and framebuffers must die before the GLFW context.
        renderer.reset();
    }

    struct GlfwSession
    {
        ~GlfwSession()
        {
            Close();
            if (window)
            {
                glfwDestroyWindow(window);
                window = nullptr;
            }
            glfwTerminate();
        }
    };

    void Error(int code, const char* description)
    {
        std::cerr << "[GLFW] " << code << ": " << (description ? description : "unknown error")
                  << '\n';
    }

    void Display()
    {
        const auto drawStart = ProfileClock::now();
        game->Draw();
        const auto drawEnd = ProfileClock::now();
        glfwSwapBuffers(window);
        const auto swapEnd = ProfileClock::now();
        profiler.RecordFrame(*renderer,
                             *game,
                             swapEnd,
                             ProfileMilliseconds(drawEnd - drawStart),
                             ProfileMilliseconds(swapEnd - drawEnd));
    }

    void Resize(GLFWwindow*, int width, int height)
    {
        // A resize drag may queue many callbacks. Allocate HDR targets only once,
        // at the final framebuffer size of the next rendered frame.
        framebufferWidth = width;
        framebufferHeight = height;
        resizePending = true;
    }

    int GameKey(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return 'a' + key - GLFW_KEY_A;
        }
        if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_GRAVE_ACCENT)
        {
            return key;
        }
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
        {
            return '0' + key - GLFW_KEY_KP_0;
        }
        switch (key)
        {
            case GLFW_KEY_TAB:
                return '\t';
            case GLFW_KEY_ESCAPE:
                return 27;
            case GLFW_KEY_KP_ADD:
                return '+';
            case GLFW_KEY_KP_SUBTRACT:
                return '-';
            default:
                return -1;
        }
    }

    void Key(GLFWwindow* handle, int key, int, int action, int)
    {
        if (action == GLFW_REPEAT || (action == GLFW_PRESS && !focused))
        {
            return;
        }
        const int translated = GameKey(key);
        if (translated < 0)
        {
            return;
        }
        const bool down = action == GLFW_PRESS;
        if (translated == 27 && down)
        {
            glfwSetWindowShouldClose(handle, GLFW_TRUE);
            return;
        }
        if (translated == 'm')
        {
            if (down)
            {
                profiler.Mark("manual_marker");
            }
            return;
        }
        if (game)
        {
            // Physical-key mapping keeps releases stable when Shift/IME changes.
            game->Key(static_cast<unsigned char>(translated), down);
        }
    }

    void Focus(GLFWwindow*, int value)
    {
        focused = value == GLFW_TRUE;
        profiler.Mark("focus", focused ? "focused" : "unfocused");
        if (game && !focused)
        {
            game->ReleaseKeys();
        }
    }

    void Iconify(GLFWwindow*, int value)
    {
        iconified = value == GLFW_TRUE;
        profiler.Mark("visibility", iconified ? "hidden" : "visible");
        if (game && iconified)
        {
            game->ReleaseKeys();
        }
    }

    void Tick(float dt)
    {
        const auto updateStart = ProfileClock::now();
        game->Update(std::min(dt, .05f));
        profiler.RecordUpdate(ProfileMilliseconds(ProfileClock::now() - updateStart), dt);
    }

    int Run()
    {
        glfwSetErrorCallback(Error);
        if (!glfwInit())
        {
            return 1;
        }
        GlfwSession session;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
        glfwWindowHint(GLFW_ALPHA_BITS, 8);
        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0);
        // Do not multiply render resolution when Windows uses display scaling.
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        window =
            glfwCreateWindow(1440, 900, u8"나이트 프로토콜 - 녹스 오픈 월드", nullptr, nullptr);
        if (!window)
        {
            return 1;
        }
        glfwSetWindowPos(window, 70, 40);
        glfwMakeContextCurrent(window);
        char vsyncOption[2] = {};
        const int swapInterval =
            GetEnvironmentVariableA("GSE_VSYNC", vsyncOption, 2) == 1 && vsyncOption[0] == '1' ? 1
                                                                                               : 0;
        glfwSwapInterval(swapInterval);
        glewExperimental = GL_TRUE;
        const GLenum glewResult = glewInit();
        if (glewResult != GLEW_OK || !GLEW_VERSION_3_3)
        {
            std::cerr << "OpenGL 3.3이 필요합니다. GLEW: " << glewGetErrorString(glewResult)
                      << '\n';
            return 1;
        }
        glGetError(); // GLEW may probe an unsupported legacy enum in core contexts.
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
        iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
        renderer = std::make_unique<Renderer>(std::max(1, framebufferWidth),
                                              std::max(1, framebufferHeight));
        if (!renderer->IsInitialized())
        {
            std::cerr << "렌더러 초기화 실패. 실행 파일 옆의 Shaders 폴더를 확인해 주세요.\n";
            return 1;
        }
        wchar_t executable[32768] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
        {
            return 1;
        }
        const auto savePath = std::filesystem::path(executable).parent_path() / L"open_world.save";
        game = std::make_unique<Prototype>(*renderer, savePath);
        glfwSetFramebufferSizeCallback(window, Resize);
        glfwSetKeyCallback(window, Key);
        glfwSetWindowFocusCallback(window, Focus);
        glfwSetWindowIconifyCallback(window, Iconify);
        std::cout << "나이트 프로토콜 - 녹스\nWASD 이동 | Space 달리기 | 스마트폰 자동 발사\n"
                  << "Tab 사거리·스캔 | E 길게 눌러 해킹 | 패배·완료 후 R 다시 시작\n"
                  << "+/- 확대·축소 | K 저장 | P 일시정지 | Esc 저장 후 종료\n"
                  << "O 후처리 | B 블룸 | V 비네트 | F 가장자리 흐림 | [ ] 노출\n"
                  << ", . 블룸 강도 | 1 2 비네트 강도 | 3 4 가장자리 흐림 강도\n"
                  << "저장 위치: " << savePath.u8string() << '\n';
        std::cout
            << "성능 분석: 실행별 CSV 및 1초 요약 | M 분석 지점 표시 | GSE_PROFILE=0 비활성화\n";
        std::cout
            << "녹스 오픈 월드 | G 패치 의뢰 | T 회복 | I 부품 구매 | Y 판매 | U 강화\n"
            << "J PK 전환 | H 주민 공격 | C 정전 의뢰 전력 우회 | 마을 7곳 / 주민 140명 / 개별 "
               "주택·우편·이사\n";
        std::cout << "Framework: GLFW " << glfwGetVersionString()
                  << " | SwapInterval=" << swapInterval << " | Framebuffer=" << framebufferWidth
                  << 'x' << framebufferHeight << '\n';
        profiler.Start(savePath.parent_path(), *renderer);
        profiler.Mark("framework",
                      std::string("GLFW ") + glfwGetVersionString()
                          + ";swap_interval=" + std::to_string(swapInterval)
                          + ";framebuffer=" + std::to_string(framebufferWidth) + "x"
                          + std::to_string(framebufferHeight));
        auto previousTick = ProfileClock::now();
        while (!glfwWindowShouldClose(window))
        {
            CpuProfile = {};
            if (iconified || framebufferWidth <= 0 || framebufferHeight <= 0)
            {
                // Keep NPC simulation/monitoring alive at 20Hz without drawing
                // a minimized window or busy-polling its event queue.
                glfwWaitEventsTimeout(.05);
            }
            else
            {
                glfwPollEvents();
            }
            if (glfwWindowShouldClose(window))
            {
                break;
            }
            profiler.SetPresentationActive(!iconified && framebufferWidth > 0
                                           && framebufferHeight > 0);
            const auto now = ProfileClock::now();
            const float dt = std::chrono::duration<float>(now - previousTick).count();
            previousTick = now;
            Tick(dt);
            if (iconified || framebufferWidth <= 0 || framebufferHeight <= 0)
            {
                continue;
            }
            if (resizePending)
            {
                renderer->Resize(framebufferWidth, framebufferHeight);
                resizePending = false;
            }
            Display();
        }
        return 0;
    }
} // namespace

int main()
{
    char profilingOption[2] = {};
    ProfilingEnabled = !(GetEnvironmentVariableA("GSE_PROFILE", profilingOption, 2) == 1
                         && profilingOption[0] == '0');
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"나이트 프로토콜 - 녹스 / 성능 로그");
    try
    {
        return Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "초기화/실행 실패: " << error.what() << '\n';
        return 1;
    }
}
