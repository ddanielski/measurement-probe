/**
 * @file measurement_serializer.hpp
 * @brief Serializer for sensor measurements using protobuf
 *
 * Bridges cloud::ISerializer interface with proto::encode_batch.
 */

#pragma once

#include "telemetry_service.hpp"

#include <proto/measurement_adapter.hpp>
#include <sensor/measurement.hpp>

namespace cloud {

/// Protobuf serializer for sensor measurements
class MeasurementSerializer final : public ISerializer<sensor::Measurement> {
public:
  size_t serialize(std::span<const sensor::Measurement> items,
                   std::span<uint8_t> buffer) override {
    return proto::encode_batch(items, buffer);
  }
};

} // namespace cloud
