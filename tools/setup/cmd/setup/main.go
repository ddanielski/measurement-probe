// Package main provides a setup tool for the measurement-probe project.
// It handles git submodule initialization, BSEC configuration, and provisioning setup.
package main

import (
	"fmt"
	"os"

	"measurement-probe/tools/setup/internal/bsec"
	"measurement-probe/tools/setup/internal/git"
	"measurement-probe/tools/setup/internal/project"
	"measurement-probe/tools/setup/internal/prompt"
	"measurement-probe/tools/setup/internal/provisioning"
)

// Menu options for BSEC configuration.
var (
	espChips = []prompt.Choice{
		{ID: "esp32c3", Display: "ESP32-C3 (RISC-V)"},
		{ID: "esp32", Display: "ESP32 (Xtensa)"},
		{ID: "esp32s2", Display: "ESP32-S2 (Xtensa)"},
		{ID: "esp32s3", Display: "ESP32-S3 (Xtensa)"},
	}

	sensorChips = []prompt.Choice{
		{ID: "bme680", Display: "BME680"},
		{ID: "bme688", Display: "BME688"},
	}

	voltageOptions = []prompt.Choice{
		{ID: "33v", Display: "3.3V"},
		{ID: "18v", Display: "1.8V"},
	}

	modeOptions = []prompt.Choice{
		{ID: "continuous", Display: "Continuous (3s sampling, LP mode) - for always-on devices"},
		{ID: "deepsleep", Display: "Deep Sleep (300s sampling, ULP mode) - for battery devices"},
	}

	historyOptions = []prompt.Choice{
		{ID: "4d", Display: "4 days (faster initial calibration)"},
		{ID: "28d", Display: "28 days (more stable long-term)"},
	}
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	ui := prompt.New(os.Stdin, os.Stdout)

	printBanner(ui)

	// Find project
	proj, err := project.Find()
	if err != nil {
		return err
	}
	ui.Print("Project root: %s\n\n", proj.Root)

	// Step 1: Submodules
	ui.Println("─── Step 1: External Dependencies ───")
	if err := setupSubmodules(proj, ui); err != nil {
		return err
	}

	// Step 2: BSEC configuration
	ui.Println("\n─── Step 2: BSEC Configuration ───")
	config := promptBSECConfig(ui)

	// Step 3: Apply configuration
	ui.Println("\n─── Step 3: Applying Configuration ───")
	if err := applyBSECConfig(proj, config, ui); err != nil {
		return err
	}

	// Step 4: Provisioning
	ui.Println("\n─── Step 4: Provisioning Secret ───")
	pop, err := setupProvisioning(proj, ui)
	if err != nil {
		return err
	}

	printSuccess(ui, pop)
	return nil
}

func printBanner(ui *prompt.Prompter) {
	ui.Println("╔══════════════════════════════════════════════════════════╗")
	ui.Println("║       Measurement Probe - Project Setup Tool             ║")
	ui.Println("╚══════════════════════════════════════════════════════════╝")
	ui.Println()
}

func setupSubmodules(proj *project.Project, ui *prompt.Prompter) error {
	ui.Println("Initializing git submodules...")

	// Define required submodules with their marker files
	submodules := []git.Submodule{
		{
			Name:   "Bosch-BSEC2-Library",
			Path:   proj.BSEC2Path,
			Marker: "src/inc/bsec_interface.h",
		},
		{
			Name:   "BME68x_SensorAPI",
			Path:   proj.BME68xPath,
			Marker: "bme68x.h",
		},
	}

	mgr := git.NewSubmoduleManager(proj.Root, submodules)
	if err := mgr.Setup(); err != nil {
		return err
	}

	for _, sub := range submodules {
		ui.Print("✓ %s ready\n", sub.Name)
	}
	return nil
}

func promptBSECConfig(ui *prompt.Prompter) *bsec.Config {
	config := &bsec.Config{}

	ui.Section("1) Target ESP Chip")
	config.ESPChip = ui.Select("Select ESP chip", espChips, 0)

	ui.Section("2) Sensor Chip Variant")
	config.ChipVariant = ui.Select("Select sensor", sensorChips, 0)

	ui.Section("3) Supply Voltage")
	config.Voltage = ui.Select("Select voltage", voltageOptions, 0)

	ui.Section("4) Operation Mode")
	mode := ui.Select("Select mode", modeOptions, 0)
	if mode == "deepsleep" {
		config.DeepSleep = true
		config.Interval = "300s"
	} else {
		config.DeepSleep = false
		config.Interval = "3s"
	}

	ui.Section("5) Calibration History")
	config.History = ui.Select("Select history", historyOptions, 0)

	return config
}

func applyBSECConfig(proj *project.Project, config *bsec.Config, ui *prompt.Prompter) error {
	ui.Print("Selected configuration: %s\n", config.Name())

	paths := bsec.Paths{
		SourceDir:     proj.BSEC2Path,
		TargetDir:     proj.BSEC2Target,
		AppConfigPath: proj.AppConfigPath(),
		Headers:       []string{"bsec_datatypes.h", "bsec_interface.h"},
		ConfigFile:    "bsec_iaq.txt",
		LibraryName:   "libalgobsec.a",
	}

	setup := bsec.NewSetup(paths)
	if err := setup.Apply(config); err != nil {
		return err
	}

	ui.Print("✓ Configuration applied: %s\n", config.Name())
	ui.Print("  Target: %s\n", config.ESPChip)
	if config.DeepSleep {
		ui.Println("  Mode: Deep Sleep (ULP, 300s intervals)")
	} else {
		ui.Println("  Mode: Continuous (LP, 3s intervals)")
	}

	return nil
}

func setupProvisioning(proj *project.Project, ui *prompt.Prompter) (string, error) {
	defaults := provisioning.Defaults{
		DeviceName:   "MeasureProbe",
		TimeoutSec:   300,
		PopBytes:     4, // 4 bytes = 8 hex chars
		OutputFile:   "provisioning_config.h",
		GeneratedDir: proj.GeneratedDir(),
	}

	setup := provisioning.NewSetup(defaults)
	config, isNew, err := setup.Generate()
	if err != nil {
		return "", err
	}

	if isNew {
		ui.Print("Generated new provisioning secret: %s\n", config.PoP)
	} else {
		ui.Print("Using existing provisioning secret: %s\n", config.PoP)
	}

	return config.PoP, nil
}

func printSuccess(ui *prompt.Prompter, pop string) {
	ui.Println("\n✓ Setup complete!")
	ui.Println("\n╔══════════════════════════════════════════════════════════╗")
	ui.Println("║  PROVISIONING SECRET (keep this safe!)                   ║")
	ui.Print("║  PoP: %-50s ║\n", pop)
	ui.Println("╚══════════════════════════════════════════════════════════╝")
	ui.Println("\nNext steps:")
	ui.Println("  1. Run 'idf.py build' to compile")
	ui.Println("  2. Run 'idf.py flash monitor' to deploy")
	ui.Println("  3. Use ESP BLE Provisioning app with the PoP above")
}
