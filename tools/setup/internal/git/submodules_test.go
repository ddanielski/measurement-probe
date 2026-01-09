package git_test

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"measurement-probe/tools/setup/internal/git"
)

// mockRunner is a test double for CommandRunner.
type mockRunner struct {
	runFunc func(dir, name string, args ...string) error
	calls   []runCall
}

type runCall struct {
	Dir  string
	Name string
	Args []string
}

func (m *mockRunner) Run(dir, name string, args ...string) error {
	m.calls = append(m.calls, runCall{Dir: dir, Name: name, Args: args})
	if m.runFunc != nil {
		return m.runFunc(dir, name, args...)
	}
	return nil
}

// testSubmodules returns a standard set of submodules for testing.
func testSubmodules(tmpDir string) []git.Submodule {
	return []git.Submodule{
		{
			Name:   "BSEC2",
			Path:   filepath.Join(tmpDir, "bsec2"),
			Marker: "src/inc/bsec_interface.h",
		},
		{
			Name:   "BME68x",
			Path:   filepath.Join(tmpDir, "bme68x"),
			Marker: "bme68x.h",
		},
	}
}

func TestSubmoduleError_Error(t *testing.T) {
	t.Parallel()

	err := &git.SubmoduleError{
		Message: "test error message",
		Hint:    "git submodule update --init",
	}

	got := err.Error()

	if !strings.Contains(got, "test error message") {
		t.Error("error message not found in output")
	}
	if !strings.Contains(got, "git submodule update --init") {
		t.Error("hint not found in output")
	}
	if !strings.Contains(got, "ERROR") {
		t.Error("ERROR label not found in output")
	}
}

func TestSubmoduleError_Formatting(t *testing.T) {
	t.Parallel()

	err := &git.SubmoduleError{
		Message: "Test",
		Hint:    "hint",
	}

	got := err.Error()

	// Verify box drawing characters
	boxChars := []string{"╔", "╚", "═", "║", "╠", "╣"}
	for _, ch := range boxChars {
		if !strings.Contains(got, ch) {
			t.Errorf("error formatting missing box character %q", ch)
		}
	}
}

func TestNewSubmoduleManager(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	mgr := git.NewSubmoduleManager(tmpDir, subs)

	if mgr == nil {
		t.Error("NewSubmoduleManager returned nil")
	}
}

func TestSubmoduleManager_CheckGitmodules_Missing(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)
	runner := &mockRunner{}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.CheckGitmodules()

	if err == nil {
		t.Error("CheckGitmodules should fail when .gitmodules is missing")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}

	if !strings.Contains(subErr.Message, ".gitmodules") {
		t.Errorf("error should mention .gitmodules: %s", subErr.Message)
	}
}

func TestSubmoduleManager_CheckGitmodules_Exists(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	subs := testSubmodules(tmpDir)
	runner := &mockRunner{}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.CheckGitmodules()

	if err != nil {
		t.Errorf("CheckGitmodules should succeed when .gitmodules exists: %v", err)
	}
}

func TestSubmoduleManager_InitSubmodules_Success(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)
	runner := &mockRunner{}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.InitSubmodules()

	if err != nil {
		t.Errorf("InitSubmodules should succeed with mock runner: %v", err)
	}

	// Verify correct command was called
	if len(runner.calls) != 1 {
		t.Fatalf("expected 1 call, got %d", len(runner.calls))
	}

	call := runner.calls[0]
	if call.Dir != tmpDir {
		t.Errorf("Dir = %q, want %q", call.Dir, tmpDir)
	}
	if call.Name != "git" {
		t.Errorf("Name = %q, want %q", call.Name, "git")
	}
	expectedArgs := []string{"submodule", "update", "--init", "--recursive"}
	if len(call.Args) != len(expectedArgs) {
		t.Errorf("Args = %v, want %v", call.Args, expectedArgs)
	}
	for i, arg := range expectedArgs {
		if call.Args[i] != arg {
			t.Errorf("Args[%d] = %q, want %q", i, call.Args[i], arg)
		}
	}
}

func TestSubmoduleManager_InitSubmodules_Failure(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)
	runner := &mockRunner{
		runFunc: func(dir, name string, args ...string) error {
			return errors.New("git command failed")
		},
	}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.InitSubmodules()

	if err == nil {
		t.Error("InitSubmodules should fail when runner returns error")
	}
	if !strings.Contains(err.Error(), "git submodule update failed") {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestSubmoduleManager_VerifySubmodules_AllPresent(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Create both markers
	bsec2Inc := filepath.Join(tmpDir, "bsec2", "src", "inc")
	if err := os.MkdirAll(bsec2Inc, 0755); err != nil {
		t.Fatalf("failed to create BSEC2 dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bsec2Inc, "bsec_interface.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BSEC2 marker: %v", err)
	}

	bme68xPath := filepath.Join(tmpDir, "bme68x")
	if err := os.MkdirAll(bme68xPath, 0755); err != nil {
		t.Fatalf("failed to create BME68x dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bme68xPath, "bme68x.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BME68x marker: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.VerifySubmodules()

	if err != nil {
		t.Errorf("VerifySubmodules should succeed when all markers exist: %v", err)
	}
}

func TestSubmoduleManager_VerifySubmodules_MissingFirst(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Only create second marker (BME68x)
	bme68xPath := filepath.Join(tmpDir, "bme68x")
	if err := os.MkdirAll(bme68xPath, 0755); err != nil {
		t.Fatalf("failed to create BME68x dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bme68xPath, "bme68x.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BME68x marker: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.VerifySubmodules()

	if err == nil {
		t.Error("VerifySubmodules should fail when first submodule marker is missing")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}
	if !strings.Contains(subErr.Message, "BSEC2") {
		t.Errorf("error should mention BSEC2: %s", subErr.Message)
	}
}

func TestSubmoduleManager_VerifySubmodules_MissingSecond(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Only create first marker (BSEC2)
	bsec2Inc := filepath.Join(tmpDir, "bsec2", "src", "inc")
	if err := os.MkdirAll(bsec2Inc, 0755); err != nil {
		t.Fatalf("failed to create BSEC2 dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bsec2Inc, "bsec_interface.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BSEC2 marker: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.VerifySubmodules()

	if err == nil {
		t.Error("VerifySubmodules should fail when second submodule marker is missing")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}
	if !strings.Contains(subErr.Message, "BME68x") {
		t.Errorf("error should mention BME68x: %s", subErr.Message)
	}
}

func TestSubmoduleManager_VerifySubmodule_Single(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()

	// Create a single submodule
	sub := git.Submodule{
		Name:   "TestModule",
		Path:   filepath.Join(tmpDir, "testmod"),
		Marker: "marker.h",
	}

	// Create marker
	if err := os.MkdirAll(sub.Path, 0755); err != nil {
		t.Fatalf("failed to create dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(sub.Path, "marker.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create marker: %v", err)
	}

	mgr := git.NewSubmoduleManager(tmpDir, nil)
	err := mgr.VerifySubmodule(sub)

	if err != nil {
		t.Errorf("VerifySubmodule should succeed: %v", err)
	}
}

func TestSubmoduleManager_VerifySubmodule_Missing(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()

	sub := git.Submodule{
		Name:   "MissingModule",
		Path:   filepath.Join(tmpDir, "missing"),
		Marker: "marker.h",
	}

	mgr := git.NewSubmoduleManager(tmpDir, nil)
	err := mgr.VerifySubmodule(sub)

	if err == nil {
		t.Error("VerifySubmodule should fail for missing marker")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}
	if !strings.Contains(subErr.Message, "MissingModule") {
		t.Errorf("error should mention module name: %s", subErr.Message)
	}
}

func TestSubmoduleManager_Setup_FailsOnMissingGitmodules(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)
	runner := &mockRunner{}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.Setup()

	if err == nil {
		t.Error("Setup should fail when .gitmodules is missing")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}

	// Verify git was never called since we failed early
	if len(runner.calls) != 0 {
		t.Errorf("expected no git calls, got %d", len(runner.calls))
	}
}

func TestSubmoduleManager_Setup_FullFlow(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Create .gitmodules
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	// Create all markers
	bsec2Inc := filepath.Join(tmpDir, "bsec2", "src", "inc")
	if err := os.MkdirAll(bsec2Inc, 0755); err != nil {
		t.Fatalf("failed to create BSEC2 dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bsec2Inc, "bsec_interface.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BSEC2 marker: %v", err)
	}

	bme68xPath := filepath.Join(tmpDir, "bme68x")
	if err := os.MkdirAll(bme68xPath, 0755); err != nil {
		t.Fatalf("failed to create BME68x dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(bme68xPath, "bme68x.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create BME68x marker: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.Setup()

	if err != nil {
		t.Errorf("Setup should succeed: %v", err)
	}

	// Verify git command was called
	if len(runner.calls) != 1 {
		t.Errorf("expected 1 git call, got %d", len(runner.calls))
	}
}

func TestSubmoduleManager_Setup_FailsOnGitError(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Create .gitmodules
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	runner := &mockRunner{
		runFunc: func(dir, name string, args ...string) error {
			return errors.New("git error")
		},
	}

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.Setup()

	if err == nil {
		t.Error("Setup should fail when git fails")
	}
}

func TestSubmoduleManager_Setup_FailsOnVerifyError(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()
	subs := testSubmodules(tmpDir)

	// Create .gitmodules
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	// Don't create any submodule markers - verification should fail
	runner := &mockRunner{} // Git succeeds

	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, subs, runner)
	err := mgr.Setup()

	if err == nil {
		t.Error("Setup should fail when verification fails")
	}

	var subErr *git.SubmoduleError
	if !errors.As(err, &subErr) {
		t.Fatalf("expected SubmoduleError, got %T", err)
	}
}

func TestSubmoduleManager_EmptySubmodules(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()

	// Create .gitmodules
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, nil, runner)
	err := mgr.Setup()

	// With empty submodules, Setup should succeed (nothing to verify)
	if err != nil {
		t.Errorf("Setup with empty submodules should succeed: %v", err)
	}
}

func TestSubmoduleManager_CustomSubmodule(t *testing.T) {
	t.Parallel()

	tmpDir := t.TempDir()

	// Define a custom submodule
	customSubs := []git.Submodule{
		{
			Name:   "CustomLib",
			Path:   filepath.Join(tmpDir, "custom"),
			Marker: "lib/include/custom.h",
		},
	}

	// Create .gitmodules
	if err := os.WriteFile(filepath.Join(tmpDir, ".gitmodules"), []byte("[submodule]"), 0644); err != nil {
		t.Fatalf("failed to create .gitmodules: %v", err)
	}

	// Create marker
	markerDir := filepath.Join(tmpDir, "custom", "lib", "include")
	if err := os.MkdirAll(markerDir, 0755); err != nil {
		t.Fatalf("failed to create marker dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(markerDir, "custom.h"), []byte(""), 0644); err != nil {
		t.Fatalf("failed to create marker: %v", err)
	}

	runner := &mockRunner{}
	mgr := git.NewSubmoduleManagerWithRunner(tmpDir, customSubs, runner)
	err := mgr.Setup()

	if err != nil {
		t.Errorf("Setup with custom submodule should succeed: %v", err)
	}
}
