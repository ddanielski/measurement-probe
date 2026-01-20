package main

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"measurement-probe/tools/provision/internal/api"
	"measurement-probe/tools/provision/internal/endpoints"
	"measurement-probe/tools/provision/internal/gcloud"
	"measurement-probe/tools/provision/internal/nvs"
	"measurement-probe/tools/provision/internal/partition"
	"measurement-probe/tools/provision/internal/serial"
)

const (
	nvsPartitionName      = "nvs"
	defaultPartitionTable = "partitions.csv"
	defaultService        = "telemetry-api"
	defaultRegion         = "us-west1"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "\n❌ Error: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	// Parse flags - minimal required args
	project := flag.String("project", "", "GCP project ID (or uses gcloud default)")
	region := flag.String("region", defaultRegion, "GCP region")
	service := flag.String("service", defaultService, "Cloud Run service name")
	port := flag.String("port", "", "Serial port (auto-detect if single device)")
	macAddress := flag.String("mac", "", "Device MAC (skip auto-detection)")
	dryRun := flag.Bool("dry-run", false, "Provision only, don't flash to device")
	skipBuild := flag.Bool("skip-build", false, "Skip automatic rebuild")
	flag.Parse()

	fmt.Println("╔═══════════════════════════════════════════════════════════╗")
	fmt.Println("║           Measurement Probe Provisioning Tool             ║")
	fmt.Println("╚═══════════════════════════════════════════════════════════╝")
	fmt.Println()

	// Step 1: Ensure gcloud authentication
	fmt.Println("→ Checking gcloud authentication...")
	if err := gcloud.EnsureAuthenticated(); err != nil {
		return fmt.Errorf("authentication failed: %w", err)
	}
	account, _ := gcloud.GetActiveAccount()
	fmt.Printf("  ✓ Authenticated as: %s\n", account)

	// Step 2: Ensure project access
	fmt.Println("\n→ Checking GCP project access...")
	projectID := *project
	if projectID == "" {
		var err error
		projectID, err = gcloud.GetCurrentProject()
		if err != nil {
			return fmt.Errorf("no project specified and none configured: use --project flag")
		}
	}
	if err := gcloud.EnsureProject(projectID); err != nil {
		return err
	}
	// Set project if it was provided explicitly
	if *project != "" {
		if err := gcloud.SetProject(projectID); err != nil {
			return err
		}
	}
	fmt.Printf("  ✓ Project: %s\n", projectID)

	// Step 3: Fetch Cloud Run service URL
	fmt.Printf("\n→ Fetching Cloud Run service URL (%s in %s)...\n", *service, *region)
	serviceURL, err := gcloud.GetServiceURL(*service, *region)
	if err != nil {
		return fmt.Errorf("failed to get service URL: %w", err)
	}
	fmt.Printf("  ✓ Service URL: %s\n", serviceURL)

	// Step 4: Validate/update endpoints.hpp
	fmt.Println("\n→ Validating firmware configuration...")
	cwd, _ := os.Getwd()
	headerPath := endpoints.FindHeaderPath(cwd)
	if headerPath == "" {
		return fmt.Errorf("endpoints.hpp not found - are you in the project directory?")
	}

	needsRebuild := false
	if err := endpoints.ValidateOrUpdate(headerPath, serviceURL); err != nil {
		fmt.Printf("  ⚠️  %v\n", err)
		needsRebuild = true
	} else {
		fmt.Printf("  ✓ Firmware URL matches\n")
	}

	// Step 5: Trigger rebuild if needed
	if needsRebuild {
		if *skipBuild {
			fmt.Println("\n⚠️  Firmware needs rebuild but --skip-build specified")
			fmt.Println("   Run 'idf.py build' manually before flashing")
		} else {
			fmt.Println("\n→ Rebuilding firmware...")
			if err := runBuild(); err != nil {
				return fmt.Errorf("build failed: %w", err)
			}
			fmt.Println("  ✓ Build complete")
		}
	}

	// Step 6: Get serial port
	fmt.Println("\n→ Detecting device...")
	serialPort := *port
	if serialPort == "" && *macAddress == "" {
		ports, err := serial.ListPorts()
		if err != nil {
			return fmt.Errorf("list ports: %w", err)
		}
		if len(ports) == 0 {
			return fmt.Errorf("no serial ports found - is device connected?")
		}
		if len(ports) > 1 {
			fmt.Println("  Multiple ports found:")
			for i, p := range ports {
				fmt.Printf("    %d: %s\n", i+1, p)
			}
			return fmt.Errorf("specify port with --port flag")
		}
		serialPort = ports[0]
	}
	if serialPort != "" {
		fmt.Printf("  ✓ Port: %s\n", serialPort)
	}

	// Step 7: Read MAC address
	mac := *macAddress
	if mac == "" {
		fmt.Println("\n→ Reading device MAC address...")
		reader := serial.NewMACReader(serialPort)
		var err error
		mac, err = reader.ReadMAC()
		if err != nil {
			return fmt.Errorf("read MAC: %w", err)
		}
	}
	fmt.Printf("  ✓ Device MAC: %s\n", mac)

	// Step 8: Get admin API key and provision
	fmt.Println("\n→ Provisioning device with backend...")
	fmt.Println("  Fetching admin API key from Secret Manager...")
	apiKey, err := gcloud.GetAdminAPIKey(projectID)
	if err != nil {
		return fmt.Errorf("get admin API key: %w", err)
	}
	fmt.Println("  ✓ API key retrieved")

	client := api.NewClient(serviceURL, apiKey)
	resp, err := client.ProvisionDevice(mac)
	if err != nil {
		return fmt.Errorf("provision failed: %w", err)
	}
	fmt.Printf("  ✓ Device ID: %s\n", resp.DeviceID)

	if *dryRun {
		fmt.Println("\n[Dry run] Skipping NVS flash")
		printCredentials(resp, serviceURL)
		return nil
	}

	// Step 9: Write to NVS
	fmt.Println("\n→ Writing credentials to device NVS...")

	// Get IDF_PATH
	idfPath := os.Getenv("IDF_PATH")
	if idfPath == "" {
		return fmt.Errorf("IDF_PATH not set - source ESP-IDF environment")
	}

	// Find partition table
	partPath := findPartitionTable()
	if partPath == "" {
		return fmt.Errorf("partition table not found")
	}

	partTable, err := partition.ParseFile(partPath)
	if err != nil {
		return fmt.Errorf("parse partition table: %w", err)
	}

	nvsPartition, err := partTable.FindByName(nvsPartitionName)
	if err != nil {
		return fmt.Errorf("find NVS partition: %w", err)
	}

	tmpDir, err := os.MkdirTemp("", "provision-*")
	if err != nil {
		return fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	creds := &nvs.Credentials{
		DeviceID: resp.DeviceID,
		Secret:   resp.Secret,
	}

	writer := nvs.NewWriter(idfPath, serialPort)
	if err := writer.WriteCredentials(creds, tmpDir, nvsPartition.Offset, nvsPartition.Size); err != nil {
		return fmt.Errorf("write NVS: %w", err)
	}

	fmt.Println("\n" + strings.Repeat("═", 60))
	fmt.Println("✓ Device provisioned successfully!")
	printCredentials(resp, serviceURL)

	return nil
}

func findPartitionTable() string {
	if _, err := os.Stat(defaultPartitionTable); err == nil {
		return defaultPartitionTable
	}

	dir, _ := os.Getwd()
	for i := 0; i < 5; i++ {
		candidate := filepath.Join(dir, defaultPartitionTable)
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

func runBuild() error {
	// Find project root (where CMakeLists.txt is)
	dir, _ := os.Getwd()
	for i := 0; i < 5; i++ {
		if _, err := os.Stat(filepath.Join(dir, "CMakeLists.txt")); err == nil {
			break
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return fmt.Errorf("project root not found")
		}
		dir = parent
	}

	cmd := exec.Command("idf.py", "build")
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func printCredentials(resp *api.ProvisionResponse, baseURL string) {
	fmt.Println()
	fmt.Println("╔══════════════════════════════════════════════════════════╗")
	fmt.Println("║                  DEVICE CREDENTIALS                      ║")
	fmt.Println("╠══════════════════════════════════════════════════════════╣")
	fmt.Printf("║ Device ID: %-45s ║\n", resp.DeviceID)
	secretDisplay := resp.Secret
	if len(secretDisplay) > 16 {
		secretDisplay = secretDisplay[:16] + "..."
	}
	fmt.Printf("║ Secret:    %-45s ║\n", secretDisplay)
	fmt.Println("╚══════════════════════════════════════════════════════════╝")
	fmt.Println()
	fmt.Printf("Backend: %s\n", baseURL)

	// Save backup
	homeDir, _ := os.UserHomeDir()
	credsDir := filepath.Join(homeDir, ".measurement-probe", "credentials")
	_ = os.MkdirAll(credsDir, 0700)

	credsFile := filepath.Join(credsDir, resp.DeviceID+".json")
	content := fmt.Sprintf(`{
  "device_id": "%s",
  "secret": "%s"
}
`, resp.DeviceID, resp.Secret)

	if err := os.WriteFile(credsFile, []byte(content), 0600); err == nil {
		fmt.Printf("Backup saved: %s\n", credsFile)
	}
}
