#pragma once

#include <Render/Mesh.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

//FBX 파일 하나에서 뽑아낸 메시 하나.
//FBX는 보통 여러 개의 메시 노드를 담고 있어서(의자 = 다리 + 등받이 + 방석 식) 결과가 배열이다.
struct LoadedMesh
{
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

struct LoadResult
{
    bool ok = false;
    std::string error;                  //실패 시 사람이 읽을 수 있는 이유
    std::vector<LoadedMesh> meshes;

    //불러온 것 전체를 감싸는 경계 상자. 카메라를 자동으로 맞출 때 쓴다.
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };

    unsigned int totalTriangles = 0;
};

//FBX 로더.
//
//왜 ufbx인가:
//  FBX는 Autodesk 폐쇄 포맷이라 선택지가 셋뿐이다.
//   1) Autodesk FBX SDK — 공식이지만 계정 등록 + 설치 필요, 배포 제약이 있고 덩치가 크다
//   2) Assimp — 강력하지만 라이브러리를 따로 빌드해야 한다(vcpkg/CMake 필요)
//   3) ufbx — .c/.h 두 개짜리, 의존성 없음, MIT/Public Domain
//  이 프로젝트는 "클론 → VS로 열기 → F5"가 전제라서 3번이 유일하게 그걸 안 깬다.
namespace FbxLoader
{
    //파일 경로를 받아 CPU 쪽 정점 데이터로 변환한다. GPU에는 올리지 않는다
    //(Mesh::Upload는 호출자가 한다 — 로더가 GL을 모르게 유지하려는 것).
    LoadResult Load(const std::string& path);
}
