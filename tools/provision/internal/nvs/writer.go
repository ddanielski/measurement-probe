package nvs

import (
	"encoding/csv"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

type Credentials struct {
	DeviceID string
	Secret   string
}

type Writer struct {
	espIdfPath string
	port       string
	namespace  string
}

func NewWriter(espIdfPath, port string) *Writer {
	return &Writer{
		espIdfPath: espIdfPath,
		port:       port,
		namespace:  "cloud",
	}
}

func (w *Writer) GenerateCSV(creds *Credentials, outputPath string) error {
	file, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("create CSV: %w", err)
	}
	defer file.Close()

	writer := csv.NewWriter(file)
	defer writer.Flush()

	// Write header
	if err := writer.Write([]string{"key", "type", "encoding", "value"}); err != nil {
		return err
	}

	// Write namespace
	if err := writer.Write([]string{w.namespace, "namespace", "", ""}); err != nil {
		return err
	}

	// Write credentials
	records := [][]string{
		{"device_id", "data", "string", creds.DeviceID},
		{"secret", "data", "string", creds.Secret},
		{"fb_api_key", "data", "string", creds.FirebaseAPIKey},
	}

	for _, record := range records {
		if err := writer.Write(record); err != nil {
			return err
		}
	}

	return nil
}

func (w *Writer) GenerateBinary(csvPath, binPath string, size int) error {
	scriptPath := filepath.Join(w.espIdfPath, "components", "nvs_flash", "nvs_partition_generator", "nvs_partition_gen.py")

	cmd := exec.Command("python3", scriptPath, "generate", csvPath, binPath, fmt.Sprintf("0x%x", size))
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("nvs_partition_gen.py failed: %w", err)
	}

	return nil
}

func (w *Writer) Flash(binPath string, offset int) error {
	cmd := exec.Command("esptool.py",
		"--port", w.port,
		"write_flash", fmt.Sprintf("0x%x", offset), binPath,
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("esptool.py failed: %w", err)
	}

	return nil
}

func (w *Writer) WriteCredentials(creds *Credentials, tmpDir string, partitionOffset, partitionSize int) error {
	csvPath := filepath.Join(tmpDir, "nvs_creds.csv")
	binPath := filepath.Join(tmpDir, "nvs_creds.bin")

	if err := w.GenerateCSV(creds, csvPath); err != nil {
		return fmt.Errorf("generate CSV: %w", err)
	}

	if err := w.GenerateBinary(csvPath, binPath, partitionSize); err != nil {
		return fmt.Errorf("generate binary: %w", err)
	}

	if err := w.Flash(binPath, partitionOffset); err != nil {
		return fmt.Errorf("flash: %w", err)
	}

	return nil
}
