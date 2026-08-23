#pragma once

#include <string>

struct GLFWwindow;

//윈도우 기본 파일 대화상자(comdlg32). 탐색기와 같은 창이 뜬다.
//
//경로를 UTF-8 std::string으로 주고받는 이유: 프로젝트의 나머지(ufbx, ImGui, 드래그앤드롭)가
//전부 UTF-8을 기대한다. 여기서 wchar_t를 밖으로 흘리면 경계가 사방으로 번진다.
//사용자가 취소하면 빈 문자열을 돌려준다.
//
//주의: 대화상자가 떠 있는 동안 렌더 루프는 완전히 멈춘다(모달).
//호출한 프레임의 통계는 FrameStats::MarkFrameStalled로 버려야 그래프가 안 튄다.
namespace FileDialog
{
    //불러올 모델 파일 하나를 고른다 (FBX/OBJ)
    std::string OpenModel(GLFWwindow* owner);
    //저장 경로를 고른다. defaultNameUtf8이 파일명 칸의 초기값이 된다.
    std::string SaveModel(GLFWwindow* owner, const std::string& defaultNameUtf8);
}
