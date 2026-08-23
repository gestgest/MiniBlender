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

void EditMode::Init()
{
    glCreateVertexArrays(1, &pointVAO);
    glCreateBuffers(1, &pointVBO);

    glVertexArrayVertexBuffer(pointVAO, 0, pointVBO, 0, sizeof(glm::vec3));
    glEnableVertexArrayAttrib(pointVAO, 0);
    glVertexArrayAttribFormat(pointVAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(pointVAO, 0, 0);
}

void EditMode::Shutdown()
{
    if (pointVAO != 0) { glDeleteVertexArrays(1, &pointVAO); pointVAO = 0; }
    if (pointVBO != 0) { glDeleteBuffers(1, &pointVBO); pointVBO = 0; }
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

    selected = -1;
    active = true;
    UploadPoints();
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
    const int keepSelected = selected;

    //Enter가 GPU에서 다시 읽어 용접 그룹까지 새로 만든다.
    //정점 위치만 바뀌고 개수는 그대로라 그룹 구성도 같으니, 선택은 그대로 살려둔다.
    if (Enter(scene, id) && keepSelected >= 0 && keepSelected < (int)uniquePositions.size())
        selected = keepSelected;
}

void EditMode::Exit()
{
    active = false;
    objectId = 0;
    targetMesh = nullptr;
    selected = -1;
    vertices.clear();
    indices.clear();
    uniquePositions.clear();
    weldGroups.clear();

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

bool EditMode::IsOccluded(size_t uniqueIndex, const glm::vec3& camLocalPos) const
{
    const glm::vec3 dir = uniquePositions[uniqueIndex] - camLocalPos;
    if (glm::dot(dir, dir) < 1e-12f)
        return false;   //카메라가 정점 위에 있다. 가릴 것도 없다.

    //광선을 camLocalPos + t*dir 로 두면 목표 정점이 정확히 t == 1 이다.
    //길이를 정규화하지 않는 게 요점: t가 "정점까지 가는 길의 몇 %"라서
    //오브젝트 스케일이 얼마든 아래 여유값(epsilon)이 그대로 통한다.
    const float tNear = 1e-4f;
    const float tFar = 1.0f - 1e-3f;   //자기가 속한 삼각형(t == 1)이 스스로를 가리지 않게 잘라낸다

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

void EditMode::PickAt(float mouseX, float mouseY, int screenW, int screenH,
    const glm::mat4& viewProj, const glm::mat4& model,
    const glm::vec3& camWorldPos, float maxPixelDistance)
{
    if (!active)
        return;

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
    {
        selected = -1;
        return;
    }

    //화면 거리가 가까운 순, 거리가 사실상 같으면(1픽셀² 이내) 카메라에 가까운 순.
    //거리를 floor로 뭉개서 비교하는 건 정렬 규칙을 성립시키기 위해서다 —
    //"차이가 1 미만이면 같다"로 두면 a==b, b==c인데 a!=c가 되어 std::sort가 깨진다.
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            return std::make_pair(a.bucket, a.depth) < std::make_pair(b.bucket, b.depth);
        });

    if (xray)
    {
        selected = (int)candidates.front().index;
        return;
    }

    //--- X-Ray 꺼짐: 가려진 후보는 건너뛴다 ---
    //광선 검사는 삼각형 전체를 훑지만(O(삼각형 수)), 후보는 보통 한두 개고
    //이 함수는 클릭한 프레임에만 돈다. 매 프레임 도는 비용이 아니라 감당할 만하다.
    const glm::vec3 camLocal = glm::vec3(glm::inverse(model) * glm::vec4(camWorldPos, 1.0f));

    for (const Candidate& c : candidates)
    {
        if (!IsOccluded(c.index, camLocal))
        {
            selected = (int)c.index;
            return;
        }
    }

    //전부 가려져 있다 = 지금 각도에서 만질 수 있는 정점이 없다. 선택을 푸는 게 정직하다.
    selected = -1;
}

void EditMode::DragSelected(float dxPixels, float dyPixels, const OrbitCamera& camera,
    int screenH, const glm::mat4& model)
{
    if (!active || selected < 0)
        return;

    //화면과 나란한 평면 위에서 옮긴다. 1픽셀이 월드에서 몇 미터인지는 카메라와의 거리에 비례한다.
    //(멀리 있는 정점일수록 같은 픽셀 이동에 더 많이 움직여야 손끝 감각이 일정하다)
    const glm::vec3 worldPos = glm::vec3(model * glm::vec4(uniquePositions[selected], 1.0f));
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
    uniquePositions[selected] += invModel * worldDelta;

    ApplyPositionChange();
}

void EditMode::SetSelectedPosition(const glm::vec3& localPos)
{
    if (!active || selected < 0)
        return;

    uniquePositions[selected] = localPos;
    ApplyPositionChange();
}

glm::vec3 EditMode::GetSelectedPosition() const
{
    if (!active || selected < 0)
        return glm::vec3(0.0f);
    return uniquePositions[selected];
}

void EditMode::ApplyPositionChange()
{
    //1) 용접 그룹에 속한 실제 정점들을 전부 같은 위치로
    for (unsigned int vi : weldGroups[selected])
        vertices[vi].position = uniquePositions[selected];

    //2) 이 정점이 속한 삼각형들의 노멀을 다시 계산한다.
    //   면 노멀(평평한 셰이딩) 방식이라, 원래 부드럽게 셰이딩되던 메시는 편집한 부분만 각져 보인다.
    //   불러오는 모델이 대부분 로우폴리 평면 셰이딩이라 이 방식이 자연스럽고 계산도 국소적이다.
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const unsigned int i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];

        bool touched = false;
        for (unsigned int vi : weldGroups[selected])
        {
            if (vi == i0 || vi == i1 || vi == i2) { touched = true; break; }
        }
        if (!touched)
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
}
