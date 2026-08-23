#include <Edit/EditMode.h>

#include <Edit/History.h>
#include <Render/OrbitCamera.h>
#include <Scene/Scene.h>

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <tuple>
#include <utility>

namespace
{
    //위치를 격자에 맞춰 정수 키로 만든다. float를 그대로 비교하면 미세한 오차 때문에
    //같은 자리인데 다른 정점으로 갈라진다. 0.1mm 격자면 실용상 충분하다.
    std::tuple<long long, long long, long long> PositionKey(const glm::vec3& p)
    {
        const float scale = 10000.0f;   //1 / 0.0001
        return { (long long)std::lround(p.x * scale),
                 (long long)std::lround(p.y * scale),
                 (long long)std::lround(p.z * scale) };
    }
}

namespace
{
    //위치만 담는 VAO 하나를 만든다. 전체 정점용과 선택 정점용이 형태가 같아서 함수로 묶었다.
    void MakePointVAO(unsigned int& vao, unsigned int& vbo)
    {
        glCreateVertexArrays(1, &vao);
        glCreateBuffers(1, &vbo);

        glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(glm::vec3));
        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao, 0, 0);
    }
}

void EditMode::Init()
{
    MakePointVAO(pointVAO, pointVBO);
    MakePointVAO(selectedVAO, selectedVBO);
}

void EditMode::Shutdown()
{
    if (pointVAO != 0) { glDeleteVertexArrays(1, &pointVAO); pointVAO = 0; }
    if (pointVBO != 0) { glDeleteBuffers(1, &pointVBO); pointVBO = 0; }
    if (selectedVAO != 0) { glDeleteVertexArrays(1, &selectedVAO); selectedVAO = 0; }
    if (selectedVBO != 0) { glDeleteBuffers(1, &selectedVBO); selectedVBO = 0; }
}

bool EditMode::Enter(Scene& scene, unsigned int id)
{
    Exit();

    SceneObject* obj = scene.FindById(id);
    if (obj == nullptr || obj->mesh == nullptr)
        return false;

    if (!obj->mesh->ReadBack(vertices, indices))
        return false;

    targetMesh = obj->mesh;
    objectId = id;

    //--- 같은 위치의 정점들을 묶는다 ---
    uniquePositions.clear();
    weldGroups.clear();

    std::map<std::tuple<long long, long long, long long>, size_t> lookup;
    for (unsigned int i = 0; i < (unsigned int)vertices.size(); ++i)
    {
        const auto key = PositionKey(vertices[i].position);
        auto it = lookup.find(key);
        if (it == lookup.end())
        {
            lookup[key] = uniquePositions.size();
            uniquePositions.push_back(vertices[i].position);
            weldGroups.push_back({ i });
        }
        else
        {
            weldGroups[it->second].push_back(i);
        }
    }

    selectedFlags.assign(uniquePositions.size(), 0);
    selectedCount = 0;
    activeVertex = -1;

    active = true;
    UploadPoints();
    UploadSelectedPoints();
    return true;
}

void EditMode::BeginStroke()
{
    strokeOpen = false;
    if (!active)
        return;

    strokeBefore = vertices;
    strokeOpen = true;
}

void EditMode::CommitStroke(History& history, const std::string& label)
{
    if (!strokeOpen)
        return;

    strokeOpen = false;

    //스트로크 도중에 편집 대상이 바뀌었으면(다른 오브젝트로 갈아탐) 비교 자체가 성립하지 않는다
    if (!active || targetMesh == nullptr || strokeBefore.size() != vertices.size())
    {
        strokeBefore.clear();
        return;
    }

    Action action;
    action.label = label;
    action.mesh = targetMesh;

    for (unsigned int i = 0; i < (unsigned int)vertices.size(); ++i)
    {
        if (strokeBefore[i].position == vertices[i].position
            && strokeBefore[i].normal == vertices[i].normal)
            continue;

        VertexDelta delta;
        delta.index = i;
        delta.oldPosition = strokeBefore[i].position;
        delta.oldNormal = strokeBefore[i].normal;
        delta.newPosition = vertices[i].position;
        delta.newNormal = vertices[i].normal;
        action.vertexDeltas.push_back(delta);
    }

    strokeBefore.clear();
    history.Push(std::move(action));   //바뀐 게 없으면 Push가 알아서 무시한다
}

void EditMode::RefreshIfEditing(Scene& scene, const Mesh* changedMesh)
{
    if (!active || targetMesh != changedMesh)
        return;

    const unsigned int id = objectId;

    //Enter가 선택을 날려버리므로 잠시 빼돌린다
    std::vector<char> keepFlags = selectedFlags;
    const int keepCount = selectedCount;
    const int keepActive = activeVertex;

    //Enter가 GPU에서 다시 읽어 용접 그룹까지 새로 만든다.
    //정점 위치만 바뀌고 개수는 그대로라 그룹 구성도 같으니, 선택은 그대로 살려둔다.
    if (Enter(scene, id) && keepFlags.size() == selectedFlags.size())
    {
        selectedFlags = std::move(keepFlags);
        selectedCount = keepCount;
        activeVertex = (keepActive < (int)uniquePositions.size()) ? keepActive : -1;
        UploadSelectedPoints();
    }
}

void EditMode::Exit()
{
    active = false;
    objectId = 0;
    targetMesh = nullptr;
    vertices.clear();
    indices.clear();
    uniquePositions.clear();
    weldGroups.clear();

    selectedFlags.clear();
    selectedCount = 0;
    activeVertex = -1;
    boxSelecting = false;

    //기록 중이던 스트로크는 버린다. Enter가 Exit을 먼저 부르기 때문에
    //RefreshIfEditing 도중에도 여기를 지나는데, 그때 남은 사본은 이미 쓸모가 없다.
    strokeBefore.clear();
    strokeOpen = false;
}

void EditMode::UploadPoints()
{
    if (uniquePositions.empty())
        return;

    //편집 중 계속 다시 올리므로 STREAM_DRAW. 크기가 바뀌지 않으니 매번 새로 할당하지 않고
    //glNamedBufferData로 통째로 갱신해도 부담이 없다(정점 수백~수천 개 수준).
    glNamedBufferData(pointVBO,
        (GLsizeiptr)(uniquePositions.size() * sizeof(glm::vec3)),
        uniquePositions.data(), GL_STREAM_DRAW);
}

void EditMode::UploadSelectedPoints()
{
    if (selectedCount <= 0)
        return;   //그릴 게 없으면 버퍼도 건드리지 않는다. 렌더러가 개수를 보고 건너뛴다.

    //선택된 것만 추려서 따로 올린다. 전체 버퍼에 "선택됨" 속성을 하나 더 다는 방법도 있지만,
    //그러면 강조 정점만 깊이 테스트 없이 그리는 지금 방식(두 패스)을 쓸 수 없다.
    std::vector<glm::vec3> picked;
    picked.reserve((size_t)selectedCount);
    for (size_t i = 0; i < uniquePositions.size(); ++i)
    {
        if (selectedFlags[i])
            picked.push_back(uniquePositions[i]);
    }

    glNamedBufferData(selectedVBO,
        (GLsizeiptr)(picked.size() * sizeof(glm::vec3)),
        picked.data(), GL_STREAM_DRAW);
}

//--- 선택 조작 ---

bool EditMode::IsSelected(int index) const
{
    return index >= 0 && index < (int)selectedFlags.size() && selectedFlags[index] != 0;
}

void EditMode::SetSelectedFlag(size_t index, bool on)
{
    if (index >= selectedFlags.size())
        return;

    const bool was = selectedFlags[index] != 0;
    if (was == on)
        return;

    selectedFlags[index] = on ? 1 : 0;
    selectedCount += on ? 1 : -1;
}

void EditMode::ClearSelection()
{
    std::fill(selectedFlags.begin(), selectedFlags.end(), (char)0);
    selectedCount = 0;
    activeVertex = -1;
    UploadSelectedPoints();
}

void EditMode::SelectOnly(int index)
{
    std::fill(selectedFlags.begin(), selectedFlags.end(), (char)0);
    selectedCount = 0;
    activeVertex = -1;

    if (index >= 0 && index < (int)selectedFlags.size())
    {
        SetSelectedFlag((size_t)index, true);
        activeVertex = index;
    }
    UploadSelectedPoints();
}

void EditMode::ToggleSelection(int index)
{
    if (index < 0 || index >= (int)selectedFlags.size())
        return;

    const bool nowOn = (selectedFlags[index] == 0);
    SetSelectedFlag((size_t)index, nowOn);

    //켜면 그게 활성 정점이 되고, 끄면 활성 자리를 비운다
    activeVertex = nowOn ? index : -1;
    UploadSelectedPoints();
}

float EditMode::OcclusionTFar(size_t uniqueIndex, const glm::mat4& model,
    const OrbitCamera& camera, float worldBias) const
{
    const glm::vec3 camPos = camera.GetPosition();
    const glm::vec3 forward = glm::normalize(camera.GetTarget() - camPos);
    const glm::vec3 worldVertex = glm::vec3(model * glm::vec4(uniquePositions[uniqueIndex], 1.0f));

    //그리는 쪽은 점을 "뷰 축 방향 깊이"에서 띄운다 — 깊이 버퍼가 재는 게 그 값이기 때문이다.
    //그래서 여기서도 카메라까지의 직선거리가 아니라 뷰 축에 투영한 깊이로 나눠야
    //화면 한가운데든 가장자리든 두 기준이 정확히 같아진다.
    //(직선거리를 쓰면 가장자리로 갈수록 코사인만큼 어긋나서, 화면 구석의 정점이
    // 그려지는데도 안 잡히는 일이 생긴다)
    const float viewDepth = glm::dot(worldVertex - camPos, forward);
    if (viewDepth < 1e-6f)
        return 0.0f;

    //t는 "카메라에서 정점까지 가는 길의 몇 %"다. 띄우는 양을 그 길의 깊이로 나누면
    //같은 단위가 된다. 이 지점보다 앞에서 막혀야 비로소 "가려졌다".
    const float t = 1.0f - worldBias / viewDepth;
    return (t < 0.0f) ? 0.0f : t;
}

bool EditMode::IsOccluded(size_t uniqueIndex, const glm::vec3& camLocalPos, float tFar) const
{
    const glm::vec3 dir = uniquePositions[uniqueIndex] - camLocalPos;
    if (glm::dot(dir, dir) < 1e-12f)
        return false;   //카메라가 정점 위에 있다. 가릴 것도 없다.

    //광선을 camLocalPos + t*dir 로 두면 목표 정점이 정확히 t == 1 이다.
    //길이를 정규화하지 않는 게 요점: t가 "정점까지 가는 길의 몇 %"라서
    //오브젝트 스케일이 얼마든 아래 여유값(epsilon)이 그대로 통한다.
    const float tNear = 1e-4f;
    //tFar는 부르는 쪽이 SurfaceBias로 계산해 넘긴다. 자기가 속한 삼각형은 t == 1에서
    //만나므로 그보다 앞에서 잘리고, 그 여유폭이 그리는 쪽이 점을 띄우는 양과 같다.

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const glm::vec3& v0 = vertices[indices[i]].position;
        const glm::vec3& v1 = vertices[indices[i + 1]].position;
        const glm::vec3& v2 = vertices[indices[i + 2]].position;

        //Möller-Trumbore. 판별식 부호가 곧 앞/뒷면이라, 뒷면을 공짜로 걸러낼 수 있다.
        const glm::vec3 e1 = v1 - v0;
        const glm::vec3 e2 = v2 - v0;
        const glm::vec3 pv = glm::cross(dir, e2);
        const float det = glm::dot(e1, pv);

        //det <= 0 이면 뒷면이거나 광선과 평행하다.
        //뒷면을 건너뛰는 이유: 렌더러가 GL_CULL_FACE로 뒷면을 아예 그리지 않으므로,
        //화면에 없는 면이 정점을 가린다고 판단하면 눈에 보이는 정점이 안 잡힌다.
        if (det <= 1e-12f)
            continue;

        const float invDet = 1.0f / det;
        const glm::vec3 tv = camLocalPos - v0;

        const float u = glm::dot(tv, pv) * invDet;
        if (u < 0.0f || u > 1.0f)
            continue;

        const glm::vec3 qv = glm::cross(tv, e1);
        const float v = glm::dot(dir, qv) * invDet;
        if (v < 0.0f || u + v > 1.0f)
            continue;

        const float t = glm::dot(e2, qv) * invDet;
        if (t > tNear && t < tFar)
            return true;
    }

    return false;
}

int EditMode::PickVertexAt(float mouseX, float mouseY, int screenW, int screenH,
    const glm::mat4& viewProj, const glm::mat4& model,
    const OrbitCamera& camera, float maxPixelDistance) const
{
    if (!active)
        return -1;

    const glm::mat4 mvp = viewProj * model;
    const float maxDist2 = maxPixelDistance * maxPixelDistance;

    //후보를 한 번에 하나만 들고 비교하지 않고 전부 모으는 이유:
    //X-Ray가 꺼져 있으면 "가장 가까운 것"이 가려져 있을 수 있고, 그때 다음 후보로 넘어가야 한다.
    struct Candidate
    {
        float bucket;   //화면 거리를 1픽셀² 단위로 뭉갠 값 (아래 정렬 설명 참고)
        float depth;
        size_t index;
    };
    std::vector<Candidate> candidates;

    for (size_t i = 0; i < uniquePositions.size(); ++i)
    {
        glm::vec4 clip = mvp * glm::vec4(uniquePositions[i], 1.0f);
        if (clip.w <= 0.0f)
            continue;   //카메라 뒤쪽

        //클립 -> NDC -> 화면 픽셀
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float sx = (ndc.x * 0.5f + 0.5f) * (float)screenW;
        const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)screenH;

        const float dx = sx - mouseX;
        const float dy = sy - mouseY;
        const float d2 = dx * dx + dy * dy;

        if (d2 < maxDist2)
            candidates.push_back({ std::floor(d2), ndc.z, i });
    }

    if (candidates.empty())
        return -1;

    //화면 거리가 가까운 순, 거리가 사실상 같으면(1픽셀² 이내) 카메라에 가까운 순.
    //거리를 floor로 뭉개서 비교하는 건 정렬 규칙을 성립시키기 위해서다 —
    //"차이가 1 미만이면 같다"로 두면 a==b, b==c인데 a!=c가 되어 std::sort가 깨진다.
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            return std::make_pair(a.bucket, a.depth) < std::make_pair(b.bucket, b.depth);
        });

    if (xray)
        return (int)candidates.front().index;

    //--- X-Ray 꺼짐: 가려진 후보는 건너뛴다 ---
    //광선 검사는 삼각형 전체를 훑지만(O(삼각형 수)), 후보는 보통 한두 개고
    //이 함수는 클릭한 프레임에만 돈다. 매 프레임 도는 비용이 아니라 감당할 만하다.
    const glm::vec3 camLocal = glm::vec3(glm::inverse(model) * glm::vec4(camera.GetPosition(), 1.0f));
    const float bias = SurfaceBias(camera.GetDistance());

    for (const Candidate& c : candidates)
    {
        if (!IsOccluded(c.index, camLocal, OcclusionTFar(c.index, model, camera, bias)))
            return (int)c.index;
    }

    //전부 가려져 있다 = 지금 각도에서 만질 수 있는 정점이 없다
    return -1;
}

//--- 박스(러버밴드) 선택 ---

void EditMode::BeginBoxSelect(float mouseX, float mouseY)
{
    if (!active)
        return;

    boxSelecting = true;
    boxStartX = boxCurX = mouseX;
    boxStartY = boxCurY = mouseY;
}

void EditMode::UpdateBoxSelect(float mouseX, float mouseY)
{
    if (!boxSelecting)
        return;

    boxCurX = mouseX;
    boxCurY = mouseY;
}

void EditMode::CancelBoxSelect()
{
    boxSelecting = false;
}

void EditMode::GetBoxRect(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const
{
    //어느 방향으로 끌었든 좌상단/우하단으로 정규화해서 넘긴다
    outMinX = std::fmin(boxStartX, boxCurX);
    outMaxX = std::fmax(boxStartX, boxCurX);
    outMinY = std::fmin(boxStartY, boxCurY);
    outMaxY = std::fmax(boxStartY, boxCurY);
}

void EditMode::EndBoxSelect(int screenW, int screenH, const glm::mat4& viewProj,
    const glm::mat4& model, const OrbitCamera& camera, bool additive)
{
    if (!boxSelecting)
        return;

    boxSelecting = false;
    if (!active)
        return;

    float minX, minY, maxX, maxY;
    GetBoxRect(minX, minY, maxX, maxY);

    //손이 떨려서 몇 픽셀 움직인 건 드래그가 아니라 클릭이다.
    //빈 곳 클릭은 선택 해제 — 블렌더와 같다.
    const float DRAG_THRESHOLD = 3.0f;
    const bool isClick = (maxX - minX) < DRAG_THRESHOLD && (maxY - minY) < DRAG_THRESHOLD;

    if (!additive)
    {
        std::fill(selectedFlags.begin(), selectedFlags.end(), (char)0);
        selectedCount = 0;
        activeVertex = -1;
    }

    if (isClick)
    {
        UploadSelectedPoints();
        return;
    }

    //--- 사각형 안에 들어오는 정점 모으기 ---
    const glm::mat4 mvp = viewProj * model;

    std::vector<size_t> inside;
    for (size_t i = 0; i < uniquePositions.size(); ++i)
    {
        glm::vec4 clip = mvp * glm::vec4(uniquePositions[i], 1.0f);
        if (clip.w <= 0.0f)
            continue;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float sx = (ndc.x * 0.5f + 0.5f) * (float)screenW;
        const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)screenH;

        if (sx >= minX && sx <= maxX && sy >= minY && sy <= maxY)
            inside.push_back(i);
    }

    if (!xray && !inside.empty())
    {
        //--- 가려진 정점 걸러내기 ---
        //원칙: "화면에 그려진 정점은 반드시 선택된다." 애매하면 넣는 쪽으로 기운다.
        //안 보이는 게 딸려오는 건 눈에 띄면 지우면 그만이지만,
        //보이는데 안 잡히는 건 사용자가 원인을 알 수 없어서 훨씬 나쁘다.
        //
        //(예전엔 여기서 평균 노멀로 "카메라를 등진 정점"을 먼저 쳐냈는데, 그게 딱
        // 그 반대로 동작했다 — 실루엣 위의 정점은 노멀이 시선과 거의 수직이라
        // 제일 잘 보이는데도 뒷면으로 판정돼 통째로 빠졌다. 근사를 걷어냈다.)
        const size_t triangles = indices.size() / 3;

        //광선 검사는 후보 하나당 삼각형 전체를 훑는다. 큰 메시를 통째로 감싸면
        //마우스를 놓는 순간 몇 초씩 멈출 수 있어서 예산을 둔다.
        const size_t RAY_BUDGET = 4000000;

        if (triangles > 0 && inside.size() * triangles <= RAY_BUDGET)
        {
            const glm::vec3 camLocal =
                glm::vec3(glm::inverse(model) * glm::vec4(camera.GetPosition(), 1.0f));
            const float bias = SurfaceBias(camera.GetDistance());

            std::vector<size_t> visible;
            visible.reserve(inside.size());
            for (size_t i : inside)
            {
                if (!IsOccluded(i, camLocal, OcclusionTFar(i, model, camera, bias)))
                    visible.push_back(i);
            }
            inside.swap(visible);
        }
        //예산을 넘으면 사각형 안을 전부 선택한다. 정확도를 싸구려 근사로 바꾸는 대신
        //X-Ray를 켠 것처럼 후하게 잡는 쪽을 고른다 — 위의 원칙과 같은 이유다.
    }

    for (size_t i : inside)
        SetSelectedFlag(i, true);

    if (!inside.empty())
        activeVertex = (int)inside.back();

    UploadSelectedPoints();
}

void EditMode::DragSelected(float dxPixels, float dyPixels, const OrbitCamera& camera,
    int screenH, const glm::mat4& model)
{
    if (!active || selectedCount <= 0)
        return;

    //선택이 여럿이면 무게중심을 기준으로 깊이를 잡는다.
    //정점마다 제 깊이로 환산하면 같은 드래그에도 앞뒤가 서로 다른 거리를 움직여서 모양이 일그러진다.
    glm::vec3 centroid(0.0f);
    for (size_t i = 0; i < uniquePositions.size(); ++i)
    {
        if (selectedFlags[i])
            centroid += uniquePositions[i];
    }
    centroid /= (float)selectedCount;

    //화면과 나란한 평면 위에서 옮긴다. 1픽셀이 월드에서 몇 미터인지는 카메라와의 거리에 비례한다.
    //(멀리 있는 정점일수록 같은 픽셀 이동에 더 많이 움직여야 손끝 감각이 일정하다)
    const glm::vec3 worldPos = glm::vec3(model * glm::vec4(centroid, 1.0f));
    const glm::vec3 camPos = camera.GetPosition();
    const glm::vec3 forward = glm::normalize(camera.GetTarget() - camPos);

    const float depth = glm::dot(worldPos - camPos, forward);
    const float halfFov = glm::radians(camera.GetFov()) * 0.5f;
    const float worldPerPixel = 2.0f * depth * std::tan(halfFov) / (float)screenH;

    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const glm::vec3 worldDelta = right * (dxPixels * worldPerPixel)
                               - up * (dyPixels * worldPerPixel);   //화면 y는 아래가 +

    //월드 이동량을 오브젝트 로컬 공간으로 되돌린다 (오브젝트가 회전/스케일돼 있을 수 있으므로)
    const glm::mat3 invModel = glm::inverse(glm::mat3(model));
    const glm::vec3 localDelta = invModel * worldDelta;

    for (size_t i = 0; i < uniquePositions.size(); ++i)
    {
        if (selectedFlags[i])
            uniquePositions[i] += localDelta;
    }

    ApplyPositionChange();
}

void EditMode::SetSelectedPosition(const glm::vec3& localPos)
{
    if (!active || activeVertex < 0)
        return;

    //숫자 입력은 활성 정점 하나만 옮긴다.
    //여럿을 같은 좌표로 몰아넣으면 메시가 한 점으로 접혀버려서, 실수로 그럴 여지를 두지 않는다.
    uniquePositions[activeVertex] = localPos;
    ApplyPositionChange();
}

glm::vec3 EditMode::GetSelectedPosition() const
{
    if (!active || activeVertex < 0)
        return glm::vec3(0.0f);
    return uniquePositions[activeVertex];
}

void EditMode::ApplyPositionChange()
{
    //1) 선택된 논리 정점들의 용접 그룹을 전부 같은 위치로 옮기고, 옮긴 실제 정점에 표시를 남긴다.
    //   표시를 남기는 이유는 2번 때문이다 — 삼각형마다 "선택된 그룹에 속하나?"를 되묻는 대신
    //   여기서 한 번 칠해두면 노멀 갱신이 (정점 수 + 삼각형 수) 한 바퀴로 끝난다.
    //   박스로 수천 개를 선택하면 이 차이가 그대로 드래그 프레임의 끊김이 된다.
    //드래그 중 매 프레임 도는 함수라 배열을 새로 잡지 않고 멤버 하나를 재사용한다
    touchedScratch.assign(vertices.size(), 0);
    std::vector<char>& touched = touchedScratch;

    for (size_t u = 0; u < uniquePositions.size(); ++u)
    {
        if (!selectedFlags[u])
            continue;

        for (unsigned int vi : weldGroups[u])
        {
            vertices[vi].position = uniquePositions[u];
            touched[vi] = 1;
        }
    }

    //2) 옮긴 정점이 낀 삼각형들의 노멀을 다시 계산한다.
    //   면 노멀(평평한 셰이딩) 방식이라, 원래 부드럽게 셰이딩되던 메시는 편집한 부분만 각져 보인다.
    //   불러오는 모델이 대부분 로우폴리 평면 셰이딩이라 이 방식이 자연스럽고 계산도 국소적이다.
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const unsigned int i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];

        if (!touched[i0] && !touched[i1] && !touched[i2])
            continue;

        const glm::vec3 e1 = vertices[i1].position - vertices[i0].position;
        const glm::vec3 e2 = vertices[i2].position - vertices[i0].position;
        const glm::vec3 n = glm::cross(e1, e2);

        //면적이 0에 가까우면(정점을 겹쳐 놓은 경우) 정규화가 NaN을 만든다. 그냥 건너뛴다.
        if (glm::dot(n, n) < 1e-16f)
            continue;

        const glm::vec3 unit = glm::normalize(n);
        vertices[i0].normal = unit;
        vertices[i1].normal = unit;
        vertices[i2].normal = unit;
    }

    //3) GPU로
    if (targetMesh != nullptr)
        targetMesh->UpdateVertices(vertices);

    UploadPoints();
    UploadSelectedPoints();
}
