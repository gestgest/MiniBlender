#include <UI/EditorUI.h>

#include <UI/FileDialog.h>

#include <Render/FrameStats.h>
#include <Render/OrbitCamera.h>
#include <Render/Renderer.h>
#include <Edit/EditMode.h>
#include <Edit/History.h>
#include <Scene/Scene.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <cstdio>
#include <cstring>

void EditorUI::Init(GLFWwindow* window)
{
    ownerWindow = window;

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

void EditorUI::Draw(Scene& scene, Renderer& renderer, OrbitCamera& camera, const FrameStats& stats,
    EditMode& edit, History& history)
{
    DrawStatsPanel(stats, renderer);
    DrawFilePanel();
    DrawOutliner(scene, history);
    DrawInspector(scene, camera, edit, history);
    DrawBoxSelect(edit);
}

void EditorUI::DrawBoxSelect(const EditMode& edit)
{
    if (!edit.IsBoxSelecting())
        return;

    float minX, minY, maxX, maxY;
    edit.GetBoxRect(minX, minY, maxX, maxY);

    //전경 드로우리스트를 쓰면 창 하나 없이 화면 맨 위에 그릴 수 있다.
    //이것 때문에 셰이더와 VAO를 따로 만드는 건 과하다 — 어차피 UI가 켜져 있을 때만 쓰는 표시다.
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(120, 170, 255, 40));
    draw->AddRect(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(150, 200, 255, 200));
}

bool EditorUI::ConsumeLoadRequest(std::string& outPath)
{
    if (!hasLoadRequest)
        return false;

    outPath = requestedPath;
    hasLoadRequest = false;
    return true;
}

bool EditorUI::ConsumeSaveRequest(std::string& outPath)
{
    if (!hasSaveRequest)
        return false;

    outPath = requestedSavePath;
    hasSaveRequest = false;
    return true;
}

bool EditorUI::ConsumeDialogStall()
{
    const bool stalled = dialogStalled;
    dialogStalled = false;
    return stalled;
}

bool EditorUI::ConsumeUndoRequest()
{
    const bool requested = pendingUndo;
    pendingUndo = false;
    return requested;
}

bool EditorUI::ConsumeRedoRequest()
{
    const bool requested = pendingRedo;
    pendingRedo = false;
    return requested;
}

void EditorUI::SetImportMessage(const std::string& msg, bool isError)
{
    importMessage = msg;
    importFailed = isError;
}

//모달 대화상자가 떠 있는 동안 GLFW는 마우스를 뗀 이벤트를 받지 못한다.
//그대로 두면 ImGui가 버튼이 계속 눌려 있다고 믿어서, 대화상자를 닫자마자
//커서 밑에 있던 위젯이 멋대로 드래그된다. 눌림 상태를 직접 풀어준다.
static void ReleaseMouseAfterModal()
{
    ImGuiIO& io = ImGui::GetIO();
    for (int button = 0; button < 3; ++button)
        io.AddMouseButtonEvent(button, false);
}

//대화상자에서 고른 경로를 입력창에도 되돌려 넣는다.
//경로가 남아 있어야 무엇을 불러왔는지 보이고, 살짝 고쳐서 다시 쓸 수도 있다.
static void CopyToBuffer(const std::string& path, char* buffer, size_t size)
{
    const size_t n = (path.size() < size - 1) ? path.size() : size - 1;
    std::memcpy(buffer, path.c_str(), n);
    buffer[n] = '\0';
}

void EditorUI::DrawFilePanel()
{
    ImGui::SetNextWindowPos(ImVec2(330, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430, 190), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("파일"))
    {
        ImGui::SeparatorText("가져오기 (FBX)");
        ImGui::TextDisabled("창에 파일을 끌어다 놓아도 됩니다");

        ImGui::SetNextItemWidth(-140.0f);
        //Enter로도 불러올 수 있게 (경로를 붙여넣고 바로 실행하는 흐름이 자연스럽다)
        const bool entered = ImGui::InputText("##path", pathBuffer, sizeof(pathBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        //"..."는 윈도우 기본 열기 창을 띄운다. 입력창은 그대로 남겨둔다 —
        //경로를 붙여넣거나 콘솔에서 복사해 오는 흐름이 대화상자보다 빠를 때가 많다.
        if (ImGui::Button("...##open"))
        {
            const std::string picked = FileDialog::OpenModel(ownerWindow);
            ReleaseMouseAfterModal();
            dialogStalled = true;   //대화상자가 떠 있던 시간은 프레임 통계에서 뺀다

            if (!picked.empty())
            {
                CopyToBuffer(picked, pathBuffer, sizeof(pathBuffer));
                //고르는 순간이 곧 "불러와 달라"는 뜻이다. 버튼을 한 번 더 누르게 할 이유가 없다.
                requestedPath = picked;
                hasLoadRequest = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("파일 찾아보기");

        ImGui::SameLine();
        const bool clicked = ImGui::Button("불러오기");

        if ((entered || clicked) && pathBuffer[0] != '\0')
        {
            requestedPath = pathBuffer;
            hasLoadRequest = true;
        }

        ImGui::SeparatorText("내보내기 (확장자로 OBJ/FBX 결정)");
        ImGui::SetNextItemWidth(-110.0f);
        const bool exportEntered = ImGui::InputText("##exportpath", exportPathBuffer, sizeof(exportPathBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        if (ImGui::Button("...##save"))
        {
            //입력창에 있던 값을 파일명 칸 초기값으로 넘겨서 확장자를 다시 고민하지 않게 한다
            const std::string picked = FileDialog::SaveModel(ownerWindow, exportPathBuffer);
            ReleaseMouseAfterModal();
            dialogStalled = true;

            if (!picked.empty())
            {
                CopyToBuffer(picked, exportPathBuffer, sizeof(exportPathBuffer));
                //덮어쓰기 확인은 대화상자가 이미 받았으니 바로 내보낸다
                requestedSavePath = picked;
                hasSaveRequest = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("저장 위치 고르기");

        ImGui::SameLine();
        const bool exportClicked = ImGui::Button("저장");

        if ((exportEntered || exportClicked) && exportPathBuffer[0] != 0)
        {
            requestedSavePath = exportPathBuffer;
            hasSaveRequest = true;
        }

        if (!importMessage.empty())
        {
            ImGui::Separator();
            if (importFailed)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", importMessage.c_str());
            else
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", importMessage.c_str());
        }
    }
    ImGui::End();
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

        //vsync가 켜져 있으면 드라이버가 프레임 큐가 찰 때까지 GL 호출 안에서 CPU를 붙잡는다.
        //그 대기가 우리 측정 구간 안에서 일어나기 때문에 CPU 값이 프레임 주기에 붙어버린다
        //(75Hz면 13.3ms 근처에서 안 움직임). 진짜 CPU 부하를 보려면 vsync를 끈 측정 모드를 써야 한다.
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("vsync가 켜져 있으면 대기 시간이 섞여 프레임 주기에 붙는다.\n"
                              "순수 CPU 부하는 측정 모드(MiniBlender.exe <개수>)에서 볼 것.");

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

        //이 체크박스가 이 프로젝트의 본론이다. 끄면 오브젝트 하나당 드로우콜 하나를 내는
        //원래 경로로 돌아가서, 위의 드로우콜/CPU 숫자가 어떻게 달라지는지 바로 보인다.
        ImGui::Checkbox("인스턴싱", &renderer.useInstancing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("같은 메시를 쓰는 오브젝트를 모아 드로우콜 하나로 그린다.\n"
                              "삼각형 수는 그대로고 드로우콜만 줄어든다.\n"
                              "vsync 때문에 여기서는 CPU 값이 잘 안 움직인다 - 측정 모드에서 볼 것.");

        ImGui::Checkbox("그리드 표시", &renderer.showGrid);
        ImGui::ColorEdit3("배경", &renderer.backgroundColor.x);
        ImGui::DragFloat3("광원 방향", &renderer.lightDirection.x, 0.01f, -1.0f, 1.0f);
    }
    ImGui::End();
}

void EditorUI::DrawOutliner(Scene& scene, History& history)
{
    ImGui::SetNextWindowPos(ImVec2(10, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("아웃라이너"))
    {
        //--- 되돌리기 ---
        //단축키(Ctrl+Z)가 본체지만, 버튼이 있어야 "무엇이" 되돌아가는지 보인다.
        ImGui::BeginDisabled(!history.CanUndo());
        if (ImGui::Button("되돌리기"))
            pendingUndo = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && history.CanUndo())
            ImGui::SetTooltip("Ctrl+Z — %s", history.UndoLabel());

        ImGui::SameLine();
        ImGui::BeginDisabled(!history.CanRedo());
        if (ImGui::Button("다시 실행"))
            pendingRedo = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && history.CanRedo())
            ImGui::SetTooltip("Ctrl+Y — %s", history.RedoLabel());

        ImGui::Separator();

        //--- 오브젝트 추가 ---
        if (ImGui::BeginCombo("추가", "프리미티브 선택..."))
        {
            for (const std::string& meshName : scene.GetMeshNames())
            {
                if (ImGui::Selectable(meshName.c_str()))
                {
                    //원기둥은 세그먼트 수에 따라 동전(각지게)부터 매끈한 원통까지 모양이 갈리니
                    //바로 추가하지 않고 몇 각형으로 만들지 먼저 물어본다.
                    if (meshName == "Cylinder")
                    {
                        showCylinderSegmentsPopup = true;
                    }
                    else
                    {
                        //씬 목록을 앞뒤로 떠서 비교한다. 추가/삭제마다 기록 코드를 따로 쓰지 않아도
                        //되돌리기가 따라붙고, 복사는 클릭한 순간 한 번뿐이라 비용도 없다.
                        const std::vector<SceneObject> before = scene.GetObjects();
                        SceneObject* obj = scene.AddObject(meshName, meshName);
                        selectedId = obj->id;
                        history.Push(History::MakeSceneDiff(before, scene.GetObjects(), "오브젝트 추가"));
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (showCylinderSegmentsPopup)
            ImGui::OpenPopup("원기둥 세그먼트");

        if (ImGui::BeginPopupModal("원기둥 세그먼트", &showCylinderSegmentsPopup,
            ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("옆면 각 수를 정하세요. 낮으면 동전처럼 각지고, 높으면 매끈한 원통이 됩니다.");
            ImGui::SliderInt("세그먼트", &cylinderSegments, 3, 128);

            if (ImGui::Button("추가"))
            {
                //콤보에 미리 구워둔 "Cylinder"(32각)와 별개로, 요청한 세그먼트로 새 메시를 만들어
                //라이브러리에 등록한다. 이렇게 만든 메시도 이후 콤보 목록에 그대로 나타나 재사용된다.
                std::vector<Vertex> verts;
                std::vector<unsigned int> indices;
                Primitives::MakeCylinder(verts, indices, cylinderSegments);

                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "Cylinder_%d각", cylinderSegments);

                const std::vector<SceneObject> before = scene.GetObjects();
                Mesh* mesh = scene.AddMesh(nameBuf, verts, indices);
                SceneObject* obj = scene.AddObject(mesh->GetName(), mesh->GetName());
                selectedId = obj->id;
                history.Push(History::MakeSceneDiff(before, scene.GetObjects(), "오브젝트 추가"));

                showCylinderSegmentsPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("취소"))
            {
                showCylinderSegmentsPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        //드로우콜 부하 테스트용. 이걸 눌러서 숫자가 어떻게 변하는지 보는 게 이 프로젝트의 재미.
        if (ImGui::Button("큐브 100개 추가"))
        {
            const std::vector<SceneObject> before = scene.GetObjects();

            int side = 10;
            for (int i = 0; i < 100; ++i)
            {
                float x = (float)(i % side) * 1.5f - side * 0.75f;
                float z = (float)(i / side) * 1.5f - side * 0.75f;
                scene.AddObject("Cube", "Cube", glm::vec3(x, 0.5f, z));
            }

            //100개가 액션 하나다. 한 번 눌러 만든 걸 되돌릴 때 100번 누르게 할 순 없다.
            history.Push(History::MakeSceneDiff(before, scene.GetObjects(), "큐브 100개 추가"));
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
                if (ImGui::MenuItem("삭제", "Del"))
                    toDelete = obj.id;
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        //순회 중에 지우면 반복자가 깨지니 루프가 끝난 뒤에 지운다
        if (toDelete != 0)
        {
            const std::vector<SceneObject> before = scene.GetObjects();
            scene.RemoveObject(toDelete);
            if (selectedId == toDelete)
                selectedId = 0;
            history.Push(History::MakeSceneDiff(before, scene.GetObjects(), "오브젝트 삭제"));
        }
    }
    ImGui::End();
}

void EditorUI::DrawInspector(Scene& scene, OrbitCamera& camera, EditMode& edit, History& history)
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

            //위젯 하나가 끝날 때마다 이걸 부른다.
            //잡은 순간(Activated)의 상태를 떠 두고, 놓은 순간(DeactivatedAfterEdit)에 한 번만 기록한다.
            //슬라이더를 끄는 내내 기록하면 되돌리기 스택이 수십 칸씩 쌓여서 쓸모가 없어진다.
            auto trackPropertyEdit = [&](const char* label)
            {
                if (ImGui::IsItemActivated())
                {
                    propertyBefore = *obj;
                    propertyEditing = true;
                }

                if (ImGui::IsItemDeactivatedAfterEdit() && propertyEditing
                    && propertyBefore.id == obj->id)
                {
                    Action action;
                    action.label = label;
                    action.hasChange = true;
                    action.before = propertyBefore;
                    action.after = *obj;
                    history.Push(std::move(action));
                    propertyEditing = false;
                }
            };

            ImGui::DragFloat3("위치", &obj->transform.position.x, 0.02f);
            trackPropertyEdit("위치 변경");
            ImGui::DragFloat3("회전", &obj->transform.rotation.x, 0.5f);
            trackPropertyEdit("회전 변경");
            ImGui::DragFloat3("크기", &obj->transform.scale.x, 0.02f, 0.01f, 100.0f);
            trackPropertyEdit("크기 변경");
            ImGui::ColorEdit3("색", &obj->color.x);
            trackPropertyEdit("색 변경");
            ImGui::Checkbox("표시", &obj->visible);
            trackPropertyEdit("표시 전환");

            ImGui::Separator();
            if (ImGui::Button("이 오브젝트로 시점 이동"))
                camera.FocusOn(obj->transform.position);

            //--- 정점 편집 ---
            ImGui::SeparatorText("정점 편집");

            if (!edit.IsActive() || edit.GetObjectId() != obj->id)
            {
                if (ImGui::Button("편집 모드 (Tab)"))
                    edit.Enter(scene, obj->id);
            }
            else
            {
                if (ImGui::Button("편집 종료 (Tab)"))
                    edit.Exit();

                ImGui::Text("정점 %d개", edit.GetVertexCount());

                //앞뒤 정점이 겹쳐 보이는 문제를 다루는 스위치. 단축키는 블렌더와 같은 Alt+Z.
                bool xray = edit.IsXRay();
                if (ImGui::Checkbox("X-Ray (Alt+Z)", &xray))
                    edit.SetXRay(xray);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "끄면: 면에 가려진 정점은 숨고 선택도 안 됩니다 (앞면만 편집).\n"
                        "켜면: 메시를 통과해 뒤쪽 정점까지 보이고 잡힙니다.");
                }

                if (edit.GetSelectedCount() == 0)
                {
                    ImGui::TextDisabled("정점 클릭 = 선택, 드래그 = 이동");
                    ImGui::TextDisabled("빈 곳에서 드래그 = 박스 선택");
                    ImGui::TextDisabled("Shift + 클릭 = 선택에 추가/제외");
                    if (!edit.IsXRay())
                        ImGui::TextDisabled("뒤쪽 정점은 시점을 돌리거나 Alt+Z");
                }
                else
                {
                    ImGui::Text("선택: %d개 (활성 #%d)", edit.GetSelectedCount(), edit.GetActiveVertex());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("해제"))
                        edit.ClearSelection();

                    //숫자 편집은 활성 정점 하나에만 적용된다 (여럿을 한 점으로 접지 않으려고)
                    glm::vec3 pos = edit.GetSelectedPosition();
                    const bool moved = ImGui::DragFloat3("로컬 좌표", &pos.x, 0.005f);

                    //BeginStroke가 값을 적용하기 "전"에 와야 편집 전 상태가 잡힌다
                    if (ImGui::IsItemActivated())
                        edit.BeginStroke();
                    if (moved)
                        edit.SetSelectedPosition(pos);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        edit.CommitStroke(history, "정점 이동");
                }
            }
        }
    }
    ImGui::End();
}
