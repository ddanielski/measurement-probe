// Package project handles measurement-probe project detection and path management.
package project

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	projectMarker = "project(measurement_probe)"
	externalDir   = "components/external"
)

// Project holds paths and configuration for the measurement-probe project.
type Project struct {
	Root        string
	ExternalDir string
	BSEC2Path   string
	BME68xPath  string
	BSEC2Target string
}

// Find locates the project root and initializes paths.
func Find() (*Project, error) {
	root, err := findRoot()
	if err != nil {
		return nil, err
	}

	extDir := filepath.Join(root, externalDir)
	return &Project{
		Root:        root,
		ExternalDir: extDir,
		BSEC2Path:   filepath.Join(extDir, "Bosch-BSEC2-Library"),
		BME68xPath:  filepath.Join(extDir, "BME68x_SensorAPI"),
		BSEC2Target: filepath.Join(extDir, "bsec2"),
	}, nil
}

func findRoot() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("failed to get working directory: %w", err)
	}

	// Walk up to 5 levels looking for project marker
	for i := 0; i < 5; i++ {
		cmakePath := filepath.Join(dir, "CMakeLists.txt")
		if content, err := os.ReadFile(cmakePath); err == nil {
			if strings.Contains(string(content), projectMarker) {
				return dir, nil
			}
		}
		dir = filepath.Dir(dir)
	}

	return "", fmt.Errorf("could not find project root (CMakeLists.txt with %s)", projectMarker)
}

// GeneratedDir returns the path to the generated components directory.
func (p *Project) GeneratedDir() string {
	return filepath.Join(p.Root, "components", "generated")
}

// AppConfigPath returns the path to app_config.hpp.
func (p *Project) AppConfigPath() string {
	return filepath.Join(p.Root, "main", "app_config.hpp")
}
