#pragma once

#include <Render/GizmoAxis.h>

#include <glm/glm.hpp>
#include <vector>

//이동 기즈모(유니티/블렌더의 그 빨강/초록/파랑 화살표) 지오메트리 생성 + GPU 업로드.
//
//메시와 다르게 이 지오메트리는 매 프레임 새로 만든다: 기즈모는 화면에서 항상 같은 크기로
//보여야 해서(카메라와의 거리에 비례해 팔 길이가 바뀐다) 위치와 크기가 매 프레임 달라지고,
//강조색(마우스가 올라간 축)도 프레임마다 바뀔 수 있다. 버텍스가 수십~백 개 수준이라
//매 프레임 재생성 비용은 무시할 만하다.
struct GizmoVertex
{
    glm::vec3 position;   //월드 공간 좌표 — 셰이더가 별도 모델 행렬 없이 바로 투영한다
    glm::vec3 color;
};

namespace GizmoBuilder
{
    //세 축(빨강/초록/파랑) 화살표를 만든다. 화살대는 선(GL_LINES)으로, 화살촉은 삼각형 뿔로 만들어
    //드로우콜을 두 번(선/삼각형)으로 나눠 그린다 — 선만으로는 광각 시점에서 두께가 1px로
    //얇아 보이지만, 화살촉이 있으면 "이게 축이다"라는 실루엣이 각도와 무관하게 남는다.
    //highlighted 축은 더 밝은 색으로 그려 마우스가 올라갔거나 드래그 중임을 알려준다.
    void BuildTranslateGizmo(const glm::vec3& worldOrigin, float armLength, GizmoAxis highlighted,
        std::vector<GizmoVertex>& outLines, std::vector<GizmoVertex>& outTris);
}

//생성된 지오메트리를 올려서 그리는 쪽. Mesh와 달리 정적 저장소(glNamedBufferStorage)가 아니라
//매 프레임 내용이 바뀌는 동적 버퍼(glNamedBufferData)를 쓴다.
class Gizmo
{
public:
    void Init();
    void Shutdown();

    //CPU에서 만든 정점을 GPU로 올린다. lines/tris 순서로 한 버퍼에 이어 담는다.
    void Upload(const std::vector<GizmoVertex>& lines, const std::vector<GizmoVertex>& tris);

    unsigned int GetVAO() const { return vao; }
    unsigned int GetLineCount() const { return lineCount; }
    unsigned int GetTriVertexCount() const { return triVertexCount; }

private:
    unsigned int vao = 0;
    unsigned int vbo = 0;
    size_t bufferCapacityBytes = 0;   //재할당(glNamedBufferData) 호출을 줄이기 위한 현재 용량

    unsigned int lineCount = 0;       //GL_LINES로 그릴 정점 수
    unsigned int triVertexCount = 0;  //GL_TRIANGLES로 그릴 정점 수 (라인 다음부터 시작)
};
