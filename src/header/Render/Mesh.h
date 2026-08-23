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

    //GPU에 올라간 정점/인덱스를 다시 CPU로 읽어온다 (파일 내보내기용).
    //CPU 사본을 따로 들고 있지 않는 이유: 메시 하나를 오브젝트 수백 개가 공유하는 구조라
    //데이터를 이중으로 갖고 있을 이유가 없다. 내보내기는 어쩌다 한 번 하는 일이라 되읽기가 싸게 먹힌다.
    //(glNamedBufferStorage를 flags=0으로 만들었어도 읽기는 된다. 매핑이 아니라 복사라서)
    bool ReadBack(std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) const;

    //정점 데이터를 GPU에 다시 올린다 (편집 모드에서 정점을 옮길 때).
    //Upload에서 GL_DYNAMIC_STORAGE_BIT를 줬기 때문에 가능하다. 플래그 0으로 만든 불변 버퍼는
    //내용 수정이 아예 거부된다 (GL_INVALID_OPERATION).
    void UpdateVertices(const std::vector<Vertex>& vertices);

    //로컬 공간 경계 상자. 클릭 피킹의 1차 걸러내기에 쓴다.
    //Upload/UpdateVertices에서 같이 계산해 두는 이유: 정점은 이미 손에 있는데
    //나중에 필요할 때 GPU에서 되읽어 다시 훑으면 순전히 낭비다.
    const glm::vec3& GetBoundsMin() const { return boundsMin; }
    const glm::vec3& GetBoundsMax() const { return boundsMax; }

    unsigned int GetVAO() const { return vao; }
    unsigned int GetIndexCount() const { return indexCount; }
    unsigned int GetVertexCount() const { return vertexCount; }
    unsigned int GetTriangleCount() const { return indexCount / 3; }
    const std::string& GetName() const { return name; }
    void SetName(const std::string& n) { name = n; }

private:
    void RecomputeBounds(const std::vector<Vertex>& vertices);

    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    unsigned int indexCount = 0;
    unsigned int vertexCount = 0;
    std::string name = "Mesh";

    //빈 메시면 원점의 점 하나로 남는다. 광선이 거기를 스칠 수는 있지만
    //삼각형이 없어서 피킹 2단계에서 어차피 걸러진다.
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };
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
