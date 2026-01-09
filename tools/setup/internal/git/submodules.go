// Package git provides git operations for project setup.
package git

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

// Submodule defines a git submodule with its verification marker.
type Submodule struct {
	Name   string
	Path   string
	Marker string // Relative path to file that indicates successful init
}

// CommandRunner executes shell commands. Allows mocking in tests.
type CommandRunner interface {
	Run(dir string, name string, args ...string) error
}

// ExecRunner is the default CommandRunner using os/exec.
type ExecRunner struct{}

// Run executes a command in the given directory.
func (r *ExecRunner) Run(dir string, name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// SubmoduleManager handles git submodule operations.
type SubmoduleManager struct {
	rootPath   string
	submodules []Submodule
	runner     CommandRunner
}

// NewSubmoduleManager creates a manager for the given project root and submodules.
func NewSubmoduleManager(rootPath string, submodules []Submodule) *SubmoduleManager {
	return &SubmoduleManager{
		rootPath:   rootPath,
		submodules: submodules,
		runner:     &ExecRunner{},
	}
}

// NewSubmoduleManagerWithRunner creates a manager with a custom command runner (for testing).
func NewSubmoduleManagerWithRunner(rootPath string, submodules []Submodule, runner CommandRunner) *SubmoduleManager {
	return &SubmoduleManager{
		rootPath:   rootPath,
		submodules: submodules,
		runner:     runner,
	}
}

// Setup initializes and verifies git submodules.
func (m *SubmoduleManager) Setup() error {
	if err := m.CheckGitmodules(); err != nil {
		return err
	}

	if err := m.InitSubmodules(); err != nil {
		return err
	}

	return m.VerifySubmodules()
}

// CheckGitmodules verifies .gitmodules file exists.
func (m *SubmoduleManager) CheckGitmodules() error {
	gitmodulesPath := filepath.Join(m.rootPath, ".gitmodules")
	if _, err := os.Stat(gitmodulesPath); os.IsNotExist(err) {
		return &SubmoduleError{
			Message: ".gitmodules not found",
			Hint:    "git clone --recursive <repository-url>",
		}
	}
	return nil
}

// InitSubmodules runs git submodule update.
func (m *SubmoduleManager) InitSubmodules() error {
	if err := m.runner.Run(m.rootPath, "git", "submodule", "update", "--init", "--recursive"); err != nil {
		return fmt.Errorf("git submodule update failed: %w", err)
	}
	return nil
}

// VerifySubmodules checks that all configured submodule markers exist.
func (m *SubmoduleManager) VerifySubmodules() error {
	for _, sub := range m.submodules {
		markerPath := filepath.Join(sub.Path, sub.Marker)
		if _, err := os.Stat(markerPath); os.IsNotExist(err) {
			return &SubmoduleError{
				Message: fmt.Sprintf("%s submodule is not initialized", sub.Name),
				Hint:    "git submodule update --init --recursive",
			}
		}
	}
	return nil
}

// VerifySubmodule checks a single submodule's marker exists.
func (m *SubmoduleManager) VerifySubmodule(sub Submodule) error {
	markerPath := filepath.Join(sub.Path, sub.Marker)
	if _, err := os.Stat(markerPath); os.IsNotExist(err) {
		return &SubmoduleError{
			Message: fmt.Sprintf("%s submodule is not initialized", sub.Name),
			Hint:    "git submodule update --init --recursive",
		}
	}
	return nil
}

// SubmoduleError represents a submodule initialization error with recovery hint.
type SubmoduleError struct {
	Message string
	Hint    string
}

func (e *SubmoduleError) Error() string {
	return fmt.Sprintf(`
╔═══════════════════════════════════════════════════════════════╗
║  ERROR: %-54s ║
╠═══════════════════════════════════════════════════════════════╣
║  Please run:                                                  ║
║    %-58s ║
║                                                               ║
║  Then re-run this setup tool.                                 ║
╚═══════════════════════════════════════════════════════════════╝`, e.Message, e.Hint)
}
