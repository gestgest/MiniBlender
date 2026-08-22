#ifndef CAMERA_H
#define CAMERA_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

// Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 2.5f;
const float SENSITIVITY = 0.2f;
const float ZOOM = 45.0f;


class Camera
{
public:
    // camera Attributes
    glm::vec3 Position;

    ///view 행렬
    ///01 00 00 00
    ///00 01 00 00
    ///00 00 01 00
    ///-x -y -z 01

    bool isThirdView;

    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    
    glm::vec3 trackingPos; //
    // euler Angles
    float Yaw; //좌우
    float Pitch; //높이
    
    // camera options
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    //: 이후는 초기화다
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM), trackingPos(glm::vec3(10.0f, 10.0f, 10.0f))
    {
        isThirdView = true;
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    }


    // constructor with scalar values
    Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = glm::vec3(posX, posY, posZ);
        WorldUp = glm::vec3(upX, upY, upZ);
        Yaw = yaw;
        Pitch = pitch;
        updateCameraPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    }

    // default : z = -1
    // returns the view matrix calculated using Euler Angles and the LookAt Matrix
    glm::mat4 GetViewMatrix()
    {
        //debugMat4(glm::lookAt(Position, Position + Front, Up));
        return glm::lookAt(Position, Position + Front, Up);
    }

    //3인칭
    void move(glm::vec3 player_pos)
    {
        //float velocity = MovementSpeed * deltaTime;
        updateCameraPosition(player_pos);
        //Front *= -1;
    }

    // processes input received from a mouse input system. Expects the offset value in both the x and y direction.
    void ProcessMouseMovement(float xoffset, float yoffset, glm::vec3 player_pos, GLboolean constrainPitch = true)
    {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw += xoffset;
        Pitch += yoffset;

        // make sure that when pitch is out of bounds, screen doesn't get flipped
        if (constrainPitch)
        {
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        //앞 벡터
        updateCameraPosition(player_pos);
    }

    // processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
    void ProcessMouseScroll(float yoffset)
    {
        Zoom -= (float)yoffset;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 90.0f)
            Zoom = 90.0f;
    }

    //앞만 보고 오른쪽 위 계산
    void updateCameraVectors(glm::vec3 front)
    {
        Front = glm::normalize(front);

        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
    glm::vec3 getTrackingPos()
    {
        return trackingPos;
    }
    glm::vec3 getFrontPlayer()
    {
        return glm::vec3(Front.x, 0, Front.z);
    }
    glm::vec3  getRightPlayer()
    {
        return glm::vec3(Right.x, 0, Right.z);
    }

private:
    // 
    // calculates the front vector from the Camera's (updated) Euler Angles
    void updateCameraPosition(glm::vec3 player_pos)
    {
        trackingPos.x = cos(glm::radians(Yaw)) * -cos(glm::radians(Pitch));
        trackingPos.y = -sin(glm::radians(Pitch));
        trackingPos.z = sin(glm::radians(Yaw)) * -cos(glm::radians(Pitch));

        //3인칭 뷰
        if (isThirdView)
        {
            trackingPos *= Zoom;

            //trackingPos는 Back;
            Position = player_pos + trackingPos;

            //카메라가 플레이어를 쳐다보는
            glm::vec3 frontCameraVector = player_pos - Position;
            updateCameraVectors(frontCameraVector);
        }
        //1인칭 뷰
        else
        {
            updateCameraVectors(-trackingPos);
            float m = 5;
            //Position = player_pos + Front;
            Position = player_pos + glm::vec3(Front.x * m, Front.y * m + 5.0, Front.z * m);
        }
    }

    //debug
    void debugVec3(glm::vec3 pos)
    {
        std::cout << pos.x << ' ' << pos.y << ' ' << pos.z << '\n';
    }

    void debugMat4(glm::mat4 m)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                std::cout << m[i][j] << ' ';
            }
            std::cout << '\n';
        }
    }
};

#endif
#pragma once
