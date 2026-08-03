#include "meat2d/scene/SceneSnapshot.hpp"

#include <utility>

namespace meat2d::scene {

std::optional<SceneSnapshot> capture_snapshot(const Scene& scene, std::size_t maximum_bytes) {
    if (maximum_bytes == 0U || maximum_bytes > maximum_scene_snapshot_bytes) {
        return std::nullopt;
    }
    auto bytes = scene.serialize();
    if (bytes.empty() || bytes.size() > maximum_bytes) {
        return std::nullopt;
    }
    return SceneSnapshot{
        .state_hash = scene.state_hash(),
        .bytes = std::move(bytes),
    };
}

std::optional<Scene> decode_snapshot(const SceneSnapshot& snapshot, std::size_t maximum_bytes) {
    if (maximum_bytes == 0U || maximum_bytes > maximum_scene_snapshot_bytes ||
        snapshot.bytes.empty() || snapshot.bytes.size() > maximum_bytes) {
        return std::nullopt;
    }
    auto scene = Scene::deserialize(snapshot.bytes);
    if (!scene || scene->state_hash() != snapshot.state_hash) {
        return std::nullopt;
    }
    return scene;
}

bool restore_snapshot(Scene& target, const SceneSnapshot& snapshot, std::size_t maximum_bytes) {
    auto scene = decode_snapshot(snapshot, maximum_bytes);
    if (!scene) {
        return false;
    }
    target = std::move(*scene);
    return true;
}

} // namespace meat2d::scene
