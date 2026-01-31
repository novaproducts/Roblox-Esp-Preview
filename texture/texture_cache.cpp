#include "texture_cache.hpp"
#include "../../ext/imgui/stb_image.h"
#include <algorithm>
#include <chrono>
#include <iostream>


c_texture_cache::~c_texture_cache() {
    if (running_.load(std::memory_order_acquire)) {
        running_.store(false, std::memory_order_release);
        queue_cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

void c_texture_cache::initialize(int worker_count) {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(true, std::memory_order_release);

    if (worker_count <= 0) {
        unsigned int hw_threads = std::thread::hardware_concurrency();
        worker_count = (hw_threads > 4) ? 3 : ((hw_threads > 2) ? 2 : 1);
    }

    workers_.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&c_texture_cache::worker_thread, this, i);
    }

}

c_user_texture_cache* c_texture_cache::get_user_cache(const std::string& user_id) {
    {
        std::shared_lock<std::shared_mutex> read_lock(cache_mutex_);
        auto it = cache_.find(user_id);
        if (it != cache_.end()) {
            return it->second.get();
        }
    }

    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    auto& cache = cache_[user_id];
    if (!cache) {
        cache = std::make_unique<c_user_texture_cache>();
    }
    return cache.get();
}

void c_texture_cache::request_texture(const std::string& user_id, int texture_index,
                                      const std::vector<unsigned char>& data, bool high_priority) {
    if (data.empty() || user_id.empty()) {
        return;
    }

    auto* user_cache = get_user_cache(user_id);
    if (user_cache) {
        std::shared_lock<std::shared_mutex> lock(user_cache->mutex);
        auto it = user_cache->textures.find(texture_index);
        if (it != user_cache->textures.end()) {
            if (it->second->ready.load(std::memory_order_acquire) ||
                it->second->decoding.load(std::memory_order_acquire)) {
                return;
            }
        }
    }

    decode_task task;
    task.user_id = user_id;
    task.texture_index = texture_index;
    task.data = data;
    task.is_face = false;
    task.priority = high_priority ? priority_high : priority_normal;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
    }

    queue_cv_.notify_one();
}

void c_texture_cache::request_face_texture(const std::string& user_id,
                                           const std::vector<unsigned char>& data) {
    if (data.empty() || user_id.empty()) {
        return;
    }

    auto* user_cache = get_user_cache(user_id);
    if (user_cache) {
        std::shared_lock<std::shared_mutex> lock(user_cache->mutex);
        if (user_cache->face_texture->ready.load(std::memory_order_acquire) ||
            user_cache->face_texture->decoding.load(std::memory_order_acquire)) {
            return;
        }
    }

    decode_task task;
    task.user_id = user_id;
    task.texture_index = -1;
    task.data = data;
    task.is_face = true;
    task.priority = priority_face;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
    }

    queue_cv_.notify_one();
}

void c_texture_cache::worker_thread(int worker_id) {
    while (running_.load(std::memory_order_acquire)) {
        decode_task task;
        bool has_task = false;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_.load(std::memory_order_acquire);
            });

            if (!running_.load(std::memory_order_acquire) && task_queue_.empty()) {
                break;
            }

            if (!task_queue_.empty()) {
                task = std::move(const_cast<decode_task&>(task_queue_.top()));
                task_queue_.pop();
                has_task = true;
            }
        }

        if (has_task) {
            active_workers_.fetch_add(1, std::memory_order_relaxed);

            bool success = false;
            if (task.is_face) {
                success = decode_face_texture(task.user_id, task.data);
            } else {
                success = decode_texture(task.user_id, task.texture_index, task.data);
            }

            active_workers_.fetch_sub(1, std::memory_order_relaxed);

            if (!success) {
                // fuck you skids, -- nova (failed)
            }
        }
    }
}

bool c_texture_cache::decode_texture(const std::string& user_id, int texture_index,
                                     const std::vector<unsigned char>& data) {
    auto* user_cache = get_user_cache(user_id);
    if (!user_cache) {
        return false;
    }

    c_decoded_texture* texture = nullptr;
    {
        std::unique_lock<std::shared_mutex> lock(user_cache->mutex);
        auto it = user_cache->textures.find(texture_index);
        if (it == user_cache->textures.end()) {
            auto new_texture = std::make_unique<c_decoded_texture>();
            texture = new_texture.get();
            user_cache->textures[texture_index] = std::move(new_texture);
        } else {
            texture = it->second.get();
        }
    }

    if (!texture) {
        return false;
    }

    texture->decoding.store(true, std::memory_order_release);
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &width, &height, &channels, 4
    );

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        texture->decoding.store(false, std::memory_order_release);
        return false;
    }
    size_t pixel_count = width * height * 4;
    texture->pixels.resize(pixel_count);
    std::memcpy(texture->pixels.data(), pixels, pixel_count);
    stbi_image_free(pixels);
    texture->width = width;
    texture->height = height;
    texture->channels = 4;
    texture->update_metrics();
    texture->decoding.store(false, std::memory_order_release);
    texture->ready.store(true, std::memory_order_release);
    user_cache->memory_usage.fetch_add(pixel_count, std::memory_order_relaxed);
    total_memory_usage_.fetch_add(pixel_count, std::memory_order_relaxed);

    return true;
}

bool c_texture_cache::decode_face_texture(const std::string& user_id,
                                          const std::vector<unsigned char>& data) {
    auto* user_cache = get_user_cache(user_id);
    if (!user_cache || !user_cache->face_texture) {
        return false;
    }

    auto& texture = user_cache->face_texture;
    texture->decoding.store(true, std::memory_order_release);
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &width, &height, &channels, 4
    );

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        texture->decoding.store(false, std::memory_order_release);
        return false;
    }
    size_t pixel_count = width * height * 4;
    texture->pixels.resize(pixel_count);
    std::memcpy(texture->pixels.data(), pixels, pixel_count);
    stbi_image_free(pixels);
    texture->width = width;
    texture->height = height;
    texture->channels = 4;
    texture->update_metrics();
    texture->decoding.store(false, std::memory_order_release);
    texture->ready.store(true, std::memory_order_release);
    user_cache->memory_usage.fetch_add(pixel_count, std::memory_order_relaxed);
    total_memory_usage_.fetch_add(pixel_count, std::memory_order_relaxed);

    return true;
}

c_decoded_texture* c_texture_cache::get_texture(const std::string& user_id, int texture_index) {
    std::shared_lock<std::shared_mutex> cache_lock(cache_mutex_);
    auto it = cache_.find(user_id);
    if (it == cache_.end()) {
        return nullptr;
    }

    auto* user_cache = it->second.get();
    std::shared_lock<std::shared_mutex> user_lock(user_cache->mutex);

    auto tex_it = user_cache->textures.find(texture_index);
    if (tex_it != user_cache->textures.end()) {
        bool ready = tex_it->second->ready.load(std::memory_order_acquire);
        bool decoding = tex_it->second->decoding.load(std::memory_order_acquire);
        if (ready) {
            return tex_it->second.get();
        }
    }

    return nullptr;
}

c_decoded_texture* c_texture_cache::get_face_texture(const std::string& user_id) {
    std::shared_lock<std::shared_mutex> cache_lock(cache_mutex_);
    auto it = cache_.find(user_id);
    if (it == cache_.end()) {
        return nullptr;
    }

    auto* user_cache = it->second.get();
    std::shared_lock<std::shared_mutex> user_lock(user_cache->mutex);

    if (user_cache->face_texture &&
        user_cache->face_texture->ready.load(std::memory_order_acquire)) {
        return user_cache->face_texture.get();
    }

    return nullptr;
}

void c_texture_cache::clear_user(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    auto it = cache_.find(user_id);
    if (it != cache_.end()) {
        size_t user_memory = it->second->memory_usage.load(std::memory_order_relaxed);
        total_memory_usage_.fetch_sub(user_memory, std::memory_order_relaxed);
        cache_.erase(it);
    }
}

void c_texture_cache::clear_all() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    cache_.clear();
    total_memory_usage_.store(0, std::memory_order_relaxed);
}

size_t c_texture_cache::get_memory_usage() const {
    return total_memory_usage_.load(std::memory_order_relaxed);
}

size_t c_texture_cache::get_queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    return task_queue_.size();
}

int c_texture_cache::get_active_workers() const {
    return active_workers_.load(std::memory_order_relaxed);
}