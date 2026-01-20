package gcloud

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
)

const (
	adminAPIKeySecret = "admin-api-key"
)

func EnsureAuthenticated() error {
	cmd := exec.Command("gcloud", "auth", "list", "--filter=status:ACTIVE", "--format=value(account)")
	output, err := cmd.Output()
	if err != nil {
		return fmt.Errorf("gcloud auth list failed: %w", err)
	}

	if strings.TrimSpace(string(output)) != "" {
		return nil
	}

	fmt.Println("No active gcloud account found. Starting login...")
	loginCmd := exec.Command("gcloud", "auth", "login")
	loginCmd.Stdin = os.Stdin
	loginCmd.Stdout = os.Stdout
	loginCmd.Stderr = os.Stderr
	if err := loginCmd.Run(); err != nil {
		return fmt.Errorf("gcloud auth login failed: %w", err)
	}
	return nil
}

func GetActiveAccount() (string, error) {
	cmd := exec.Command("gcloud", "auth", "list", "--filter=status:ACTIVE", "--format=value(account)")
	output, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(output)), nil
}

func EnsureProject(project string) error {
	if project == "" {
		cmd := exec.Command("gcloud", "config", "get-value", "project")
		output, err := cmd.Output()
		if err != nil {
			return fmt.Errorf("no project set - use --project flag or run: gcloud config set project <PROJECT_ID>")
		}
		project = strings.TrimSpace(string(output))
		if project == "" || project == "(unset)" {
			return fmt.Errorf("no project set - use --project flag or run: gcloud config set project <PROJECT_ID>")
		}
	}

	cmd := exec.Command("gcloud", "projects", "describe", project, "--format=value(projectId)")
	output, err := cmd.Output()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			return fmt.Errorf("cannot access project %s: %s", project, string(exitErr.Stderr))
		}
		return fmt.Errorf("cannot access project %s: %w", project, err)
	}

	if strings.TrimSpace(string(output)) == "" {
		return fmt.Errorf("project %s not found or no access", project)
	}

	return nil
}

func SetProject(project string) error {
	cmd := exec.Command("gcloud", "config", "set", "project", project)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to set project: %w", err)
	}
	return nil
}

func GetCurrentProject() (string, error) {
	cmd := exec.Command("gcloud", "config", "get-value", "project")
	output, err := cmd.Output()
	if err != nil {
		return "", err
	}
	project := strings.TrimSpace(string(output))
	if project == "" || project == "(unset)" {
		return "", fmt.Errorf("no project configured")
	}
	return project, nil
}

func GetServiceURL(service, region string) (string, error) {
	cmd := exec.Command("gcloud", "run", "services", "describe", service,
		"--region", region,
		"--format", "value(status.url)")

	output, err := cmd.Output()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			return "", fmt.Errorf("gcloud failed: %s", string(exitErr.Stderr))
		}
		return "", err
	}

	url := strings.TrimSpace(string(output))
	if url == "" {
		return "", fmt.Errorf("service %s not found in region %s", service, region)
	}

	return url, nil
}

// GetAdminAPIKey fetches the admin API key from Secret Manager
// User must have roles/secretmanager.secretAccessor on the secret
func GetAdminAPIKey(projectID string) (string, error) {
	cmd := exec.Command("gcloud", "secrets", "versions", "access", "latest",
		"--secret", adminAPIKeySecret,
		"--project", projectID)

	output, err := cmd.Output()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			stderr := strings.TrimSpace(string(exitErr.Stderr))
			if strings.Contains(stderr, "PERMISSION_DENIED") || strings.Contains(stderr, "does not have") {
				return "", fmt.Errorf("no permission to access secret %s - contact infra team to add your email to provisioner_users", adminAPIKeySecret)
			}
			return "", fmt.Errorf("failed to access secret: %s", stderr)
		}
		return "", fmt.Errorf("failed to access secret: %w", err)
	}

	return strings.TrimSpace(string(output)), nil
}

// Deprecated: ProvisionerServiceAccount returns the provisioner SA name (no longer used)
func ProvisionerServiceAccount(projectID string) string {
	return fmt.Sprintf("provisioner@%s.iam.gserviceaccount.com", projectID)
}

// Deprecated: GetIdentityToken is no longer used - use GetAdminAPIKey instead
func GetIdentityToken(projectID, audience string) (string, error) {
	return "", fmt.Errorf("GetIdentityToken is deprecated - use GetAdminAPIKey instead")
}
