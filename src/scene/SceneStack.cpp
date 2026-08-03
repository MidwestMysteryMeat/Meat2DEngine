#include "meat2d/scene/SceneStack.hpp"

#include <algorithm>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::size_t maximum_scene_transitions = 1'024U;

} // namespace

bool SceneStack::register_scene(std::string name, Scene scene) {
    if (name.empty() || has_scene(name)) {
        return false;
    }
    scenes_.push_back(Entry{.name = std::move(name), .scene = std::move(scene)});
    if (stack_.empty()) {
        stack_.push_back(scenes_.size() - 1U);
    }
    return true;
}

bool SceneStack::unregister_scene(std::string_view name) {
    const auto index = find_index(name);
    if (index == scenes_.size() ||
        std::find(stack_.begin(), stack_.end(), index) != stack_.end()) {
        return false;
    }
    scenes_.erase(scenes_.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& stack_index : stack_) {
        if (stack_index > index) {
            --stack_index;
        }
    }
    return true;
}

bool SceneStack::has_scene(std::string_view name) const noexcept {
    return find_index(name) != scenes_.size();
}

bool SceneStack::replace(std::string_view name) {
    const auto index = find_index(name);
    if (index == scenes_.size()) {
        return false;
    }
    const auto from = active_name();
    if (stack_.empty()) {
        stack_.push_back(index);
    } else {
        stack_.back() = index;
    }
    record_transition(SceneTransition{
        .type = SceneTransitionType::Replace,
        .from = std::string(from),
        .to = std::string(name),
    });
    return true;
}

bool SceneStack::push(std::string_view name) {
    const auto index = find_index(name);
    if (index == scenes_.size()) {
        return false;
    }
    const auto from = active_name();
    stack_.push_back(index);
    record_transition(SceneTransition{
        .type = SceneTransitionType::Push,
        .from = std::string(from),
        .to = std::string(name),
    });
    return true;
}

bool SceneStack::pop() {
    if (stack_.size() <= 1U) {
        return false;
    }
    const auto from = active_name();
    stack_.pop_back();
    record_transition(SceneTransition{
        .type = SceneTransitionType::Pop,
        .from = std::string(from),
        .to = std::string(active_name()),
    });
    return true;
}

Scene* SceneStack::active() noexcept {
    return stack_.empty() ? nullptr : &scenes_[stack_.back()].scene;
}

const Scene* SceneStack::active() const noexcept {
    return stack_.empty() ? nullptr : &scenes_[stack_.back()].scene;
}

std::string_view SceneStack::active_name() const noexcept {
    return stack_.empty() ? std::string_view{} : scenes_[stack_.back()].name;
}

Scene* SceneStack::find(std::string_view name) noexcept {
    const auto index = find_index(name);
    return index == scenes_.size() ? nullptr : &scenes_[index].scene;
}

const Scene* SceneStack::find(std::string_view name) const noexcept {
    const auto index = find_index(name);
    return index == scenes_.size() ? nullptr : &scenes_[index].scene;
}

std::size_t SceneStack::depth() const noexcept {
    return stack_.size();
}

std::span<const SceneTransition> SceneStack::transitions() const noexcept {
    return transitions_;
}

void SceneStack::clear_transitions() noexcept {
    transitions_.clear();
}

std::size_t SceneStack::find_index(std::string_view name) const noexcept {
    const auto iterator = std::find_if(
        scenes_.begin(), scenes_.end(), [name](const Entry& entry) { return entry.name == name; });
    return iterator == scenes_.end()
               ? scenes_.size()
               : static_cast<std::size_t>(std::distance(scenes_.begin(), iterator));
}

void SceneStack::record_transition(SceneTransition transition) {
    if (transitions_.size() < maximum_scene_transitions) {
        transitions_.push_back(std::move(transition));
    }
}

} // namespace meat2d::scene
