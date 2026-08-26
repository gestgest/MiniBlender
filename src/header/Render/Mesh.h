#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
};

//인스턴스 하나가 갖는 데이터 = "이 메시를 어디에 어떤 색으로 그릴 것인가".
//
//정점 데이터와 같은 VBO 방식으로 GPU에 올라가지만 읽는 속도가 다르다:
//  position/normal은 "정점마다" 다음 칸으로 넘어가고 (divisor 0),
//  이건 "인스턴스마다" 넘어간다 (divisor 1).
//즉 큐브의 정점 24개를 그리는 동안 model은 고정이고, 다음 큐브로 넘어갈 때 한 칸 전진한다.
struct InstanceData
{
    glm::mat4 model;
    glm::vec3 color;
    float _pad = 0.0f;   //stride를 16의 배수로 맞춰둔다 (나중에 UBO/SSBO로 옮길 때 그대로 쓰려고)
};



//GPU에 올라간 메시 하나. VAO/VBO/EBO를 소유한다.
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

    //(OBJ 변환에서 쓰인다.)
    //리버스 드로우콜 => GPU -> CPU 
    bool ReadBack(std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) const;

    //드래그 정점들을 옮겼다면 => 단체 속성 변경 (바로 GPU로)
    void UpdateVertices(const std::vector<Vertex>& vertices);
    
    //바인드
    void BindInstanceBuffer(unsigned int instanceVBO, size_t byteOffset);

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
    bool instanceAttribsReady = false;   //인스턴스 attribute 형식을 잡아둔 VAO인지
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
