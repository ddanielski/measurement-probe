/**
 * @file measurement.hpp
 * @brief Measurement types and metadata for sensors
 *
 * Uses MeasurementTraits for compile-time type safety.
 * Each MeasurementId has an associated type, name, and unit.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace sensor {

enum class MeasurementId : uint8_t {
  Timestamp = 1,
  Temperature,
  Humidity,
  Pressure,
  IAQ,
  IAQAccuracy,
  CO2,
  VOC,
  Count
};

/// Value type for measurements (all practical types)
using MeasurementValue = std::variant<float, double, int32_t, int64_t, uint32_t,
                                      uint64_t, uint8_t, bool>;

// ============================================================================
// MeasurementTraits - Compile-time type and metadata for each MeasurementId
// ============================================================================

template <MeasurementId Id> struct MeasurementTraits;

// Helper macro to reduce boilerplate
#define MEASUREMENT_TRAIT(ID, TYPE, NAME, UNIT)                                \
  template <> struct MeasurementTraits<MeasurementId::ID> {                    \
    using type = TYPE;                                                         \
    static constexpr const char *name = NAME;                                  \
    static constexpr const char *unit = UNIT;                                  \
  }

// System
MEASUREMENT_TRAIT(Timestamp, uint64_t, "timestamp", "ms");

// Environmental
MEASUREMENT_TRAIT(Temperature, float, "temperature", "°C");
MEASUREMENT_TRAIT(Humidity, float, "humidity", "%");
MEASUREMENT_TRAIT(Pressure, float, "pressure", "hPa");

// Air quality
MEASUREMENT_TRAIT(IAQ, float, "iaq", "");
MEASUREMENT_TRAIT(IAQAccuracy, uint8_t, "iaq_accuracy", "/3");
MEASUREMENT_TRAIT(CO2, float, "co2", "ppm");
MEASUREMENT_TRAIT(VOC, float, "voc", "ppm");

#undef MEASUREMENT_TRAIT

struct MeasurementMeta {
  const char *name;
  const char *unit;
};

namespace detail {
template <std::size_t... Is>
constexpr auto make_meta_table(std::index_sequence<Is...>) {
  return std::array<MeasurementMeta, sizeof...(Is)>{MeasurementMeta{
      MeasurementTraits<static_cast<MeasurementId>(Is + 1)>::name,
      MeasurementTraits<static_cast<MeasurementId>(Is + 1)>::unit}...};
}
} // namespace detail

inline constexpr auto MEASUREMENT_META = detail::make_meta_table(
    std::make_index_sequence<static_cast<std::size_t>(MeasurementId::Count) -
                             1>{});

// ============================================================================
// Type traits helpers
// ============================================================================

template <typename T, typename Variant> struct is_variant_member;

template <typename T, typename... Types>
struct is_variant_member<T, std::variant<Types...>>
    : std::disjunction<std::is_same<T, Types>...> {};

template <typename T>
inline constexpr bool is_measurement_type_v =
    is_variant_member<T, MeasurementValue>::value;

// ============================================================================
// Measurement struct - Runtime storage
// ============================================================================

struct Measurement {
  MeasurementId id;
  MeasurementValue value;

  // Default constructor
  Measurement() : id(MeasurementId::Timestamp), value(uint64_t{0}) {}

  // Generic constructor (runtime, no compile-time type check on id)
  template <typename T> Measurement(MeasurementId id, T v) : id(id), value(v) {
    static_assert(is_measurement_type_v<T>,
                  "T must be a MeasurementValue type");
  }

  /// Type checking: m.is<float>(), m.is<uint64_t>(), etc.
  template <typename T> [[nodiscard]] bool is() const {
    static_assert(is_measurement_type_v<T>,
                  "T must be a MeasurementValue type");
    return std::holds_alternative<T>(value);
  }

  /// Check if value is an integer type
  [[nodiscard]] bool is_integer() const {
    return is<int32_t>() || is<int64_t>() || is<uint32_t>() || is<uint64_t>();
  }

  /// Check if value is a floating-point type
  [[nodiscard]] bool is_floating() const { return is<float>() || is<double>(); }

  /// Convert value to target type
  template <typename Target> [[nodiscard]] Target to() const {
    return std::visit(
        [](auto &&v) -> Target {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, bool>) {
            return static_cast<Target>(v ? 1 : 0);
          } else {
            return static_cast<Target>(v);
          }
        },
        value);
  }

  /// Get value with compile-time type deduction from MeasurementId
  /// Usage: float t = m.get<MeasurementId::Temperature>();
  template <MeasurementId Id>
  [[nodiscard]] typename MeasurementTraits<Id>::type get() const {
    return to<typename MeasurementTraits<Id>::type>();
  }

  /// Visit the value with a callable (type-safe dispatch)
  /// The visitor receives the actual stored type.
  template <typename Visitor> auto visit(Visitor &&vis) const {
    return std::visit(std::forward<Visitor>(vis), value);
  }

  [[nodiscard]] const MeasurementMeta &meta() const {
    return MEASUREMENT_META[static_cast<size_t>(id) - 1];
  }

  [[nodiscard]] const char *name() const { return meta().name; }
  [[nodiscard]] const char *unit() const { return meta().unit; }
};

// ============================================================================
// Type-safe factory function (compile-time checked)
// ============================================================================

/// Create a Measurement with compile-time type checking
/// Usage: auto m = make<MeasurementId::Temperature>(25.5f);
template <MeasurementId Id>
[[nodiscard]] Measurement make(typename MeasurementTraits<Id>::type value) {
  return Measurement(Id, value);
}

} // namespace sensor
