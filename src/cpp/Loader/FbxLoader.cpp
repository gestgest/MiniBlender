#include <Loader/FbxLoader.h>

#include <ufbx/ufbx.h>

#include <algorithm>
#include <cfloat>

namespace
{
    glm::vec3 ToGlm(const ufbx_vec3& v)
    {
        return glm::vec3((float)v.x, (float)v.y, (float)v.z);
    }
}

namespace FbxLoader
{

LoadResult Load(const std::string& path)
{
    LoadResult result;

    ufbx_load_opts opts = {};

    //--- 좌표계와 단위를 우리 쪽에 맞춰 통일 ---
    //FBX는 만든 툴마다 축과 단위가 제각각이다(3ds Max는 Z-up, 마야는 Y-up, 유니티 에셋은 cm 단위...).
    //그대로 읽으면 모델이 옆으로 누워 있거나 100배 크게 나온다.
    //ufbx가 로드 시점에 변환해주므로 우리 코드는 항상 "Y-up, 1유닛 = 1미터"만 가정하면 된다.
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;

    //노멀이 없는 파일이 흔하다. 없으면 면 노멀로 만들어 준다 — 없으면 조명이 새까맣게 나온다.
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (scene == nullptr)
    {
        char buf[512];
        ufbx_format_error(buf, sizeof(buf), &error);
        result.error = buf;
        return result;
    }

    glm::vec3 bmin(FLT_MAX);
    glm::vec3 bmax(-FLT_MAX);

    //--- 노드를 돌면서 메시를 가진 것만 뽑는다 ---
    for (size_t ni = 0; ni < scene->nodes.count; ++ni)
    {
        const ufbx_node* node = scene->nodes.data[ni];
        if (node->is_root || node->mesh == nullptr)
            continue;

        const ufbx_mesh* mesh = node->mesh;

        //정점을 월드 공간으로 옮겨 굽는다.
        //FBX의 노드 계층(부모-자식 변환)을 우리 Scene이 아직 지원하지 않기 때문에,
        //지금은 각 메시를 최종 위치로 확정시켜서 넣는다.
        //(나중에 계층 구조를 지원하면 이 변환을 Transform으로 넘기면 된다)
        const ufbx_matrix geomToWorld = node->geometry_to_world;
        //노멀은 위치와 같은 행렬을 쓰면 안 된다. 비균등 스케일에서 방향이 틀어져서
        //역전치 행렬이 필요한데, ufbx가 만들어 준다.
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&geomToWorld);

        LoadedMesh out;
        out.name = node->name.length > 0 ? std::string(node->name.data, node->name.length)
                                         : std::string("Mesh");

        //삼각형화 버퍼. 폴리곤(사각형 이상)을 삼각형으로 쪼갠 결과가 여기 담긴다.
        //FBX는 사각형 면을 흔히 쓰는데 OpenGL은 삼각형만 그린다.
        std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);

        out.vertices.reserve(mesh->num_indices);
        out.indices.reserve(mesh->num_indices);

        for (size_t fi = 0; fi < mesh->faces.count; ++fi)
        {
            const ufbx_face face = mesh->faces.data[fi];
            const uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);

            for (uint32_t i = 0; i < numTris * 3; ++i)
            {
                const uint32_t index = triIndices[i];

                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                p = ufbx_transform_position(&geomToWorld, p);

                ufbx_vec3 n = { 0.0, 1.0, 0.0 };
                if (mesh->vertex_normal.exists)
                {
                    n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                    n = ufbx_transform_direction(&normalMatrix, n);
                }

                Vertex v;
                v.position = ToGlm(p);
                v.normal = glm::normalize(ToGlm(n));

                //정점 병합(중복 제거)은 하지 않는다. 지금은 "일단 화면에 띄우기"가 목표라
                //단순함을 택했다. 인덱스 버퍼의 이점을 살리려면 나중에 해시 기반 병합을 넣어야 한다.
                out.indices.push_back((unsigned int)out.vertices.size());
                out.vertices.push_back(v);

                bmin = glm::min(bmin, v.position);
                bmax = glm::max(bmax, v.position);
            }
        }

        if (out.indices.empty())
            continue;

        result.totalTriangles += (unsigned int)(out.indices.size() / 3);
        result.meshes.push_back(std::move(out));
    }

    ufbx_free_scene(scene);

    if (result.meshes.empty())
    {
        result.error = "파일은 읽혔지만 메시가 하나도 없다 (카메라/라이트만 있는 파일일 수 있음)";
        return result;
    }

    result.boundsMin = bmin;
    result.boundsMax = bmax;
    result.ok = true;
    return result;
}

} //namespace FbxLoader
