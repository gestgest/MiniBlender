#include <Loader/ObjExporter.h>

#include <Render/Mesh.h>
#include <Scene/Scene.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <fstream>
#include <string>
#include <vector>

namespace
{
    //OBJ는 이름에 공백이 들어가면 파서가 헷갈린다. 안전하게 밑줄로 바꾼다.
    std::string SanitizeName(const std::string& name)
    {
        std::string out = name;
        for (char& c : out)
        {
            if (c == ' ' || c == '\t' || c == '\n')
                c = '_';
        }
        return out.empty() ? std::string("Object") : out;
    }
}

ExportResult ExportSceneToObj(const Scene& scene, const std::string& path)
{
    ExportResult result;

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        result.message = "파일을 열 수 없다: " + path;
        return result;
    }

    //소수점 6자리면 밀리미터 단위까지 충분하다. 더 늘리면 파일만 커진다.
    out.precision(6);
    out << std::fixed;

    out << "# MiniBlender OBJ export\n";

    //OBJ의 인덱스는 1부터 시작하고, 파일 전체에서 이어지는 통번호다.
    //오브젝트별로 0부터 다시 세면 안 되기 때문에 지금까지 쓴 개수를 계속 누적한다.
    unsigned int vertexOffset = 1;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    for (const SceneObject& obj : scene.GetObjects())
    {
        if (obj.mesh == nullptr || !obj.visible)
            continue;

        if (!obj.mesh->ReadBack(vertices, indices))
            continue;

        const glm::mat4 model = obj.transform.GetMatrix();
        //노멀에 모델 행렬을 그대로 곱하면 비균등 스케일에서 방향이 틀어진다.
        //역전치 행렬을 써야 표면에 수직인 성질이 유지된다.
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

        out << "o " << SanitizeName(obj.name) << "\n";

        for (const Vertex& v : vertices)
        {
            const glm::vec3 p = glm::vec3(model * glm::vec4(v.position, 1.0f));
            out << "v " << p.x << " " << p.y << " " << p.z << "\n";
        }

        for (const Vertex& v : vertices)
        {
            const glm::vec3 n = glm::normalize(normalMatrix * v.normal);
            out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
        }

        //f 형식은 "정점/UV/노멀". UV가 없으니 가운데를 비워서 v//vn 으로 쓴다.
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const unsigned int a = indices[i + 0] + vertexOffset;
            const unsigned int b = indices[i + 1] + vertexOffset;
            const unsigned int c = indices[i + 2] + vertexOffset;

            out << "f " << a << "//" << a
                << " "  << b << "//" << b
                << " "  << c << "//" << c << "\n";
        }

        vertexOffset += (unsigned int)vertices.size();
        result.triangles += (unsigned int)(indices.size() / 3);
        ++result.objectCount;
    }

    if (result.objectCount == 0)
    {
        result.message = "내보낼 오브젝트가 없다 (씬이 비었거나 전부 숨김 상태)";
        return result;
    }

    out.close();
    if (out.fail())
    {
        result.message = "파일 쓰기에 실패했다: " + path;
        return result;
    }

    result.ok = true;
    result.message = "내보내기 완료 — 오브젝트 " + std::to_string(result.objectCount)
        + "개, 삼각형 " + std::to_string(result.triangles) + "개 → " + path;
    return result;
}
