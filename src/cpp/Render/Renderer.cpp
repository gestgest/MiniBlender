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
    const glm::mat4 mvp = camera.GetProjectionMatrix(aspect) * camera.GetViewMatrix()
        * obj->transform.GetMatrix();

    //깊이 테스트를 끈다: 정점은 면에 딱 붙어 있어서 z-파이팅으로 깜빡이고,
    //뒷면의 정점도 보여야 반대편을 편집할 수 있다(블렌더도 기본이 이렇다).
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);   //셰이더에서 gl_PointSize를 쓰려면 필요

    pointShader->use();
    pointShader->setMat4("mvp", mvp);

    //1) 전체 정점
    pointShader->setFloat("pointSize", 7.0f);
    pointShader->setVec3("pointColor", glm::vec3(0.15f, 0.55f, 1.0f));
    glBindVertexArray(edit.GetPointVAO());
    glDrawArrays(GL_POINTS, 0, edit.GetVertexCount());
    stats.AddDrawCall(0);

    //2) 선택된 정점만 크고 밝게 덧그린다
    if (edit.GetSelected() >= 0)
    {
        pointShader->setFloat("pointSize", 12.0f);
        pointShader->setVec3("pointColor", glm::vec3(1.0f, 0.65f, 0.1f));
        glDrawArrays(GL_POINTS, edit.GetSelected(), 1);
        stats.AddDrawCall(0);
    }

    glDisable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}
