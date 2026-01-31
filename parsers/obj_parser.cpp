#include "obj_parser.hpp"
#include "../../ext/imgui/stb_image.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>


static std::vector<unsigned char> decompress_data(const std::vector<unsigned char>& data) {
    if (data.size() < 2 || data[0] != 0x1F || data[1] != 0x8B) {
        return data;
    }

    int outlen = 0;
    char* decompressed = stbi_zlib_decode_malloc(
        reinterpret_cast<const char*>(data.data()),
        static_cast<int>(data.size()),
        &outlen
    );

    if (!decompressed || outlen <= 0) {
        if (decompressed) {
            free(decompressed);
        }
        return {};
    }

    std::vector<unsigned char> result(
        reinterpret_cast<unsigned char*>(decompressed),
        reinterpret_cast<unsigned char*>(decompressed) + outlen
    );
    free(decompressed);

    return result;
}

static std::string clean_string(const std::vector<unsigned char>& data) {
    std::string result;
    result.reserve(data.size());

    for (unsigned char c : data) {
        if (c == '\0') break;
        if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
            result += static_cast<char>(c);
        }
    }

    return result;
}

static inline void trim_whitespace(std::string& str) {
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

static inline bool parse_int(const char* str, int& out) {
    char* end;
    out = static_cast<int>(std::strtol(str, &end, 10));
    return end != str;
}

static void calculate_face_normal(c_obj_face& face, const std::vector<c_obj_vertex>& vertices) {
    if (face.vertex_indices.size() < 3) return;

    int idx0 = face.vertex_indices[0];
    int idx1 = face.vertex_indices[1];
    int idx2 = face.vertex_indices[2];

    if (idx0 < 0 || idx0 >= static_cast<int>(vertices.size()) ||
        idx1 < 0 || idx1 >= static_cast<int>(vertices.size()) ||
        idx2 < 0 || idx2 >= static_cast<int>(vertices.size())) {
        return;
    }

    const auto& v0 = vertices[idx0];
    const auto& v1 = vertices[idx1];
    const auto& v2 = vertices[idx2];

    float dx1 = v1.x - v0.x;
    float dy1 = v1.y - v0.y;
    float dz1 = v1.z - v0.z;

    float dx2 = v2.x - v0.x;
    float dy2 = v2.y - v0.y;
    float dz2 = v2.z - v0.z;

    float nx = dy1 * dz2 - dz1 * dy2;
    float ny = dz1 * dx2 - dx1 * dz2;
    float nz = dx1 * dy2 - dy1 * dx2;

    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.0001f) {
        face.normal[0] = nx / len;
        face.normal[1] = ny / len;
        face.normal[2] = nz / len;
    }
}

static int parse_vertex_index(const std::string& token, size_t vertex_count) {
    if (token.empty()) return -1;

    int idx = 0;
    if (!parse_int(token.c_str(), idx)) return -1;

    if (idx > 0) {
        return idx - 1;
    }
    else if (idx < 0) {
        int abs_idx = std::abs(idx);
        if (abs_idx <= static_cast<int>(vertex_count)) {
            return static_cast<int>(vertex_count) - abs_idx;
        }
    }

    return -1;
}

static void parse_face_line(const std::string& line, c_obj_face& face,
    const std::vector<c_obj_vertex>& vertices,
    const std::vector<c_obj_vertex>& tex_coords) {
    size_t start = 0;
    size_t pos = 0;

    while (pos < line.length()) {
        if (line[pos] == ' ' || line[pos] == '\t') {
            if (pos > start) {
                std::string token = line.substr(start, pos - start);

                size_t slash1 = token.find('/');
                size_t slash2 = (slash1 != std::string::npos) ? token.find('/', slash1 + 1) : std::string::npos;

                std::string v_str, vt_str, vn_str;

                if (slash1 == std::string::npos) {
                    v_str = token;
                }
                else {
                    v_str = token.substr(0, slash1);
                    if (slash2 != std::string::npos) {
                        vt_str = token.substr(slash1 + 1, slash2 - slash1 - 1);
                        vn_str = token.substr(slash2 + 1);
                    }
                    else {
                        vt_str = token.substr(slash1 + 1);
                    }
                }

                int v_idx = parse_vertex_index(v_str, vertices.size());
                if (v_idx >= 0 && v_idx < static_cast<int>(vertices.size())) {
                    face.vertex_indices.push_back(v_idx);
                }

                if (!vt_str.empty()) {
                    int vt_idx = parse_vertex_index(vt_str, tex_coords.size());
                    face.texcoord_indices.push_back(vt_idx);
                }
                else {
                    face.texcoord_indices.push_back(-1);
                }
            }
            while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            start = pos;
        }
        else {
            pos++;
        }
    }

    if (pos > start) {
        std::string token = line.substr(start);

        size_t slash1 = token.find('/');
        size_t slash2 = (slash1 != std::string::npos) ? token.find('/', slash1 + 1) : std::string::npos;

        std::string v_str, vt_str, vn_str;

        if (slash1 == std::string::npos) {
            v_str = token;
        }
        else {
            v_str = token.substr(0, slash1);
            if (slash2 != std::string::npos) {
                vt_str = token.substr(slash1 + 1, slash2 - slash1 - 1);
                vn_str = token.substr(slash2 + 1);
            }
            else {
                vt_str = token.substr(slash1 + 1);
            }
        }

        int v_idx = parse_vertex_index(v_str, vertices.size());
        if (v_idx >= 0 && v_idx < static_cast<int>(vertices.size())) {
            face.vertex_indices.push_back(v_idx);
        }

        if (!vt_str.empty()) {
            int vt_idx = parse_vertex_index(vt_str, tex_coords.size());
            face.texcoord_indices.push_back(vt_idx);
        }
        else {
            face.texcoord_indices.push_back(-1);
        }
    }

    while (face.texcoord_indices.size() < face.vertex_indices.size()) {
        face.texcoord_indices.push_back(-1);
    }
}

bool parse_obj(const std::vector<unsigned char>& obj_data, c_obj_model& model) {
    if (obj_data.empty()) {
        return false;
    }

    std::vector<unsigned char> decompressed = decompress_data(obj_data);
    if (decompressed.empty()) {
        return false;
    }

    std::string content = clean_string(decompressed);
    if (content.size() < 10 || content.find("v ") == std::string::npos) {
        return false;
    }

    model.vertices.clear();
    model.tex_coords.clear();
    model.faces.clear();
    model.materials.clear();
    model.valid = false;

    model.vertices.reserve(10000);
    model.tex_coords.reserve(10000);
    model.faces.reserve(5000);

    std::string current_material = "";

    size_t line_start = 0;
    size_t content_len = content.length();

    int face_debug_count = 0;
    int material_switch_count = 0;
    int tex_coord_count = 0;
    int line_number = 0;
    bool showed_vt_samples = false;

    while (line_start < content_len) {
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = content_len;
        }

        std::string line = content.substr(line_start, line_end - line_start);
        line_start = line_end + 1;
        line_number++;

        trim_whitespace(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (line.length() < 2) continue;

        if (line.compare(0, 7, "usemtl ") == 0) {
            current_material = line.substr(7);
            trim_whitespace(current_material);
            material_switch_count++;
            continue;
        }

        size_t space_pos = line.find(' ');
        if (space_pos == std::string::npos) space_pos = line.find('\t');
        if (space_pos == std::string::npos) continue;

        std::string prefix = line.substr(0, space_pos);
        std::string data = line.substr(space_pos + 1);
        trim_whitespace(data);

        if (prefix == "v") {
            c_obj_vertex v;
            const char* str = data.c_str();
            char* end;
            v.x = std::strtof(str, &end);
            if (end == str) continue;
            v.y = std::strtof(end, &end);
            if (end == str) continue;
            v.z = std::strtof(end, &end);
            model.vertices.push_back(v);
        }
        else if (prefix == "vt") {
            c_obj_vertex vt;
            const char* str = data.c_str();
            char* end;
            vt.u = std::strtof(str, &end);
            if (end == str) continue;
            vt.v = std::strtof(end, &end);
            model.tex_coords.push_back(vt);
            tex_coord_count++;

            if (tex_coord_count == 5) {
                showed_vt_samples = true;
            }
        }
        else if (prefix == "f") {
            c_obj_face face;
            face.material_name = current_material;

            parse_face_line(data, face, model.vertices, model.tex_coords);

            if (face.vertex_indices.size() >= 3) {
                calculate_face_normal(face, model.vertices);
                model.faces.push_back(face);
            }
        }
    }

    model.valid = !model.vertices.empty() && !model.faces.empty();
    return model.valid;
}