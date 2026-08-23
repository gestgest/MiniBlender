#pragma once

#include <Scene/SceneObject.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

class Scene;
class Mesh;
class EditMode;

//되돌리기 / 다시 실행 (Ctrl+Z / Ctrl+Y).
struct ObjectSnapshot
{
    SceneObject object;
    size_t index = 0;
};

//정점 하나가 어떻게 바뀌었는지.
//노멀까지 담는 이유: 정점을 옮기면 인접 삼각형의 노멀이 다시 계산되는데,
//되돌릴 때 위치만 복구하면 셰이딩이 편집 후 상태로 남아서 그 면만 어색하게 튄다.
struct VertexDelta
{
    unsigned int index = 0;
    glm::vec3 oldPosition{ 0.0f };
    glm::vec3 newPosition{ 0.0f };
    glm::vec3 oldNormal{ 0.0f };
    glm::vec3 newNormal{ 0.0f };
};

//한 번의 편집. 세 종류가 한 구조체에 같이 들어 있다 —
//variant로 나누면 타입이 늘 때마다 방문자 코드가 따라 늘어서, 이 규모에선 오히려 손해다.
struct Action
{
    std::string label;   //"되돌리기: 정점 이동" 처럼 UI에 그대로 붙여 쓴다

    //--- 씬 편집 (추가 / 삭제 / 가져오기) ---
    //가져오기는 예시 큐브를 치우면서 오브젝트를 추가하니 둘 다 채워진다.
    std::vector<ObjectSnapshot> removed;
    std::vector<ObjectSnapshot> added;

    //--- 오브젝트 속성 변경 (위치 / 회전 / 크기 / 색 / 표시) ---
    bool hasChange = false;
    SceneObject before;
    SceneObject after;

    //--- 정점 편집 ---
    Mesh* mesh = nullptr;
    std::vector<VertexDelta> vertexDeltas;

    bool IsEmpty() const
    {
        return removed.empty() && added.empty() && !hasChange && vertexDeltas.empty();
    }
};

class History
{
public:
    //새 액션이 쌓이면 다시 실행 스택은 무효가 된다 (되돌린 뒤 다른 편집을 하면 갈래가 갈리므로).
    //빈 액션은 무시하니, 바뀐 게 없는지 부르는 쪽에서 따로 확인할 필요는 없다.
    void Push(Action&& action);
    void Clear();

    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    const char* UndoLabel() const;
    const char* RedoLabel() const;

    bool Undo(Scene& scene, EditMode& edit);
    bool Redo(Scene& scene, EditMode& edit);

    //씬 목록을 편집 전/후로 비교해서 액션을 만든다.
    //추가·삭제 지점마다 기록 코드를 심는 대신 이렇게 하면, 나중에 씬을 건드리는 기능이 늘어도
    //앞뒤로 스냅샷만 뜨면 되돌리기가 따라온다 (가져오기가 정확히 이 경우다).
    static Action MakeSceneDiff(const std::vector<SceneObject>& before,
        const std::vector<SceneObject>& after, const std::string& label);

private:
    void Apply(const Action& action, bool undo, Scene& scene, EditMode& edit);

    //길게 잡아봐야 메모리만 먹는다. 블렌더 기본값도 이 근처(32~256).
    static const size_t MAX_DEPTH = 8;

    std::vector<Action> undoStack;
    std::vector<Action> redoStack;
};
