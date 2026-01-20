# Device Provisioning Tool

Provisions measurement probe devices with the backend service.

## Prerequisites

1. **esptool.py** - Install via `pip install esptool`
2. **gcloud CLI** - Authenticated with access to the Cloud Run service
3. **ESP-IDF** - For NVS partition generation

## Setup

```bash
# Install dependencies
cd tools/provision
go mod download

# Ensure gcloud is authenticated
gcloud auth login
```

## Usage

### Basic Usage

```bash
# Set environment variables
export SERVICE_URL="https://telemetry-api-xxx-uw.a.run.app"
export IDF_PATH="/path/to/esp-idf"

# Provision device connected to /dev/ttyUSB0
go run ./cmd/provision --port /dev/ttyUSB0
```

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `--port` | Serial port | Auto-detect |
| `--service-url` | Backend API URL | `$SERVICE_URL` |
| `--idf-path` | ESP-IDF installation path | `$IDF_PATH` |
| `--mac` | Device MAC address | Read from device |
| `--nvs-offset` | NVS partition offset | `0x9000` |
| `--nvs-size` | NVS partition size | `0x6000` |
| `--dry-run` | Provision only, don't flash | `false` |

### Examples

```bash
# Auto-detect port
go run ./cmd/provision

# Specify port explicitly
go run ./cmd/provision --port /dev/ttyACM0

# Dry run (provision in backend but don't flash device)
go run ./cmd/provision --dry-run

# Provision with known MAC (skip device connection)
go run ./cmd/provision --mac AA:BB:CC:DD:EE:FF --dry-run
```

## How It Works

1. **Read MAC Address** - Uses esptool to read the device's MAC address
2. **Provision with Backend** - Calls `POST /admin/devices/provision` with the MAC
3. **Write to NVS** - Generates NVS partition and flashes credentials to device

## Credentials Storage

The tool stores credentials in the device's NVS partition under the `cloud` namespace:

| Key | Type | Description |
|-----|------|-------------|
| `device_id` | string | Device UUID |
| `secret` | string | 64-char hex authentication secret |
| `base_url` | string | Backend API URL |

A backup of the credentials is also saved to `~/.measurement-probe/credentials/`.

## Troubleshooting

### "gcloud auth failed"

```bash
# Re-authenticate with gcloud
gcloud auth login

# For Cloud Run access, you may need:
gcloud auth application-default login
```

### "Could not find MAC"

Ensure the device is in bootloader mode:
1. Hold BOOT button
2. Press and release RESET
3. Release BOOT button

Or specify MAC manually with `--mac`.

### "NVS partition gen failed"

Ensure ESP-IDF is properly installed and `IDF_PATH` is set:

```bash
source /path/to/esp-idf/export.sh
```
