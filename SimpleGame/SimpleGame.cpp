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
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <Windows.h>

namespace
{
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Prototype> game;
    auto previousTick = std::chrono::steady_clock::now();
    bool closing = false;

    struct FrameProfiler
    {
        using Clock = std::chrono::steady_clock;

        Clock::time_point sampleStart = Clock::now();
        std::uint64_t frames = 0;
        std::uint64_t totalDrawCalls = 0;
        std::uint64_t minimumDrawCalls = 0;
        std::uint64_t maximumDrawCalls = 0;

        void RecordFrame(std::uint64_t drawCalls)
        {
            const auto now = Clock::now();
            if (frames == 0)
            {
                minimumDrawCalls = drawCalls;
                maximumDrawCalls = drawCalls;
            }
            ++frames;
            totalDrawCalls += drawCalls;
            minimumDrawCalls = std::min(minimumDrawCalls, drawCalls);
            maximumDrawCalls = std::max(maximumDrawCalls, drawCalls);

            const double seconds = std::chrono::duration<double>(now - sampleStart).count();
            if (seconds < 1.0)
            {
                return;
            }

            // Use real display intervals, including update/timer/swap waits, not clamped game dt.
            std::ostringstream line;
            line << std::fixed << std::setprecision(1)
                 << "[Profile] FPS=" << double(frames) / seconds
                 << " | FrameMs(avg)=" << seconds * 1000.0 / double(frames)
                 << " | DrawCalls/frame: last=" << drawCalls
                 << " avg=" << double(totalDrawCalls) / double(frames)
                 << " min=" << minimumDrawCalls << " max=" << maximumDrawCalls
                 << " | Frames=" << frames << '\n';
            std::cout << line.str() << std::flush;

            // Include logging overhead in the following interval.
            sampleStart = now;
            frames = 0;
            totalDrawCalls = 0;
        }
    } profiler;

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
        game.reset();
        // freeglut invokes this callback while the window context still exists.
        renderer.reset();
    }

    void Display()
    {
        if (!game || closing)
        {
            return;
        }
        game->Draw();
        glutSwapBuffers();
        profiler.RecordFrame(renderer->DrawCalls());
    }

    void Resize(int width, int height)
    {
        if (renderer)
        {
            renderer->Resize(width, height);
        }
    }

    void KeyDown(unsigned char key, int, int)
    {
        if (key == 27)
        {
            Close();
            glutLeaveMainLoop();
            return;
        }
        if (game)
        {
            game->Key(key, true);
        }
    }

    void KeyUp(unsigned char key, int, int)
    {
        if (game)
        {
            game->Key(key, false);
        }
    }

    void Visibility(int visible)
    {
        if (game && visible != GLUT_VISIBLE)
        {
            game->ReleaseKeys();
        }
    }

    void Tick(int)
    {
        if (!game || closing)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previousTick).count();
        previousTick = now;
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        if (foregroundProcess != GetCurrentProcessId())
        {
            game->ReleaseKeys();
        }
        game->Update(std::min(dt, .05f));
        glutPostRedisplay();
        glutTimerFunc(16, Tick, 0);
    }
} // namespace

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"나이트 / 링크 - 성능 로그");
    glutInit(&argc, argv);
    glutInitContextVersion(3, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1440, 900);
    glutInitWindowPosition(70, 40);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    const int window = glutCreateWindow("NIGHT / LINK - Level 01: Scavenger District");
    if (!window)
    {
        std::cerr << "OpenGL 창을 생성할 수 없습니다.\n";
        return 1;
    }
    // freeglut's narrow title API depends on the Windows ANSI code page.
    if (const HWND handle = WindowFromDC(wglGetCurrentDC()))
    {
        SetWindowTextW(handle, L"나이트 / 링크 - 레벨 01: 수집가 구역");
    }
    glewExperimental = GL_TRUE;
    const GLenum glewResult = glewInit();
    if (glewResult != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3이 필요합니다. GLEW: " << glewGetErrorString(glewResult) << '\n';
        glutDestroyWindow(window);
        return 1;
    }
    glGetError(); // Some GLEW versions probe an unsupported legacy enum in core contexts.
    renderer = std::make_unique<Renderer>(1440, 900);
    if (!renderer->IsInitialized())
    {
        std::cerr << "렌더러 초기화 실패. 실행 파일 옆의 Shaders 폴더를 확인해 주세요.\n";
        renderer.reset();
        glutDestroyWindow(window);
        return 1;
    }
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768)
    {
        renderer.reset();
        glutDestroyWindow(window);
        return 1;
    }
    const auto savePath = std::filesystem::path(executable).parent_path() / L"level_one.save";
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
    std::cout << "나이트 / 링크 - 레벨 01\nWASD 이동 | Space 달리기 | 스마트폰 자동 발사\n"
              << "Tab 사거리·스캔 | E 길게 눌러 해킹 | 패배·완료 후 R 다시 시작\n"
              << "+/- 확대·축소 | K 저장 | P 일시정지 | Esc 저장 후 종료\n"
              << "O 후처리 | B 블룸 | V 비네트 | F 가장자리 흐림 | [ ] 노출\n"
              << ", . 블룸 강도 | 1 2 비네트 강도 | 3 4 가장자리 흐림 강도\n"
              << "저장 위치: " << savePath.u8string() << '\n';
    std::cout << "성능 로그: 1초마다 FPS 및 프레임당 draw call 출력 (월드 + HUD + 후처리).\n";
    profiler = FrameProfiler{};
    glutMainLoop();
    Close();
    return 0;
}
