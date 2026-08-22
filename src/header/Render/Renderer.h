#pragma once

#include <Render/FrameStats.h>

#include <glm/glm.hpp>

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

    //그리드 on/off (UI에서 토글)
    bool showGrid = true;

    glm::vec3 lightDirection{ -0.4f, -1.0f, -0.55f };
    glm::vec3 backgroundColor{ 0.13f, 0.13f, 0.15f };

private:
    Shader* objectShader = nullptr;
    Shader* gridShader = nullptr;

    //그리드는 버텍스 데이터가 없지만 VAO는 반드시 하나 바인딩돼 있어야 한다 (core 프로파일 규칙).
    //그래서 텅 빈 VAO를 하나 만들어 둔다.
    unsigned int emptyVAO = 0;
};
