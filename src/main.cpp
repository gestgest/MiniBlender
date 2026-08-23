//콘솔 한글 출력 위해 UTF-8 코드페이지로 전환.
//Windows.h를 glad보다 "먼저" 넣는 이유: 둘 다 APIENTRY를 정의해서 순서 바뀌면 C4005 경고가 뜬다.
#ifdef _WIN32
#include <Windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <Config.h>
#include <Edit/EditMode.h>
#include <Edit/History.h>
#include <Loader/FbxExporter.h>
#include <Loader/ObjExporter.h>
#include <Loader/SceneImport.h>
#include <Render/Benchmark.h>
#include <Render/FrameStats.h>
#include <Render/OrbitCamera.h>
#include <Render/Renderer.h>
#include <Scene/Picking.h>
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

//저장 경로의 확장자를 보고 익스포터를 고른다. UI와 CLI 두 곳에서 같은 규칙을 쓰기 위해 함수로 뺐다.
static ExportResult ExportScene(const Scene& scene, const std::string& path)
{
    if (path.size() > 4 && _stricmp(path.c_str() + path.size() - 4, ".fbx") == 0)
        return ExportSceneToFbx(scene, path);
    return ExportSceneToObj(scene, path);
}

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

    //인자가 모델 경로면 시작하자마자 불러온다 (탐색기에서 파일을 exe에 끌어다 놓는 경우 포함).
    //ufbx는 .obj도 읽어주므로 확장자를 둘 다 받는다.
    bool argIsModelPath = false;
    std::string convertOnceTarget;   //비어있지 않으면 변환 모드
    if (argc > 1)
    {
        std::string a1 = argv[1];
        const bool isModel = a1.size() > 4
            && (_stricmp(a1.c_str() + a1.size() - 4, ".fbx") == 0
                || _stricmp(a1.c_str() + a1.size() - 4, ".obj") == 0);
        if (isModel)
        {
#ifdef _WIN32
            //한글 경로를 살리려면 argv 대신 유니코드 커맨드라인에서 다시 가져와야 한다
            const std::string utf8 = ArgToUtf8(1);
            if (!utf8.empty())
                a1 = utf8;
#endif
            g_droppedFiles.push_back(a1);
            argIsModelPath = true;

            //두 번째 인자가 .obj면 "변환 모드": 불러오고 바로 내보낸 뒤 종료한다.
            //UI 버튼은 자동 검증이 안 되니 테스트 통로가 되고, 배치 변환에도 쓸 수 있다.
            if (argc > 2)
            {
                const std::string a2raw = argv[2];
                const bool isExportTarget = a2raw.size() > 4
                    && (_stricmp(a2raw.c_str() + a2raw.size() - 4, ".obj") == 0
                        || _stricmp(a2raw.c_str() + a2raw.size() - 4, ".fbx") == 0);
                if (isExportTarget)
                {
#ifdef _WIN32
                    const std::string a2utf8 = ArgToUtf8(2);
                    convertOnceTarget = a2utf8.empty() ? a2raw : a2utf8;
#else
                    convertOnceTarget = a2raw;
#endif
                }
            }
        }
    }

    //측정 모드(숫자 인자)면 씬을 자동 구성하고 vsync를 끈다. 인자가 없으면 전부 no-op.
    Benchmark bench;
    if (!argIsModelPath)
        bench.ParseArgs(argc, argv);
    if (!bench.Init(window, scene, camera))
    {
        renderer.Shutdown();
        stats.Shutdown();
        glfwTerminate();
        return -1;
    }

    //측정 모드에서만 인스턴싱을 끌 수 있다(--no-instancing). 평소 모드는 항상 켠 채로 시작하고,
    //필요하면 통계 패널의 체크박스로 끈다.
    renderer.useInstancing = bench.UseInstancing();

    EditMode edit;
    edit.Init();

    History history;

    EditorUI ui;
    ui.Init(window);

    //뷰포트 마우스 조작 상태
    double lastMouseX = 0.0, lastMouseY = 0.0;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

    bool tabWasDown = false;
    bool leftWasDown = false;
    bool pickWasDown = false;
    //이번 좌클릭이 정점 이동인지(true) 박스 선택인지(false). 누른 순간에 정해진다.
    bool vertexDragging = false;
    bool deleteWasDown = false;
    bool xrayWasDown = false;
    bool undoWasDown = false;
    bool redoWasDown = false;

    double lastTitleUpdate = glfwGetTime();

    //--- 렌더 루프 ---
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        stats.BeginFrame();

        //--- 카메라 조작 ---
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        float dx = (float)(mx - lastMouseX);
        float dy = (float)(my - lastMouseY);
        lastMouseX = mx;
        lastMouseY = my;

        //ImGui 패널 위에서는 뷰포트 조작을 막는다. 안 그러면 슬라이더 드래그에 카메라가 따라 돈다.
        if (!ui.WantCaptureMouse())
        {
            //마우스 관련 입력
            const bool orbitDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            const bool panDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            //둘이 동시에 눌렸으면 팬을 우선한다. 한 프레임에 둘 다 적용하면
            //회전과 이동이 겹쳐서 화면이 튄다.
            if (panDown)
                camera.Pan(dx, dy);
            else if (orbitDown)
                camera.Orbit(dx, dy);

            if (g_scrollDelta != 0.0f)
                camera.Zoom(g_scrollDelta);
        }
        g_scrollDelta = 0.0f;   //조작을 막았더라도 쌓인 값은 버려야 나중에 몰아서 튀지 않는다

        if (!ui.WantCaptureKeyboard() && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        //--- 편집 모드 (Tab 토글, 블렌더와 같은 키) ---
        {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);

            const bool tabDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
            if (tabDown && !tabWasDown && !ui.WantCaptureKeyboard())
            {
                if (edit.IsActive())
                    edit.Exit();
                else if (ui.GetSelectedId() != 0)
                    edit.Enter(scene, ui.GetSelectedId());
            }
            tabWasDown = tabDown;

            //편집 대상이 사라졌으면(삭제 등) 편집 모드도 빠져나온다
            if (edit.IsActive() && scene.FindById(edit.GetObjectId()) == nullptr)
                edit.Exit();

            if (edit.IsActive() && !ui.WantCaptureMouse())
            {
                const SceneObject* target = scene.FindById(edit.GetObjectId());
                const glm::mat4 model = target ? target->transform.GetMatrix() : glm::mat4(1.0f);
                const float aspect = (fbH > 0) ? (float)fbW / (float)fbH : 1.0f;
                const glm::mat4 viewProj = camera.GetProjectionMatrix(aspect) * camera.GetViewMatrix();

                const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                const bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                    || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

                //누른 자리에 정점이 있었는지가 이번 드래그의 성격을 정한다.
                //  정점 위에서 시작 -> 이동
                //  빈 곳에서 시작   -> 박스 선택
                //블렌더와 같은 구분이고, 둘 다 좌클릭 드래그라 이 판단이 없으면 서로를 잡아먹는다.
                if (leftDown && !leftWasDown)
                {
                    const int hit = edit.PickVertexAt((float)mx, (float)my, fbW, fbH,
                        viewProj, model, camera);

                    if (hit >= 0)
                    {
                        if (shiftDown)
                            edit.ToggleSelection(hit);
                        else if (!edit.IsSelected(hit))
                            edit.SelectOnly(hit);
                        //이미 선택된 정점을 다시 누른 경우는 선택을 건드리지 않는다.
                        //여럿 골라놓고 그중 하나를 잡아 통째로 끌 수 있어야 하니까.

                        //누른 순간부터 뗄 때까지가 되돌리기 한 칸이다
                        edit.BeginStroke();
                        vertexDragging = edit.GetSelectedCount() > 0;
                    }
                    else
                    {
                        vertexDragging = false;
                        edit.BeginBoxSelect((float)mx, (float)my);
                    }
                }
                else if (leftDown)
                {
                    if (vertexDragging && (dx != 0.0f || dy != 0.0f))
                        edit.DragSelected(dx, dy, camera, fbH, model);
                    else if (edit.IsBoxSelecting())
                        edit.UpdateBoxSelect((float)mx, (float)my);
                }
                else if (!leftDown && leftWasDown)
                {
                    if (vertexDragging)
                    {
                        edit.CommitStroke(history, "정점 이동");
                        vertexDragging = false;
                    }
                    else
                    {
                        edit.EndBoxSelect(fbW, fbH, viewProj, model, camera, shiftDown);
                    }
                }

                leftWasDown = leftDown;
            }
            else
            {
                //패널 위로 커서가 넘어가거나 편집 모드를 나가도 끌던 건 마무리해야 한다.
                //안 그러면 기록이 열린 채 남아서 다음 스트로크에 통째로 섞인다.
                if (leftWasDown && vertexDragging)
                    edit.CommitStroke(history, "정점 이동");

                //그리다 만 선택 사각형은 그냥 버린다. 커서가 패널로 빠진 시점의
                //사각형으로 선택을 바꾸면 사용자가 의도하지 않은 범위가 잡힌다.
                edit.CancelBoxSelect();
                vertexDragging = false;
                leftWasDown = false;
            }
        }

        //--- X-Ray 토글 (Alt+Z, 블렌더와 같은 키) ---
        //정점 편집이 힘든 이유의 절반은 앞뒤 정점이 화면에서 겹쳐 보이는 것이다.
        //꺼두면 보이는 면의 정점만 남아서 클릭이 정확해지고, 반대편을 만질 때만 켠다.
        {
            const bool altDown = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            const bool xrayDown = altDown && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;

            if (xrayDown && !xrayWasDown && !ui.WantCaptureKeyboard())
            {
                edit.ToggleXRay();
                ui.SetImportMessage(edit.IsXRay()
                    ? "X-Ray 켬 — 뒤쪽 정점까지 보이고 선택됩니다"
                    : "X-Ray 끔 — 보이는 면의 정점만 선택됩니다", false);
            }
            xrayWasDown = xrayDown;
        }

        //--- 뷰포트에서 오브젝트 선택 ---
        //아웃라이너에서만 고를 수 있으면 모델을 눈앞에 두고도 목록에서 이름을 찾아야 한다.
        {
            const bool pickDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool pickClicked = pickDown && !pickWasDown;
            pickWasDown = pickDown;

            //편집 모드에선 좌클릭이 정점 선택이라 손대지 않는다 (블렌더도 같다).
            if (pickClicked && !edit.IsActive() && !ui.WantCaptureMouse())
            {
                int fbW = 0, fbH = 0;
                glfwGetFramebufferSize(window, &fbW, &fbH);

                //빈 곳을 누르면 0이 돌아와서 선택이 풀린다 — 이것도 의도한 동작이다
                ui.SetSelectedId(PickObject(scene, camera, (float)mx, (float)my, fbW, fbH));
            }
        }

        //--- 선택한 오브젝트 삭제 (Delete) ---
        //아웃라이너 우클릭 메뉴와 같은 일을 하지만, 뷰포트에서 클릭한 손 그대로 지울 수 있게 한다.
        {
            const bool deleteDown = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;

            //누른 순간에만. 누르고 있는 동안 매 프레임 지우면 씬이 순식간에 비어버린다.
            //편집 모드에선 Delete가 정점 쪽 키라 오브젝트를 통째로 지우지 않는다 (블렌더와 같다).
            if (deleteDown && !deleteWasDown && !edit.IsActive() && !ui.WantCaptureKeyboard()
                && ui.GetSelectedId() != 0)
            {
                const unsigned int target = ui.GetSelectedId();
                if (scene.FindById(target) != nullptr)
                {
                    //추가/가져오기와 같은 방식: 앞뒤 목록 차이를 액션으로 남겨 Ctrl+Z로 되살린다
                    const std::vector<SceneObject> before = scene.GetObjects();
                    scene.RemoveObject(target);
                    ui.SetSelectedId(0);
                    history.Push(History::MakeSceneDiff(before, scene.GetObjects(), "오브젝트 삭제"));
                    ui.SetImportMessage("오브젝트 삭제", false);
                }
            }
            deleteWasDown = deleteDown;
        }

        //--- 되돌리기 / 다시 실행 ---
        //ImGui가 키보드를 잡고 있으면(입력창에 커서가 있으면) 건드리지 않는다.
        //경로를 타이핑하다 Ctrl+Z를 누르면 글자가 지워져야지 씬이 되돌아가면 안 된다.
        {
            const bool ctrlDown = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            const bool shiftHeld = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

            //Ctrl+Shift+Z와 Ctrl+Y 둘 다 다시 실행으로 받는다 (앱마다 관례가 갈려서)
            const bool undoDown = ctrlDown && !shiftHeld && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
            const bool redoDown = ctrlDown && ((shiftHeld && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
                || glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS);

            //키를 누르고 있는 동안 매 프레임 되돌아가면 스택이 순식간에 비어버린다. 눌린 순간에만.
            const bool undoPressed = (undoDown && !undoWasDown && !ui.WantCaptureKeyboard())
                || ui.ConsumeUndoRequest();
            const bool redoPressed = (redoDown && !redoWasDown && !ui.WantCaptureKeyboard())
                || ui.ConsumeRedoRequest();

            undoWasDown = undoDown;
            redoWasDown = redoDown;

            if (undoPressed && history.Undo(scene, edit))
                ui.SetImportMessage("되돌리기", false);
            else if (redoPressed && history.Redo(scene, edit))
                ui.SetImportMessage("다시 실행", false);

            //되돌리기로 오브젝트가 사라졌으면 선택도 풀어준다
            if (ui.GetSelectedId() != 0 && scene.FindById(ui.GetSelectedId()) == nullptr)
                ui.SetSelectedId(0);
        }

        //--- 파일 불러오기 요청 처리 ---
        //UI 버튼과 드래그앤드롭 두 경로가 여기서 만난다
        {
            std::string requested;
            if (ui.ConsumeLoadRequest(requested))
                g_droppedFiles.push_back(requested);

            for (const std::string& file : g_droppedFiles)
            {
                //가져오기는 예시 큐브를 치우면서 오브젝트를 여럿 추가한다.
                //앞뒤 목록을 비교하면 그 둘이 한 액션으로 묶여서, Ctrl+Z 한 번이면 통째로 물러난다.
                const std::vector<SceneObject> beforeImport = scene.GetObjects();

                const ImportReport report = ImportFbxIntoScene(scene, camera, file);
                ui.SetImportMessage(report.message, !report.ok);
                std::cout << (report.ok ? "[가져오기] " : "[가져오기 실패] ") << report.message << std::endl;

                if (report.ok)
                    history.Push(History::MakeSceneDiff(beforeImport, scene.GetObjects(), "가져오기"));
            }
            g_droppedFiles.clear();

            //--- 변환 모드: 불러오기가 끝났으면 바로 내보내고 종료 ---
            if (!convertOnceTarget.empty())
            {
                const ExportResult saved = ExportScene(scene, convertOnceTarget);
                std::cout << (saved.ok ? "[내보내기] " : "[내보내기 실패] ") << saved.message << std::endl;
                convertOnceTarget.clear();
                glfwSetWindowShouldClose(window, true);
            }

            //--- 내보내기 요청 ---
            std::string savePath;
            if (ui.ConsumeSaveRequest(savePath))
            {
                const ExportResult saved = ExportScene(scene, savePath);
                ui.SetImportMessage(saved.message, !saved.ok);
                std::cout << (saved.ok ? "[내보내기] " : "[내보내기 실패] ") << saved.message << std::endl;
            }
        }

        //--- 렌더링 ---
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

        renderer.RenderScene(scene, camera, fbWidth, fbHeight, stats);
        renderer.RenderEditPoints(edit, scene, camera, fbWidth, fbHeight, stats);

        //--- UI ---
        if (!bench.ShouldSkipUI())
        {
            ui.BeginFrame();
            ui.Draw(scene, renderer, camera, stats, edit, history);
            ui.EndFrame();

            //파일 대화상자는 모달이라 떠 있는 동안 이 루프가 통째로 멈춘다.
            //그 시간을 계측에 넣으면 "CPU 8000ms" 같은 가짜 스파이크가 그래프에 남는다.
            if (ui.ConsumeDialogStall())
                stats.MarkFrameStalled();
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
    edit.Shutdown();
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
