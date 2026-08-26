#include <Render/Gizmo.h>

#include <glad/glad.h>

#include <cmath>
#include <cstddef>   //offsetof

namespace
{
    constexpr float PI = 3.14159265358979323846f;

    //축 하나(화살대+화살촉)를 만들어 두 출력 목록에 이어 붙인다.
    void AppendArrow(const glm::vec3& origin, GizmoAxis axis, float armLength, bool isHighlighted,
        std::vector<GizmoVertex>& outLines, std::vector<GizmoVertex>& outTris)
    {
        const glm::vec3 dir = GizmoAxisDirection(axis);
        glm::vec3 color = GizmoAxisColor(axis);
        if (isHighlighted)
            color = glm::mix(color, glm::vec3(1.0f), 0.6f);   //강조: 흰색 쪽으로 섞어 밝힌다

        const float shaftLen = armLength * 0.72f;
        const float headRadius = armLength * 0.05f;

        const glm::vec3 shaftEnd = origin + dir * shaftLen;
        const glm::vec3 tip = origin + dir * armLength;

        //화살대 — 얇지만 색이 뚜렷해서 "이 방향"이라는 건 항상 보인다
        outLines.push_back({ origin, color });
        outLines.push_back({ shaftEnd, color });

        //화살촉(뿔) — dir에 수직인 임의의 두 축(u, v)을 잡아 밑면 링을 두른다.
        //dir이 거의 (0,1,0)이면 기준 벡터로 (0,1,0)을 쓰면 외적이 0에 가까워지므로 (1,0,0)으로 바꾼다.
        const glm::vec3 arbitrary = (std::fabs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(arbitrary, dir));
        const glm::vec3 v = glm::cross(dir, u);

        const int segs = 10;
        glm::vec3 ring[segs];
        for (int i = 0; i < segs; ++i)
        {
            const float theta = 2.0f * PI * (float)i / (float)segs;
            ring[i] = shaftEnd + (u * std::cos(theta) + v * std::sin(theta)) * headRadius;
        }

        //기즈모는 컬링을 끄고 그리므로(항상 양면 보이게) 감는 방향은 신경 쓰지 않는다.
        for (int i = 0; i < segs; ++i)
        {
            const glm::vec3& a = ring[i];
            const glm::vec3& b = ring[(i + 1) % segs];

            outTris.push_back({ tip, color });
            outTris.push_back({ a, color });
            outTris.push_back({ b, color });

            outTris.push_back({ shaftEnd, color });
            outTris.push_back({ b, color });
            outTris.push_back({ a, color });
        }
    }
}

void GizmoBuilder::BuildTranslateGizmo(const glm::vec3& worldOrigin, float armLength, GizmoAxis highlighted,
    std::vector<GizmoVertex>& outLines, std::vector<GizmoVertex>& outTris)
{
    outLines.clear();
    outTris.clear();

    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    for (GizmoAxis axis : axes)
        AppendArrow(worldOrigin, axis, armLength, axis == highlighted, outLines, outTris);
}

void Gizmo::Init()
{
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(GizmoVertex));

    //attribute 0 = position, 1 = color (Mesh와 같은 DSA 방식)
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, (GLuint)offsetof(GizmoVertex, position));
    glVertexArrayAttribBinding(vao, 0, 0);

    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, (GLuint)offsetof(GizmoVertex, color));
    glVertexArrayAttribBinding(vao, 1, 0);
}

void Gizmo::Shutdown()
{
    if (vbo != 0) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao != 0) { glDeleteVertexArrays(1, &vao); vao = 0; }
    bufferCapacityBytes = 0;
    lineCount = 0;
    triVertexCount = 0;
}

void Gizmo::Upload(const std::vector<GizmoVertex>& lines, const std::vector<GizmoVertex>& tris)
{
    lineCount = (unsigned int)lines.size();
    triVertexCount = (unsigned int)tris.size();

    std::vector<GizmoVertex> combined;
    combined.reserve(lines.size() + tris.size());
    combined.insert(combined.end(), lines.begin(), lines.end());
    combined.insert(combined.end(), tris.begin(), tris.end());

    if (combined.empty())
        return;

    const size_t bytes = combined.size() * sizeof(GizmoVertex);

    //매 프레임 내용이 바뀌는 버퍼라 Mesh처럼 불변 저장소를 쓰지 않는다.
    //용량이 커질 때만 재할당하고, 줄어들 땐 기존 버퍼를 그냥 재사용한다(남는 뒷부분은 안 그리므로 무해).
    if (bytes > bufferCapacityBytes)
    {
        glNamedBufferData(vbo, (GLsizeiptr)bytes, combined.data(), GL_DYNAMIC_DRAW);
        bufferCapacityBytes = bytes;
    }
    else
    {
        glNamedBufferSubData(vbo, 0, (GLsizeiptr)bytes, combined.data());
    }
}
