#include <Scene/Scene.h>

#include <algorithm>

void Scene::InitDefaultMeshes()
{

    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;

    auto add = [&](const std::string& name)
    {
        auto mesh = std::make_unique<Mesh>();
        mesh->SetName(name);
        mesh->Upload(verts, indices);
        meshes.push_back(std::move(mesh));
        meshNames.push_back(name);
    };

    Primitives::MakeCube(verts, indices);      add("Cube");
    Primitives::MakePlane(verts, indices);     add("Plane");
    Primitives::MakeSphere(verts, indices);    add("Sphere");
    Primitives::MakeCylinder(verts, indices);  add("Cylinder");

    //고해상도 구 — 삼각형 부하를 늘려서 GPU 시간이 어떻게 변하는지 보려고 일부러 넣어둔다
    Primitives::MakeSphere(verts, indices, 128, 64);
    add("Sphere(고해상도)");
}

Mesh* Scene::GetMesh(const std::string& name)
{
    for (size_t i = 0; i < meshNames.size(); ++i)
    {
        if (meshNames[i] == name)
            return meshes[i].get();
    }
    return nullptr;
}

Mesh* Scene::AddMesh(const std::string& name, const std::vector<Vertex>& vertices,
    const std::vector<unsigned int>& indices)
{
    //이름 충돌 회피: 같은 파일을 두 번 불러도 서로 다른 메시로 남게 한다
    std::string unique = name;
    int suffix = 1;
    while (GetMesh(unique) != nullptr)
        unique = name + "." + std::to_string(suffix++);

    auto mesh = std::make_unique<Mesh>();
    mesh->SetName(unique);
    mesh->Upload(vertices, indices);

    Mesh* raw = mesh.get();
    meshes.push_back(std::move(mesh));
    meshNames.push_back(unique);
    return raw;
}

//주의: 반환 포인터는 다음 AddObject 전까지만 유효하다 (vector 재할당 때문).
//오래 들고 있어야 하면 포인터 대신 id를 저장할 것.
SceneObject* Scene::AddObject(const std::string& name, const std::string& meshName, const glm::vec3& position)
{
    SceneObject obj;
    obj.name = name;
    obj.mesh = GetMesh(meshName);
    obj.transform.position = position;
    obj.id = nextId++;

    objects.push_back(obj);
    return &objects.back();
}

void Scene::RemoveObject(unsigned int id)
{
    objects.erase(
        std::remove_if(objects.begin(), objects.end(),
            [id](const SceneObject& o) { return o.id == id; }),
        objects.end());
}

int Scene::RemovePlaceholders()
{
    const size_t before = objects.size();
    objects.erase(
        std::remove_if(objects.begin(), objects.end(),
            [](const SceneObject& o) { return o.isPlaceholder; }),
        objects.end());
    return (int)(before - objects.size());
}

SceneObject* Scene::FindById(unsigned int id)
{
    for (auto& o : objects)
    {
        if (o.id == id)
            return &o;
    }
    return nullptr;
}
