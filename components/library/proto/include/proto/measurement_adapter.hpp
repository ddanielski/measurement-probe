/**
 * @file measurement_adapter.hpp
 * @brief Adapters to convert between C++ domain model and protobuf
 *
 * Converts sensor::Measurement ↔ sensor_Measurement (nanopb)
 *
 */

#pragma once

#include "../measurement.pb.h"

#include <sensor/measurement.hpp>

#include <pb_decode.h>
#include <pb_encode.h>

#include <cstdint>
#include <span>
#include <vector>

namespace proto {

/// Convert C++ Measurement to nanopb struct
inline sensor_Measurement to_proto(const sensor::Measurement &m) {
  sensor_Measurement pb = sensor_Measurement_init_zero;
  pb.id = static_cast<uint32_t>(m.id);

  std::visit(
      [&pb](auto &&v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          pb.which_value = sensor_Measurement_float_val_tag;
          pb.value.float_val = v;
        } else if constexpr (std::is_same_v<T, double>) {
          pb.which_value = sensor_Measurement_double_val_tag;
          pb.value.double_val = v;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          pb.which_value = sensor_Measurement_int32_val_tag;
          pb.value.int32_val = v;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          pb.which_value = sensor_Measurement_int64_val_tag;
          pb.value.int64_val = v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          // uint8 maps to uint32 in protobuf
          pb.which_value = sensor_Measurement_uint32_val_tag;
          pb.value.uint32_val = v;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          pb.which_value = sensor_Measurement_uint32_val_tag;
          pb.value.uint32_val = v;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          pb.which_value = sensor_Measurement_uint64_val_tag;
          pb.value.uint64_val = v;
        } else if constexpr (std::is_same_v<T, bool>) {
          pb.which_value = sensor_Measurement_bool_val_tag;
          pb.value.bool_val = v;
        }
      },
      m.value);

  return pb;
}

/// Convert nanopb struct to C++ Measurement
inline sensor::Measurement from_proto(const sensor_Measurement &pb) {
  auto id = static_cast<sensor::MeasurementId>(pb.id);

  switch (pb.which_value) {
  case sensor_Measurement_float_val_tag:
    return {id, pb.value.float_val};
  case sensor_Measurement_double_val_tag:
    return {id, pb.value.double_val};
  case sensor_Measurement_int32_val_tag:
    return {id, pb.value.int32_val};
  case sensor_Measurement_int64_val_tag:
    return {id, pb.value.int64_val};
  case sensor_Measurement_uint32_val_tag:
    return {id, pb.value.uint32_val};
  case sensor_Measurement_uint64_val_tag:
    return {id, pb.value.uint64_val};
  case sensor_Measurement_bool_val_tag:
    return {id, pb.value.bool_val};
  default:
    return {id, 0.0F}; // fallback
  }
}

/// Convert C++ measurements to nanopb batch
inline sensor_MeasurementBatch
to_proto_batch(std::span<const sensor::Measurement> measurements) {
  sensor_MeasurementBatch pb = sensor_MeasurementBatch_init_zero;

  // Copy measurements (up to max_count from .options file)
  size_t count = measurements.size();
  if (count > sizeof(pb.measurements) / sizeof(pb.measurements[0])) {
    count = sizeof(pb.measurements) / sizeof(pb.measurements[0]);
  }

  for (size_t i = 0; i < count; ++i) {
    pb.measurements[i] = to_proto(measurements[i]);
  }
  pb.measurements_count = static_cast<pb_size_t>(count);

  return pb;
}

/// Convert nanopb batch to C++ measurements
inline std::vector<sensor::Measurement>
from_proto_batch(const sensor_MeasurementBatch &pb) {
  std::vector<sensor::Measurement> result;
  result.reserve(pb.measurements_count);

  for (pb_size_t i = 0; i < pb.measurements_count; ++i) {
    result.push_back(from_proto(pb.measurements[i]));
  }

  return result;
}

/// Encode a batch of measurements to bytes
/// @param measurements All measurements to encode (from read_all)
/// @param buffer Output buffer
/// @return Number of bytes written, or 0 on error
inline size_t encode_batch(std::span<const sensor::Measurement> measurements,
                           std::span<uint8_t> buffer) {
  sensor_MeasurementBatch pb = to_proto_batch(measurements);
  pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
  if (!pb_encode(&stream, sensor_MeasurementBatch_fields, &pb)) {
    return 0;
  }
  return stream.bytes_written;
}

/// Decode bytes to a batch of measurements
/// @param buffer Input buffer
/// @return Vector of measurements (empty on error)
inline std::vector<sensor::Measurement>
decode_batch(std::span<const uint8_t> buffer) {
  sensor_MeasurementBatch pb = sensor_MeasurementBatch_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(buffer.data(), buffer.size());
  if (!pb_decode(&stream, sensor_MeasurementBatch_fields, &pb)) {
    return {};
  }
  return from_proto_batch(pb);
}

/// Maximum encoded size for a measurement batch
inline constexpr size_t MAX_BATCH_SIZE = sensor_MeasurementBatch_size;

/// Maximum number of measurements per batch
inline constexpr size_t MAX_MEASUREMENTS_PER_BATCH =
    sizeof(sensor_MeasurementBatch::measurements) /
    sizeof(sensor_MeasurementBatch::measurements[0]);

} // namespace proto
