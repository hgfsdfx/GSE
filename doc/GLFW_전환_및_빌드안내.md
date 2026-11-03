# GLFW 전환 및 빌드 안내

## 적용 내용

FreeGLUT 창·콜백·16ms 타이머 루프를 GLFW 3.4 기반으로 교체했다.
게임 렌더러, GPU 배칭·메시 캐시, 스키닝, HDR/Bloom과 SceneGraph는 기존 코드를 사용한다.
FreeGLUT 헤더·링크 설정·라이브러리·DLL은 제거했다.

`SimpleGame/Dependencies/GLFW/`에 공식 소스 패키지 전체와 정적 라이브러리용 Visual Studio
프로젝트를 포함했다. `SimpleGame.sln`과 게임 프로젝트 참조에 연결되어 같이 빌드된다.
특정 PC에 설치한 GLFW, 환경 변수의 라이브러리 경로, 네트워크 다운로드에 의존하지 않는다.
출처·고정 버전·해시는 해당 폴더의 `GSE_BUILD.md`에 기록했다.

## 다른 Windows PC에서 빌드

1. 저장소 전체를 내려받고 `SimpleGame.sln`을 연다.
2. Visual Studio 2022의 C++ 데스크톱 개발 도구(v143)와 Windows 10/11 SDK를 설치한다.
3. 시작 프로젝트는 **SimpleGame**으로 선택한다.
4. Debug/Release, x64/x86 중 원하는 조합을 빌드한다. x86은 프로젝트의 Win32 구성에 대응한다.
5. GLFW가 같은 구성·아키텍처로 먼저 정적 빌드되고 게임에 링크된다.

GLFW를 별도로 설치하거나 미리 빌드된 GLFW `.lib`를 구할 필요가 없다.
GLFW 생성물은 `SimpleGame/Dependencies/GLFW/build/<플랫폼>/<구성>/`에 생긴다.
이 폴더는 Git에서 제외하며, 재생성에 필요한 모든 원본은 커밋에 포함한다.

기존 GLEW 헤더·링크 라이브러리는 유지했다. 아키텍처별 GLEW DLL 원본을
`SimpleGame/Dependencies/Runtime/Win32/`와 `x64/`에 포함하고 빌드 후 출력 폴더로 복사한다.
출력 폴더를 바꿔도 DLL을 수동으로 찾을 필요가 없도록 `$(ProjectDir)`와 `$(OutDir)`를 사용한다.
Debug 라이브러리는 /MDd, Release는 /MD로 빌드해 게임과 CRT 구성을 맞춘다.
다른 툴셋으로 변경한다면 게임과 GLFW 두 프로젝트를 함께 리타깃한다.

배포 시 실행파일과 `glew32.dll`, `Shaders/`, `assets/`, `Licenses/`를 함께 복사한다.
GLFW는 정적 링크이므로 `glfw3.dll`이 필요하지 않다. 기존 사용자 저장 파일은 그대로 사용한다.
프로그램 자체는 여전히 Windows용이며, 이번 변경이 Linux/macOS용 게임 포팅을 포함하지는 않는다.

## 입력과 창 동작

- WASD, 달리기, Tab, 해킹, 저장, PK, 후처리 조작과 M 분석 지점 표시를 유지한다.
- 키의 누름·해제를 같은 물리 키로 연결해 Shift나 한글 입력 상태 변경으로 키가 눌린 채 남지 않도록 한다.
- GLFW_REPEAT는 무시해 토글이 반복 실행되지 않게 한다.
- 포커스 상실·최소화 시 눌린 키를 해제한다. Esc 또는 창 닫기는 저장 후 종료한다.
- 한글 제목은 GLFW의 UTF-8 API로 설정한다.
- 실제 framebuffer 크기로 렌더러를 초기화하고 리사이즈한다. Windows 배율에 맞춰
  렌더 해상도를 추가로 키우는 옵션과 MSAA는 기본 비활성화해 GPU 부하 증가를 피한다.
- 리사이즈 콜백은 크기만 보관하고 다음 프레임에 마지막 크기로 한 번 적용한다.
- GPU 프로파일러·렌더러는 GLFW 창과 OpenGL 컨텍스트를 파괴하기 전에 해제한다.

## 성능을 위한 변경과 확인 항목

활성 창에서는 `glfwPollEvents → Update → Draw → SwapBuffers`를 한 번씩 처리한다.
기존 `glutTimerFunc(16)`의 프레임 예약 대기를 제거했다. 프레임마다 창을 만들거나
리소스를 다시 로드하지 않으며, GLFW 전환을 위해 draw call이나 GPU 동기화 호출을 추가하지 않았다.
기존 프레임당 foreground 프로세스 조회는 포커스 콜백으로 대체했다.

기본 `glfwSwapInterval(0)`은 디스플레이 주사율에 의한 FPS 제한을 요청하지 않는다.
동기화가 필요하면 실행 전 `GSE_VSYNC=1`을 지정한다. 이 경우 모니터 주사율에 따라 FPS가
제한될 수 있으며 GPU 드라이버 설정도 영향을 줄 수 있다. 무제한 모드의 높은 CPU/GPU 사용률은
더 많은 프레임을 그리는 결과일 수 있으므로 프레임 처리 시간과 함께 비교해야 한다.

최소화 또는 0 크기 framebuffer에서는 그리기를 중지하고 `glfwWaitEventsTimeout(0.05)`로 기다린다.
NPC·모니터링 갱신은 계속하되 바쁜 이벤트 폴링을 피한다. 입력 등 이벤트가 도착하면 일찍 깨어날 수 있다.
숨겨진 동안의 시간·Update 비용은 표시 프레임의 FPS/Update 평균에 합산하지 않는다.
복원 시 프로파일 측정 구간을 새로 시작해 최소화 시간을 긴 프레임으로 오인하지 않도록 한다.

기존 `[Profile]`의 FPS, FrameMs p95/p99, UpdateMs, DrawMs, SwapMs, DrawCalls와
CSV 분석 도구를 유지했다. 콘솔과 `events.csv`의 `framework` 이벤트에 GLFW 버전,
요청한 swap interval, 초기 framebuffer 크기를 남긴다. `presentation` 이벤트는
렌더링 중지/재개 구간을 표시한다.

사용자가 실행할 때는 이전 버전과 **같은 빌드 구성·해상도·장소·저장 데이터·후처리 설정**에서 비교한다.
첫 모델 업로드가 끝난 뒤 30~60초 정도의 로그에서 평균과 p95/p99를 비교하는 것이 좋다.
새 빌드의 FPS가 60 등에 고정됐다면 먼저 swap interval과 드라이버 동기화 설정을 확인한다.
DrawCalls와 MeshNew/MeshLoad가 안정화된 상황에서 DrawMs/SwapMs가 어떻게 달라졌는지도 확인한다.
최소화·복원·Alt+Tab·창 크기 변경 후 키 상태, 화면 크기, 종료 저장도 확인한다.

## 수행한 검증 범위

빌드 설정의 네 가지 구성, 프로젝트 참조, GLFW Windows용 소스 목록, 상대 경로,
DLL 아키텍처, FreeGLUT 의존성 제거, 공식 패키지와 포함 소스의 일치를 정적 점검한다.
사용자의 기존 요청에 따라 게임 빌드·실행 및 전후 FPS 실측은 수행하지 않는다.
따라서 성능 저하가 없음을 실측으로 확인했다는 의미는 아니다.

공식 문서:

- [GLFW 빌드 및 링크](https://www.glfw.org/docs/3.4/build_guide.html)
- [이벤트 처리와 입력](https://www.glfw.org/docs/3.4/input_guide.html)
- [창과 framebuffer](https://www.glfw.org/docs/3.4/window_guide.html)
