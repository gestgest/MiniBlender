#pragma once

#include <Render/Mesh.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

class Scene;
class OrbitCamera;
class History;

//정점 편집 모드 (블렌더의 Edit Mode에 해당).
//
//핵심 문제 — 왜 "용접(weld)"이 필요한가:
//  불러온 메시는 삼각형마다 정점을 따로 갖고 있다. 의자 모델을 재보니 정점 504개인데
//  실제 위치는 100개뿐이었다(한 자리에 평균 5개가 겹침). 면마다 노멀이 달라서 공유가 안 되기 때문이다.
//  이 상태에서 정점 하나만 옮기면 그 삼각형의 모서리만 떨어져 나가 메시가 찢어진다.
//  그래서 편집 모드에 들어갈 때 "같은 위치의 정점들"을 한 덩어리로 묶고,
//  사용자가 옮기는 건 그 덩어리 = 논리적 정점 하나다.
class EditMode
{
public:
    void Init();
    void Shutdown();

    //편집 시작/종료. 시작할 때 GPU에서 정점을 되읽고 용접 그룹을 만든다.
    bool Enter(Scene& scene, unsigned int objectId);
    void Exit();

    bool IsActive() const { return active; }
    unsigned int GetObjectId() const { return objectId; }

    int GetVertexCount() const { return (int)uniquePositions.size(); }
    int GetSelected() const { return selected; }

    //화면 좌표에서 가장 가까운 정점을 고른다. 못 찾으면 선택 해제.
    //maxPixelDistance보다 멀면 무시 — 아무 데나 클릭했을 때 엉뚱한 정점이 잡히는 걸 막는다.
    void PickAt(float mouseX, float mouseY, int screenW, int screenH,
        const glm::mat4& viewProj, const glm::mat4& model, float maxPixelDistance = 14.0f);

    //선택된 정점을 화면 평면 위에서 끌어 옮긴다 (마우스 픽셀 이동량 기준)
    void DragSelected(float dxPixels, float dyPixels, const OrbitCamera& camera,
        int screenH, const glm::mat4& model);

    //선택된 정점의 위치를 직접 지정 (속성 패널에서 숫자로 편집)
    void SetSelectedPosition(const glm::vec3& localPos);
    glm::vec3 GetSelectedPosition() const;

    //정점 표시용 VAO. 유니크 위치만 담고 있다.
    unsigned int GetPointVAO() const { return pointVAO; }

    //--- 되돌리기 연동 ---
    //편집 한 덩어리(스트로크)의 시작과 끝.
    //드래그는 마우스를 누른 채 프레임마다 조금씩 움직이는 거라, 그대로 기록하면
    //Ctrl+Z를 서른 번 눌러야 원위치가 된다. 누른 순간부터 뗀 순간까지를 하나로 묶는다.
    void BeginStroke();
    //바뀐 정점이 하나도 없으면 아무것도 쌓지 않는다 (정점을 고르기만 하고 안 옮긴 경우).
    void CommitStroke(History& history, const std::string& label);

    //되돌리기가 이 메시를 건드렸다면 CPU 사본을 다시 읽어온다.
    //안 그러면 화면의 점과 실제 메시가 어긋난 채로 다음 편집이 엉뚱한 값 위에 얹힌다.
    void RefreshIfEditing(Scene& scene, const Mesh* changedMesh);

private:
    void ApplyPositionChange();   //용접 그룹 전체에 반영 + 노멀 갱신 + GPU 업로드

    bool active = false;
    unsigned int objectId = 0;
    Mesh* targetMesh = nullptr;

    std::vector<Vertex> vertices;          //메시 원본 정점 (CPU 사본)
    std::vector<unsigned int> indices;

    std::vector<glm::vec3> uniquePositions;              //논리적 정점 = 화면에 점으로 보이는 것
    std::vector<std::vector<unsigned int>> weldGroups;   //각 논리 정점이 묶고 있는 실제 정점 번호들

    int selected = -1;

    //스트로크 시작 시점의 정점 사본. 끝날 때 지금 값과 비교해서 바뀐 것만 델타로 남긴다.
    //한 번에 하나만 살아 있어서(드래그 중 하나) 메모리 부담이 없다.
    std::vector<Vertex> strokeBefore;
    bool strokeOpen = false;

    //점 렌더링용 버퍼 (유니크 위치만)
    unsigned int pointVAO = 0;
    unsigned int pointVBO = 0;

    void UploadPoints();
};
