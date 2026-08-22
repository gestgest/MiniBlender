#include <Loader/FbxExporter.h>

#include <Render/Mesh.h>
#include <Scene/Scene.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
    //FBX는 객체마다 유일한 64비트 ID를 요구하고, Connections에서 이 ID로 서로를 가리킨다.
    //값 자체엔 의미가 없고 파일 안에서 겹치지만 않으면 된다.
    long long g_nextId = 1000000;

    std::string EscapeName(const std::string& name)
    {
        //ASCII FBX는 이름을 큰따옴표로 감싸므로 안에 따옴표가 들어가면 깨진다
        std::string out;
        for (char c : name)
            out += (c == '"') ? '\'' : c;
        return out.empty() ? std::string("Object") : out;
    }
}

ExportResult ExportSceneToFbx(const Scene& scene, const std::string& path)
{
    ExportResult result;

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        result.message = "파일을 열 수 없다: " + path;
        return result;
    }

    out.precision(6);
    out << std::fixed;

    //--- 내보낼 대상을 먼저 모은다 (Definitions에 개수를 적어야 해서 미리 세야 한다) ---
    struct Item
    {
        const SceneObject* obj;
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        long long geometryId;
        long long modelId;
    };

    std::vector<Item> items;
    for (const SceneObject& obj : scene.GetObjects())
    {
        if (obj.mesh == nullptr || !obj.visible)
            continue;

        Item item;
        item.obj = &obj;
        if (!obj.mesh->ReadBack(item.vertices, item.indices))
            continue;

        item.geometryId = g_nextId++;
        item.modelId = g_nextId++;
        result.triangles += (unsigned int)(item.indices.size() / 3);
        items.push_back(std::move(item));
    }

    if (items.empty())
    {
        result.message = "내보낼 오브젝트가 없다 (씬이 비었거나 전부 숨김 상태)";
        return result;
    }

    result.objectCount = (int)items.size();

    //--- 헤더 ---
    out << "; FBX 7.4.0 project file\n";
    out << "; Created by MiniBlender\n\n";

    out << "FBXHeaderExtension:  {\n";
    out << "\tFBXHeaderVersion: 1003\n";
    out << "\tFBXVersion: 7400\n";
    out << "\tCreator: \"MiniBlender\"\n";
    out << "}\n\n";

    //--- 좌표계와 단위 선언 ---
    //OBJ에는 없던 부분이고, FBX를 쓰는 실질적인 이유 중 하나다.
    //UpAxis=1(Y), FrontAxis=2(Z), CoordAxis=0(X) → 우리 월드와 같은 Y-up 오른손 좌표계.
    //UnitScaleFactor는 "1유닛이 몇 cm인가". 우리는 1유닛 = 1m이므로 100.
    out << "GlobalSettings:  {\n";
    out << "\tVersion: 1000\n";
    out << "\tProperties70:  {\n";
    out << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n";
    out << "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n";
    out << "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n";
    out << "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n";
    out << "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n";
    out << "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n";
    out << "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n";
    out << "\t}\n";
    out << "}\n\n";

    //--- Definitions: 어떤 종류의 객체가 몇 개 들어있는지 미리 알린다 ---
    out << "Definitions:  {\n";
    out << "\tVersion: 100\n";
    out << "\tCount: " << (items.size() * 2 + 1) << "\n";
    out << "\tObjectType: \"GlobalSettings\" {\n\t\tCount: 1\n\t}\n";
    out << "\tObjectType: \"Geometry\" {\n\t\tCount: " << items.size() << "\n\t}\n";
    out << "\tObjectType: \"Model\" {\n\t\tCount: " << items.size() << "\n\t}\n";
    out << "}\n\n";

    //--- Objects: 실제 데이터 ---
    out << "Objects:  {\n";

    for (const Item& item : items)
    {
        const std::string name = EscapeName(item.obj->name);

        //=== Geometry: 로컬 좌표의 "모양" ===
        out << "\tGeometry: " << item.geometryId << ", \"Geometry::" << name << "\", \"Mesh\" {\n";

        //정점 좌표를 x,y,z 순으로 한 줄에 쭉 나열한다
        out << "\t\tVertices: *" << (item.vertices.size() * 3) << " {\n\t\t\ta: ";
        for (size_t i = 0; i < item.vertices.size(); ++i)
        {
            const glm::vec3& p = item.vertices[i].position;
            if (i > 0) out << ",";
            out << p.x << "," << p.y << "," << p.z;
        }
        out << "\n\t\t}\n";

        //=== 여기가 FBX에서 가장 잘 틀리는 부분 ===
        //면의 마지막 인덱스는 비트 반전(~i = -i-1)해서 음수로 쓴다. 그게 "면이 여기서 끝난다"는 표시다.
        //이걸 빼먹으면 모든 삼각형이 하나의 거대한 폴리곤으로 이어져서 형체가 뭉개진다.
        out << "\t\tPolygonVertexIndex: *" << item.indices.size() << " {\n\t\t\ta: ";
        for (size_t i = 0; i + 2 < item.indices.size(); i += 3)
        {
            if (i > 0) out << ",";
            out << item.indices[i] << "," << item.indices[i + 1] << ","
                << (-(int)item.indices[i + 2] - 1);
        }
        out << "\n\t\t}\n";

        out << "\t\tGeometryVersion: 124\n";

        //노멀은 "폴리곤 정점마다 하나씩(ByPolygonVertex), 인덱스 없이 직접(Direct)" 방식으로 쓴다.
        //즉 PolygonVertexIndex와 같은 순서/개수로 나열하면 된다.
        out << "\t\tLayerElementNormal: 0 {\n";
        out << "\t\t\tVersion: 101\n";
        out << "\t\t\tName: \"\"\n";
        out << "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n";
        out << "\t\t\tReferenceInformationType: \"Direct\"\n";
        out << "\t\t\tNormals: *" << (item.indices.size() * 3) << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i < item.indices.size(); ++i)
        {
            const glm::vec3& n = item.vertices[item.indices[i]].normal;
            if (i > 0) out << ",";
            out << n.x << "," << n.y << "," << n.z;
        }
        out << "\n\t\t\t}\n";
        out << "\t\t}\n";

        //Layer는 "이 지오메트리가 어떤 레이어 요소들을 쓰는지"의 목차다. 없으면 노멀이 무시된다.
        out << "\t\tLayer: 0 {\n";
        out << "\t\t\tVersion: 100\n";
        out << "\t\t\tLayerElement:  {\n";
        out << "\t\t\t\tType: \"LayerElementNormal\"\n";
        out << "\t\t\t\tTypedIndex: 0\n";
        out << "\t\t\t}\n";
        out << "\t\t}\n";
        out << "\t}\n";

        //=== Model: 그 모양을 어디에 놓을지 (씬 노드) ===
        //OBJ와 갈리는 지점이다. 정점은 로컬 좌표 그대로 두고 위치/회전/크기는 여기에 적는다.
        //그래서 불러온 쪽에서 트랜스폼을 그대로 편집할 수 있다.
        const Transform& t = item.obj->transform;
        out << "\tModel: " << item.modelId << ", \"Model::" << name << "\", \"Mesh\" {\n";
        out << "\t\tVersion: 232\n";
        out << "\t\tProperties70:  {\n";
        out << "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\","
            << t.position.x << "," << t.position.y << "," << t.position.z << "\n";
        //회전 순서: 우리 Transform은 X→Y→Z 순으로 적용한다. FBX 기본값(eEulerXYZ)과 같아서
        //RotationOrder를 따로 적지 않아도 맞는다.
        out << "\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\","
            << t.rotation.x << "," << t.rotation.y << "," << t.rotation.z << "\n";
        out << "\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\","
            << t.scale.x << "," << t.scale.y << "," << t.scale.z << "\n";
        out << "\t\t}\n";
        out << "\t\tShading: T\n";
        out << "\t\tCulling: \"CullingOff\"\n";
        out << "\t}\n";
    }

    out << "}\n\n";

    //--- Connections: 이게 없으면 데이터는 있는데 화면엔 아무것도 안 나온다 ---
    //"OO"는 Object-to-Object 연결. Geometry를 Model에 붙이고, Model을 루트(0)에 붙인다.
    out << "Connections:  {\n";
    for (const Item& item : items)
    {
        out << "\tC: \"OO\"," << item.geometryId << "," << item.modelId << "\n";
        out << "\tC: \"OO\"," << item.modelId << ",0\n";
    }
    out << "}\n";

    out.close();
    if (out.fail())
    {
        result.message = "파일 쓰기에 실패했다: " + path;
        return result;
    }

    result.ok = true;
    result.message = "FBX 내보내기 완료 — 오브젝트 " + std::to_string(result.objectCount)
        + "개, 삼각형 " + std::to_string(result.triangles) + "개 → " + path;
    return result;
}
