#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::ai {

inline constexpr std::int32_t neural_fixed_scale = 65'536;
inline constexpr std::size_t maximum_neural_layers = 16U;
inline constexpr std::size_t maximum_neural_units = 256U;
inline constexpr std::size_t maximum_neural_parameters = 65'536U;

enum class NeuralActivation : std::uint8_t { Linear, ReLU };

struct NeuralLayer {
    std::uint16_t input_units{};
    std::uint16_t output_units{};
    std::vector<std::int32_t> weights;
    std::vector<std::int32_t> biases;
    NeuralActivation activation{NeuralActivation::Linear};
};

// Deterministic fixed-point inference. Models are intended to be trained or
// exported by external tooling; runtime execution never allocates unbounded
// tensors or depends on platform floating-point behavior.
class FixedNeuralNetwork {
  public:
    bool set_layers(std::vector<NeuralLayer> layers);
    [[nodiscard]] std::optional<std::vector<std::int32_t>> infer(
        std::span<const std::int32_t> input) const;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] std::span<const NeuralLayer> layers() const noexcept;

  private:
    std::vector<NeuralLayer> layers_;
};

} // namespace meat2d::ai
