#pragma once

struct GLFWwindow;
class Scene;
class Renderer;
class OrbitCamera;
class FrameStats;

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
    void Draw(Scene& scene, Renderer& renderer, OrbitCamera& camera, const FrameStats& stats);
    void EndFrame();

    //ImGui가 마우스를 쓰고 있으면(패널 위에 커서가 있으면) 뷰포트 조작을 막아야 한다.
    //안 그러면 슬라이더를 드래그할 때 카메라가 같이 돌아간다.
    bool WantCaptureMouse() const;
    bool WantCaptureKeyboard() const;

    unsigned int GetSelectedId() const { return selectedId; }
    void SetSelectedId(unsigned int id) { selectedId = id; }

private:
    void DrawStatsPanel(const FrameStats& stats, Renderer& renderer);
    void DrawOutliner(Scene& scene);
    void DrawInspector(Scene& scene, OrbitCamera& camera);

    unsigned int selectedId = 0;

    //성능 그래프용 링버퍼. 숫자 하나만 보면 튀는 프레임(스파이크)을 놓친다.
    static const int HISTORY = 120;
    float cpuHistory[HISTORY] = {};
    float gpuHistory[HISTORY] = {};
    int historyIndex = 0;
};
