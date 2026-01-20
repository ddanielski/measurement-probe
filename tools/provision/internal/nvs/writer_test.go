package nvs

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestGenerateCSV(t *testing.T) {
	tmpDir := t.TempDir()
	csvPath := filepath.Join(tmpDir, "test.csv")

	writer := NewWriter("/fake/idf", "/dev/ttyUSB0")
	creds := &Credentials{
		DeviceID: "test-device-id",
		Secret:   "test-secret-value",
	}

	if err := writer.GenerateCSV(creds, csvPath); err != nil {
		t.Fatalf("GenerateCSV() error = %v", err)
	}

	content, err := os.ReadFile(csvPath)
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(content), "\n")

	// Check header
	if !strings.Contains(lines[0], "key,type,encoding,value") {
		t.Errorf("missing header: %s", lines[0])
	}

	// Check namespace
	if !strings.Contains(lines[1], "cloud,namespace") {
		t.Errorf("missing namespace: %s", lines[1])
	}

	// Check device_id
	found := false
	for _, line := range lines {
		if strings.Contains(line, "device_id") && strings.Contains(line, "test-device-id") {
			found = true
			break
		}
	}
	if !found {
		t.Error("device_id not found in CSV")
	}

	// Check secret
	found = false
	for _, line := range lines {
		if strings.Contains(line, "secret") && strings.Contains(line, "test-secret-value") {
			found = true
			break
		}
	}
	if !found {
		t.Error("secret not found in CSV")
	}
}

func TestNewWriter(t *testing.T) {
	writer := NewWriter("/esp/idf", "/dev/ttyUSB0")

	if writer.espIdfPath != "/esp/idf" {
		t.Errorf("espIdfPath = %s, want /esp/idf", writer.espIdfPath)
	}
	if writer.port != "/dev/ttyUSB0" {
		t.Errorf("port = %s, want /dev/ttyUSB0", writer.port)
	}
	if writer.namespace != "cloud" {
		t.Errorf("namespace = %s, want cloud", writer.namespace)
	}
}
