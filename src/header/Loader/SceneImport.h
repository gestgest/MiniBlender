#pragma once

#include <string>

class Scene;
class OrbitCamera;

//로더(파일 → 정점 데이터)와 씬(GPU 메시 + 오브젝트)을 이어주는 접착층.
//FbxLoader는 GL을 모르고 Scene은 파일 포맷을 모르는데, 둘을 아는 코드가 한 군데는 필요하다.
struct ImportReport
{
    bool ok = false;
    std::string message;      //성공/실패 모두 UI에 그대로 띄울 수 있는 한 줄
    int meshCount = 0;
    unsigned int triangles = 0;
};

//FBX를 읽어 씬에 오브젝트로 추가한다.
//frameCamera가 true면 불러온 모델 전체가 보이도록 카메라를 맞춘다
//(이게 없으면 모델이 화면 밖이나 점만 하게 보여서 "안 불러와졌나?" 하고 헷갈린다).
ImportReport ImportFbxIntoScene(Scene& scene, OrbitCamera& camera, const std::string& path,
    bool frameCamera = true);
