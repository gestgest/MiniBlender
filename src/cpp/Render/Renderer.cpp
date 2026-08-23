#include <Render/Renderer.h>

#include <Render/Mesh.h>
#include <Render/OrbitCamera.h>
#include <Edit/EditMode.h>
#include <Scene/Scene.h>

#include <glad/glad.h>
#include <header/shader.h>

#include <glm/gtc/matrix_transform.hpp>

void Renderer::Init()
{
    objectShader = new Shader("src/vs/basic.vs", "src/fs/basic.fs");
    gridShader = new Shader("src/vs/grid.vs", "src/fs/grid.fs");
    pointShader = new Shader("src/vs/point.vs", "src/fs/point.fs");

    glCreateVertexArrays(1, &emptyVAO);
}

void Renderer::Shutdown()
{
    delete objectShader; objectShader = nullptr;
    delete gridShader;   gridShader = nullptr;
    delete pointShader;  pointShader = nullptr;

    if (emptyVAO != 0)
    {
        glDeleteVertexArrays(1, &emptyVAO);
        emptyVAO = 0;
    }
}

void Renderer::RenderScene(Scene& scene, const OrbitCamera& camera, int width, int height, FrameStats& stats)
{
    glViewport(0, 0, width, height);
    glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);   //뒷면은 어차피 안 보이니 래스터화 자체를 건너뛴다 (공짜 최적화)
    glCullFace(GL_BACK);

    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * view;

    //--- 1패스: 불투명 오브젝트 ---
    //지금은 오브젝트 하나에 드로우콜 하나 = 가장 순진한 방식.
    //이게 기준선(baseline)이고, 앞으로 이 숫자를 어떻게 줄이는지가 이 프로젝트의 본론.
    objectShader->use();
    objectShader->setMat4("view", view);
    objectShader->setMat4("projection", proj);
    objectShader->setVec3("lightDir", glm::normalize(lightDirection));

    for (const SceneObject& obj : scene.GetObjects())
    {
        if (!obj.visible || obj.mesh == nullptr)
            continue;

        objectShader->setMat4("model", obj.transform.GetMatrix());
        objectShader->setVec3("objectColor", obj.color);

        glBindVertexArray(obj.mesh->GetVAO());
        glDrawElements(GL_TRIANGLES, obj.mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);

        stats.AddDrawCall(obj.mesh->GetTriangleCount());
    }

    //--- 2패스: 무한 그리드 ---
    //반투명이라 불투명 오브젝트를 다 그린 뒤에 그린다.
    //컬링을 꺼야 하는 이유: 풀스크린 삼각형은 감는 방향이 정해져 있지 않아서 컬링되면 통째로 사라진다.
    if (showGrid)
    {
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        //깊이 쓰기를 끈다: 그리드가 깊이 버퍼를 오염시키면 나중에 그릴 것들이 엉뚱하게 가려진다.
        //읽기는 켜둬야 오브젝트 뒤의 그리드가 제대로 숨는다.
        glDepthMask(GL_FALSE);

        gridShader->use();
        gridShader->setMat4("invViewProj", glm::inverse(viewProj));
        gridShader->setMat4("viewProj", viewProj);
        gridShader->setVec3("camPos", camera.GetPosition());
        gridShader->setFloat("farPlane", camera.GetFar());

        glBindVertexArray(emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);   //버텍스 3개로 화면 전체 = 드로우콜 1개
        stats.AddDrawCall(1);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }

    glBindVertexArray(0);
}

void Renderer::RenderEditPoints(const EditMode& edit, const Scene& scene,
    const OrbitCamera& camera, int width, int height, FrameStats& stats)
{
    if (!edit.IsActive() || edit.GetVertexCount() == 0)
        return;

    const SceneObject* obj = nullptr;
    for (const SceneObject& o : scene.GetObjects())
    {
        if (o.id == edit.GetObjectId()) { obj = &o; break; }
    }
    if (obj == nullptr)
        return;

    const float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    const glm::mat4 projection = camera.GetProjectionMatrix(aspect);
    const glm::mat4 modelView = camera.GetViewMatrix() * obj->transform.GetMatrix();

    glEnable(GL_PROGRAM_POINT_SIZE);   //셰이더에서 gl_PointSize를 쓰려면 필요

    pointShader->use();
    pointShader->setMat4("modelView", modelView);
    pointShader->setMat4("projection", projection);

    if (edit.IsXRay())
    {
        //X-Ray: 깊이 테스트를 끄면 메시를 통과해서 뒤쪽 정점까지 전부 보인다.
        //반대편을 편집할 때 필요하지만, 앞뒤가 겹쳐 보여서 어느 게 앞인지 알 수 없다.
        glDisable(GL_DEPTH_TEST);
        pointShader->setFloat("depthNudge", 0.0f);
    }
    else
    {
        //X-Ray 끔: 면에 가려진 정점은 GPU가 알아서 버린다 — 보이는 것만 만지게 된다.
        //  LEQUAL + 뷰 공간 밀어내기: 정점은 면 위에 정확히 얹혀 있어서 그냥 켜면
        //  자기가 붙은 면과 깊이가 같아 깜빡인다. 카메라 쪽으로 아주 조금 당겨 확실히 이기게 한다.
        //  밀어내는 양을 카메라 거리에 비례시키는 이유: 모델 크기가 0.01이든 1000이든
        //  화면에서 차지하는 크기는 비슷해서, 화면 기준으로 일정한 양이 되어야 한다.
        //  깊이 쓰기는 끈다. 점이 깊이 버퍼를 오염시키면 뒤에 그릴 것들이 엉뚱하게 가려진다.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        //피킹의 가림 판정과 반드시 같은 값이어야 한다. 어긋나면 화면엔 보이는데
        //클릭은 안 먹는 정점이 생긴다 — 그래서 상수를 EditMode 한 군데에 두고 같이 쓴다.
        pointShader->setFloat("depthNudge", EditMode::SurfaceBias(camera.GetDistance()));
    }

    //1) 전체 정점
    pointShader->setFloat("pointSize", 7.0f);
    pointShader->setVec3("pointColor", glm::vec3(0.15f, 0.55f, 1.0f));
    glBindVertexArray(edit.GetPointVAO());
    glDrawArrays(GL_POINTS, 0, edit.GetVertexCount());
    stats.AddDrawCall(0);

    //2) 선택된 정점을 크고 밝게 덧그린다. 선택된 것만 모아둔 버퍼가 따로 있어서
    //   개수와 상관없이 드로우콜 하나로 끝난다.
    //   이건 X-Ray와 무관하게 항상 깊이 테스트 없이 그린다 —
    //   시점을 돌리다 선택한 정점이 메시 뒤로 넘어가면 화면에서 사라지는데,
    //   드래그는 여전히 먹어서 "내가 뭘 잡고 있는지" 알 수 없어진다.
    if (edit.GetSelectedCount() > 0)
    {
        glDisable(GL_DEPTH_TEST);
        pointShader->setFloat("depthNudge", 0.0f);
        pointShader->setFloat("pointSize", 12.0f);
        pointShader->setVec3("pointColor", glm::vec3(1.0f, 0.65f, 0.1f));
        glBindVertexArray(edit.GetSelectedVAO());
        glDrawArrays(GL_POINTS, 0, edit.GetSelectedCount());
        stats.AddDrawCall(0);
    }

    //다음 패스가 기본 상태를 기대하므로 되돌려 놓는다
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}
