#include "scene.h"

#include "utilities.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/string_cast.hpp>
#include "json.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <cfloat>

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;
using json = nlohmann::json;

Scene::Scene(string filename)
{
    cout << "Reading scene from " << filename << " ..." << endl;
    cout << " " << endl;
    auto ext = filename.substr(filename.find_last_of('.'));
    if (ext == ".json")
    {
        loadFromJSON(filename);
        return;
    }
    else
    {
        cout << "Couldn't read from " << filename << endl;
        exit(-1);
    }
}

void Scene::loadFromJSON(const std::string& jsonName)
{
    std::ifstream f(jsonName);
    json data = json::parse(f);
    const auto& materialsData = data["Materials"];
    std::unordered_map<std::string, uint32_t> MatNameToID;
    for (const auto& item : materialsData.items())
    {
        const auto& name = item.key();
        const auto& p = item.value();
        Material newMaterial{};
        // TODO: handle materials loading differently
        if (p["TYPE"] == "Diffuse")
        {
            const auto& col = p["RGB"];
            newMaterial.color = glm::vec3(col[0], col[1], col[2]);
        }
        else if (p["TYPE"] == "Emitting")
        {
            const auto& col = p["RGB"];
            newMaterial.color = glm::vec3(col[0], col[1], col[2]);
            newMaterial.emittance = p["EMITTANCE"];
        }
        else if (p["TYPE"] == "Specular")
        {
            const auto& col = p["RGB"];
            newMaterial.color = glm::vec3(col[0], col[1], col[2]);
            newMaterial.specular.color = newMaterial.color;
            newMaterial.hasReflective = 1.0f;
        }
        else if (p["TYPE"] == "Refractive")
        {
            const auto& col = p["RGB"];
            newMaterial.color = glm::vec3(col[0], col[1], col[2]);
            newMaterial.specular.color = newMaterial.color;
            newMaterial.hasRefractive = 1.0f;
            newMaterial.indexOfRefraction = p["IOR"];
        }
        MatNameToID[name] = materials.size();
        materials.emplace_back(newMaterial);
    }
    const auto& objectsData = data["Objects"];
    for (const auto& p : objectsData)
    {
        const auto& type = p["TYPE"];
        Geom newGeom;
        if (type == "cube")
        {
            newGeom.type = CUBE;
        }

        else if (type == "mesh")
        {
            newGeom.type = MESH;
        }

        else
        {
            newGeom.type = SPHERE;
        }
        newGeom.materialid = MatNameToID[p["MATERIAL"]];
        const auto& trans = p["TRANS"];
        const auto& rotat = p["ROTAT"];
        const auto& scale = p["SCALE"];
        newGeom.translation = glm::vec3(trans[0], trans[1], trans[2]);
        newGeom.rotation = glm::vec3(rotat[0], rotat[1], rotat[2]);
        newGeom.scale = glm::vec3(scale[0], scale[1], scale[2]);
        newGeom.transform = utilityCore::buildTransformationMatrix(
            newGeom.translation, newGeom.rotation, newGeom.scale);
        newGeom.inverseTransform = glm::inverse(newGeom.transform);
        newGeom.invTranspose = glm::inverseTranspose(newGeom.transform);

        if (newGeom.type == MESH)
        {
            
            std::string baseDir = jsonName.substr(0, jsonName.find_last_of("/\\") + 1);
            loadOBJ(baseDir + p["FILE"].get<std::string>(), newGeom);
        }


        geoms.push_back(newGeom);
    }
    const auto& cameraData = data["Camera"];
    Camera& camera = state.camera;
    RenderState& state = this->state;
    camera.resolution.x = cameraData["RES"][0];
    camera.resolution.y = cameraData["RES"][1];
    float fovy = cameraData["FOVY"];
    state.iterations = cameraData["ITERATIONS"];
    state.traceDepth = cameraData["DEPTH"];
    state.imageName = cameraData["FILE"];
    const auto& pos = cameraData["EYE"];
    const auto& lookat = cameraData["LOOKAT"];
    const auto& up = cameraData["UP"];
    camera.position = glm::vec3(pos[0], pos[1], pos[2]);
    camera.lookAt = glm::vec3(lookat[0], lookat[1], lookat[2]);
    camera.up = glm::vec3(up[0], up[1], up[2]);
    camera.lensRadius = cameraData.value("LENS_RADIUS", 0.0f);
    camera.focalDistance = cameraData.value("FOCAL_DISTANCE", 10.0f);

    //calculate fov based on resolution
    float yscaled = tan(fovy * (PI / 180));
    float xscaled = (yscaled * camera.resolution.x) / camera.resolution.y;
    float fovx = (atan(xscaled) * 180) / PI;
    camera.fov = glm::vec2(fovx, fovy);

    camera.right = glm::normalize(glm::cross(camera.view, camera.up));
    camera.pixelLength = glm::vec2(2 * xscaled / (float)camera.resolution.x,
        2 * yscaled / (float)camera.resolution.y);

    camera.view = glm::normalize(camera.lookAt - camera.position);

    //set up render camera stuff
    int arraylen = camera.resolution.x * camera.resolution.y;
    state.image.resize(arraylen);
    std::fill(state.image.begin(), state.image.end(), glm::vec3());
}


void Scene::loadOBJ(const std::string& path, Geom& geom)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> objMaterials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &objMaterials, &warn, &err, path.c_str());
    if (!warn.empty()) cout << "OBJ warning: " << warn << endl;
    if (!ok)
    {
        cerr << "Failed to load OBJ " << path << ": " << err << endl;
        exit(-1);
    }

    geom.triStart = (int)triangles.size();
    geom.bboxMin = glm::vec3(FLT_MAX);
    geom.bboxMax = glm::vec3(-FLT_MAX);

    for (const auto& shape : shapes)
    {
        const auto& idx = shape.mesh.indices;   // LoadObj 默认会三角化：每 3 个索引是一个三角形
        for (size_t f = 0; f + 2 < idx.size(); f += 3)
        {
            glm::vec3 v[3], n[3];
            bool hasNormals = true;
            for (int k = 0; k < 3; k++)
            {
                const tinyobj::index_t& id = idx[f + k];
                glm::vec3 pos(attrib.vertices[3 * id.vertex_index + 0],
                    attrib.vertices[3 * id.vertex_index + 1],
                    attrib.vertices[3 * id.vertex_index + 2]);
                v[k] = glm::vec3(geom.transform * glm::vec4(pos, 1.0f));   // 变换到世界坐标

                if (id.normal_index >= 0)
                {
                    glm::vec3 nrm(attrib.normals[3 * id.normal_index + 0],
                        attrib.normals[3 * id.normal_index + 1],
                        attrib.normals[3 * id.normal_index + 2]);
                    n[k] = glm::normalize(glm::vec3(geom.invTranspose * glm::vec4(nrm, 0.0f)));
                }
                else
                {
                    hasNormals = false;
                }
            }

            glm::vec3 faceN = glm::cross(v[1] - v[0], v[2] - v[0]);
            if (glm::length(faceN) < 1e-12f) continue;   // 跳过退化的三角形
            if (!hasNormals)
            {
                n[0] = n[1] = n[2] = glm::normalize(faceN);
            }

            Triangle tri;
            tri.v0 = v[0]; tri.v1 = v[1]; tri.v2 = v[2];
            tri.n0 = n[0]; tri.n1 = n[1]; tri.n2 = n[2];
            triangles.push_back(tri);

            for (int k = 0; k < 3; k++)
            {
                geom.bboxMin = glm::min(geom.bboxMin, v[k]);
                geom.bboxMax = glm::max(geom.bboxMax, v[k]);
            }
        }
    }

    geom.triCount = (int)triangles.size() - geom.triStart;
    cout << "Loaded " << path << ": " << geom.triCount << " triangles" << endl;
}
