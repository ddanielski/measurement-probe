package project_test

import (
	"os"
	"path/filepath"
	"testing"

	"measurement-probe/tools/setup/internal/project"
)

func TestProject_GeneratedDir(t *testing.T) {
	t.Parallel()

	proj := &project.Project{Root: "/test/root"}

	got := proj.GeneratedDir()
	want := "/test/root/components/generated"

	if got != want {
		t.Errorf("GeneratedDir() = %q, want %q", got, want)
	}
}

func TestProject_AppConfigPath(t *testing.T) {
	t.Parallel()

	proj := &project.Project{Root: "/test/root"}

	got := proj.AppConfigPath()
	want := "/test/root/main/app_config.hpp"

	if got != want {
		t.Errorf("AppConfigPath() = %q, want %q", got, want)
	}
}

func TestFind_Success(t *testing.T) {
	// Note: not parallel because it changes working directory
	tmpDir := t.TempDir()

	// Create mock CMakeLists.txt with project marker
	cmakeContent := `cmake_minimum_required(VERSION 3.16)
project(measurement_probe)
`
	if err := os.WriteFile(filepath.Join(tmpDir, "CMakeLists.txt"), []byte(cmakeContent), 0644); err != nil {
		t.Fatalf("failed to create CMakeLists.txt: %v", err)
	}

	// Create subdirectory and change to it
	subDir := filepath.Join(tmpDir, "tools", "setup")
	if err := os.MkdirAll(subDir, 0755); err != nil {
		t.Fatalf("failed to create subdirectory: %v", err)
	}

	// Save current dir and restore after test
	originalDir, err := os.Getwd()
	if err != nil {
		t.Fatalf("failed to get current dir: %v", err)
	}
	t.Cleanup(func() {
		os.Chdir(originalDir)
	})

	if err := os.Chdir(subDir); err != nil {
		t.Fatalf("failed to change to subdirectory: %v", err)
	}

	// Test finding project from subdirectory
	proj, err := project.Find()
	if err != nil {
		t.Fatalf("Find() error = %v", err)
	}

	if proj.Root != tmpDir {
		t.Errorf("Root = %q, want %q", proj.Root, tmpDir)
	}

	// Verify derived paths
	expectedExternal := filepath.Join(tmpDir, "components", "external")
	if proj.ExternalDir != expectedExternal {
		t.Errorf("ExternalDir = %q, want %q", proj.ExternalDir, expectedExternal)
	}
}

func TestFind_NotFound(t *testing.T) {
	// Note: not parallel because it changes working directory
	tmpDir := t.TempDir()

	// Create CMakeLists.txt WITHOUT the project marker
	cmakeContent := `cmake_minimum_required(VERSION 3.16)
project(some_other_project)
`
	if err := os.WriteFile(filepath.Join(tmpDir, "CMakeLists.txt"), []byte(cmakeContent), 0644); err != nil {
		t.Fatalf("failed to create CMakeLists.txt: %v", err)
	}

	// Save current dir and restore after test
	originalDir, err := os.Getwd()
	if err != nil {
		t.Fatalf("failed to get current dir: %v", err)
	}
	t.Cleanup(func() {
		os.Chdir(originalDir)
	})

	if err := os.Chdir(tmpDir); err != nil {
		t.Fatalf("failed to change directory: %v", err)
	}

	_, err = project.Find()
	if err == nil {
		t.Error("Find() should fail when project marker not found")
	}
}
