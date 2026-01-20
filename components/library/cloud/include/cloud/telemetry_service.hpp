/**
 * @file telemetry_service.hpp
 * @brief Telemetry upload service
 *
 * Handles serialization and upload of sensor data.
 * Decoupled from CloudClient via dependency injection.
 */

#pragma once

#include "cloud_client.hpp"
#include "endpoints.hpp"

#include <cstdint>
#include <span>

namespace cloud {

/// Serializer interface - injected to decouple from proto
template <typename T> struct ISerializer {
  ISerializer() = default;
  virtual ~ISerializer() = default;
  ISerializer(const ISerializer &) = delete;
  ISerializer &operator=(const ISerializer &) = delete;
  ISerializer(ISerializer &&) = delete;
  ISerializer &operator=(ISerializer &&) = delete;

  virtual size_t serialize(std::span<const T> items,
                           std::span<uint8_t> buffer) = 0;
};

/// Result of telemetry upload
struct TelemetryResult {
  bool success{false};
  int status_code{0};
  CloudError error{CloudError::None};
};

/// Telemetry upload service
/// @tparam T Measurement type (injected, no direct dependency)
template <typename T, size_t BufferSize> class TelemetryService {
public:
  TelemetryService(CloudClient &client, ISerializer<T> &serializer)
      : client_(client), serializer_(serializer) {}

  /// Upload measurements
  [[nodiscard]] TelemetryResult send(std::span<const T> measurements) {
    if (measurements.empty()) {
      return {.success = true};
    }

    size_t encoded = serializer_.serialize(measurements, buffer_);
    if (encoded == 0) {
      return {.error = CloudError::ParseError};
    }

    auto response =
        client_.post(endpoints::TELEMETRY_PROTO, {buffer_.data(), encoded},
                     transport::ContentType::Protobuf);

    return {
        .success = response.success,
        .status_code = response.status_code,
        .error = response.error,
    };
  }

private:
  CloudClient &client_;
  ISerializer<T> &serializer_;
  std::array<uint8_t, BufferSize> buffer_{};
};

} // namespace cloud
