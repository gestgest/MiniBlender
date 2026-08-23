#include <Edit/History.h>

#include <Edit/EditMode.h>
#include <Render/Mesh.h>
#include <Scene/Scene.h>

#include <algorithm>

namespace
{
    bool ContainsId(const std::vector<SceneObject>& list, unsigned int id)
    {
        for (const SceneObject& o : list)
        {
            if (o.id == id)
                return true;
        }
        return false;
    }

    //자리(index)가 작은 것부터 끼워 넣어야 뒤 원소의 자리가 안 밀린다.
    void InsertAll(Scene& scene, std::vector<ObjectSnapshot> snapshots)
    {
        std::sort(snapshots.begin(), snapshots.end(),
            [](const ObjectSnapshot& a, const ObjectSnapshot& b) { return a.index < b.index; });

        for (const ObjectSnapshot& s : snapshots)
            scene.InsertObject(s.object, s.index);
    }

    void RemoveAll(Scene& scene, const std::vector<ObjectSnapshot>& snapshots)
    {
        for (const ObjectSnapshot& s : snapshots)
            scene.RemoveObject(s.object.id);
    }

    //메시의 정점을 GPU에서 되읽어 델타를 덮어쓰고 다시 올린다.
    //CPU 사본을 들고 있지 않는 프로젝트 규칙을 그대로 따른다 — 되돌리기는 어쩌다 한 번이라
    //되읽기 비용이 사본을 계속 유지하는 비용보다 싸다.
    void ApplyVertexDeltas(Mesh* mesh, const std::vector<VertexDelta>& deltas, bool undo)
    {
        if (mesh == nullptr || deltas.empty())
            return;

        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        if (!mesh->ReadBack(vertices, indices))
            return;

        for (const VertexDelta& d : deltas)
        {
            if (d.index >= vertices.size())
                continue;   //되돌리기 이후 메시가 통째로 바뀐 경우. 조용히 넘긴다.

            vertices[d.index].position = undo ? d.oldPosition : d.newPosition;
            vertices[d.index].normal = undo ? d.oldNormal : d.newNormal;
        }

        mesh->UpdateVertices(vertices);
    }
}

void History::Push(Action&& action)
{
    if (action.IsEmpty())
        return;

    undoStack.push_back(std::move(action));

    //깊이를 넘으면 가장 오래된 것부터 버린다.
    //vector 앞을 지우는 건 O(n)이지만 100칸짜리라 실질 비용이 없고, deque보다 읽기 쉽다.
    if (undoStack.size() > MAX_DEPTH)
        undoStack.erase(undoStack.begin());

    redoStack.clear();
}

void History::Clear()
{
    undoStack.clear();
    redoStack.clear();
}

const char* History::UndoLabel() const
{
    return undoStack.empty() ? "" : undoStack.back().label.c_str();
}

const char* History::RedoLabel() const
{
    return redoStack.empty() ? "" : redoStack.back().label.c_str();
}

bool History::Undo(Scene& scene, EditMode& edit)
{
    if (undoStack.empty())
        return false;

    Action action = std::move(undoStack.back());
    undoStack.pop_back();

    Apply(action, true, scene, edit);

    redoStack.push_back(std::move(action));
    return true;
}

bool History::Redo(Scene& scene, EditMode& edit)
{
    if (redoStack.empty())
        return false;

    Action action = std::move(redoStack.back());
    redoStack.pop_back();

    Apply(action, false, scene, edit);

    undoStack.push_back(std::move(action));
    return true;
}

void History::Apply(const Action& action, bool undo, Scene& scene, EditMode& edit)
{
    //지우기를 먼저, 되살리기를 나중에. 순서를 바꾸면 되살린 오브젝트가 자리를 잡기 전에
    //인덱스가 흔들려서 아웃라이너 순서가 어긋난다.
    if (undo)
    {
        RemoveAll(scene, action.added);
        InsertAll(scene, action.removed);
    }
    else
    {
        RemoveAll(scene, action.removed);
        InsertAll(scene, action.added);
    }

    if (action.hasChange)
    {
        const SceneObject& target = undo ? action.before : action.after;
        if (SceneObject* obj = scene.FindById(target.id))
            *obj = target;
    }

    if (!action.vertexDeltas.empty())
    {
        ApplyVertexDeltas(action.mesh, action.vertexDeltas, undo);

        //편집 모드가 이 메시를 열어둔 채라면 CPU 사본이 낡았다.
        //다시 읽어오지 않으면 화면의 점과 실제 메시가 어긋나서 다음 편집이 엉뚱한 곳에 적용된다.
        edit.RefreshIfEditing(scene, action.mesh);
    }
}

Action History::MakeSceneDiff(const std::vector<SceneObject>& before,
    const std::vector<SceneObject>& after, const std::string& label)
{
    Action action;
    action.label = label;

    //O(n*m)이지만 이 함수는 클릭 한 번에 한 번만 돈다. 오브젝트 1000개 기준 백만 번 비교라도
    //사람이 못 느끼는 시간이고, id -> 위치 맵을 만드는 것보다 코드가 짧다.
    for (size_t i = 0; i < before.size(); ++i)
    {
        if (!ContainsId(after, before[i].id))
            action.removed.push_back({ before[i], i });
    }

    for (size_t i = 0; i < after.size(); ++i)
    {
        if (!ContainsId(before, after[i].id))
            action.added.push_back({ after[i], i });
    }

    return action;
}
