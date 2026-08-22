#pragma once

#include <glm/glm.hpp>

//블렌더식 오빗 카메라.
//
//게임 카메라(MoreCreatures의 camera.h)와 근본적으로 다른 점:
//  게임 = "내가 서 있는 위치"가 상태. 앞으로 걸으면 위치가 변한다.
//  DCC  = "내가 보고 있는 대상(target)"이 상태. 카메라는 그 주위를 도는 위성일 뿐.
//그래서 상태가 (target, 거리, 각도) 3개고, 위치는 매번 거기서 계산해낸다.
//이게 "오브젝트를 중심에 두고 관찰"하는 작업에 훨씬 편하다.
//
//조작 (블렌더 기본과 동일):
//  중간 버튼 드래그        = 궤도 회전(orbit)
//  Shift + 중간 버튼 드래그 = 평행 이동(pan)
//  휠                      = 줌
class OrbitCamera
{
public:
    void Orbit(float deltaX, float deltaY);
    void Pan(float deltaX, float deltaY);
    void Zoom(float scrollDelta);

    //선택한 오브젝트로 시점 이동 (블렌더의 넘패드 . 에 해당)
    void FocusOn(const glm::vec3& point) { target = point; }

    glm::vec3 GetPosition() const;
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspect) const;

    float GetFov() const { return fov; }
    float GetNear() const { return nearPlane; }
    float GetFar() const { return farPlane; }
    const glm::vec3& GetTarget() const { return target; }
    float GetDistance() const { return distance; }

private:
    glm::vec3 target{ 0.0f, 0.0f, 0.0f };
    float distance = 8.0f;
    float yaw = -45.0f;      //수평 각도(도)
    float pitch = 25.0f;     //수직 각도(도)

    float fov = 45.0f;
    float nearPlane = 0.05f;
    float farPlane = 500.0f;

    float orbitSpeed = 0.4f;
    float zoomSpeed = 0.12f;
};
