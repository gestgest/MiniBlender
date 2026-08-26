#include <Render/Mesh.h>

#include <glad/glad.h>

#include <cmath>
#include <cstddef>   //offsetof
#include <utility>   //std::move

Mesh::~Mesh()
{
    Release();
}

Mesh::Mesh(Mesh&& other) noexcept
{
    *this = std::move(other);
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        Release();
        vao = other.vao;
        vbo = other.vbo;
        ebo = other.ebo;
        indexCount = other.indexCount;
        vertexCount = other.vertexCount;
        instanceAttribsReady = other.instanceAttribsReady;
        name = std::move(other.name);
        boundsMin = other.boundsMin;
        boundsMax = other.boundsMax;

        //원본은 빈 껍데기로 만들어야 소멸자에서 남의 버퍼를 지우지 않는다
        other.vao = other.vbo = other.ebo = 0;
        other.indexCount = 0;
        other.vertexCount = 0;
        other.instanceAttribsReady = false;
    }
    return *this;
}

void Mesh::Release()
{
    if (vao != 0) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo != 0) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ebo != 0) { glDeleteBuffers(1, &ebo); ebo = 0; }
    indexCount = 0;
    vertexCount = 0;
    instanceAttribsReady = false;   //VAO를 지웠으니 형식 설정도 없던 일이 된다
}

void Mesh::Upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
{
    Release();
    indexCount = (unsigned int)indices.size();
    vertexCount = (unsigned int)vertices.size();

    //DSA(Direct State Access) 사용 — 4.5부터 가능.
    //예전 방식은 "바인딩해서 현재 대상을 바꾼 뒤 조작"이라 전역 상태에 의존하고 버그가 잘 났다.
    //DSA는 객체를 직접 지목해서 조작하니 바인딩 순서 실수가 원천 차단된다.
    glCreateBuffers(1, &vbo);

    //DYNAMIC_STORAGE_BIT: 나중에 glNamedBufferSubData로 내용을 고칠 수 있게 한다.
    //편집 모드에서 정점을 옮기려면 필수. 이 플래그 없이 만든 버퍼는 수정이 거부된다.
    glNamedBufferStorage(vbo, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &ebo);
    glNamedBufferStorage(ebo, indices.size() * sizeof(unsigned int), indices.data(), 0);

    glCreateVertexArrays(1, &vao);
    //버텍스 버퍼를 바인딩 슬롯 0에 연결 (stride = 버텍스 하나 크기)
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

    //attribute 0 = position
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(vao, 0, 0);

    //attribute 1 = normal
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(vao, 1, 0);

    RecomputeBounds(vertices);
}

void Mesh::BindInstanceBuffer(unsigned int instanceVBO, size_t byteOffset)
{
    if (vao == 0 || instanceVBO == 0)
        return;

    if (!instanceAttribsReady)
    {
        //mat4는 attribute 하나에 안 들어간다 — GL의 attribute 한 칸은 vec4가 최대라
        //열 4개를 location 2,3,4,5에 나눠 싣는다. 셰이더에서 `in mat4`로 받으면 알아서 다시 합쳐진다.
        for (unsigned int col = 0; col < 4; ++col)
        {
            const unsigned int loc = 2 + col;
            glEnableVertexArrayAttrib(vao, loc);
            glVertexArrayAttribFormat(vao, loc, 4, GL_FLOAT, GL_FALSE,
                (GLuint)(offsetof(InstanceData, model) + col * sizeof(glm::vec4)));
            glVertexArrayAttribBinding(vao, loc, 1);
        }

        //attribute 6 = 인스턴스 색
        glEnableVertexArrayAttrib(vao, 6);
        glVertexArrayAttribFormat(vao, 6, 3, GL_FLOAT, GL_FALSE, (GLuint)offsetof(InstanceData, color));
        glVertexArrayAttribBinding(vao, 6, 1);

        //이 한 줄이 인스턴싱의 스위치다.
        //바인딩 슬롯 1에서 읽는 attribute는 "정점마다"가 아니라 "인스턴스 1개마다" 한 칸씩 전진한다.
        //슬롯 단위로 거는 것(glVertexArrayBindingDivisor)이라 attribute 5개에 한 번만 걸면 된다.
        glVertexArrayBindingDivisor(vao, 1, 1);

        instanceAttribsReady = true;
    }

    //메시별 인스턴스 묶음은 한 버퍼 안에 이어 붙여 두고, 시작 오프셋만 바꿔가며 그린다.
    //(glDrawElementsInstancedBaseInstance로도 되지만, 이쪽이 드로우 인자에 상태를 안 섞어서 읽기 쉽다)
    glVertexArrayVertexBuffer(vao, 1, instanceVBO, (GLintptr)byteOffset, sizeof(InstanceData));
}


void Mesh::UpdateVertices(const std::vector<Vertex>& vertices)
{
    if (vbo == 0 || vertices.size() != vertexCount)
        return;

    glNamedBufferSubData(vbo, 0, (GLsizeiptr)(vertices.size() * sizeof(Vertex)), vertices.data());

    //정점을 옮겼으면 경계 상자도 따라 움직여야 한다.
    //안 그러면 편집한 정점이 상자 밖으로 나가서 그 부분을 클릭해도 안 잡힌다.
    RecomputeBounds(vertices);
}

//GPU에만 있는 정점 정보를 OBJ로 변환시킬때 바로 값을 되돌리는 것
bool Mesh::ReadBack(std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) const
{
    if (vbo == 0 || ebo == 0 || vertexCount == 0 || indexCount == 0)
        return false;

    outVertices.resize(vertexCount);
    outIndices.resize(indexCount);

    glGetNamedBufferSubData(vbo, 0, (GLsizeiptr)(vertexCount * sizeof(Vertex)), outVertices.data());
    glGetNamedBufferSubData(ebo, 0, (GLsizeiptr)(indexCount * sizeof(unsigned int)), outIndices.data());
    return true;
}

void Mesh::RecomputeBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty())
    {
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
        return;
    }

    boundsMin = vertices[0].position;
    boundsMax = vertices[0].position;
    for (const Vertex& v : vertices)
    {
        boundsMin = glm::min(boundsMin, v.position);
        boundsMax = glm::max(boundsMax, v.position);
    }
}






namespace Primitives
{

void MakeCube(std::vector<Vertex>& v, std::vector<unsigned int>& idx)
{
    v.clear();
    idx.clear();

    //면마다 노멀이 달라서 꼭짓점을 공유할 수 없다 (공유하면 노멀이 평균돼서 큐브가 둥글어 보임).
    //그래서 6면 x 4버텍스 = 24개.
    const glm::vec3 normals[6] = {
        { 0, 0, 1}, { 0, 0,-1}, {-1, 0, 0},
        { 1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
    };
    //각 면의 4개 꼭짓점 (반시계 방향 = 앞면)
    const glm::vec3 faces[6][4] = {
        {{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}, //+Z
        {{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}}, //-Z
        {{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}}, //-X
        {{ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}}, //+X
        {{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}}, //+Y
        {{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}}, //-Y
    };

    for (int f = 0; f < 6; ++f)
    {
        unsigned int base = (unsigned int)v.size();
        for (int i = 0; i < 4; ++i)
            v.push_back({ faces[f][i], normals[f] });

        idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base + 2); idx.push_back(base + 3); idx.push_back(base + 0);
    }
}

void MakePlane(std::vector<Vertex>& v, std::vector<unsigned int>& idx)
{
    v.clear();
    idx.clear();

    const glm::vec3 up(0, 1, 0);
    v.push_back({ {-0.5f, 0.0f,  0.5f}, up });
    v.push_back({ { 0.5f, 0.0f,  0.5f}, up });
    v.push_back({ { 0.5f, 0.0f, -0.5f}, up });
    v.push_back({ {-0.5f, 0.0f, -0.5f}, up });

    idx = { 0, 1, 2, 2, 3, 0 };
}

void MakeSphere(std::vector<Vertex>& v, std::vector<unsigned int>& idx, int segments, int rings)
{
    v.clear();
    idx.clear();

    const float PI = 3.14159265358979323846f;

    //UV 구: 위도(ring) x 경도(segment) 격자로 만든다.
    //구는 중심에서 밖으로 뻗는 방향이 곧 노멀이라 normal = normalize(position)으로 공짜.
    for (int r = 0; r <= rings; ++r)
    {
        float phi = PI * (float)r / (float)rings;          //0(북극) ~ PI(남극)
        float y = std::cos(phi) * 0.5f;
        float radius = std::sin(phi) * 0.5f;

        for (int s = 0; s <= segments; ++s)
        {
            float theta = 2.0f * PI * (float)s / (float)segments;
            glm::vec3 pos(radius * std::cos(theta), y, radius * std::sin(theta));
            v.push_back({ pos, glm::normalize(pos) });
        }
    }

    //격자 한 칸 = 사각형 = 삼각형 2개. 단 극점 쪽 한 칸은 삼각형 1개로 줄어든다(면적 0짜리 방지).
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            unsigned int a = r * (segments + 1) + s;
            unsigned int b = a + segments + 1;

            if (r != 0)
            {
                idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
            }
            if (r != rings - 1)
            {
                idx.push_back(a + 1); idx.push_back(b); idx.push_back(b + 1);
            }
        }
    }
}

void MakeCylinder(std::vector<Vertex>& v, std::vector<unsigned int>& idx, int segments)
{
    v.clear();
    idx.clear();

    const float PI = 3.14159265358979323846f;
    const float half = 0.5f;

    //옆면: 노멀이 바깥 방향(수평)
    for (int s = 0; s <= segments; ++s)
    {
        float theta = 2.0f * PI * (float)s / (float)segments;
        float x = std::cos(theta) * 0.5f;
        float z = std::sin(theta) * 0.5f;
        glm::vec3 n = glm::normalize(glm::vec3(x, 0.0f, z));

        v.push_back({ {x, -half, z}, n });
        v.push_back({ {x,  half, z}, n });
    }

    for (int s = 0; s < segments; ++s)
    {
        unsigned int a = s * 2;
        idx.push_back(a); idx.push_back(a + 2); idx.push_back(a + 1);
        idx.push_back(a + 1); idx.push_back(a + 2); idx.push_back(a + 3);
    }

    //뚜껑: 옆면과 노멀이 완전히 달라서(수직) 버텍스를 따로 만든다
    for (int cap = 0; cap < 2; ++cap)
    {
        float y = (cap == 0) ? half : -half;
        glm::vec3 n = (cap == 0) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);

        unsigned int center = (unsigned int)v.size();
        v.push_back({ {0.0f, y, 0.0f}, n });

        for (int s = 0; s <= segments; ++s)
        {
            float theta = 2.0f * PI * (float)s / (float)segments;
            v.push_back({ {std::cos(theta) * 0.5f, y, std::sin(theta) * 0.5f}, n });
        }

        for (int s = 0; s < segments; ++s)
        {
            //위/아래 뚜껑은 감는 방향이 반대여야 둘 다 바깥을 향한다
            if (cap == 0)
            {
                idx.push_back(center);
                idx.push_back(center + 1 + s);
                idx.push_back(center + 2 + s);
            }
            else
            {
                idx.push_back(center);
                idx.push_back(center + 2 + s);
                idx.push_back(center + 1 + s);
            }
        }
    }
}

} //namespace Primitives
