#pragma once

#include <Render/Mesh.h>
#include <Scene/SceneObject.h>

#include <memory>
#include <string>
#include <vector>

//메시 라이브러리 + 오브젝트 목록.
//메시를 이름으로 등록해두고 오브젝트는 포인터만 가리키게 해서, 같은 메시를 몇 번을 써도
//GPU 버퍼는 하나만 존재하게 만든다. 이게 나중에 인스턴싱/배칭으로 갈 때의 기반이 된다.
class Scene
{
public:
    void InitDefaultMeshes();

    Mesh* GetMesh(const std::string& name);

    //런타임에 만들어진 메시(FBX 임포트 등)를 라이브러리에 등록한다.
    //같은 이름이 이미 있으면 뒤에 번호를 붙여 유일하게 만든다.
    Mesh* AddMesh(const std::string& name, const std::vector<Vertex>& vertices,
        const std::vector<unsigned int>& indices);

    //메시 이름으로 오브젝트 추가. 반환값은 추가된 오브젝트 포인터
    SceneObject* AddObject(const std::string& name, const std::string& meshName,
        const glm::vec3& position = glm::vec3(0.0f));

    void RemoveObject(unsigned int id);
    //예시용 오브젝트를 모두 제거한다. 반환값은 지운 개수
    int RemovePlaceholders();
    SceneObject* FindById(unsigned int id);

    std::vector<SceneObject>& GetObjects() { return objects; }
    const std::vector<SceneObject>& GetObjects() const { return objects; }

    const std::vector<std::string>& GetMeshNames() const { return meshNames; }

private:
    //vector<Mesh>로 하면 재할당 때 Mesh가 이동하면서 포인터가 무효화된다. unique_ptr로 주소를 고정.
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<std::string> meshNames;

    std::vector<SceneObject> objects;
    unsigned int nextId = 1;   //0은 "선택 없음"으로 예약
};
