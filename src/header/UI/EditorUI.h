#pragma once

#include <Scene/SceneObject.h>

#include <string>

struct GLFWwindow;
class Scene;
class Renderer;
class OrbitCamera;
class FrameStats;
class EditMode;
class History;

//Dear ImGui로 만든 에디터 패널들.
//통계 표시를 창 제목(간단)과 ImGui 패널(본격) 두 군데에 다 넣어둔 이유:
//창 제목은 의존성 0이라 ImGui가 죽어도 살아있는 최후의 계기판이고,
//ImGui 쪽은 그래프/토글/속성 편집까지 갈 수 있는 본채다.
class EditorUI
{
public:
    void Init(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void Draw(Scene& scene, Renderer& renderer, OrbitCamera& camera, const FrameStats& stats,
        EditMode& edit, History& history);
    void EndFrame();

    //ImGui가 마우스를 쓰고 있으면(패널 위에 커서가 있으면) 뷰포트 조작을 막아야 한다.
    //안 그러면 슬라이더를 드래그할 때 카메라가 같이 돌아간다.
    bool WantCaptureMouse() const;
    bool WantCaptureKeyboard() const;

    unsigned int GetSelectedId() const { return selectedId; }

    //UI는 파일을 직접 읽지 않는다. "이 경로를 불러와 달라"는 요청만 남기고
    //실제 로딩은 main이 처리한다 (UI가 로더/GL에 의존하지 않게 하려는 것).
    bool ConsumeLoadRequest(std::string& outPath);
    bool ConsumeSaveRequest(std::string& outPath);
    //파일 대화상자를 띄운 프레임인지. 모달이라 그동안 루프가 멈춰서 통계에서 빼야 한다.
    bool ConsumeDialogStall();

    //되돌리기 버튼도 요청만 남긴다. 실제 실행은 main이 한다 —
    //되돌리기는 오브젝트 목록을 갈아엎는 일이라, UI가 그 목록을 순회하는 도중에 하면 안 된다.
    bool ConsumeUndoRequest();
    bool ConsumeRedoRequest();
    //불러오기 결과를 패널에 표시하기 위해 돌려받는다
    void SetImportMessage(const std::string& msg, bool isError);
    void SetSelectedId(unsigned int id) { selectedId = id; }

private:
    void DrawStatsPanel(const FrameStats& stats, Renderer& renderer);
    void DrawOutliner(Scene& scene, History& history);
    void DrawInspector(Scene& scene, OrbitCamera& camera, EditMode& edit, History& history);
    void DrawFilePanel();

    //파일 대화상자를 띄울 때 부모 창으로 넘긴다 (모달로 앱 위에 뜨게 하려고)
    GLFWwindow* ownerWindow = nullptr;

    unsigned int selectedId = 0;

    //속성 슬라이더는 드래그하는 내내 값이 바뀐다. 매 프레임 기록하면 Ctrl+Z가 수십 번 필요해지니
    //위젯을 잡은 순간의 상태만 들고 있다가, 놓을 때 한 번만 액션으로 남긴다.
    SceneObject propertyBefore;
    bool propertyEditing = false;

    //가져오기 패널 상태
    char pathBuffer[512] = "";
    bool hasLoadRequest = false;
    std::string requestedPath;

    //내보내기 상태. 기본 경로를 채워두면 처음 쓰는 사람이 형식을 안 헷갈린다
    char exportPathBuffer[512] = "export.obj";
    bool hasSaveRequest = false;
    std::string requestedSavePath;
    std::string importMessage;
    bool importFailed = false;
    bool dialogStalled = false;
    bool pendingUndo = false;
    bool pendingRedo = false;

    //성능 그래프용 링버퍼. 숫자 하나만 보면 튀는 프레임(스파이크)을 놓친다.
    static const int HISTORY = 120;
    float cpuHistory[HISTORY] = {};
    float gpuHistory[HISTORY] = {};
    int historyIndex = 0;
};
