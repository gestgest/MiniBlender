#pragma once

#include <string>

class Scene;

struct ExportResult
{
    bool ok = false;
    std::string message;      //성공/실패 모두 UI에 그대로 띄울 수 있는 한 줄
    int objectCount = 0;
    unsigned int triangles = 0;
};

//씬을 Wavefront OBJ로 내보낸다.
//
//FBX 저장 전에 OBJ부터 하는 이유:
//  내보내기는 "씬 순회 → 정점 수집 → 변환 적용 → 파일 쓰기"라는 파이프라인이 본체고,
//  포맷은 그 마지막 단계일 뿐이다. OBJ는 결과를 메모장으로 열어 눈으로 검증할 수 있어서
//  파이프라인이 맞는지 먼저 확인하기에 좋다. 여기서 한 번 성공해두면
//  나중에 FBX가 안 열릴 때 "내 정점 데이터 문제"는 이미 배제된 상태로 시작할 수 있다.
//
//OBJ가 담지 못하는 것(계층, 단위/축, 애니메이션, 인스턴스)은 그냥 버려진다.
//지금 MiniBlender의 데이터가 위치+노멀뿐이라 실질적인 손실은 없다.
ExportResult ExportSceneToObj(const Scene& scene, const std::string& path);
