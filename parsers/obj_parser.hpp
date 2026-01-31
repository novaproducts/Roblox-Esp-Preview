#pragma once
#include <vector>
#include <string>
#include <unordered_map>

struct c_obj_vertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float u = 0.0f, v = 0.0f;
};

struct c_obj_material {
    float diffuse[3] = {0.8f, 0.8f, 0.8f};
    float ambient[3] = {0.2f, 0.2f, 0.2f};
    float specular[3] = {1.0f, 1.0f, 1.0f};
    float shininess = 0.0f;
    std::string diffuse_texture;
    int texture_index = -1;
    float sampled_color[3] = {0.8f, 0.8f, 0.8f};
};

struct c_obj_face {
    std::vector<int> vertex_indices;
    std::vector<int> texcoord_indices;
    std::string material_name;
    float normal[3] = {0.0f, 1.0f, 0.0f};
};

struct c_obj_model {
    std::vector<c_obj_vertex> vertices;
    std::vector<c_obj_vertex> tex_coords;
    std::vector<c_obj_face> faces;
    std::unordered_map<std::string, c_obj_material> materials;
    std::string current_material;
    bool valid = false;
};

bool parse_obj(const std::vector<unsigned char>& obj_data, c_obj_model& model);