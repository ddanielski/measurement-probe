# Measurement Probe

ESP32-based environmental sensor probe with BME680/BME688 for temperature, humidity, pressure, and air quality (IAQ, CO2, VOC).

## Features

- **BME680/BME688 Sensor**: Temperature, humidity, pressure, gas resistance
- **Bosch BSEC Library**: Advanced air quality calculations (IAQ, estimated CO2/VOC)
- **Low Power Modes**: Continuous (3s) or Ultra-Low Power (300s) sampling
- **Modern C++ Architecture**: VFS-style drivers, sensor abstraction, CRTP patterns

## Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/) v5.x
- [Go](https://go.dev/) 1.19+ (for setup tool)
- Git

## Quick Start

### 1. Clone the Repository

```bash
git clone <repository-url>
cd measurement-probe
```

### 2. Run the Setup Tool

The setup tool initializes git submodules and configures BSEC for your target:

```bash
go run tools/setup/main.go
```

The tool will prompt you for:
- **ESP Chip**: ESP32, ESP32-C3, ESP32-S2, or ESP32-S3
- **Sensor Chip**: BME680 or BME688
- **Supply Voltage**: 3.3V or 1.8V
- **Operation Mode**: Continuous (3s sampling) or Deep Sleep (300s sampling)
- **Calibration History**: 4 days or 28 days

### 3. Build and Flash

```bash
# Set target (e.g., esp32c3)
idf.py set-target esp32c3

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Project Structure

```
measurement-probe/
├── main/                           # Application entry point
│   ├── main.cpp
│   ├── probe_app.hpp
│   └── app_config.hpp              # Application configuration
├── components/
│   ├── library/                    # Reusable libraries
│   │   ├── core/                   # Result, Status types
│   │   ├── driver_base/            # IDriver interface
│   │   └── sensor_base/            # ISensor, SensorManager
│   ├── driver/
│   │   └── bme680/                 # BME680 low-level driver
│   ├── sensor/
│   │   └── bme680_sensor/          # BME680 + BSEC sensor
│   └── external/                   # External dependencies
│       ├── Bosch-BSEC2-Library/    # (git submodule)
│       ├── BME68x_SensorAPI/       # (git submodule)
│       ├── bme68x_api/             # CMake wrapper for BME68x
│       └── bsec2/                  # CMake wrapper for BSEC2
└── tools/
    └── setup/                      # Go setup tool
        └── main.go
```

## Configuration

### Hardware Pins (ESP32-C3)

Edit `main/app_config.hpp`:

```cpp
namespace config {
  inline constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
  inline constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
  inline constexpr uint8_t BME680_I2C_ADDR = 0x77;  // or 0x76
}
```

### BSEC Mode

Re-run the setup tool to change BSEC configuration:

```bash
go run tools/setup/main.go
```

## Protobuf Generation

The project uses [nanopb](https://jpa.kapsi.fi/nanopb/) for embedded-friendly protobuf serialization.

### Regenerate Protobuf Files

After modifying `proto/measurement.proto`:

```bash
# Generate descriptor
python -m grpc_tools.protoc -I proto \
    --descriptor_set_out=proto/generated/measurement.pb \
    proto/measurement.proto

# Generate nanopb C files
nanopb_generator -D proto/generated \
    -f proto/measurement.options \
    proto/generated/measurement.pb

# Move to component
mv proto/generated/measurement.pb.h components/library/proto/include/
mv proto/generated/measurement.pb.c components/library/proto/src/
```

### Proto Schema Location

- **Schema**: `proto/measurement.proto`
- **Options**: `proto/measurement.options` (nanopb static allocation limits)
- **Generated**: `proto/generated/` (not committed)
- **Component**: `components/library/proto/`

## Architecture

### Driver Layer (VFS-style)

```cpp
class IDriver {
  virtual Status open() = 0;
  virtual Status close() = 0;
  virtual Result<size_t> read(std::span<uint8_t> buffer) = 0;
  virtual Result<size_t> write(std::span<const uint8_t> data) = 0;
  virtual Result<std::any> ioctl(uint32_t cmd, std::any arg) = 0;
};
```

### Sensor Layer

```cpp
class ISensor {
  virtual Result<std::span<const Measurement>> read() = 0;
  virtual std::chrono::milliseconds min_interval() = 0;
};
```

### Measurement Types

Sensors return `Measurement` structs with typed IDs:

```cpp
struct Measurement {
  MeasurementId id;  // Temperature, Humidity, IAQ, CO2, VOC, etc.
  float value;
};
```

## External Dependencies

This project uses Bosch proprietary libraries via git submodules:

| Library | License | Purpose |
|---------|---------|---------|
| [BME68x_SensorAPI](https://github.com/boschsensortec/BME68x_SensorAPI) | BSD-3-Clause | BME680/688 sensor driver |
| [Bosch-BSEC2-Library](https://github.com/boschsensortec/Bosch-BSEC2-Library) | Proprietary | Air quality algorithms |

**Note**: The BSEC library is proprietary. Review Bosch's license before commercial use.

## Troubleshooting

### "bme68x_init failed: -2"

The sensor is not responding. Check:
- I2C wiring (SDA, SCL)
- I2C address (0x76 or 0x77)
- Pull-up resistors on I2C lines

### "IAQ accuracy: 0/3"

BSEC is still calibrating. The sensor needs:
- ~5 minutes for initial readings
- ~24-48 hours for full calibration
- State persistence (saved to NVS every 100 reads)

### Build Errors After git pull

Submodules may need updating:

```bash
git submodule update --init --recursive
go run tools/setup/main.go
idf.py fullclean
idf.py build
```
