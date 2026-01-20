package serial

import (
	"bufio"
	"fmt"
	"os/exec"
	"regexp"
	"strings"
	"time"

	"go.bug.st/serial"
)

type MACReader struct {
	port string
}

func NewMACReader(port string) *MACReader {
	return &MACReader{port: port}
}

func (r *MACReader) ReadMAC() (string, error) {
	cmd := exec.Command("esptool.py", "--port", r.port, "read_mac")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("esptool read_mac failed: %w\nOutput: %s", err, string(output))
	}

	// Parse MAC from output
	// Expected format: "MAC: aa:bb:cc:dd:ee:ff"
	re := regexp.MustCompile(`MAC:\s*([0-9a-fA-F:]{17})`)
	matches := re.FindStringSubmatch(string(output))
	if len(matches) < 2 {
		return "", fmt.Errorf("could not find MAC in output: %s", string(output))
	}

	return strings.ToLower(matches[1]), nil
}

func (r *MACReader) ReadMACFromSerial(timeout time.Duration) (string, error) {
	mode := &serial.Mode{
		BaudRate: 115200,
	}

	port, err := serial.Open(r.port, mode)
	if err != nil {
		return "", fmt.Errorf("open port: %w", err)
	}
	defer port.Close()

	// Set read timeout
	if err := port.SetReadTimeout(timeout); err != nil {
		return "", fmt.Errorf("set timeout: %w", err)
	}

	// Reset device to get boot output
	if err := port.SetDTR(false); err == nil {
		time.Sleep(100 * time.Millisecond)
		_ = port.SetDTR(true)
	}

	// Read output looking for MAC
	scanner := bufio.NewScanner(port)
	re := regexp.MustCompile(`MAC:\s*([0-9a-fA-F:]{17})`)

	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if scanner.Scan() {
			line := scanner.Text()
			if matches := re.FindStringSubmatch(line); len(matches) >= 2 {
				return strings.ToLower(matches[1]), nil
			}
		}
	}

	return "", fmt.Errorf("timeout waiting for MAC address")
}

func ListPorts() ([]string, error) {
	ports, err := serial.GetPortsList()
	if err != nil {
		return nil, fmt.Errorf("get ports: %w", err)
	}
	return ports, nil
}
