#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>

class Mesh;

//블렌더처럼 위치/회전/스케일을 따로 들고 있다가 필요할 때 행렬로 굽는다.
//행렬을 직접 들고 있지 않는 이유: UI에서 "Y 회전 45도"를 보여주고 편집하려면 원본 값이 있어야 한다.
//행렬만 있으면 거기서 오일러 각을 역산해야 하는데, 짐벌 상황에서 값이 튄다.
struct Transform
{
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f };   //오일러 각(도 단위). 라디안 대신 도를 쓰는 건 순전히 UI 편의
    glm::vec3 scale{ 1.0f };

    glm::mat4 GetMatrix() const
    {
        glm::mat4 m(1.0f);
        //적용 순서: 스케일 -> 회전 -> 이동. 행렬 곱은 오른쪽부터 적용되니 코드는 역순으로 쓴다.
        m = glm::translate(m, position);
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        m = glm::scale(m, scale);
        return m;
    }
};

//씬에 놓인 오브젝트 하나. 메시는 여러 오브젝트가 공유하니까 소유하지 않고 포인터만 본다
//(큐브 1000개를 놔도 메시 데이터는 GPU에 하나만 올라감 — 나중에 인스턴싱의 전제가 되는 구조).
struct SceneObject
{
    std::string name = "Object";
    Transform transform;
    Mesh* mesh = nullptr;
    glm::vec3 color{ 0.8f, 0.8f, 0.8f };
    bool visible = true;

    //피킹용 ID. 0은 "빈 공간"으로 예약해서 1부터 매긴다
    unsigned int id = 0;
};
