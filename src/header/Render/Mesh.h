#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
};

//GPU에 올라간 메시 하나. VAO/VBO/EBO를 소유한다.
//
//인덱스 버퍼를 쓰는 이유: 큐브를 36개 버텍스로 그리면 같은 꼭짓점이 여러 번 중복된다.
//인덱스를 쓰면 24개 버텍스 + 36개 인덱스가 되고, GPU의 버텍스 캐시가 재사용을 해준다.
//나중에 MultiDrawIndirect로 갈 때도 인덱스 기반이 기본이라 처음부터 이렇게 간다.
class Mesh
{
public:
    Mesh() = default;
    ~Mesh();

    //복사되면 소멸자에서 GPU 버퍼를 두 번 지우게 되니 막는다 (이동은 허용)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void Upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    void Release();

    unsigned int GetVAO() const { return vao; }
    unsigned int GetIndexCount() const { return indexCount; }
    unsigned int GetTriangleCount() const { return indexCount / 3; }
    const std::string& GetName() const { return name; }
    void SetName(const std::string& n) { name = n; }

private:
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    unsigned int indexCount = 0;
    std::string name = "Mesh";
};

//프리미티브 생성기. 블렌더의 Add > Mesh 메뉴에 해당하는 것들.
namespace Primitives
{
    void MakeCube(std::vector<Vertex>& outVerts, std::vector<unsigned int>& outIndices);
    void MakePlane(std::vector<Vertex>& outVerts, std::vector<unsigned int>& outIndices);
    //segments = 경도 분할, rings = 위도 분할. 값을 올리면 삼각형 수가 확 늘어나서 부하 테스트용으로도 쓴다.
    void MakeSphere(std::vector<Vertex>& outVerts, std::vector<unsigned int>& outIndices,
        int segments = 32, int rings = 16);
    void MakeCylinder(std::vector<Vertex>& outVerts, std::vector<unsigned int>& outIndices,
        int segments = 32);
}
