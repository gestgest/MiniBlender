#include <Render/Renderer.h>

#include <Render/Mesh.h>
#include <Render/OrbitCamera.h>
#include <Scene/Scene.h>

#include <glad/glad.h>
#include <header/shader.h>

#include <glm/gtc/matrix_transform.hpp>

void Renderer::Init()
{
    objectShader = new Shader("src/vs/basic.vs", "src/fs/basic.fs");
    gridShader = new Shader("src/vs/grid.vs", "src/fs/grid.fs");

    glCreateVertexArrays(1, &emptyVAO);
}

void Renderer::Shutdown()
{
    delete objectShader; objectShader = nullptr;
    delete gridShader;   gridShader = nullptr;

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
