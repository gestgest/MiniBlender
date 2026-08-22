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
        name = std::move(other.name);

        //원본은 빈 껍데기로 만들어야 소멸자에서 남의 버퍼를 지우지 않는다
        other.vao = other.vbo = other.ebo = 0;
        other.indexCount = 0;
    }
    return *this;
}

void Mesh::Release()
{
    if (vao != 0) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo != 0) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ebo != 0) { glDeleteBuffers(1, &ebo); ebo = 0; }
    indexCount = 0;
}

void Mesh::Upload(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
{
    Release();
    indexCount = (unsigned int)indices.size();

    //DSA(Direct State Access) 사용 — 4.5부터 가능.
    //예전 방식은 "바인딩해서 현재 대상을 바꾼 뒤 조작"이라 전역 상태에 의존하고 버그가 잘 났다.
    //DSA는 객체를 직접 지목해서 조작하니 바인딩 순서 실수가 원천 차단된다.
    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, vertices.size() * sizeof(Vertex), vertices.data(), 0);

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
