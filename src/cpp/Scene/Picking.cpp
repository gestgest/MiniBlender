#include <Scene/Picking.h>

#include <Render/Mesh.h>
#include <Render/OrbitCamera.h>
#include <Scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace
{
    struct Ray
    {
        glm::vec3 origin{ 0.0f };
        glm::vec3 dir{ 0.0f };
    };

    //화면 픽셀 -> 월드 광선.
    //근평면과 원평면의 같은 픽셀을 각각 월드로 되돌려 잇는 방식이라,
    //투영 행렬이 무엇이든(원근이든 직교든) 같은 코드가 그대로 통한다.
    Ray MakeRay(const OrbitCamera& camera, float mouseX, float mouseY, int screenW, int screenH)
    {
        const float aspect = (screenH > 0) ? (float)screenW / (float)screenH : 1.0f;
        const glm::mat4 invViewProj =
            glm::inverse(camera.GetProjectionMatrix(aspect) * camera.GetViewMatrix());

        const float ndcX = 2.0f * mouseX / (float)screenW - 1.0f;
        const float ndcY = 1.0f - 2.0f * mouseY / (float)screenH;   //화면 y는 아래가 +

        glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        Ray ray;
        ray.origin = glm::vec3(nearPoint);
        ray.dir = glm::normalize(glm::vec3(farPoint - nearPoint));
        return ray;
    }

    //슬랩 방법: 광선이 상자 안에 머무는 구간 [tMin, tMax]를 축마다 좁혀 나간다.
    //한 번이라도 tMin > tMax가 되면 빗나간 것.
    bool RayAabb(const Ray& ray, const glm::vec3& lo, const glm::vec3& hi, float& outT)
    {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();

        for (int axis = 0; axis < 3; ++axis)
        {
            //광선이 이 축과 나란하면 나눗셈이 무한대로 튄다. 그땐 시작점이 범위 안인지만 본다.
            if (std::fabs(ray.dir[axis]) < 1e-8f)
            {
                if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis])
                    return false;
                continue;
            }

            const float inv = 1.0f / ray.dir[axis];
            float t1 = (lo[axis] - ray.origin[axis]) * inv;
            float t2 = (hi[axis] - ray.origin[axis]) * inv;
            if (t1 > t2)
                std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax)
                return false;
        }

        outT = tMin;
        return true;
    }

    //Moller-Trumbore. 평면 방정식을 거치지 않고 무게중심 좌표를 바로 푼다.
    bool RayTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
        float& outT)
    {
        const glm::vec3 edge1 = b - a;
        const glm::vec3 edge2 = c - a;
        const glm::vec3 pvec = glm::cross(ray.dir, edge2);
        const float det = glm::dot(edge1, pvec);

        //det의 부호는 삼각형의 앞/뒤를 뜻하는데 여기선 보지 않는다.
        //법선이 뒤집힌 채로 들어온 모델도 골라져야 하니까 (불러온 FBX에 종종 있다).
        if (std::fabs(det) < 1e-12f)
            return false;   //광선이 삼각형과 나란함

        const float invDet = 1.0f / det;
        const glm::vec3 tvec = ray.origin - a;

        const float u = glm::dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
            return false;

        const glm::vec3 qvec = glm::cross(tvec, edge1);
        const float v = glm::dot(ray.dir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f)
            return false;

        const float t = glm::dot(edge2, qvec) * invDet;
        if (t < 0.0f)
            return false;   //카메라 뒤쪽

        outT = t;
        return true;
    }

    //광선을 오브젝트 로컬 공간으로 옮긴다. 상자와 삼각형을 월드로 변환하는 것보다 훨씬 싸다
    //(광선 하나를 옮기느냐, 정점 수천 개를 옮기느냐의 차이).
    //방향 벡터는 w=0으로 곱해야 이동 성분이 안 섞인다.
    Ray ToLocal(const Ray& ray, const glm::mat4& invModel)
    {
        Ray local;
        local.origin = glm::vec3(invModel * glm::vec4(ray.origin, 1.0f));
        local.dir = glm::vec3(invModel * glm::vec4(ray.dir, 0.0f));
        return local;
    }

    //로컬 t는 오브젝트 스케일이 섞여 있어서 오브젝트끼리 비교할 수 없다
    //(2배로 키운 오브젝트의 t=1과 원본의 t=1은 실제 거리가 다르다).
    //맞은 지점을 월드로 되돌려 카메라로부터의 실제 거리를 잰다.
    float WorldDistance(const Ray& worldRay, const Ray& localRay, float localT, const glm::mat4& model)
    {
        const glm::vec3 hit = glm::vec3(model * glm::vec4(localRay.origin + localRay.dir * localT, 1.0f));
        return glm::dot(hit - worldRay.origin, worldRay.dir);
    }

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        bool ok = false;
    };
}

unsigned int PickObject(Scene& scene, const OrbitCamera& camera,
    float mouseX, float mouseY, int screenW, int screenH)
{
    if (screenW <= 0 || screenH <= 0)
        return 0;

    const Ray ray = MakeRay(camera, mouseX, mouseY, screenW, screenH);

    //--- 1단계: 경계 상자로 후보 추리기 ---
    struct Candidate
    {
        const SceneObject* object;
        float boxDistance;   //상자에 들어간 지점까지의 거리 = 이 오브젝트가 가질 수 있는 최소 거리
    };
    std::vector<Candidate> candidates;

    for (const SceneObject& obj : scene.GetObjects())
    {
        //숨긴 오브젝트는 클릭도 안 되어야 한다. 안 보이는 게 골라지면 사용자는 영문을 모른다.
        if (!obj.visible || obj.mesh == nullptr)
            continue;

        const glm::mat4 model = obj.transform.GetMatrix();
        const Ray local = ToLocal(ray, glm::inverse(model));

        float localT = 0.0f;
        if (!RayAabb(local, obj.mesh->GetBoundsMin(), obj.mesh->GetBoundsMax(), localT))
            continue;

        candidates.push_back({ &obj, WorldDistance(ray, local, localT, model) });
    }

    if (candidates.empty())
        return 0;

    //가까운 것부터 본다. 정렬해 두면 아래에서 일찍 끊을 수 있다.
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.boxDistance < b.boxDistance; });

    //--- 2단계: 삼각형을 실제로 맞혀 보기 ---
    //메시는 여러 오브젝트가 공유하니(큐브 100개 = 메시 1개) 되읽은 결과를 클릭 한 번 동안 캐시한다.
    //이게 없으면 같은 버퍼를 100번 내려받는다.
    std::map<const Mesh*, MeshData> cache;

    unsigned int bestId = 0;
    float bestDistance = std::numeric_limits<float>::max();

    for (const Candidate& candidate : candidates)
    {
        //남은 후보의 상자가 이미 찾은 삼각형보다 멀면, 그 안의 삼각형은 더 멀 수밖에 없다.
        //가까운 순으로 정렬돼 있으니 뒤도 전부 마찬가지 — 여기서 끝낸다.
        if (candidate.boxDistance > bestDistance)
            break;

        const SceneObject& obj = *candidate.object;

        auto it = cache.find(obj.mesh);
        if (it == cache.end())
        {
            MeshData data;
            data.ok = obj.mesh->ReadBack(data.vertices, data.indices);
            it = cache.emplace(obj.mesh, std::move(data)).first;
        }

        if (!it->second.ok)
        {
            //정점을 못 읽으면 상자 판정으로 만족한다. 아무것도 못 고르는 것보단 낫다.
            if (candidate.boxDistance < bestDistance)
            {
                bestDistance = candidate.boxDistance;
                bestId = obj.id;
            }
            continue;
        }

        const std::vector<Vertex>& vertices = it->second.vertices;
        const std::vector<unsigned int>& indices = it->second.indices;

        const glm::mat4 model = obj.transform.GetMatrix();
        const Ray local = ToLocal(ray, glm::inverse(model));

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const unsigned int i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                continue;   //깨진 인덱스. 크래시 대신 조용히 넘긴다.

            float localT = 0.0f;
            if (!RayTriangle(local, vertices[i0].position, vertices[i1].position,
                vertices[i2].position, localT))
                continue;

            const float distance = WorldDistance(ray, local, localT, model);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestId = obj.id;
            }
        }
    }

    return bestId;
}
