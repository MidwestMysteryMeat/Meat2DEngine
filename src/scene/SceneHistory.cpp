#include "meat2d/scene/SceneHistory.hpp"

#include <algorithm>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::size_t minimum_history_entries = 1U;
constexpr std::size_t maximum_history_entries = 4'096U;

} // namespace

SceneHistory::SceneHistory(Scene initial, std::size_t maximum_entries)
    : scene_(std::move(initial)),
      maximum_entries_(std::clamp(maximum_entries, minimum_history_entries,
                                  maximum_history_entries)) {
    snapshots_.push_back(scene_.serialize());
}

Scene& SceneHistory::scene() noexcept {
    return scene_;
}

const Scene& SceneHistory::scene() const noexcept {
    return scene_;
}

bool SceneHistory::checkpoint() {
    auto snapshot = scene_.serialize();
    if (snapshot.empty()) {
        return false;
    }
    if (snapshot == snapshots_[cursor_]) {
        return true;
    }
    if (cursor_ + 1U < snapshots_.size()) {
        snapshots_.erase(snapshots_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1U),
                         snapshots_.end());
    }
    snapshots_.push_back(std::move(snapshot));
    cursor_ = snapshots_.size() - 1U;
    if (snapshots_.size() > maximum_entries_) {
        snapshots_.erase(snapshots_.begin());
        --cursor_;
    }
    return true;
}

bool SceneHistory::undo() {
    return cursor_ > 0U && restore(cursor_ - 1U);
}

bool SceneHistory::redo() {
    return cursor_ + 1U < snapshots_.size() && restore(cursor_ + 1U);
}

void SceneHistory::clear_history() {
    auto snapshot = scene_.serialize();
    if (snapshot.empty()) {
        return;
    }
    snapshots_.clear();
    snapshots_.push_back(std::move(snapshot));
    cursor_ = 0;
}

std::size_t SceneHistory::undo_count() const noexcept {
    return cursor_;
}

std::size_t SceneHistory::redo_count() const noexcept {
    return snapshots_.size() - cursor_ - 1U;
}

std::size_t SceneHistory::maximum_entries() const noexcept {
    return maximum_entries_;
}

bool SceneHistory::restore(std::size_t index) {
    const auto restored = Scene::deserialize(snapshots_[index]);
    if (!restored) {
        return false;
    }
    scene_ = *restored;
    cursor_ = index;
    return true;
}

} // namespace meat2d::scene
