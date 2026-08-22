#include <UI/EditorUI.h>

#include <Render/FrameStats.h>
#include <Render/OrbitCamera.h>
#include <Render/Renderer.h>
#include <Scene/Scene.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <cstdio>

void EditorUI::Init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   //패널을 가장자리에 붙일 수 있게

    ImGui::StyleColorsDark();

    //ImGui 기본 폰트(ProggyClean)엔 한글 글리프가 아예 없어서 전부 ????로 나온다.
    //시스템에 깔린 맑은 고딕을 물려준다. 없는 환경이면 기본 폰트로 조용히 넘어감(영문만 나옴).
    {
        ImFontConfig cfg;
        cfg.OversampleH = 2;   //작은 글자에서 한글 획이 뭉개지는 걸 줄인다
        cfg.OversampleV = 1;
        if (io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 16.0f,
            &cfg, io.Fonts->GetGlyphRangesKorean()) == nullptr)
        {
            io.Fonts->AddFontDefault();
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    //셰이더 버전은 GLSL 문법 버전. 컨텍스트가 4.6이니 460으로 맞춘다.
    ImGui_ImplOpenGL3_Init("#version 460");
}

void EditorUI::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUI::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool EditorUI::WantCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool EditorUI::WantCaptureKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

void EditorUI::Draw(Scene& scene, Renderer& renderer, OrbitCamera& camera, const FrameStats& stats)
{
    DrawStatsPanel(stats, renderer);
    DrawOutliner(scene);
    DrawInspector(scene, camera);
}

void EditorUI::DrawStatsPanel(const FrameStats& stats, Renderer& renderer)
{
    //히스토리 갱신 (그래프용)
    cpuHistory[historyIndex] = stats.GetCpuMs();
    gpuHistory[historyIndex] = stats.GetGpuMs();
    historyIndex = (historyIndex + 1) % HISTORY;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 340), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("통계"))
    {
        ImGui::Text("FPS      %.1f", stats.GetFps());
        ImGui::Text("프레임   %.2f ms", stats.GetFrameMs());
        ImGui::Separator();

        //CPU와 GPU를 나눠 보는 게 핵심.
        //CPU가 높으면 드로우콜/상태변경이 병목 -> 배칭·인스턴싱이 답
        //GPU가 높으면 픽셀/버텍스 부하가 병목 -> 컬링·LOD·셰이더 최적화가 답
        ImGui::Text("CPU      %.2f ms", stats.GetCpuMs());
        ImGui::Text("GPU      %.2f ms", stats.GetGpuMs());
        ImGui::Separator();

        ImGui::Text("드로우콜 %u", stats.GetDrawCalls());
        ImGui::Text("삼각형   %u", stats.GetTriangles());

        ImGui::Separator();
        ImGui::PlotLines("CPU ms", cpuHistory, HISTORY, historyIndex, nullptr,
            0.0f, 8.0f, ImVec2(0, 45));
        ImGui::PlotLines("GPU ms", gpuHistory, HISTORY, historyIndex, nullptr,
            0.0f, 8.0f, ImVec2(0, 45));

        ImGui::Separator();
        ImGui::Checkbox("그리드 표시", &renderer.showGrid);
        ImGui::ColorEdit3("배경", &renderer.backgroundColor.x);
        ImGui::DragFloat3("광원 방향", &renderer.lightDirection.x, 0.01f, -1.0f, 1.0f);
    }
    ImGui::End();
}

void EditorUI::DrawOutliner(Scene& scene)
{
    ImGui::SetNextWindowPos(ImVec2(10, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("아웃라이너"))
    {
        //--- 오브젝트 추가 ---
        if (ImGui::BeginCombo("추가", "프리미티브 선택..."))
        {
            for (const std::string& meshName : scene.GetMeshNames())
            {
                if (ImGui::Selectable(meshName.c_str()))
                {
                    SceneObject* obj = scene.AddObject(meshName, meshName);
                    selectedId = obj->id;
                }
            }
            ImGui::EndCombo();
        }

        //드로우콜 부하 테스트용. 이걸 눌러서 숫자가 어떻게 변하는지 보는 게 이 프로젝트의 재미.
        if (ImGui::Button("큐브 100개 추가"))
        {
            int side = 10;
            for (int i = 0; i < 100; ++i)
            {
                float x = (float)(i % side) * 1.5f - side * 0.75f;
                float z = (float)(i / side) * 1.5f - side * 0.75f;
                scene.AddObject("Cube", "Cube", glm::vec3(x, 0.5f, z));
            }
        }

        ImGui::Separator();
        ImGui::Text("오브젝트 %d개", (int)scene.GetObjects().size());
        ImGui::Separator();

        //--- 목록 ---
        unsigned int toDelete = 0;
        for (SceneObject& obj : scene.GetObjects())
        {
            ImGui::PushID((int)obj.id);

            bool isSelected = (obj.id == selectedId);
            if (ImGui::Selectable(obj.name.c_str(), isSelected))
                selectedId = obj.id;

            //우클릭 컨텍스트 메뉴
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("삭제"))
                    toDelete = obj.id;
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        //순회 중에 지우면 반복자가 깨지니 루프가 끝난 뒤에 지운다
        if (toDelete != 0)
        {
            scene.RemoveObject(toDelete);
            if (selectedId == toDelete)
                selectedId = 0;
        }
    }
    ImGui::End();
}

void EditorUI::DrawInspector(Scene& scene, OrbitCamera& camera)
{
    ImGui::SetNextWindowPos(ImVec2(10, 630), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("속성"))
    {
        SceneObject* obj = scene.FindById(selectedId);
        if (obj == nullptr)
        {
            ImGui::TextDisabled("선택된 오브젝트 없음");
        }
        else
        {
            ImGui::Text("%s (id %u)", obj->name.c_str(), obj->id);
            if (obj->mesh != nullptr)
                ImGui::TextDisabled("메시: %s (삼각형 %u)", obj->mesh->GetName().c_str(), obj->mesh->GetTriangleCount());

            ImGui::Separator();
            ImGui::DragFloat3("위치", &obj->transform.position.x, 0.02f);
            ImGui::DragFloat3("회전", &obj->transform.rotation.x, 0.5f);
            ImGui::DragFloat3("크기", &obj->transform.scale.x, 0.02f, 0.01f, 100.0f);
            ImGui::ColorEdit3("색", &obj->color.x);
            ImGui::Checkbox("표시", &obj->visible);

            ImGui::Separator();
            if (ImGui::Button("이 오브젝트로 시점 이동"))
                camera.FocusOn(obj->transform.position);
        }
    }
    ImGui::End();
}
