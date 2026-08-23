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

    //--- 선택 ---
    //복수 선택이라 "선택된 정점"은 인덱스 하나가 아니라 플래그 배열이다.
    //activeVertex는 그중 마지막으로 찍은 것 — 속성 패널이 숫자로 편집하는 대상이다.
    //(블렌더도 여러 개를 선택해도 N 패널은 활성 정점 하나를 보여준다)
    int GetActiveVertex() const { return activeVertex; }
    int GetSelectedCount() const { return selectedCount; }
    bool IsSelected(int index) const;

    void ClearSelection();
    void SelectOnly(int index);
    void ToggleSelection(int index);

    //화면 좌표에서 가장 가까운 정점을 찾는다. 없으면 -1.
    //선택을 바꾸지 않는 순수 조회인 이유: 같은 클릭이라도 "새로 고르기"일 수도,
    //"이미 고른 것들을 끌기 시작"일 수도 있어서 그 판단은 부르는 쪽 몫이다.
    //maxPixelDistance보다 멀면 무시 — 아무 데나 클릭했을 때 엉뚱한 정점이 잡히는 걸 막는다.
    //X-Ray가 꺼져 있으면 메시에 가려진 정점은 후보에서 빠진다(그래서 카메라 위치가 필요하다).
    int PickVertexAt(float mouseX, float mouseY, int screenW, int screenH,
        const glm::mat4& viewProj, const glm::mat4& model,
        const OrbitCamera& camera, float maxPixelDistance = 14.0f) const;

    //--- 박스(러버밴드) 선택 ---
    //빈 곳에서 시작한 드래그는 사각형이 되고, 놓는 순간 그 안의 정점이 전부 선택된다.
    //정점 위에서 시작한 드래그는 이동이라, 시작점에 정점이 있었는지로 둘이 갈린다.
    void BeginBoxSelect(float mouseX, float mouseY);
    void UpdateBoxSelect(float mouseX, float mouseY);
    void CancelBoxSelect();
    bool IsBoxSelecting() const { return boxSelecting; }
    //화면에 사각형을 그리기 위해 UI가 읽어간다 (정규화된 좌상단/우하단)
    void GetBoxRect(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const;

    //드래그를 놓는 순간. additive(Shift)면 기존 선택에 더하고, 아니면 갈아치운다.
    //사각형이 몇 픽셀도 안 되면 "그냥 빈 곳 클릭"으로 보고 선택을 푼다.
    void EndBoxSelect(int screenW, int screenH, const glm::mat4& viewProj,
        const glm::mat4& model, const OrbitCamera& camera, bool additive);

    //선택된 정점 "전부"를 화면 평면 위에서 끌어 옮긴다 (마우스 픽셀 이동량 기준)
    void DragSelected(float dxPixels, float dyPixels, const OrbitCamera& camera,
        int screenH, const glm::mat4& model);

    //활성 정점의 위치를 직접 지정 (속성 패널에서 숫자로 편집)
    void SetSelectedPosition(const glm::vec3& localPos);
    glm::vec3 GetSelectedPosition() const;

    //정점 표시용 VAO. 유니크 위치만 담고 있다.
    unsigned int GetPointVAO() const { return pointVAO; }
    //선택된 정점만 따로 담은 VAO. 강조해서 덧그리는 용도라 버퍼를 나눠 뒀다.
    unsigned int GetSelectedVAO() const { return selectedVAO; }

    //정점 점을 자기가 얹힌 면보다 얼마나 앞으로 띄울 것인가 (뷰 공간 = 월드 단위).
    //
    //이 값이 왜 공용이어야 하는가:
    //  정점은 면 위에 정확히 얹혀 있어서, 그냥 두면 깊이가 같아 z-파이팅으로 깜빡인다.
    //  그래서 그릴 때 카메라 쪽으로 살짝 당긴다. 그런데 "가려졌나?"를 판정하는 피킹이
    //  다른 여유값을 쓰면 두 기준이 어긋난다 — 화면엔 그려지는데 클릭은 안 먹는 정점이 생긴다.
    //  둘 다 여기를 거치게 해서 어긋날 수가 없게 만든다.
    //
    //카메라 거리에 비례시키는 이유: 모델이 0.01이든 1000이든 화면에서 차지하는 크기는
    //비슷하게 맞춰 보게 되므로, 여유값도 화면 기준으로 일정해야 한다.
    static float SurfaceBias(float cameraDistance) { return cameraDistance * 0.004f; }

    //--- X-Ray (블렌더의 Alt+Z) ---
    //끄면(기본) 메시에 가려진 정점은 그려지지도, 선택되지도 않는다 — 앞면만 만지게 된다.
    //켜면 메시를 통과해서 뒤쪽 정점까지 전부 보이고 잡힌다(반대편을 편집할 때).
    //이 상태는 편집 모드를 드나들어도 유지된다. 사용자가 정한 "보기 방식"이지 편집 대상의 속성이 아니다.
    bool IsXRay() const { return xray; }
    void SetXRay(bool on) { xray = on; }
    void ToggleXRay() { xray = !xray; }

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
    void ApplyPositionChange();   //선택된 용접 그룹 전체에 반영 + 노멀 갱신 + GPU 업로드

    //카메라에서 해당 정점까지 가는 길을 이 메시의 삼각형이 막고 있는가 (전부 로컬 공간).
    //깊이 버퍼를 되읽는 대신 CPU에서 계산하는 이유: 피킹은 렌더링 "전"에 일어나서
    //그 시점의 깊이 버퍼는 지난 프레임 것(스왑 후라 내용 보장도 없다)이다.
    //tFar는 "이 지점보다 앞에서 막히면 가려진 것으로 본다"는 경계다(광선 길이의 비율).
    //정점 자신이 속한 삼각형은 t == 1에서 만나므로 반드시 1보다 작아야 하고,
    //그 여유폭이 곧 SurfaceBias — 그리는 쪽이 띄우는 양과 같아야 한다.
    bool IsOccluded(size_t uniqueIndex, const glm::vec3& camLocalPos, float tFar) const;

    //정점 하나에 대한 tFar를 구한다. 월드 기준 여유값을 그 정점까지의 실제 거리로 나눠
    //광선 파라미터(t) 단위로 바꾸는 일 — 오브젝트에 스케일이 걸려 있어도 맞게 나온다.
    float OcclusionTFar(size_t uniqueIndex, const glm::mat4& model,
        const OrbitCamera& camera, float worldBias) const;

    void SetSelectedFlag(size_t index, bool on);

    bool active = false;
    unsigned int objectId = 0;
    Mesh* targetMesh = nullptr;

    std::vector<Vertex> vertices;          //메시 원본 정점 (CPU 사본)
    std::vector<unsigned int> indices;

    std::vector<glm::vec3> uniquePositions;              //논리적 정점 = 화면에 점으로 보이는 것
    std::vector<std::vector<unsigned int>> weldGroups;   //각 논리 정점이 묶고 있는 실제 정점 번호들

    //uniquePositions와 같은 길이. vector<bool>을 안 쓰는 이유는 비트 특수화 때문 —
    //여기선 크기보다 평범하게 동작하는 게 낫다.
    std::vector<char> selectedFlags;
    int selectedCount = 0;
    int activeVertex = -1;

    bool xray = false;

    bool boxSelecting = false;
    float boxStartX = 0.0f, boxStartY = 0.0f;
    float boxCurX = 0.0f, boxCurY = 0.0f;

    //노멀을 다시 계산할 삼각형을 고르기 위한 표시판. 드래그 중 매 프레임 쓰므로
    //그때마다 새로 잡지 않고 멤버로 들고 재사용한다.
    std::vector<char> touchedScratch;

    //스트로크 시작 시점의 정점 사본. 끝날 때 지금 값과 비교해서 바뀐 것만 델타로 남긴다.
    //한 번에 하나만 살아 있어서(드래그 중 하나) 메모리 부담이 없다.
    std::vector<Vertex> strokeBefore;
    bool strokeOpen = false;

    //점 렌더링용 버퍼 (유니크 위치만)
    unsigned int pointVAO = 0;
    unsigned int pointVBO = 0;

    //선택된 정점만 모아 담는 버퍼. 선택이 바뀌거나 정점이 움직일 때마다 다시 올린다.
    unsigned int selectedVAO = 0;
    unsigned int selectedVBO = 0;

    void UploadSelectedPoints();
    void UploadPoints();
};
