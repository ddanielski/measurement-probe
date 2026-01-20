package endpoints

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestFindHeaderPath(t *testing.T) {
	tmpDir := t.TempDir()

	// Create nested structure
	headerDir := filepath.Join(tmpDir, RelativePath)
	if err := os.MkdirAll(headerDir, 0755); err != nil {
		t.Fatal(err)
	}

	headerPath := filepath.Join(headerDir, HeaderFileName)
	if err := os.WriteFile(headerPath, []byte("test"), 0644); err != nil {
		t.Fatal(err)
	}

	t.Run("from root", func(t *testing.T) {
		got := FindHeaderPath(tmpDir)
		if got != headerPath {
			t.Errorf("FindHeaderPath() = %q, want %q", got, headerPath)
		}
	})

	t.Run("from subdirectory", func(t *testing.T) {
		subDir := filepath.Join(tmpDir, "some", "subdir")
		if err := os.MkdirAll(subDir, 0755); err != nil {
			t.Fatal(err)
		}
		got := FindHeaderPath(subDir)
		if got != headerPath {
			t.Errorf("FindHeaderPath() = %q, want %q", got, headerPath)
		}
	})

	t.Run("not found", func(t *testing.T) {
		got := FindHeaderPath("/nonexistent")
		if got != "" {
			t.Errorf("FindHeaderPath() = %q, want empty", got)
		}
	})
}

func TestWriteHeader(t *testing.T) {
	tmpDir := t.TempDir()
	path := filepath.Join(tmpDir, "endpoints.hpp")
	url := "https://example.run.app"

	if err := WriteHeader(path, url); err != nil {
		t.Fatalf("WriteHeader() error = %v", err)
	}

	content, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	checks := []string{
		"#pragma once",
		"namespace cloud::endpoints",
		`BASE_URL = "https://example.run.app"`,
		"AUTH_DEVICE",
		"TELEMETRY_PROTO",
	}

	for _, check := range checks {
		if !strings.Contains(string(content), check) {
			t.Errorf("WriteHeader() content missing %q", check)
		}
	}
}

func TestReadBaseURL(t *testing.T) {
	tmpDir := t.TempDir()
	path := filepath.Join(tmpDir, "endpoints.hpp")

	content := `#pragma once
namespace cloud::endpoints {
inline constexpr std::string_view BASE_URL = "https://test.run.app";
}
`
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}

	got, err := ReadBaseURL(path)
	if err != nil {
		t.Fatalf("ReadBaseURL() error = %v", err)
	}
	if got != "https://test.run.app" {
		t.Errorf("ReadBaseURL() = %q, want %q", got, "https://test.run.app")
	}
}

func TestReadBaseURL_NotFound(t *testing.T) {
	tmpDir := t.TempDir()
	path := filepath.Join(tmpDir, "endpoints.hpp")

	if err := os.WriteFile(path, []byte("no url here"), 0644); err != nil {
		t.Fatal(err)
	}

	_, err := ReadBaseURL(path)
	if err == nil {
		t.Error("ReadBaseURL() expected error when BASE_URL not found")
	}
}

func TestValidateOrUpdate(t *testing.T) {
	t.Run("matching URL", func(t *testing.T) {
		tmpDir := t.TempDir()
		path := filepath.Join(tmpDir, "endpoints.hpp")
		url := "https://test.run.app"

		if err := WriteHeader(path, url); err != nil {
			t.Fatal(err)
		}

		err := ValidateOrUpdate(path, url)
		if err != nil {
			t.Errorf("ValidateOrUpdate() unexpected error = %v", err)
		}
	})

	t.Run("mismatched URL updates file", func(t *testing.T) {
		tmpDir := t.TempDir()
		path := filepath.Join(tmpDir, "endpoints.hpp")
		oldURL := "https://old.run.app"
		newURL := "https://new.run.app"

		if err := WriteHeader(path, oldURL); err != nil {
			t.Fatal(err)
		}

		err := ValidateOrUpdate(path, newURL)
		if err == nil {
			t.Error("ValidateOrUpdate() expected error for mismatch")
		}
		if !strings.Contains(err.Error(), "rebuild required") {
			t.Errorf("ValidateOrUpdate() error should mention rebuild: %v", err)
		}

		// Verify file was updated
		got, _ := ReadBaseURL(path)
		if got != newURL {
			t.Errorf("ValidateOrUpdate() didn't update file, got %q, want %q", got, newURL)
		}
	})

	t.Run("missing file creates it", func(t *testing.T) {
		tmpDir := t.TempDir()
		path := filepath.Join(tmpDir, "endpoints.hpp")
		url := "https://new.run.app"

		err := ValidateOrUpdate(path, url)
		if err == nil {
			t.Error("ValidateOrUpdate() expected error for new file")
		}

		// Verify file was created
		got, readErr := ReadBaseURL(path)
		if readErr != nil {
			t.Errorf("File not created: %v", readErr)
		}
		if got != url {
			t.Errorf("ValidateOrUpdate() created with wrong URL, got %q, want %q", got, url)
		}
	})
}
