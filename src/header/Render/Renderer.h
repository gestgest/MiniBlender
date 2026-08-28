#pragma once

#include <Render/FrameStats.h>
#include <Render/Mesh.h>
#include <Render/Gizmo.h>

#include <glm/glm.hpp>

#include <vector>

class Scene;
class OrbitCamera;
class Shader;

//씬을 화면에 그리는 책임만 갖는다.
//main.cpp에서 직접 glDrawElements를 부르지 않고 여기로 모은 이유:
//나중에 인스턴싱 / MultiDrawIndirect / GPU 컬링으로 갈아끼울 때
//"제출하는 쪽"은 그대로 두고 "실행하는 쪽"만 바꾸면 되게 하려는 것.
class Renderer
{
public:
    void Init();
    void Shutdown();

    void RenderScene(Scene& scene, const OrbitCamera& camera, int width, int height, FrameStats& stats);

    //편집 모드일 때 정점을 점으로 덧그린다. 씬 패스 뒤에 별도 패스로 나가므로 드로우콜 1개가 추가된다.
    void RenderEditPoints(const class EditMode& edit, const Scene& scene,
        const OrbitCamera& camera, int width, int height, FrameStats& stats);

    //선택된 정점이 있을 때 이동 기즈모(X/Y/Z 화살표)를 덧그린다.
    void RenderGizmo(const class EditMode& edit, const Scene& scene,
        const OrbitCamera& camera, int width, int height, FrameStats& stats);

    //면 선택 모드에서 선택된 면을 반투명 주황으로 덧칠한다.
    void RenderFaceHighlight(const class EditMode& edit, const Scene& scene,
        const OrbitCamera& camera, int width, int height, FrameStats& stats);

    //그리드 on/off (UI에서 토글)
    bool showGrid = true;

    //인스턴싱 on/off. 끄면 오브젝트 하나당 드로우콜 하나를 내는 원래(순진한) 경로로 돌아간다.
    //지우지 않고 토글로 남겨둔 이유: 이 프로젝트의 작업 방식이 "같은 씬을 켜고/끄고 재서 비교"라서,
    //비교 대상이 코드에서 사라지면 다음에 또 만들어야 한다. 측정 모드에서도 --no-instancing으로 고를 수 있다.
    bool useInstancing = true;

    glm::vec3 lightDirection{ -0.4f, -1.0f, -0.55f };
    glm::vec3 backgroundColor{ 0.13f, 0.13f, 0.15f };

private:
    //메시별로 오브젝트를 모아 인스턴스 데이터를 만들고, 메시당 드로우콜 하나로 그린다.
    void DrawObjectsNaive(Scene& scene, FrameStats& stats);
    void DrawObjectsInstanced(Scene& scene, FrameStats& stats);

    //한 메시를 공유하는 오브젝트 묶음. 프레임마다 새로 만들지 않고 재사용한다
    //(clear()는 capacity를 남기니까, 두 번째 프레임부터는 할당이 아예 없다.
    // 매 프레임 10000개짜리 vector를 새로 할당하면 그게 곧 CPU 시간으로 잡혀서 측정이 오염된다)
    struct InstanceGroup
    {
        Mesh* mesh = nullptr;
        size_t first = 0;    //instanceScratch 안에서 이 묶음이 시작하는 인덱스
        size_t count = 0;
    };
    std::vector<InstanceGroup> instanceGroups;
    std::vector<InstanceData> instanceScratch;

    //인스턴스 데이터를 담는 GPU 버퍼. 매 프레임 통째로 새로 올린다.
    //불변 저장소(glNamedBufferStorage)가 아니라 glNamedBufferData를 쓰는 이유:
    //  오브젝트 수는 언제든 늘 수 있는데 불변 저장소는 크기를 못 바꾼다. 다시 만들면 이름(id)이 바뀌고,
    //  그러면 VAO에 걸어둔 연결이 끊겨서 메시마다 다시 걸어야 한다.
    //  glNamedBufferData는 같은 이름으로 크기만 갈아끼우므로 연결이 살아있다.
    unsigned int instanceVBO = 0;
    size_t instanceBufferBytes = 0;

    Shader* objectShader = nullptr;
    Shader* instancedShader = nullptr;
    Shader* gridShader = nullptr;
    Shader* pointShader = nullptr;
    Shader* gizmoShader = nullptr;
    Shader* faceHighlightShader = nullptr;

    //이동 기즈모 지오메트리. 매 프레임 위치/크기/강조색이 바뀔 수 있어 내용을 다시 올린다.
    Gizmo gizmo;
    std::vector<GizmoVertex> gizmoLineScratch;
    std::vector<GizmoVertex> gizmoTriScratch;

    //그리드는 버텍스 데이터가 없지만 VAO는 반드시 하나 바인딩돼 있어야 한다 (core 프로파일 규칙).
    //그래서 텅 빈 VAO를 하나 만들어 둔다.
    unsigned int emptyVAO = 0;
};
