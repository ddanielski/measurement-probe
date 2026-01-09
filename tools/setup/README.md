# Measurement Probe Setup Tool

Interactive CLI tool for configuring the measurement-probe project. Handles git submodules, BSEC library configuration, and WiFi provisioning setup.

## Quick Start

```bash
cd tools/setup
go run ./cmd/setup
```

Or build and run:

```bash
go build -o setup ./cmd/setup
./setup
```

## What It Does

1. **Git Submodules** - Initializes Bosch BSEC2 and BME68x API submodules
2. **BSEC Configuration** - Copies headers, library, and generates config for your ESP chip
3. **Provisioning Secret** - Generates a unique Proof of Possession (PoP) for BLE WiFi provisioning

## Configuration Options

| Option | Choices | Default |
|--------|---------|---------|
| ESP Chip | ESP32-C3, ESP32, ESP32-S2, ESP32-S3 | ESP32-C3 |
| Sensor | BME680, BME688 | BME680 |
| Voltage | 3.3V, 1.8V | 3.3V |
| Mode | Continuous (3s), Deep Sleep (300s) | Continuous |
| History | 4 days, 28 days | 4 days |

## Project Structure

```
tools/setup/
├── cmd/setup/main.go           # Entry point & orchestration
├── go.mod
└── internal/
    ├── bsec/                   # BSEC library configuration
    │   ├── bsec.go
    │   └── bsec_test.go
    ├── git/                    # Git submodule operations
    │   ├── submodules.go
    │   └── submodules_test.go
    ├── project/                # Project root detection
    │   ├── project.go
    │   └── project_test.go
    ├── prompt/                 # User interaction (testable)
    │   ├── prompt.go
    │   └── prompt_test.go
    └── provisioning/           # PoP secret generation
        ├── provisioning.go
        └── provisioning_test.go
```

## Development

### Run Tests

```bash
go test ./...
```

### Run Tests with Coverage

```bash
go test -cover ./...
```

### Generate Coverage Report

```bash
go test -coverprofile=coverage.out ./...
go tool cover -html=coverage.out -o coverage.html
```

### Code Quality

```bash
go vet ./...
go fmt ./...
```

## Generated Files

The tool generates:

| File | Purpose |
|------|---------|
| `components/external/bsec2/include/*.h` | BSEC headers |
| `components/external/bsec2/lib/libalgobsec.a` | BSEC library for target chip |
| `components/external/bsec2/include/bsec_config.h` | Generated configuration |
| `components/generated/provisioning_config.h` | WiFi provisioning secret |
