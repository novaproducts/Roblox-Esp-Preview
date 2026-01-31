#include "main_api.hpp"
#include <curl.h>
#include "../../ext/json/json.hpp"
#include "../../ext/imgui/stb_image.h"
#include <chrono>
#include <algorithm>
#include <thread>

c_avatar_3d_api::~c_avatar_3d_api() {
    if (running_.load()) {
        running_.store(false);
        queue_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

void c_avatar_3d_api::initialize() {
    if (running_.load()) return;
    running_.store(true);
    worker_ = std::thread(&c_avatar_3d_api::worker_thread, this);

    c_texture_cache::get().initialize();

}

std::string c_avatar_3d_api::get_cdn_url(const std::string& hash) {
    if (hash.length() < 38) return "";

    int i = 31;
    for (int t = 0; t < 38 && t < static_cast<int>(hash.length()); t++) {
        i ^= static_cast<unsigned char>(hash[t]);
    }

    return "https://t" + std::to_string(i % 8) + ".rbxcdn.com/" + hash;
}

static size_t avatar_3d_curl_write(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::vector<unsigned char>* buffer = static_cast<std::vector<unsigned char>*>(userp);
    buffer->insert(buffer->end(),
        static_cast<unsigned char*>(contents),
        static_cast<unsigned char*>(contents) + total);
    return total;
}

std::vector<unsigned char> c_avatar_3d_api::http_get(const std::string& url, bool decompress) {
    CURL* curl = curl_easy_init();
    std::vector<unsigned char> buffer;

    if (!curl) {
        return buffer;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, avatar_3d_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    if (decompress) {
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
        curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
    }

    curl_easy_cleanup(curl);
    return buffer;
}

bool c_avatar_3d_api::fetch_model_json(const std::string& url, nlohmann::json& json) {
    std::vector<unsigned char> data = http_get(url, true);
    if (data.empty()) {
        return false;
    }

    std::string content(data.begin(), data.end());

    if (content.size() >= 2 &&
        static_cast<unsigned char>(content[0]) == 0x1F &&
        static_cast<unsigned char>(content[1]) == 0x8B) {
        int outlen = 0;
        char* decompressed = stbi_zlib_decode_malloc(content.data(), static_cast<int>(content.size()), &outlen);
        if (decompressed && outlen > 0) {
            content = std::string(decompressed, outlen);
            free(decompressed);
        } else {
            if (decompressed) {
                free(decompressed);
            }
            return false;
        }
    }

    if (content.empty() || content[0] != '{') {
        return false;
    }

    try {
        json = nlohmann::json::parse(content);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool c_avatar_3d_api::fetch_api_data(const std::string& user_id, c_avatar_3d_data& data) {
    std::string api_url = "https://thumbnails.roblox.com/v1/users/avatar-3d?userId=" + user_id;
    std::vector<unsigned char> response = http_get(api_url);
    if (response.empty()) {
        return false;
    }

    std::string json_str(response.begin(), response.end());
    if (json_str.empty()) {
        return false;
    }

    try {
        nlohmann::json json = nlohmann::json::parse(json_str);

        if (json.contains("targetId")) {
            if (json["targetId"].is_number()) {
                data.target_id = std::to_string(json["targetId"].get<int64_t>());
            } else {
                data.target_id = json["targetId"].get<std::string>();
            }
        }

        data.state = json.value("state", "");
        data.image_url = json.value("imageUrl", "");

        if (data.state != "Completed" || data.image_url.empty()) {
            return false;
        }

        std::string base_url = data.image_url;
        size_t obj_pos = base_url.find("-Obj");
        if (obj_pos != std::string::npos) {
            base_url = base_url.substr(0, obj_pos);
        }

        nlohmann::json model_json;
        if (!fetch_model_json(data.image_url, model_json)) {
            if (!fetch_model_json(base_url, model_json)) {
                return false;
            }
        }

        if (model_json.contains("camera")) {
            auto& cam = model_json["camera"];
            if (cam.contains("position")) {
                data.camera.position[0] = cam["position"].value("x", 0.0f);
                data.camera.position[1] = cam["position"].value("y", 0.0f);
                data.camera.position[2] = cam["position"].value("z", 0.0f);
            }
            if (cam.contains("direction")) {
                data.camera.direction[0] = cam["direction"].value("x", 0.0f);
                data.camera.direction[1] = cam["direction"].value("y", 0.0f);
                data.camera.direction[2] = cam["direction"].value("z", -1.0f);
            }
            data.camera.fov = cam.value("fov", 70.0f);
        }

        if (model_json.contains("aabb")) {
            auto& aabb = model_json["aabb"];
            if (aabb.contains("min")) {
                data.aabb.min[0] = aabb["min"].value("x", 0.0f);
                data.aabb.min[1] = aabb["min"].value("y", 0.0f);
                data.aabb.min[2] = aabb["min"].value("z", 0.0f);
            }
            if (aabb.contains("max")) {
                data.aabb.max[0] = aabb["max"].value("x", 0.0f);
                data.aabb.max[1] = aabb["max"].value("y", 0.0f);
                data.aabb.max[2] = aabb["max"].value("z", 0.0f);
            }
        }

        data.mtl_hash = model_json.value("mtl", "");
        data.obj_hash = model_json.value("obj", "");

        if (model_json.contains("textures") && model_json["textures"].is_array()) {
            data.texture_hashes.clear();
            for (const auto& tex : model_json["textures"]) {
                data.texture_hashes.push_back(tex.get<std::string>());
            }
        }

        if (model_json.contains("face")) {
            data.face_texture_hash = model_json["face"].get<std::string>();
        } else if (model_json.contains("faceTexture")) {
            data.face_texture_hash = model_json["faceTexture"].get<std::string>();
        } else if (model_json.contains("faceId")) {
            data.face_texture_hash = model_json["faceId"].get<std::string>();
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void c_avatar_3d_api::load_files(c_avatar_3d_data& data) {
    if (data.mtl_hash.empty() || data.obj_hash.empty()) {
        return;
    }

    data.mtl_data = http_get(get_cdn_url(data.mtl_hash), true);
    data.obj_data = http_get(get_cdn_url(data.obj_hash), true);

    data.texture_data.clear();
    data.texture_data.reserve(data.texture_hashes.size());
    for (const auto& hash : data.texture_hashes) {
        data.texture_data.push_back(http_get(get_cdn_url(hash)));
    }

    if (!data.face_texture_hash.empty()) {
        data.face_texture_data = http_get(get_cdn_url(data.face_texture_hash));
    }

    data.ready = !data.mtl_data.empty() && !data.obj_data.empty();
    if (data.ready) {
         data.mtl_data.size(), data.obj_data.size(), data.texture_data.size();
    } 
    else {
    }
}

void c_avatar_3d_api::process_user(const std::string& user_id) {
    std::unordered_map<std::string, int> retry_counts;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> retry_times;

    while (running_.load()) {
        c_avatar_3d_data data;

        if (!fetch_api_data(user_id, data)) {
            int retry_count = retry_counts[user_id];
            auto now = std::chrono::steady_clock::now();

            if (retry_count < max_retries) {
                if (retry_times.find(user_id) == retry_times.end() ||
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - retry_times[user_id]).count() >= retry_delay_ms) {
                    retry_counts[user_id]++;
                    retry_times[user_id] = now;
                    continue;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
            }

            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it = cache_.find(user_id);
                if (it != cache_.end()) {
                    it->second.state = e_avatar_3d_load_state::failed;
                }
            }
            return;
        }

        load_files(data);

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(user_id);
            if (it != cache_.end()) {
                it->second.data = std::move(data);
                it->second.state = it->second.data.ready ? e_avatar_3d_load_state::loaded : e_avatar_3d_load_state::failed;
                it->second.last_update = std::chrono::steady_clock::now();
            }
        }

        return;
    }
}

void c_avatar_3d_api::worker_thread() {

    while (running_.load()) {
        std::string user_id;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load();
            });

            if (!running_.load() && queue_.empty()) {
                break;
            }

            if (!queue_.empty()) {
                user_id = std::move(queue_.front());
                queue_.pop();
            }
        }

        if (!user_id.empty()) {
            process_user(user_id);
        }
    }

}

c_avatar_3d_data* c_avatar_3d_api::request_data(const std::string& user_id) {
    if (user_id.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(user_id);
        if (it != cache_.end()) {
            if (it->second.state == e_avatar_3d_load_state::loaded) {
                return &it->second.data;
            }
            if (it->second.state == e_avatar_3d_load_state::loading) {
                return nullptr;
            }
        } else {
            c_avatar_3d_cache_entry entry;
            entry.state = e_avatar_3d_load_state::loading;
            entry.last_update = std::chrono::steady_clock::now();
            cache_[user_id] = entry;
        }
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        bool already_queued = false;
        std::queue<std::string> temp = queue_;
        while (!temp.empty()) {
            if (temp.front() == user_id) {
                already_queued = true;
                break;
            }
            temp.pop();
        }

        if (!already_queued) {
            queue_.push(user_id);
        }
    }

    queue_cv_.notify_one();

    if (!running_.load()) {
        initialize();
    }

    return nullptr;
}

e_avatar_3d_load_state c_avatar_3d_api::get_state(const std::string& user_id) {
    if (user_id.empty()) return e_avatar_3d_load_state::not_loaded;

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(user_id);
    return (it != cache_.end()) ? it->second.state : e_avatar_3d_load_state::not_loaded;
}

void c_avatar_3d_api::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}

void c_avatar_3d_api::clear_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.erase(user_id);
    c_texture_cache::get().clear_user(user_id);

}

bool c_avatar_3d_api::parse_obj_model(const std::vector<unsigned char>& obj_data, c_obj_model& model) {
    return parse_obj(obj_data, model);
}

bool c_avatar_3d_api::parse_mtl_data(const std::vector<unsigned char>& mtl_data, c_obj_model& model, const std::vector<std::string>& texture_hashes) {
    return parse_mtl(mtl_data, model, texture_hashes);
}

void c_avatar_3d_api::request_texture_decode(const std::string& user_id, int texture_index, const std::vector<unsigned char>& data, bool high_priority) {
    c_texture_cache::get().request_texture(user_id, texture_index, data, high_priority);
}

void c_avatar_3d_api::request_face_texture_decode(const std::string& user_id, const std::vector<unsigned char>& data) {
    c_texture_cache::get().request_face_texture(user_id, data);
}

c_decoded_texture* c_avatar_3d_api::get_decoded_texture(const std::string& user_id, int texture_index) {
    return c_texture_cache::get().get_texture(user_id, texture_index);
}

c_decoded_texture* c_avatar_3d_api::get_decoded_face_texture(const std::string& user_id) {
    return c_texture_cache::get().get_face_texture(user_id);
}