#pragma once

#include <string>

struct GLFWwindow;
class Scene;
class OrbitCamera;
class FrameStats;

//측정 모드. 인자 없이 실행하면 전부 no-op이라 평소 동작에는 아무 영향이 없다.
//
//왜 따로 만들었나:
//창 제목의 숫자는 눈으로 읽는 순간값이라 비교에 못 쓴다. vsync에 걸려 프레임이 3ms든 12ms든
//똑같이 75FPS로 보이고, ImGui 렌더링 비용도 섞여 있고, 조건을 바꾸려면 소스를 고쳐야 한다.
//최적화 기법을 넣을 때마다 "같은 조건으로" before/after를 재려면 이게 필요하다.
//
//사용법:
//    MiniBlender.exe <개수> [메시이름] [워밍업프레임] [샘플프레임] [--no-instancing]
//    예) MiniBlender.exe 10000 Cube
//        MiniBlender.exe 64 HiSphere 120 300
//        MiniBlender.exe 10000 Cube --no-instancing   (인스턴싱 넣기 전의 기준선)
//
//출력(stdout 한 줄) 후 자동 종료:
//    RESULT mesh=Cube n=10000 inst=on calls=2 tris=120001 cpu=0.412 gpu=1.655 frame=1.702 fps=587.5
class Benchmark
{
public:
    void ParseArgs(int argc, char** argv);

    bool IsActive() const { return active; }

    //vsync 끄기 + 씬 구성 + 카메라를 전체가 보이는 거리로. 측정 모드가 아니면 아무것도 안 한다.
    //false를 돌려주면 인자가 잘못된 것이니 바로 종료해야 한다.
    //(일반 모드로 흘려보내면 창이 뜬 채 안 닫혀서, 스크립트로 여러 조건을 돌릴 때 거기서 멈춘다)
    bool Init(GLFWwindow* window, Scene& scene, OrbitCamera& camera);

    //매 프레임 호출. 측정이 끝나면 true를 돌려준다(그때 결과를 출력한 상태).
    bool Tick(const FrameStats& stats);

    //측정 중에는 UI를 그리지 않는다. ImGui 비용이 숫자에 섞이면 안 되니까.
    bool ShouldSkipUI() const { return active; }

    //인스턴싱 on/off. 측정 모드가 아니면 항상 true라 평소 동작(켜짐)을 안 건드린다.
    bool UseInstancing() const { return useInstancing; }

private:
    bool active = false;
    bool useInstancing = true;
    int count = 0;
    std::string meshName = "Cube";

    //워밍업이 필요한 이유: 초반 몇 프레임은 셰이더 컴파일, 버퍼 업로드, 드라이버 캐시 워밍 때문에
    //값이 크게 튄다. 그걸 평균에 넣으면 비교가 무의미해진다.
    int warmupFrames = 120;
    int sampleFrames = 300;

    int frame = 0;
    double accCpu = 0.0;
    double accGpu = 0.0;
    double accFrame = 0.0;
};
