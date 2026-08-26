#pragma once

#include <glm/glm.hpp>

//이동 기즈모가 다루는 세 축. EditMode(피킹/드래그)와 Gizmo(그리기)가 같이 써야 해서
//둘 중 어느 쪽에도 속하지 않는 가벼운 헤더로 뺐다 — Gizmo.h를 EditMode.h가 끌어오면
//OpenGL 관련 타입까지 딸려오니, 열거값만 여기 둔다.
enum class GizmoAxis
{
    None,
    X,
    Y,
    Z
};

//축 하나를 월드 공간 단위 방향 벡터로. 기즈모는 항상 월드 축에 정렬된다(블렌더의 "글로벌" 모드).
inline glm::vec3 GizmoAxisDirection(GizmoAxis axis)
{
    switch (axis)
    {
    case GizmoAxis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
    case GizmoAxis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
    case GizmoAxis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
    default:           return glm::vec3(0.0f);
    }
}

//축마다 고정 색(유니티/블렌더와 같은 관용색: X=빨강, Y=초록, Z=파랑)
inline glm::vec3 GizmoAxisColor(GizmoAxis axis)
{
    switch (axis)
    {
    case GizmoAxis::X: return glm::vec3(0.9f, 0.15f, 0.15f);
    case GizmoAxis::Y: return glm::vec3(0.2f, 0.85f, 0.2f);
    case GizmoAxis::Z: return glm::vec3(0.2f, 0.45f, 0.95f);
    default:           return glm::vec3(1.0f);
    }
}
