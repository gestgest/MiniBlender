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
    //프래그먼트 셰이더는 같은 것을 쓴다 — 바뀌는 건 "정점을 어떻게 배치하는가"뿐이라
    //픽셀을 칠하는 비용은 두 경로가 완전히 동일하다. 비교할 때 이게 중요하다.
    instancedShader = new Shader("src/vs/basic_instanced.vs", "src/fs/basic.fs");
    gridShader = new Shader("src/vs/grid.vs", "src/fs/grid.fs");
    pointShader = new Shader("src/vs/point.vs", "src/fs/point.fs");
    gizmoShader = new Shader("src/vs/gizmo.vs", "src/fs/gizmo.fs");
    faceHighlightShader = new Shader("src/vs/facehighlight.vs", "src/fs/facehighlight.fs");

    glCreateVertexArrays(1, &emptyVAO);
    gizmo.Init();
}

void Renderer::Shutdown()
{
    delete objectShader; objectShader = nullptr;
    delete instancedShader; instancedShader = nullptr;
    delete gridShader;   gridShader = nullptr;
    delete pointShader;  pointShader = nullptr;
    delete gizmoShader;  gizmoShader = nullptr;
    delete faceHighlightShader; faceHighlightShader = nullptr;
    gizmo.Shutdown();

    if (emptyVAO != 0)
    {
        glDeleteVertexArrays(1, &emptyVAO);
        emptyVAO = 0;
    }

    if (instanceVBO != 0)
    {
        glDeleteBuffers(1, &instanceVBO);
        instanceVBO = 0;
        instanceBufferBytes = 0;
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
    Shader* shader = useInstancing ? instancedShader : objectShader;
    shader->use();
    shader->setMat4("view", view);
    shader->setMat4("projection", proj);
    shader->setVec3("lightDir", glm::normalize(lightDirection));

    if (useInstancing)
        DrawObjectsInstanced(scene, stats);
    else
        DrawObjectsNaive(scene, stats);

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

void Renderer::DrawObjectsNaive(Scene& scene, FrameStats& stats)
{
    //오브젝트 하나에 드로우콜 하나 = 가장 순진한 방식. 이게 기준선(baseline)이다.
    //오브젝트마다 uniform 두 개를 밀어넣고 VAO를 다시 바인딩하는데,
    //큐브 10000개면 이 세 줄이 10000번 반복된다 — 그리는 삼각형은 12개뿐인데도.
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
}

void Renderer::DrawObjectsInstanced(Scene& scene, FrameStats& stats)
{
    //1) 같은 메시를 쓰는 오브젝트끼리 모은다.
    //   Scene이 처음부터 메시를 포인터로 공유하게 돼 있어서(오브젝트 1000개 = 메시 1개),
    //   포인터 비교만으로 묶인다. 메시 종류는 보통 한 자릿수라 선형 탐색으로 충분하다.
    instanceGroups.clear();
    instanceScratch.clear();

    for (const SceneObject& obj : scene.GetObjects())
    {
        if (!obj.visible || obj.mesh == nullptr)
            continue;

        InstanceGroup* group = nullptr;
        for (InstanceGroup& candidate : instanceGroups)
        {
            if (candidate.mesh == obj.mesh) { group = &candidate; break; }
        }

        if (group == nullptr)
        {
            instanceGroups.push_back(InstanceGroup{ obj.mesh, instanceScratch.size(), 0 });
            group = &instanceGroups.back();
        }

        //묶음이 버퍼 안에서 이어져 있어야 오프셋 하나로 그릴 수 있다.
        //섞인 순서로 들어오면 아래 삽입이 O(n)이 되지만, 실제로는 같은 메시가 연달아 들어오는 게
        //대부분이라(가져온 모델 하나 = 메시 하나) 뒤에 붙이는 경로만 타게 된다.
        const InstanceData data{ obj.transform.GetMatrix(), obj.color, 0.0f };
        instanceScratch.insert(instanceScratch.begin() + (group->first + group->count), data);
        ++group->count;

        //내 뒤에 있던 묶음들은 한 칸씩 밀렸다
        for (InstanceGroup& other : instanceGroups)
        {
            if (other.first > group->first)
                ++other.first;
        }
    }

    if (instanceScratch.empty())
        return;

    //2) 전부 한 버퍼에 통째로 올린다. 묶음마다 따로 올리면 업로드 횟수가 메시 종류만큼 늘어난다.
    if (instanceVBO == 0)
        glCreateBuffers(1, &instanceVBO);

    const size_t bytes = instanceScratch.size() * sizeof(InstanceData);
    if (bytes > instanceBufferBytes)
    {
        //필요한 만큼만 잡으면 오브젝트가 하나 늘 때마다 재할당이 일어난다. 여유를 두고 잡는다.
        instanceBufferBytes = bytes + bytes / 2;
        glNamedBufferData(instanceVBO, (GLsizeiptr)instanceBufferBytes, nullptr, GL_STREAM_DRAW);
    }
    glNamedBufferSubData(instanceVBO, 0, (GLsizeiptr)bytes, instanceScratch.data());

    //3) 메시 하나당 드로우콜 하나.
    for (const InstanceGroup& group : instanceGroups)
    {
        if (group.count == 0)
            continue;

        group.mesh->BindInstanceBuffer(instanceVBO, group.first * sizeof(InstanceData));

        glBindVertexArray(group.mesh->GetVAO());

        //씬 인스턴스
        glDrawElementsInstanced(GL_TRIANGLES, group.mesh->GetIndexCount(),
            GL_UNSIGNED_INT, nullptr, (GLsizei)group.count);

        //삼각형 수는 인스턴스 수만큼 곱해서 센다 — GPU가 실제로 처리하는 양은 그대로다.
        //줄어드는 건 "그려라"라고 말하는 횟수뿐이라는 게 이 표에서 바로 보여야 한다.
        stats.AddDrawCall(group.mesh->GetTriangleCount() * (unsigned int)group.count);
    }
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

void Renderer::RenderGizmo(const EditMode& edit, const Scene& scene,
    const OrbitCamera& camera, int width, int height, FrameStats& stats)
{
    if (!edit.IsActive() || edit.GetSelectedCount() <= 0)
        return;

    const SceneObject* obj = nullptr;
    for (const SceneObject& o : scene.GetObjects())
    {
        if (o.id == edit.GetObjectId()) { obj = &o; break; }
    }
    if (obj == nullptr)
        return;

    const glm::mat4 model = obj->transform.GetMatrix();

    glm::vec3 worldOrigin;
    float armLength;
    if (!edit.GetGizmoPlacement(model, camera, worldOrigin, armLength))
        return;

    GizmoBuilder::BuildTranslateGizmo(worldOrigin, armLength, edit.GetHighlightedGizmoAxis(),
        gizmoLineScratch, gizmoTriScratch);
    gizmo.Upload(gizmoLineScratch, gizmoTriScratch);

    const float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    const glm::mat4 viewProj = camera.GetProjectionMatrix(aspect) * camera.GetViewMatrix();

    //항상 또렷하게 위에 보여야 잡기 쉽다 — 깊이 테스트/컬링을 끄고 그린다.
    //(화살촉 옆면/밑면을 감는 방향 신경 안 쓰고 만든 것도 컬링을 끈다는 전제 때문이다)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    gizmoShader->use();
    gizmoShader->setMat4("viewProj", viewProj);

    glBindVertexArray(gizmo.GetVAO());

    if (gizmo.GetLineCount() > 0)
    {
        glLineWidth(3.0f);   //코어 프로파일 드라이버에 따라 무시될 수 있지만 해될 건 없다
        glDrawArrays(GL_LINES, 0, (GLsizei)gizmo.GetLineCount());
        stats.AddDrawCall(0);
    }
    if (gizmo.GetTriVertexCount() > 0)
    {
        glDrawArrays(GL_TRIANGLES, (GLint)gizmo.GetLineCount(), (GLsizei)gizmo.GetTriVertexCount());
        stats.AddDrawCall(0);
    }

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderFaceHighlight(const EditMode& edit, const Scene& scene,
    const OrbitCamera& camera, int width, int height, FrameStats& stats)
{
    if (!edit.IsActive() || edit.GetFaceVertexCount() <= 0)
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

    faceHighlightShader->use();
    faceHighlightShader->setMat4("modelView", modelView);
    faceHighlightShader->setMat4("projection", projection);

    //RenderEditPoints와 같은 원칙: X-Ray면 깊이 테스트 없이(뒷면도 보임), 아니면 살짝 당겨서
    //자기가 얹힌 면과 z-파이팅 없이 이긴다. 같은 SurfaceBias를 써서 점/기즈모 판정과도 어긋나지 않는다.
    if (edit.IsXRay())
    {
        glDisable(GL_DEPTH_TEST);
        faceHighlightShader->setFloat("depthNudge", 0.0f);
    }
    else
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        faceHighlightShader->setFloat("depthNudge", EditMode::SurfaceBias(camera.GetDistance()));
    }

    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);   //반투명 칠이라 뒷면도 보여야 자연스럽다
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(edit.GetFaceVAO());
    glDrawArrays(GL_TRIANGLES, 0, edit.GetFaceVertexCount());
    stats.AddDrawCall(0);

    //다음 패스가 기본 상태를 기대하므로 되돌려 놓는다
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
