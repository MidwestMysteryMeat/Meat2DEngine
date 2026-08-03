#include "meat2d/ai/NeuralNetwork.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::ai {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer> void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (index * 8U)));
    }
}

std::int32_t saturate(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

} // namespace

bool FixedNeuralNetwork::set_layers(std::vector<NeuralLayer> layers) {
    if (layers.empty() || layers.size() > maximum_neural_layers) {
        return false;
    }
    std::size_t parameters = 0;
    std::size_t previous_outputs = 0;
    for (std::size_t index = 0; index < layers.size(); ++index) {
        auto& layer = layers[index];
        if (layer.input_units == 0U || layer.output_units == 0U ||
            layer.input_units > maximum_neural_units || layer.output_units > maximum_neural_units ||
            (index != 0U && layer.input_units != previous_outputs) ||
            layer.weights.size() != static_cast<std::size_t>(layer.input_units) * layer.output_units ||
            layer.biases.size() != layer.output_units ||
            parameters > maximum_neural_parameters - layer.weights.size() - layer.biases.size()) {
            return false;
        }
        if (layer.activation != NeuralActivation::Linear &&
            layer.activation != NeuralActivation::ReLU) {
            return false;
        }
        parameters += layer.weights.size() + layer.biases.size();
        previous_outputs = layer.output_units;
    }
    layers_ = std::move(layers);
    return true;
}

std::optional<std::vector<std::int32_t>> FixedNeuralNetwork::infer(
    std::span<const std::int32_t> input) const {
    if (layers_.empty() || input.size() != layers_.front().input_units) {
        return std::nullopt;
    }
    std::vector<std::int32_t> values(input.begin(), input.end());
    for (const auto& layer : layers_) {
        std::vector<std::int32_t> next(layer.output_units);
        for (std::size_t output = 0; output < layer.output_units; ++output) {
            std::int64_t sum = static_cast<std::int64_t>(layer.biases[output]) * neural_fixed_scale;
            for (std::size_t input_index = 0; input_index < layer.input_units; ++input_index) {
                sum += static_cast<std::int64_t>(values[input_index]) *
                       layer.weights[output * layer.input_units + input_index];
            }
            auto value = saturate(sum / neural_fixed_scale);
            if (layer.activation == NeuralActivation::ReLU) {
                value = std::max<std::int32_t>(0, value);
            }
            next[output] = value;
        }
        values = std::move(next);
    }
    return values;
}

std::uint64_t FixedNeuralNetwork::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, static_cast<std::uint32_t>(layers_.size()));
    for (const auto& layer : layers_) {
        hash_integer(hash, layer.input_units);
        hash_integer(hash, layer.output_units);
        hash_integer(hash, static_cast<std::uint8_t>(layer.activation));
        for (const auto value : layer.weights) {
            hash_integer(hash, value);
        }
        for (const auto value : layer.biases) {
            hash_integer(hash, value);
        }
    }
    return hash;
}

std::span<const NeuralLayer> FixedNeuralNetwork::layers() const noexcept {
    return std::span<const NeuralLayer>(layers_);
}

} // namespace meat2d::ai
