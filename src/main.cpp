//콘솔 한글 출력 위해 UTF-8 코드페이지로 전환.
//Windows.h를 glad보다 "먼저" 넣는 이유: 둘 다 APIENTRY를 정의해서 순서 바뀌면 C4005 경고가 뜬다.
#ifdef _WIN32
#include <Windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <Config.h>
#include <Loader/SceneImport.h>
#include <Render/Benchmark.h>
#include <Render/FrameStats.h>
#include <Render/OrbitCamera.h>
#include <Render/Renderer.h>
#include <Scene/Scene.h>
#include <UI/EditorUI.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void DropCallback(GLFWwindow* window, int count, const char** paths);
void APIENTRY GLDebugCallback(GLenum source, GLenum type, unsigned int id, GLenum severity,
    GLsizei length, const char* message, const void* userParam);

//스크롤은 콜백으로만 들어오는 이벤트라(폴링이 불가능) 여기 모았다가 프레임에서 소비한다
static float g_scrollDelta = 0.0f;

#ifdef _WIN32
//argv는 콘솔 ANSI 코드페이지(한국어 윈도우면 CP949)로 들어온다.
//그런데 ufbx를 비롯한 라이브러리들은 경로를 UTF-8로 기대해서, 한글이 든 경로가 그대로는 안 열린다.
//("File not found (C:\...\?ѱ?????.fbx)" 처럼 물음표로 깨져 나온다)
//윈도우가 원본 그대로 보관하는 유니코드 커맨드라인을 받아서 UTF-8로 다시 만든다.
static std::string ArgToUtf8(int index)
{
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr || index >= wargc)
        return std::string();

    const int need = WideCharToMultiByte(CP_UTF8, 0, wargv[index], -1, nullptr, 0, nullptr, nullptr);
    std::string out;
    if (need > 1)
    {
        out.resize((size_t)need - 1);   //-1: 널 종료 문자는 std::string이 알아서 관리한다
        WideCharToMultiByte(CP_UTF8, 0, wargv[index], -1, out.data(), need, nullptr, nullptr);
    }

    LocalFree(wargv);
    return out;
}
#endif

//창에 끌어다 놓은 파일 경로. 콜백에서 바로 로딩하지 않고 모아뒀다가 루프에서 처리한다.
//콜백은 glfwPollEvents 안에서 불리는데, 거기서 GPU 업로드 같은 무거운 일을 하면
//입력 처리 흐름 한가운데서 프레임이 길게 멈춘다.
static std::vector<std::string> g_droppedFiles;

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    //--- GLFW 초기화 ---
    if (!glfwInit())
    {
        std::cout << "GLFW 초기화 실패\n";
        return -1;
    }

    //4.6을 잡는 이유: compute 셰이더, SSBO, MultiDrawIndirect, DSA를 쓰려면 4.3~4.5가 필요하다.
    //나중에 올리면 셰이더 버전부터 전부 손봐야 하니 처음부터 올려둔다.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef _DEBUG
    //디버그 컨텍스트를 요청해야 glDebugMessageCallback이 제대로 동작한다 (릴리스에선 오버헤드라 뺌)
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "MiniBlender", nullptr, nullptr);
    if (window == nullptr)
    {
        std::cout << "윈도우 생성 실패\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    //콜백은 ImGui 초기화보다 "먼저" 등록해야 한다.
    //ImGui 백엔드가 기존 콜백을 기억했다가 자기 처리 후에 이어서 불러주는 구조(체이닝)라,
    //순서가 뒤바뀌면 우리 콜백이 통째로 씹힌다.
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetDropCallback(window, DropCallback);

    //--- GLAD 로드 ---
    //컨텍스트를 current로 만든 "다음"에 호출해야 함. 순서 바뀌면 함수 포인터가 전부 null.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "GLAD 초기화 실패\n";
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << "\n";

    //--- 디버그 출력 연결 ---
    //GL 에러를 glGetError로 일일이 찾는 대신 콜백으로 즉시 받는다. 이거 없으면 눈뜬 장님으로 디버깅하게 됨.
    {
        int flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
        {
            glEnable(GL_DEBUG_OUTPUT);
            //SYNCHRONOUS를 켜야 에러 발생 지점에서 콜스택이 그대로 잡힌다 (브레이크포인트 걸기 좋음)
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(GLDebugCallback, nullptr);
            //드라이버 잡담(NOTIFICATION)은 너무 시끄러워서 끈다
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
            std::cout << "디버그 출력 활성화됨\n";
        }
    }

    //--- 시스템 초기화 ---
    Scene scene;
    scene.InitDefaultMeshes();
    //시작 화면이 텅 비어 보이지 않게 큐브를 하나 놓되, 파일을 불러오면 치워질 것으로 표시해 둔다
    if (SceneObject* startupCube = scene.AddObject("Cube", "Cube", glm::vec3(0.0f, 0.5f, 0.0f)))
        startupCube->isPlaceholder = true;

    Renderer renderer;
    renderer.Init();

    FrameStats stats;
    stats.Init();

    OrbitCamera camera;

    //인자가 .fbx 경로면 시작하자마자 불러온다 (탐색기에서 파일을 exe에 끌어다 놓는 경우 포함)
    bool argIsFbxPath = false;
    if (argc > 1)
    {
        std::string a1 = argv[1];
        if (a1.size() > 4 && _stricmp(a1.c_str() + a1.size() - 4, ".fbx") == 0)
        {
#ifdef _WIN32
            //한글 경로를 살리려면 argv 대신 유니코드 커맨드라인에서 다시 가져와야 한다
            const std::string utf8 = ArgToUtf8(1);
            if (!utf8.empty())
                a1 = utf8;
#endif
            g_droppedFiles.push_back(a1);
            argIsFbxPath = true;
        }
    }

    //측정 모드(숫자 인자)면 씬을 자동 구성하고 vsync를 끈다. 인자가 없으면 전부 no-op.
    Benchmark bench;
    if (!argIsFbxPath)
        bench.ParseArgs(argc, argv);
    if (!bench.Init(window, scene, camera))
    {
        renderer.Shutdown();
        stats.Shutdown();
        glfwTerminate();
        return -1;
    }

    EditorUI ui;
    ui.Init(window);

    //뷰포트 마우스 조작 상태
    double lastMouseX = 0.0, lastMouseY = 0.0;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

    double lastTitleUpdate = glfwGetTime();

    //--- 렌더 루프 ---
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        stats.BeginFrame();

        //--- 카메라 조작 (블렌더 방식) ---
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        float dx = (float)(mx - lastMouseX);
        float dy = (float)(my - lastMouseY);
        lastMouseX = mx;
        lastMouseY = my;

        //ImGui 패널 위에서는 뷰포트 조작을 막는다. 안 그러면 슬라이더 드래그에 카메라가 따라 돈다.
        if (!ui.WantCaptureMouse())
        {
            bool middleDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

            if (middleDown)
            {
                if (shiftDown)
                    camera.Pan(dx, dy);
                else
                    camera.Orbit(dx, dy);
            }

            if (g_scrollDelta != 0.0f)
                camera.Zoom(g_scrollDelta);
        }
        g_scrollDelta = 0.0f;   //조작을 막았더라도 쌓인 값은 버려야 나중에 몰아서 튀지 않는다

        if (!ui.WantCaptureKeyboard() && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        //--- 파일 불러오기 요청 처리 ---
        //UI 버튼과 드래그앤드롭 두 경로가 여기서 만난다
        {
            std::string requested;
            if (ui.ConsumeLoadRequest(requested))
                g_droppedFiles.push_back(requested);

            for (const std::string& file : g_droppedFiles)
            {
                const ImportReport report = ImportFbxIntoScene(scene, camera, file);
                ui.SetImportMessage(report.message, !report.ok);
                std::cout << (report.ok ? "[가져오기] " : "[가져오기 실패] ") << report.message << std::endl;
            }
            g_droppedFiles.clear();
        }

        //--- 렌더링 ---
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

        renderer.RenderScene(scene, camera, fbWidth, fbHeight, stats);

        //--- UI ---
        if (!bench.ShouldSkipUI())
        {
            ui.BeginFrame();
            ui.Draw(scene, renderer, camera, stats);
            ui.EndFrame();
        }

        stats.EndFrame();

        //측정이 끝나면 결과 한 줄을 찍고 종료한다
        if (bench.Tick(stats))
            glfwSetWindowShouldClose(window, true);

        //--- 통계를 창 제목에도 표시 ---
        //ImGui 패널과 중복이지만 의도적으로 남겨둔다: 의존성이 0이라 UI가 죽어도 살아있는 최후의 계기판.
        //매 프레임 갱신하면 글자가 읽을 수 없게 깜빡여서 0.25초마다만 바꾼다.
        double now = glfwGetTime();
        if (now - lastTitleUpdate > 0.25)
        {
            char title[256];
            std::snprintf(title, sizeof(title),
                "MiniBlender | %.1f FPS | CPU %.2fms GPU %.2fms | 드로우콜 %u | 삼각형 %u",
                stats.GetFps(), stats.GetCpuMs(), stats.GetGpuMs(),
                stats.GetDrawCalls(), stats.GetTriangles());
            glfwSetWindowTitle(window, title);
            lastTitleUpdate = now;
        }

        glfwSwapBuffers(window);
    }

    //--- 정리 ---
    ui.Shutdown();
    stats.Shutdown();
    renderer.Shutdown();

    glfwTerminate();
    return 0;
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    g_scrollDelta += (float)yoffset;
}

void DropCallback(GLFWwindow* window, int count, const char** paths)
{
    for (int i = 0; i < count; ++i)
        g_droppedFiles.push_back(paths[i]);
}

void APIENTRY GLDebugCallback(GLenum source, GLenum type, unsigned int id, GLenum severity,
    GLsizei length, const char* message, const void* userParam)
{
    //GL 내부에서 호출되는 함수라 시그니처(APIENTRY 포함)를 정확히 맞춰야 한다
    const char* srcStr = "기타";
    switch (source)
    {
    case GL_DEBUG_SOURCE_API:             srcStr = "API";       break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   srcStr = "윈도우";     break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: srcStr = "셰이더";     break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:     srcStr = "서드파티";   break;
    case GL_DEBUG_SOURCE_APPLICATION:     srcStr = "앱";        break;
    }

    const char* typeStr = "기타";
    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR:               typeStr = "에러";       break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "폐기예정";   break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "미정의동작"; break;
    case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "이식성";     break;
    case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "성능";       break;
    case GL_DEBUG_TYPE_MARKER:              typeStr = "마커";       break;
    }

    const char* sevStr = "낮음";
    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:   sevStr = "높음";  break;
    case GL_DEBUG_SEVERITY_MEDIUM: sevStr = "보통";  break;
    }

    std::cout << "[GL " << sevStr << "] " << srcStr << "/" << typeStr
        << " (id=" << id << "): " << message << "\n";
}
