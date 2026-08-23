#include <UI/FileDialog.h>

#include <cstring>

#ifdef _WIN32

//Windows.h를 먼저. GLFW보다 뒤에 넣으면 APIENTRY 재정의 경고가 난다(main.cpp와 같은 이유).
#include <Windows.h>
#include <commdlg.h>

//네이티브 핸들을 꺼내려면 이 매크로를 include "전에" 정의해야 한다
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#pragma comment(lib, "comdlg32.lib")

namespace
{
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return std::wstring();

        const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out((size_t)need, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), need);
        return out;
    }

    std::string WideToUtf8(const wchar_t* s)
    {
        if (s == nullptr || s[0] == L'\0')
            return std::string();

        const int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
        std::string out;
        if (need > 1)
        {
            out.resize((size_t)need - 1);   //-1: 널 종료는 std::string이 알아서 관리한다
            WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), need, nullptr, nullptr);
        }
        return out;
    }

    //MAX_PATH(260)로 잡으면 깊은 폴더의 파일이 잘려서 "파일 없음"으로 실패한다
    const DWORD PATH_BUFFER = 4096;

    //필터는 "표시이름\0패턴\0...\0\0" 형태의 하나짜리 문자열. 마지막이 널 두 개라 배열로 두고 쓴다.
    const wchar_t OPEN_FILTER[] =
        L"모델 파일 (*.fbx;*.obj)\0*.fbx;*.obj\0"
        L"FBX (*.fbx)\0*.fbx\0"
        L"OBJ (*.obj)\0*.obj\0"
        L"모든 파일 (*.*)\0*.*\0";

    const wchar_t SAVE_FILTER[] =
        L"OBJ (*.obj)\0*.obj\0"
        L"FBX (*.fbx)\0*.fbx\0";

    bool HasExtension(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;

        //"C:\내 폴더.old\model" 처럼 폴더 이름에 점이 있는 경우를 가려낸다
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos || dot > slash;
    }

    HWND OwnerHwnd(GLFWwindow* owner)
    {
        return owner ? glfwGetWin32Window(owner) : nullptr;
    }
}

std::string FileDialog::OpenModel(GLFWwindow* owner)
{
    std::wstring buffer((size_t)PATH_BUFFER, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    //부모 창을 넘겨야 대화상자가 앱 위에 모달로 뜬다. 안 넘기면 뒤로 숨어서 앱이 멈춘 것처럼 보인다.
    ofn.hwndOwner = OwnerHwnd(owner);
    ofn.lpstrFilter = OPEN_FILTER;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = PATH_BUFFER;
    ofn.lpstrTitle = L"모델 불러오기";
    //OFN_NOCHANGEDIR가 핵심: 이게 없으면 대화상자가 프로세스의 작업 디렉터리를 바꿔버려서
    //이후 "export.obj" 같은 상대 경로가 엉뚱한 곳에 저장된다.
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return std::string();   //취소했거나 실패. 둘을 구분할 이유가 없어서 똑같이 빈 문자열.

    return WideToUtf8(buffer.c_str());
}

std::string FileDialog::SaveModel(GLFWwindow* owner, const std::string& defaultNameUtf8)
{
    std::wstring buffer((size_t)PATH_BUFFER, L'\0');

    //파일명 칸에 지금 입력창에 있던 값을 미리 넣어준다
    const std::wstring initial = Utf8ToWide(defaultNameUtf8);
    if (!initial.empty() && initial.size() < PATH_BUFFER)
        std::memcpy(buffer.data(), initial.c_str(), (initial.size() + 1) * sizeof(wchar_t));

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = OwnerHwnd(owner);
    ofn.lpstrFilter = SAVE_FILTER;
    //초기값이 .fbx면 FBX 필터를 미리 선택해 둔다 (2번 항목)
    ofn.nFilterIndex = (initial.size() > 4 && _wcsicmp(initial.c_str() + initial.size() - 4, L".fbx") == 0) ? 2 : 1;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = PATH_BUFFER;
    ofn.lpstrTitle = L"모델 내보내기";
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn))
        return std::string();

    //확장자를 안 붙이고 저장하면 내보내기 쪽이 확장자로 형식을 고르지 못한다.
    //lpstrDefExt는 확장자가 하나로 고정될 때만 쓸 수 있어서, 고른 필터를 보고 직접 붙인다.
    std::wstring chosen = buffer.c_str();
    if (!HasExtension(chosen))
        chosen += (ofn.nFilterIndex == 2) ? L".fbx" : L".obj";

    return WideToUtf8(chosen.c_str());
}

#else

//윈도우가 아니면 대화상자가 없다. 경로 입력창으로 계속 쓸 수 있게 빈 문자열만 돌려준다.
std::string FileDialog::OpenModel(GLFWwindow*) { return std::string(); }
std::string FileDialog::SaveModel(GLFWwindow*, const std::string&) { return std::string(); }

#endif
