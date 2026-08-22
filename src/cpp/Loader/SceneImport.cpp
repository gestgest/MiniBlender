#include <Loader/SceneImport.h>

#include <Loader/FbxLoader.h>
#include <Render/OrbitCamera.h>
#include <Scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
    //경로에서 파일 이름만 뽑는다 (구분자는 / 와 \ 둘 다 올 수 있다)
    std::string FileNameOf(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
            name = name.substr(0, dot);

        return name.empty() ? std::string("Imported") : name;
    }
}

ImportReport ImportFbxIntoScene(Scene& scene, OrbitCamera& camera, const std::string& path,
    bool frameCamera)
{
    ImportReport report;

    LoadResult loaded = FbxLoader::Load(path);
    if (!loaded.ok)
    {
        report.message = "불러오기 실패: " + loaded.error;
        return report;
    }

    const std::string base = FileNameOf(path);

    //불러오기가 성공한 시점에 예시용 큐브를 치운다.
    //실패했을 때 치우면 화면이 텅 비어서 더 혼란스러우니 반드시 성공 후에.
    scene.RemovePlaceholders();

    for (LoadedMesh& lm : loaded.meshes)
    {
        //메시 이름은 "파일명:노드명" 형태로. 아웃라이너에서 어느 파일에서 왔는지 바로 보이게.
        const std::string meshName = base + ":" + lm.name;

        Mesh* mesh = scene.AddMesh(meshName, lm.vertices, lm.indices);
        if (mesh == nullptr)
            continue;

        //정점을 이미 월드 공간으로 구워서 넣었으므로 오브젝트 위치는 원점 그대로 둔다
        scene.AddObject(mesh->GetName(), mesh->GetName(), glm::vec3(0.0f));
        ++report.meshCount;
    }

    if (report.meshCount == 0)
    {
        report.message = "메시를 하나도 추가하지 못했다";
        return report;
    }

    //--- 카메라를 모델에 맞춘다 ---
    {
        const glm::vec3 center = (loaded.boundsMin + loaded.boundsMax) * 0.5f;
        const glm::vec3 size = loaded.boundsMax - loaded.boundsMin;

        //경계 상자를 감싸는 "구"의 반지름 = 대각선의 절반.
        //가장 긴 변만 쓰면 안 된다 — 상자를 비스듬히 보면 대각선이 화면에 걸리기 때문에
        //변 기준으로 거리를 잡으면 모서리가 잘린다.
        const float radius = glm::length(size) * 0.5f;

        //시야각에서 거리를 역산한다. 반지름 r인 구가 시야에 딱 들어오는 거리는 r / sin(fov/2).
        //(수직 시야각 기준 — 화면이 가로로 넓으니 세로가 항상 더 빡빡하다)
        const float halfFov = glm::radians(camera.GetFov()) * 0.5f;
        const float fitDistance = (radius > 0.0f) ? radius / std::sin(halfFov) : 1.0f;
        //1.3배 여유. 딱 맞추면 화면에 꽉 차서 답답하다.
        const float framedDistance = std::max(fitDistance * 1.3f, camera.GetNear() * 4.0f);

        //임포트가 이상할 때 제일 먼저 봐야 하는 값이라 콘솔에 남긴다
        //(모델이 안 보이면 십중팔구 단위/축 변환 문제라 크기와 중심만 보면 원인이 잡힌다)
        std::cout << "[가져오기] 크기 " << size.x << " x " << size.y << " x " << size.z
                  << ", 중심 (" << center.x << ", " << center.y << ", " << center.z << ")"
                  << ", 카메라 거리 " << framedDistance << std::endl;

        if (frameCamera)
        {

            camera.FocusOn(center);
            camera.SetDistance(framedDistance);
        }
    }

    report.ok = true;
    report.triangles = loaded.totalTriangles;
    report.message = base + " — 메시 " + std::to_string(report.meshCount) + "개, 삼각형 "
        + std::to_string(loaded.totalTriangles) + "개";
    return report;
}
