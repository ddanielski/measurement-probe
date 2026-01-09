package bsec_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"measurement-probe/tools/setup/internal/bsec"
)

// testPaths returns standard paths for testing.
func testPaths(tmpDir string) bsec.Paths {
	return bsec.Paths{
		SourceDir:     filepath.Join(tmpDir, "bsec2-lib"),
		TargetDir:     filepath.Join(tmpDir, "bsec2-target"),
		AppConfigPath: filepath.Join(tmpDir, "main", "app_config.hpp"),
		Headers:       []string{"bsec_datatypes.h", "bsec_interface.h"},
		ConfigFile:    "bsec_iaq.txt",
		LibraryName:   "libalgobsec.a",
	}
}

func TestConfig_Name(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name   string
		config bsec.Config
		want   string
	}{
		{
			name: "BME680 LP 3.3V 4 days",
			config: bsec.Config{
				ChipVariant: "bme680",
				Voltage:     "33v",
				Interval:    "3s",
				History:     "4d",
			},
			want: "bme680_iaq_33v_3s_4d",
		},
		{
			name: "BME688 ULP 1.8V 28 days",
			config: bsec.Config{
				ChipVariant: "bme688",
				Voltage:     "18v",
				Interval:    "300s",
				History:     "28d",
			},
			want: "bme688_iaq_18v_300s_28d",
		},
		{
			name: "BME680 ULP 3.3V 28 days",
			config: bsec.Config{
				ChipVariant: "bme680",
				Voltage:     "33v",
				Interval:    "300s",
				History:     "28d",
			},
			want: "bme680_iaq_33v_300s_28d",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			got := tt.config.Name()
			if got != tt.want {
				t.Errorf("Name() = %q, want %q", got, tt.want)
			}
		})
	}
}

func TestConfig_SampleRate(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		interval string
		want     string
	}{
		{
			name:     "LP mode 3s interval",
			interval: "3s",
			want:     "BSEC_SAMPLE_RATE_LP",
		},
		{
			name:     "ULP mode 300s interval",
			interval: "300s",
			want:     "BSEC_SAMPLE_RATE_ULP",
		},
		{
			name:     "unknown interval defaults to LP",
			interval: "10s",
			want:     "BSEC_SAMPLE_RATE_LP",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			config := bsec.Config{Interval: tt.interval}
			got := config.SampleRate()

			if got != tt.want {
				t.Errorf("SampleRate() = %q, want %q", got, tt.want)
			}
		})
	}
}

func TestConfig_IntervalMs(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		interval string
		want     int
	}{
		{
			name:     "LP mode 3s",
			interval: "3s",
			want:     3000,
		},
		{
			name:     "ULP mode 300s",
			interval: "300s",
			want:     300000,
		},
		{
			name:     "unknown interval defaults to LP",
			interval: "unknown",
			want:     3000,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			config := bsec.Config{Interval: tt.interval}
			got := config.IntervalMs()

			if got != tt.want {
				t.Errorf("IntervalMs() = %d, want %d", got, tt.want)
			}
		})
	}
}

func TestNewSetup(t *testing.T) {
	t.Parallel()

	paths := bsec.Paths{
		SourceDir:   "/test/src",
		TargetDir:   "/test/dst",
		Headers:     []string{"test.h"},
		ConfigFile:  "config.txt",
		LibraryName: "lib.a",
	}
	setup := bsec.NewSetup(paths)

	if setup == nil {
		t.Error("NewSetup returned nil")
	}
}

func TestSetup_Apply_MissingConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	err := setup.Apply(config)

	if err == nil {
		t.Error("Apply() should fail when config source doesn't exist")
	}
	if !strings.Contains(err.Error(), "configuration not found") {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestSetup_Apply_MissingHeaders(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create config dir but no headers
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	err := setup.Apply(config)

	if err == nil {
		t.Error("Apply() should fail when headers are missing")
	}
	if !strings.Contains(err.Error(), "failed to copy header") {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestSetup_Apply_MissingLibrary(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create config dir and headers but no library
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	for _, h := range paths.Headers {
		if err := os.WriteFile(filepath.Join(incDir, h), []byte("// header"), 0644); err != nil {
			t.Fatalf("failed to create header: %v", err)
		}
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	err := setup.Apply(config)

	if err == nil {
		t.Error("Apply() should fail when library is missing")
	}
	if !strings.Contains(err.Error(), "BSEC library not found") {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestSetup_Apply_UpdatesExistingAppConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create app_config.hpp with existing BSEC_DEEP_SLEEP_MODE
	mainDir := filepath.Dir(paths.AppConfigPath)
	if err := os.MkdirAll(mainDir, 0755); err != nil {
		t.Fatalf("failed to create main dir: %v", err)
	}

	existingConfig := `namespace config {
inline constexpr bool BSEC_DEEP_SLEEP_MODE = false;
} // namespace config`

	if err := os.WriteFile(paths.AppConfigPath, []byte(existingConfig), 0644); err != nil {
		t.Fatalf("failed to create app_config.hpp: %v", err)
	}

	// Setup full mock structure
	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
		DeepSleep:   true, // Change to true
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify app_config was updated
	content, err := os.ReadFile(paths.AppConfigPath)
	if err != nil {
		t.Fatalf("failed to read app_config.hpp: %v", err)
	}

	if !strings.Contains(string(content), "BSEC_DEEP_SLEEP_MODE = true") {
		t.Errorf("app_config.hpp not updated correctly: %s", string(content))
	}
}

func TestSetup_Apply_InsertsNewAppConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create app_config.hpp WITHOUT BSEC_DEEP_SLEEP_MODE
	mainDir := filepath.Dir(paths.AppConfigPath)
	if err := os.MkdirAll(mainDir, 0755); err != nil {
		t.Fatalf("failed to create main dir: %v", err)
	}

	existingConfig := `namespace config {
inline constexpr int SOME_OTHER_CONFIG = 42;
} // namespace config`

	if err := os.WriteFile(paths.AppConfigPath, []byte(existingConfig), 0644); err != nil {
		t.Fatalf("failed to create app_config.hpp: %v", err)
	}

	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
		DeepSleep:   true,
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify BSEC_DEEP_SLEEP_MODE was inserted
	content, err := os.ReadFile(paths.AppConfigPath)
	if err != nil {
		t.Fatalf("failed to read app_config.hpp: %v", err)
	}

	if !strings.Contains(string(content), "BSEC_DEEP_SLEEP_MODE = true") {
		t.Errorf("BSEC_DEEP_SLEEP_MODE not inserted: %s", string(content))
	}
}

func TestSetup_Apply_NoAppConfig(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.AppConfigPath = "" // No app config

	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	// Should not fail when AppConfigPath is empty
	if err := setup.Apply(config); err != nil {
		t.Errorf("Apply() should not fail when AppConfigPath is empty: %v", err)
	}
}

func TestSetup_Apply_AppConfigNotExists(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	// paths.AppConfigPath points to non-existent file

	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	// Should not fail even if app_config.hpp doesn't exist
	if err := setup.Apply(config); err != nil {
		t.Errorf("Apply() should not fail when app_config.hpp doesn't exist: %v", err)
	}
}

func TestSetup_Apply_Integration_AllChips(t *testing.T) {
	t.Parallel()

	espChips := []string{"esp32", "esp32c3", "esp32s2", "esp32s3"}

	for _, chip := range espChips {
		t.Run(chip, func(t *testing.T) {
			t.Parallel()

			tmpDir := t.TempDir()
			paths := testPaths(tmpDir)

			setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", chip)

			setup := bsec.NewSetup(paths)
			config := &bsec.Config{
				ESPChip:     chip,
				ChipVariant: "bme680",
				Voltage:     "33v",
				Interval:    "3s",
				History:     "4d",
			}

			if err := setup.Apply(config); err != nil {
				t.Errorf("Apply() failed for %s: %v", chip, err)
			}

			// Verify library was copied
			libPath := filepath.Join(paths.TargetDir, "lib", paths.LibraryName)
			if _, err := os.Stat(libPath); os.IsNotExist(err) {
				t.Errorf("library not copied for %s", chip)
			}
		})
	}
}

func TestSetup_Apply_ConfigHeader_ULP(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	setupMockBSECStructure(t, paths, "bme688", "18v", "300s", "28d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme688",
		Voltage:     "18v",
		Interval:    "300s",
		History:     "28d",
		DeepSleep:   true,
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify ULP-specific content
	content, err := os.ReadFile(filepath.Join(paths.TargetDir, "include", "bsec_config.h"))
	if err != nil {
		t.Fatalf("failed to read config header: %v", err)
	}

	checks := []string{
		"BSEC_SAMPLE_RATE_ULP",
		"BSEC_CONFIGURED_INTERVAL_MS 300000",
		"BME688",
		"18v supply, 300s sample rate, 28d history",
	}

	for _, check := range checks {
		if !strings.Contains(string(content), check) {
			t.Errorf("config header missing %q", check)
		}
	}
}

func TestSetup_Apply_ConfigDataFormatting(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create config with many values to test wrapping
	configData := strings.Repeat("1, ", 50) + "1"
	setupMockBSECStructureWithData(t, paths, "bme680", "33v", "3s", "4d", "esp32c3", configData)

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	content, err := os.ReadFile(filepath.Join(paths.TargetDir, "include", "bsec_config.h"))
	if err != nil {
		t.Fatalf("failed to read config header: %v", err)
	}

	// Verify data is formatted with indentation
	if !strings.Contains(string(content), "    1, 1") {
		t.Error("config data not properly indented")
	}
}

func TestSetup_Apply_MissingConfigTxt(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create config dir WITHOUT bsec_iaq.txt
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	// Don't create bsec_iaq.txt

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	for _, h := range paths.Headers {
		if err := os.WriteFile(filepath.Join(incDir, h), []byte("// header"), 0644); err != nil {
			t.Fatalf("failed to create header: %v", err)
		}
	}

	libDir := filepath.Join(paths.SourceDir, "src", "esp32c3")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, paths.LibraryName), []byte("mock"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	err := setup.Apply(config)

	if err == nil {
		t.Error("Apply() should fail when bsec_iaq.txt is missing")
	}
	if !strings.Contains(err.Error(), "failed to read config file") {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestSetup_Apply_DeepSleepFalse(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)

	// Create app_config.hpp with existing BSEC_DEEP_SLEEP_MODE = true
	mainDir := filepath.Dir(paths.AppConfigPath)
	if err := os.MkdirAll(mainDir, 0755); err != nil {
		t.Fatalf("failed to create main dir: %v", err)
	}

	existingConfig := `namespace config {
inline constexpr bool BSEC_DEEP_SLEEP_MODE = true;
} // namespace config`

	if err := os.WriteFile(paths.AppConfigPath, []byte(existingConfig), 0644); err != nil {
		t.Fatalf("failed to create app_config.hpp: %v", err)
	}

	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
		DeepSleep:   false, // Change to false
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify app_config was updated to false
	content, err := os.ReadFile(paths.AppConfigPath)
	if err != nil {
		t.Fatalf("failed to read app_config.hpp: %v", err)
	}

	if !strings.Contains(string(content), "BSEC_DEEP_SLEEP_MODE = false") {
		t.Errorf("app_config.hpp not updated correctly: %s", string(content))
	}
}

func TestSetup_Apply_CustomHeaders(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.Headers = []string{"custom_header.h"} // Custom header list

	// Create structure with custom header
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	// Only create our custom header
	if err := os.WriteFile(filepath.Join(incDir, "custom_header.h"), []byte("// custom"), 0644); err != nil {
		t.Fatalf("failed to create header: %v", err)
	}

	libDir := filepath.Join(paths.SourceDir, "src", "esp32c3")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, paths.LibraryName), []byte("mock"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify custom header was copied
	copiedHeader := filepath.Join(paths.TargetDir, "include", "custom_header.h")
	if _, err := os.Stat(copiedHeader); os.IsNotExist(err) {
		t.Error("custom header was not copied")
	}
}

func TestSetup_Apply_CustomLibraryName(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.LibraryName = "custom_lib.a" // Custom library name

	setupMockBSECStructureWithLib(t, paths, "bme680", "33v", "3s", "4d", "esp32c3", "custom_lib.a")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify custom library was copied
	copiedLib := filepath.Join(paths.TargetDir, "lib", "custom_lib.a")
	if _, err := os.Stat(copiedLib); os.IsNotExist(err) {
		t.Error("custom library was not copied")
	}
}

func TestSetup_Apply_CustomConfigFile(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.ConfigFile = "custom_config.txt" // Custom config filename

	// Create structure with custom config file
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, "custom_config.txt"), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	for _, h := range paths.Headers {
		if err := os.WriteFile(filepath.Join(incDir, h), []byte("// header"), 0644); err != nil {
			t.Fatalf("failed to create header: %v", err)
		}
	}

	libDir := filepath.Join(paths.SourceDir, "src", "esp32c3")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, paths.LibraryName), []byte("mock"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify config header was generated
	headerPath := filepath.Join(paths.TargetDir, "include", "bsec_config.h")
	if _, err := os.Stat(headerPath); os.IsNotExist(err) {
		t.Error("config header was not generated")
	}
}

func TestSetup_Apply_EmptyHeaders(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.Headers = []string{} // No headers to copy

	// Create minimal structure without headers
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	libDir := filepath.Join(paths.SourceDir, "src", "esp32c3")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, paths.LibraryName), []byte("mock"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	// Should succeed even with no headers
	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed with empty headers: %v", err)
	}
}

func TestSetup_Apply_NestedTargetDir(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	// Deeply nested target directory
	paths.TargetDir = filepath.Join(tmpDir, "a", "b", "c", "target")

	setupMockBSECStructure(t, paths, "bme680", "33v", "3s", "4d", "esp32c3")

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify nested directories were created
	libPath := filepath.Join(paths.TargetDir, "lib", paths.LibraryName)
	if _, err := os.Stat(libPath); os.IsNotExist(err) {
		t.Error("library was not copied to nested target dir")
	}
}

func TestSetup_Apply_MultipleHeaders(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	paths := testPaths(tmpDir)
	paths.Headers = []string{"header1.h", "header2.h", "header3.h"}

	// Create structure with multiple headers
	configDir := filepath.Join(paths.SourceDir, "src", "config", "bme680", "bme680_iaq_33v_3s_4d")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1,2,3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	for _, h := range paths.Headers {
		if err := os.WriteFile(filepath.Join(incDir, h), []byte("// "+h), 0644); err != nil {
			t.Fatalf("failed to create header: %v", err)
		}
	}

	libDir := filepath.Join(paths.SourceDir, "src", "esp32c3")
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, paths.LibraryName), []byte("mock"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}

	setup := bsec.NewSetup(paths)
	config := &bsec.Config{
		ESPChip:     "esp32c3",
		ChipVariant: "bme680",
		Voltage:     "33v",
		Interval:    "3s",
		History:     "4d",
	}

	if err := setup.Apply(config); err != nil {
		t.Fatalf("Apply() failed: %v", err)
	}

	// Verify all headers were copied
	for _, h := range paths.Headers {
		headerPath := filepath.Join(paths.TargetDir, "include", h)
		if _, err := os.Stat(headerPath); os.IsNotExist(err) {
			t.Errorf("header %s was not copied", h)
		}
	}
}

// Helper function to create mock BSEC structure
func setupMockBSECStructure(t *testing.T, paths bsec.Paths, chip, voltage, interval, history, espChip string) {
	t.Helper()
	setupMockBSECStructureWithData(t, paths, chip, voltage, interval, history, espChip, "1, 2, 3, 4, 5")
}

func setupMockBSECStructureWithData(t *testing.T, paths bsec.Paths, chip, voltage, interval, history, espChip, configData string) {
	t.Helper()
	setupMockBSECStructureWithLib(t, paths, chip, voltage, interval, history, espChip, paths.LibraryName)

	configName := chip + "_iaq_" + voltage + "_" + interval + "_" + history
	configDir := filepath.Join(paths.SourceDir, "src", "config", chip, configName)
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte(configData), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}
}

func setupMockBSECStructureWithLib(t *testing.T, paths bsec.Paths, chip, voltage, interval, history, espChip, libName string) {
	t.Helper()

	configName := chip + "_iaq_" + voltage + "_" + interval + "_" + history
	configDir := filepath.Join(paths.SourceDir, "src", "config", chip, configName)
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatalf("failed to create config dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(configDir, paths.ConfigFile), []byte("1, 2, 3"), 0644); err != nil {
		t.Fatalf("failed to create config file: %v", err)
	}

	incDir := filepath.Join(paths.SourceDir, "src", "inc")
	if err := os.MkdirAll(incDir, 0755); err != nil {
		t.Fatalf("failed to create inc dir: %v", err)
	}
	for _, h := range paths.Headers {
		if err := os.WriteFile(filepath.Join(incDir, h), []byte("// header"), 0644); err != nil {
			t.Fatalf("failed to create header: %v", err)
		}
	}

	libDir := filepath.Join(paths.SourceDir, "src", espChip)
	if err := os.MkdirAll(libDir, 0755); err != nil {
		t.Fatalf("failed to create lib dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(libDir, libName), []byte("mock lib"), 0644); err != nil {
		t.Fatalf("failed to create library: %v", err)
	}
}
