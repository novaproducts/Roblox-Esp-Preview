#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <chrono>
#include <curl.h>
#include "json/json.hpp"
#include "parsers/obj_parser.hpp"
#include "parsers/mtl_parser.hpp"
#include "texture/texture_cache.hpp"

struct c_avatar_3d_data {
    std::string target_id;
    std::string state;
    std::string image_url;

    struct c_camera {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float direction[3] = {0.0f, 0.0f, -1.0f};
        float fov = 70.0f;
    } camera;

    struct c_aabb {
        float min[3] = {0.0f, 0.0f, 0.0f};
        float max[3] = {0.0f, 0.0f, 0.0f};
    } aabb;

    std::string mtl_hash;
    std::string obj_hash;
    std::vector<std::string> texture_hashes;
    std::string face_texture_hash;

    std::vector<unsigned char> obj_data;
    std::vector<unsigned char> mtl_data;
    std::vector<std::vector<unsigned char>> texture_data;
    std::vector<unsigned char> face_texture_data;

    bool ready = false;
};

enum class e_avatar_3d_load_state {
    not_loaded,
    loading,
    loaded,
    failed
};

struct c_avatar_3d_cache_entry {
    c_avatar_3d_data data;
    e_avatar_3d_load_state state = e_avatar_3d_load_state::not_loaded;
    std::chrono::steady_clock::time_point last_update;
};

class c_avatar_3d_api {
public:
    static c_avatar_3d_api& get() {
        static c_avatar_3d_api instance;
        return instance;
    }

    void initialize();

    c_avatar_3d_data* request_data(const std::string& user_id);
    e_avatar_3d_load_state get_state(const std::string& user_id);

    void clear_cache();
    void clear_user(const std::string& user_id);

    bool parse_obj_model(const std::vector<unsigned char>& obj_data, c_obj_model& model);
    bool parse_mtl_data(const std::vector<unsigned char>& mtl_data, c_obj_model& model, const std::vector<std::string>& texture_hashes);
    void request_texture_decode(const std::string& user_id, int texture_index, const std::vector<unsigned char>& data, bool high_priority = false);
    void request_face_texture_decode(const std::string& user_id, const std::vector<unsigned char>& data);
    c_decoded_texture* get_decoded_texture(const std::string& user_id, int texture_index);
    c_decoded_texture* get_decoded_face_texture(const std::string& user_id);

private:
    c_avatar_3d_api() = default;
    ~c_avatar_3d_api();

    void worker_thread();
    void process_user(const std::string& user_id);
    bool fetch_api_data(const std::string& user_id, c_avatar_3d_data& data);
    bool fetch_model_json(const std::string& url, nlohmann::json& json);
    void load_files(c_avatar_3d_data& data);
    std::vector<unsigned char> http_get(const std::string& url, bool decompress = false);
    std::string get_cdn_url(const std::string& hash);

    std::unordered_map<std::string, c_avatar_3d_cache_entry> cache_;
    std::mutex cache_mutex_;

    std::queue<std::string> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::atomic<bool> running_{false};
    std::thread worker_;

    static constexpr int max_retries = 30;
    static constexpr int retry_delay_ms = 2000;
};