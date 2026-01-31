#include "mtl_parser.hpp"
#include "../../ext/imgui/stb_image.h"
#include <algorithm>
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

static std::string extract_hash_from_path(const std::string& path) {
    size_t last_slash = path.find_last_of('/');
    std::string filename = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;

    size_t dash_pos = filename.find('-');
    if (dash_pos != std::string::npos && dash_pos + 1 < filename.length()) {
        return filename.substr(dash_pos + 1);
    }

    if (filename.length() >= 32) {
        return filename.substr(filename.length() - 32);
    }

    return filename;
}

static int find_texture_index(const std::string& texture_path, const std::vector<std::string>& texture_hashes) {
    std::string extracted_hash = extract_hash_from_path(texture_path);

    for (size_t i = 0; i < texture_hashes.size(); i++) {
        const std::string& hash = texture_hashes[i];
        if (hash.find(extracted_hash) != std::string::npos ||
            extracted_hash.find(hash) != std::string::npos ||
            hash == extracted_hash) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static float clamp_float(float val) {
    return std::max(0.0f, std::min(1.0f, val));
}

bool parse_mtl(const std::vector<unsigned char>& mtl_data, c_obj_model& model, const std::vector<std::string>& texture_hashes) {
    if (mtl_data.empty()) {
        return false;
    }

    std::vector<unsigned char> decompressed = decompress_data(mtl_data);
    if (decompressed.empty()) {
        return false;
    }

    std::string content = clean_string(decompressed);
    if (content.size() < 5) {
        return false;
    }

    std::string current_material;
    int texture_counter = 0;
    size_t material_count = 0;

    size_t line_start = 0;
    size_t content_len = content.length();

    while (line_start < content_len) {
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = content_len;
        }

        std::string line = content.substr(line_start, line_end - line_start);
        line_start = line_end + 1;

        trim_whitespace(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (line.length() < 3) continue;

        size_t space_pos = line.find(' ');
        if (space_pos == std::string::npos) space_pos = line.find('\t');
        if (space_pos == std::string::npos) continue;

        std::string type = line.substr(0, space_pos);
        std::string data = line.substr(space_pos + 1);
        trim_whitespace(data);

        if (type == "newmtl") {
            current_material = data;
            if (!current_material.empty()) {
                model.materials[current_material] = c_obj_material();
                material_count++;
            }
        }
        else if (!current_material.empty() && model.materials.find(current_material) != model.materials.end()) {
            c_obj_material& mat = model.materials[current_material];

            if (type == "Kd") {
                const char* str = data.c_str();
                char* end;
                mat.diffuse[0] = clamp_float(std::strtof(str, &end));
                mat.diffuse[1] = clamp_float(std::strtof(end, &end));
                mat.diffuse[2] = clamp_float(std::strtof(end, &end));
            }
            else if (type == "Ka") {
                const char* str = data.c_str();
                char* end;
                mat.ambient[0] = clamp_float(std::strtof(str, &end));
                mat.ambient[1] = clamp_float(std::strtof(end, &end));
                mat.ambient[2] = clamp_float(std::strtof(end, &end));
            }
            else if (type == "Ks") {
                const char* str = data.c_str();
                char* end;
                mat.specular[0] = clamp_float(std::strtof(str, &end));
                mat.specular[1] = clamp_float(std::strtof(end, &end));
                mat.specular[2] = clamp_float(std::strtof(end, &end));
            }
            else if (type == "Ns") {
                const char* str = data.c_str();
                char* end;
                float shininess = std::strtof(str, &end);
                mat.shininess = std::max(0.0f, shininess);
            }
            else if (type == "map_Kd") {
                mat.diffuse_texture = data;
                int tex_idx = find_texture_index(data, texture_hashes);
                mat.texture_index = (tex_idx >= 0) ? tex_idx : texture_counter++;
            }
        }
    }

    return material_count > 0;
}