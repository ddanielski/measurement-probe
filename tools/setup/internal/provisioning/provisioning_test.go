package provisioning_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"measurement-probe/tools/setup/internal/provisioning"
)

// testDefaults returns standard defaults for testing.
func testDefaults(tmpDir string) provisioning.Defaults {
	return provisioning.Defaults{
		DeviceName:   "TestDevice",
		TimeoutSec:   120,
		PopBytes:     4, // 4 bytes = 8 hex chars
		OutputFile:   "provisioning_config.h",
		GeneratedDir: filepath.Join(tmpDir, "generated"),
	}
}

func TestSetup_Generate_NewSecret(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if !isNew {
		t.Error("Generate() isNew = false, want true for new generation")
	}

	if config == nil {
		t.Fatal("Generate() config is nil")
	}

	// Verify PoP format (8 hex chars from 4 bytes)
	if len(config.PoP) != 8 {
		t.Errorf("PoP length = %d, want 8", len(config.PoP))
	}

	for _, c := range config.PoP {
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
			t.Errorf("PoP contains non-hex character: %c", c)
		}
	}

	// Verify defaults were used
	if config.DeviceName != "TestDevice" {
		t.Errorf("DeviceName = %q, want %q", config.DeviceName, "TestDevice")
	}

	if config.TimeoutSec != 120 {
		t.Errorf("TimeoutSec = %d, want %d", config.TimeoutSec, 120)
	}
}

func TestSetup_Generate_CustomPopBytes(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	defaults.PopBytes = 8 // 8 bytes = 16 hex chars

	setup := provisioning.NewSetup(defaults)
	config, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	// Verify PoP length matches custom setting
	if len(config.PoP) != 16 {
		t.Errorf("PoP length = %d, want 16", len(config.PoP))
	}
}

func TestSetup_Generate_ExistingSecret(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	// Create existing config with known PoP
	if err := os.MkdirAll(defaults.GeneratedDir, 0755); err != nil {
		t.Fatalf("failed to create directory: %v", err)
	}

	existingConfig := `#define PROVISIONING_POP "deadbeef"
#define PROVISIONING_DEVICE_NAME "OldDevice"
#define PROVISIONING_TIMEOUT_SEC 60`

	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if err := os.WriteFile(configPath, []byte(existingConfig), 0644); err != nil {
		t.Fatalf("failed to write existing config: %v", err)
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if isNew {
		t.Error("Generate() isNew = true, want false for existing config")
	}

	if config.PoP != "deadbeef" {
		t.Errorf("PoP = %q, want %q", config.PoP, "deadbeef")
	}

	// DeviceName and TimeoutSec should come from defaults, not file
	if config.DeviceName != "TestDevice" {
		t.Errorf("DeviceName = %q, want %q", config.DeviceName, "TestDevice")
	}
}

func TestSetup_Generate_CreatesFile(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	setup := provisioning.NewSetup(defaults)
	config, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	// Verify file was created
	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	content, err := os.ReadFile(configPath)
	if err != nil {
		t.Fatalf("failed to read generated file: %v", err)
	}

	// Verify file content uses our defaults
	checks := []string{
		"#pragma once",
		"PROVISIONING_POP \"" + config.PoP + "\"",
		"PROVISIONING_DEVICE_NAME \"TestDevice\"",
		"PROVISIONING_TIMEOUT_SEC 120",
		"DO NOT COMMIT",
	}

	for _, check := range checks {
		if !strings.Contains(string(content), check) {
			t.Errorf("generated file missing %q", check)
		}
	}
}

func TestSetup_Generate_Uniqueness(t *testing.T) {
	t.Parallel()

	secrets := make(map[string]bool)
	iterations := 100

	for i := 0; i < iterations; i++ {
		tmpDir := t.TempDir()
		defaults := testDefaults(tmpDir)

		setup := provisioning.NewSetup(defaults)
		config, _, err := setup.Generate()
		if err != nil {
			t.Fatalf("Generate() error = %v", err)
		}

		if secrets[config.PoP] {
			t.Errorf("duplicate PoP generated: %s", config.PoP)
		}
		secrets[config.PoP] = true
	}
}

func TestSetup_Generate_MalformedExistingConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	if err := os.MkdirAll(defaults.GeneratedDir, 0755); err != nil {
		t.Fatalf("failed to create directory: %v", err)
	}

	// Create malformed config - PROVISIONING_POP without proper quotes
	malformedConfig := `#define PROVISIONING_POP badformat
#define PROVISIONING_DEVICE_NAME "MeasureProbe"`

	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if err := os.WriteFile(configPath, []byte(malformedConfig), 0644); err != nil {
		t.Fatalf("failed to write malformed config: %v", err)
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if !isNew {
		t.Error("Generate() isNew = false, want true for malformed existing config")
	}

	if len(config.PoP) != 8 {
		t.Errorf("PoP length = %d, want 8", len(config.PoP))
	}
}

func TestSetup_Generate_EmptyExistingConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	if err := os.MkdirAll(defaults.GeneratedDir, 0755); err != nil {
		t.Fatalf("failed to create directory: %v", err)
	}

	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if err := os.WriteFile(configPath, []byte(""), 0644); err != nil {
		t.Fatalf("failed to write empty config: %v", err)
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if !isNew {
		t.Error("Generate() isNew = false, want true for empty existing config")
	}

	if len(config.PoP) != 8 {
		t.Errorf("PoP length = %d, want 8", len(config.PoP))
	}
}

func TestSetup_Generate_PartialQuotesInConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	if err := os.MkdirAll(defaults.GeneratedDir, 0755); err != nil {
		t.Fatalf("failed to create directory: %v", err)
	}

	// Config with PROVISIONING_POP but empty string
	malformedConfig := `#define PROVISIONING_POP ""`

	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if err := os.WriteFile(configPath, []byte(malformedConfig), 0644); err != nil {
		t.Fatalf("failed to write config: %v", err)
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if !isNew {
		t.Error("Generate() isNew = false, want true")
	}

	if len(config.PoP) != 8 {
		t.Errorf("PoP length = %d, want 8", len(config.PoP))
	}
}

func TestSetup_Generate_ConfigWithSingleQuote(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)

	if err := os.MkdirAll(defaults.GeneratedDir, 0755); err != nil {
		t.Fatalf("failed to create directory: %v", err)
	}

	// Config with PROVISIONING_POP but only one quote
	malformedConfig := `#define PROVISIONING_POP "abc`

	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if err := os.WriteFile(configPath, []byte(malformedConfig), 0644); err != nil {
		t.Fatalf("failed to write config: %v", err)
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if !isNew {
		t.Error("Generate() isNew = false, want true")
	}

	if len(config.PoP) != 8 {
		t.Errorf("PoP length = %d, want 8", len(config.PoP))
	}
}

func TestNewSetup(t *testing.T) {
	t.Parallel()

	defaults := provisioning.Defaults{
		DeviceName:   "Test",
		TimeoutSec:   60,
		PopBytes:     4,
		OutputFile:   "test.h",
		GeneratedDir: "/tmp",
	}

	setup := provisioning.NewSetup(defaults)

	if setup == nil {
		t.Error("NewSetup returned nil")
	}
}

func TestSetup_Generate_CustomOutputFile(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	defaults.OutputFile = "custom_provisioning.h"

	setup := provisioning.NewSetup(defaults)
	_, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	// Verify custom filename was used
	customPath := filepath.Join(defaults.GeneratedDir, "custom_provisioning.h")
	if _, err := os.Stat(customPath); os.IsNotExist(err) {
		t.Error("custom output file was not created")
	}
}

func TestSetup_Generate_ZeroPopBytes(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	defaults.PopBytes = 0 // Edge case: zero bytes

	setup := provisioning.NewSetup(defaults)
	config, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	// Zero bytes = empty PoP
	if len(config.PoP) != 0 {
		t.Errorf("PoP length = %d, want 0", len(config.PoP))
	}
}

func TestSetup_Generate_LargePopBytes(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	defaults.PopBytes = 32 // 32 bytes = 64 hex chars

	setup := provisioning.NewSetup(defaults)
	config, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if len(config.PoP) != 64 {
		t.Errorf("PoP length = %d, want 64", len(config.PoP))
	}
}

func TestSetup_Generate_NestedGeneratedDir(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	// Deeply nested directory that doesn't exist
	defaults.GeneratedDir = filepath.Join(tmpDir, "a", "b", "c", "d", "generated")

	setup := provisioning.NewSetup(defaults)
	_, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	// Verify deeply nested directory was created
	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	if _, err := os.Stat(configPath); os.IsNotExist(err) {
		t.Error("config file was not created in nested directory")
	}
}

func TestSetup_Generate_CustomDeviceNameAndTimeout(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	defaults := testDefaults(tmpDir)
	defaults.DeviceName = "MyCustomDevice"
	defaults.TimeoutSec = 999

	setup := provisioning.NewSetup(defaults)
	config, _, err := setup.Generate()

	if err != nil {
		t.Fatalf("Generate() error = %v", err)
	}

	if config.DeviceName != "MyCustomDevice" {
		t.Errorf("DeviceName = %q, want %q", config.DeviceName, "MyCustomDevice")
	}

	if config.TimeoutSec != 999 {
		t.Errorf("TimeoutSec = %d, want %d", config.TimeoutSec, 999)
	}

	// Also verify it's written to file
	configPath := filepath.Join(defaults.GeneratedDir, defaults.OutputFile)
	content, err := os.ReadFile(configPath)
	if err != nil {
		t.Fatalf("failed to read file: %v", err)
	}

	if !strings.Contains(string(content), "MyCustomDevice") {
		t.Error("custom device name not in generated file")
	}
	if !strings.Contains(string(content), "999") {
		t.Error("custom timeout not in generated file")
	}
}
