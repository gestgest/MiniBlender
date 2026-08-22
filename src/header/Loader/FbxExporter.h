#pragma once

#include <Loader/ObjExporter.h>   //ExportResult 재사용

#include <string>

class Scene;

//씬을 ASCII FBX 7.4로 내보낸다.
//
//바이너리가 아니라 ASCII인 이유:
//  FBX 바이너리는 노드마다 "자식 블록의 끝 오프셋"을 미리 적어야 해서, 쓰는 도중엔 알 수 없는 값을
//  나중에 되돌아가 채워야 하고 배열 압축(zlib)까지 얽힌다. ASCII는 중괄호 텍스트 트리라
//  앞에서부터 순서대로 쓰면 끝이고, 결과를 눈으로 검증할 수 있다.
//  블렌더/마야/유니티 전부 ASCII FBX를 읽는다. 대신 파일이 3~5배 크다.
//
//OBJ 익스포터와 결정적으로 다른 점:
//  OBJ는 정점을 월드 좌표로 구워 넣는다(계층을 담을 수 없으니).
//  FBX는 Geometry(로컬 좌표 모양)와 Model(위치/회전/크기를 가진 노드)을 분리해서 쓰고
//  Connections로 이어 붙인다. 그래서 불러온 쪽에서 트랜스폼을 그대로 편집할 수 있다.
ExportResult ExportSceneToFbx(const Scene& scene, const std::string& path);
