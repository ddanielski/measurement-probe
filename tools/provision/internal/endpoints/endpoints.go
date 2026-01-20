package endpoints

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	HeaderFileName = "endpoints.hpp"
	RelativePath   = "components/library/cloud/include/cloud"
)

func FindHeaderPath(startDir string) string {
	dir := startDir
	for i := 0; i < 6; i++ {
		candidate := filepath.Join(dir, RelativePath, HeaderFileName)
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return ""
}

func ReadBaseURL(headerPath string) (string, error) {
	file, err := os.Open(headerPath)
	if err != nil {
		return "", err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.Contains(line, "BASE_URL") && strings.Contains(line, "=") {
			start := strings.Index(line, `"`)
			if start == -1 {
				continue
			}
			end := strings.LastIndex(line, `"`)
			if end <= start {
				continue
			}
			return line[start+1 : end], nil
		}
	}
	return "", fmt.Errorf("BASE_URL not found in %s", headerPath)
}

func WriteHeader(headerPath, url string) error {
	content := fmt.Sprintf(`// Auto-generated - DO NOT EDIT
// Generated: %s

#pragma once

#include <string_view>

namespace cloud::endpoints {

inline constexpr std::string_view BASE_URL = "%s";

inline constexpr std::string_view AUTH_DEVICE = "/auth/device";
inline constexpr std::string_view AUTH_REFRESH = "/auth/refresh";
inline constexpr std::string_view TELEMETRY_PROTO = "/telemetry/proto";
inline constexpr std::string_view COMMANDS = "/commands";
inline constexpr std::string_view DEVICE_INFO = "/devices/info";

} // namespace cloud::endpoints
`, time.Now().UTC().Format(time.RFC3339), url)

	return os.WriteFile(headerPath, []byte(content), 0644)
}

func ValidateOrUpdate(headerPath, expectedURL string) error {
	currentURL, err := ReadBaseURL(headerPath)
	if err != nil {
		if writeErr := WriteHeader(headerPath, expectedURL); writeErr != nil {
			return fmt.Errorf("failed to generate %s: %w", headerPath, writeErr)
		}
		return fmt.Errorf("generated %s - rebuild required", headerPath)
	}

	if currentURL == expectedURL {
		return nil
	}

	if writeErr := WriteHeader(headerPath, expectedURL); writeErr != nil {
		return fmt.Errorf("failed to update %s: %w", headerPath, writeErr)
	}
	return fmt.Errorf("updated %s: %s -> %s - rebuild required", headerPath, currentURL, expectedURL)
}
