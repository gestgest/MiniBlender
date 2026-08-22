#include <Render/OrbitCamera.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

glm::vec3 OrbitCamera::GetPosition() const
{
    //구면 좌표 -> 직교 좌표. target에서 (yaw, pitch) 방향으로 distance만큼 떨어진 지점.
    float y = glm::radians(yaw);
    float p = glm::radians(pitch);

    glm::vec3 offset;
    offset.x = std::cos(p) * std::cos(y);
    offset.y = std::sin(p);
    offset.z = std::cos(p) * std::sin(y);

    return target + offset * distance;
}

glm::mat4 OrbitCamera::GetViewMatrix() const
{
    return glm::lookAt(GetPosition(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::GetProjectionMatrix(float aspect) const
{
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

void OrbitCamera::Orbit(float deltaX, float deltaY)
{
    yaw += deltaX * orbitSpeed;
    pitch += deltaY * orbitSpeed;

    //위/아래 극점을 넘지 못하게 막는다. 넘어가면 up 벡터와 시선이 평행해져서 lookAt이 뒤집힌다.
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void OrbitCamera::Pan(float deltaX, float deltaY)
{
    //화면 기준으로 밀어야 자연스럽다. 카메라의 right/up 축을 구해서 그 방향으로 target을 옮긴다.
    glm::vec3 forward = glm::normalize(target - GetPosition());
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    //멀리서 볼수록 한 픽셀이 담당하는 월드 거리가 커지므로 distance에 비례시킨다.
    //이거 없으면 줌아웃 상태에서 팬이 답답할 정도로 느려진다.
    float speed = distance * 0.0015f;
    target += (-right * deltaX + up * deltaY) * speed;
}

void OrbitCamera::Zoom(float scrollDelta)
{
    //거리에 비례해서 줄이는 이유: 절대값으로 빼면 가까울 때 한 칸에 확 뚫고 지나가버린다.
    //비례 방식이면 가까울수록 미세하게, 멀수록 크게 움직여서 감각이 일정하다.
    distance *= (1.0f - scrollDelta * zoomSpeed);
    distance = std::clamp(distance, 0.2f, 300.0f);
}
