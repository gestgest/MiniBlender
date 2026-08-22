#include <Render/Benchmark.h>

#include <Render/FrameStats.h>
#include <Render/OrbitCamera.h>
#include <Scene/Scene.h>

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
    //argv는 콘솔 코드페이지(CP949)로 들어와서 UTF-8 소스의 한글 메시 이름과 안 맞는다.
    //그냥 넘기면 GetMesh가 nullptr을 돌려주고 오브젝트가 0개 추가된 채 "조용히" 측정이 끝난다.
    //(실제로 여기 한 번 걸려서 tris=13이 나왔다) 그래서 ASCII 별칭을 둔다.
    const char* ResolveMeshName(const std::string& alias)
    {
        if (alias == "HiSphere")
            return "Sphere(고해상도)";
        return nullptr;   //별칭 아님 = 그대로 사용
    }
}

void Benchmark::ParseArgs(int argc, char** argv)
{
    if (argc < 2)
        return;

    //첫 인자가 숫자가 아니면 사용법만 알리고 평소 모드로 진행
    char* end = nullptr;
    long n = std::strtol(argv[1], &end, 10);
    if (end == argv[1] || n < 0)
    {
        std::cout << "사용법: MiniBlender.exe <개수> [메시이름] [워밍업프레임] [샘플프레임]\n"
                  << "  메시이름: Cube(기본) | Plane | Sphere | Cylinder | HiSphere\n";
        return;
    }

    active = true;
    count = (int)n;

    if (argc > 2) meshName = argv[2];
    if (argc > 3) warmupFrames = std::atoi(argv[3]);
    if (argc > 4) sampleFrames = std::atoi(argv[4]);

    if (sampleFrames < 1) sampleFrames = 1;
    if (warmupFrames < 0) warmupFrames = 0;
}

bool Benchmark::Init(GLFWwindow* window, Scene& scene, OrbitCamera& camera)
{
    if (!active)
        return true;

    //vsync를 꺼야 실제 부하가 보인다. 켜져 있으면 3ms짜리 프레임과 12ms짜리 프레임이
    //똑같이 모니터 주사율에 맞춰져서 구분이 안 된다.
    glfwSwapInterval(0);

    const char* resolved = ResolveMeshName(meshName);
    const std::string actualMesh = resolved ? resolved : meshName;

    if (scene.GetMesh(actualMesh) == nullptr)
    {
        std::cout << "[벤치] 메시를 찾을 수 없음: " << meshName << "\n사용 가능한 메시: ";
        for (const std::string& name : scene.GetMeshNames())
            std::cout << name << " ";
        std::cout << "\n(한글 이름은 콘솔 인코딩 때문에 인자로 못 넘긴다. 별칭 HiSphere를 쓸 것)\n";
        return false;
    }

    //정사각 격자로 배치
    if (count > 0)
    {
        const float spacing = 1.5f;
        const int side = (int)std::ceil(std::sqrt((double)count));
        const float spread = side * spacing;

        for (int i = 0; i < count; ++i)
        {
            const float x = (float)(i % side) * spacing - spread * 0.5f;
            const float z = (float)(i / side) * spacing - spread * 0.5f;
            scene.AddObject(actualMesh, actualMesh, glm::vec3(x, 0.5f, z));
        }

        //전체가 화면에 들어와야 GPU 부하도 같이 측정된다.
        //(화면 밖으로 나가면 래스터화가 생략돼서 GPU 시간만 실제보다 낮게 나온다)
        camera.SetDistance(spread * 1.1f + 8.0f);
    }

    std::cout << "[벤치] " << meshName << " x" << count
              << " (워밍업 " << warmupFrames << "프레임, 샘플 " << sampleFrames << "프레임)\n";
    return true;
}

bool Benchmark::Tick(const FrameStats& stats)
{
    if (!active)
        return false;

    ++frame;
    if (frame <= warmupFrames)
        return false;

    accCpu += stats.GetCpuMs();
    accGpu += stats.GetGpuMs();
    accFrame += stats.GetFrameMs();

    if (frame < warmupFrames + sampleFrames)
        return false;

    const double cpu = accCpu / sampleFrames;
    const double gpu = accGpu / sampleFrames;
    const double frameMs = accFrame / sampleFrames;

    //파싱하기 쉽게 key=value 한 줄로. 여러 조건을 스크립트로 돌려 표를 만들 때 편하다.
    std::cout << "RESULT"
              << " mesh=" << meshName
              << " n=" << count
              << " calls=" << stats.GetDrawCalls()
              << " tris=" << stats.GetTriangles()
              << " cpu=" << cpu
              << " gpu=" << gpu
              << " frame=" << frameMs
              << " fps=" << (frameMs > 0.0 ? 1000.0 / frameMs : 0.0)
              << std::endl;

    return true;
}
