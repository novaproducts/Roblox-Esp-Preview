#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <queue>
#include <condition_variable>
#include <array>
#include <shared_mutex>


struct c_decoded_texture {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::atomic<bool> ready{false};
    std::atomic<bool> decoding{false};
    float inv_width = 0.0f;
    float inv_height = 0.0f;

    inline void update_metrics() {
        inv_width = (width > 0) ? (1.0f / static_cast<float>(width)) : 0.0f;
        inv_height = (height > 0) ? (1.0f / static_cast<float>(height)) : 0.0f;
    }

    inline void sample(float u, float v, float& r, float& g, float& b, float& a) const {
        if (!ready.load(std::memory_order_acquire) || pixels.empty() || width <= 0 || height <= 0) {
            r = g = b = a = 0.0f;
            return;
        }
        u = (u < 0.0f) ? 0.0f : ((u > 1.0f) ? 1.0f : u);
        v = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
        float fx = u * (width - 1.0f);
        float fy = (1.0f - v) * (height - 1.0f);

        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = (x0 < width - 1) ? (x0 + 1) : x0;
        int y1 = (y0 < height - 1) ? (y0 + 1) : y0;
        float frac_x = fx - x0;
        float frac_y = fy - y0;
        const int stride = width * 4;
        const int idx00 = y0 * stride + x0 * 4;
        const int idx10 = y0 * stride + x1 * 4;
        const int idx01 = y1 * stride + x0 * 4;
        const int idx11 = y1 * stride + x1 * 4;
        float inv_255 = 1.0f / 255.0f;
        float r00 = pixels[idx00] * inv_255;
        float g00 = pixels[idx00 + 1] * inv_255;
        float b00 = pixels[idx00 + 2] * inv_255;
        float a00 = pixels[idx00 + 3] * inv_255;
        float r10 = pixels[idx10] * inv_255;
        float g10 = pixels[idx10 + 1] * inv_255;
        float b10 = pixels[idx10 + 2] * inv_255;
        float a10 = pixels[idx10 + 3] * inv_255;
        float r01 = pixels[idx01] * inv_255;
        float g01 = pixels[idx01 + 1] * inv_255;
        float b01 = pixels[idx01 + 2] * inv_255;
        float a01 = pixels[idx01 + 3] * inv_255;
        float r11 = pixels[idx11] * inv_255;
        float g11 = pixels[idx11 + 1] * inv_255;
        float b11 = pixels[idx11 + 2] * inv_255;
        float a11 = pixels[idx11 + 3] * inv_255;
        float r0 = r00 + (r10 - r00) * frac_x;
        float r1 = r01 + (r11 - r01) * frac_x;
        r = r0 + (r1 - r0) * frac_y;
        float g0 = g00 + (g10 - g00) * frac_x;
        float g1 = g01 + (g11 - g01) * frac_x;
        g = g0 + (g1 - g0) * frac_y;
        float b0 = b00 + (b10 - b00) * frac_x;
        float b1 = b01 + (b11 - b01) * frac_x;
        b = b0 + (b1 - b0) * frac_y;
        float a0 = a00 + (a10 - a00) * frac_x;
        float a1 = a01 + (a11 - a01) * frac_x;
        a = a0 + (a1 - a0) * frac_y;
    }
};

struct c_user_texture_cache {
    std::unordered_map<int, std::unique_ptr<c_decoded_texture>> textures;
    std::unique_ptr<c_decoded_texture> face_texture;
    mutable std::shared_mutex mutex; // Allow concurrent reads
    std::atomic<size_t> memory_usage{0};

    c_user_texture_cache() : face_texture(std::make_unique<c_decoded_texture>()) {}
};

class c_texture_cache {
public:
    static c_texture_cache& get() {
        static c_texture_cache instance;
        return instance;
    }

    void initialize(int worker_count = 0);
    void request_texture(const std::string& user_id, int texture_index,
                        const std::vector<unsigned char>& data, bool high_priority = false);
    void request_face_texture(const std::string& user_id,
                             const std::vector<unsigned char>& data);
    c_decoded_texture* get_texture(const std::string& user_id, int texture_index);
    c_decoded_texture* get_face_texture(const std::string& user_id);
    void clear_user(const std::string& user_id);
    void clear_all();
    size_t get_memory_usage() const;
    size_t get_queue_size() const;
    int get_active_workers() const;

private:
    c_texture_cache() = default;
    ~c_texture_cache();
    struct decode_task {
        std::string user_id;
        int texture_index;
        std::vector<unsigned char> data;
        bool is_face;
        int priority; 

        bool operator<(const decode_task& other) const {
            return priority < other.priority; // Priority queue sorts in reverse
        }
    };
    void worker_thread(int worker_id);
    bool decode_texture(const std::string& user_id, int texture_index,
                       const std::vector<unsigned char>& data);
    bool decode_face_texture(const std::string& user_id,
                            const std::vector<unsigned char>& data);
    c_user_texture_cache* get_user_cache(const std::string& user_id);
    std::unordered_map<std::string, std::unique_ptr<c_user_texture_cache>> cache_;
    mutable std::shared_mutex cache_mutex_;
    std::priority_queue<decode_task> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<int> active_workers_{0};
    std::atomic<size_t> total_memory_usage_{0};
    static constexpr int default_worker_count = 3;
    static constexpr int priority_face = 100;
    static constexpr int priority_high = 50;
    static constexpr int priority_normal = 10;
    static constexpr size_t max_memory_mb = 512; // MB
};