package serial

import (
	"regexp"
	"strings"
	"testing"
)

func TestMACRegex(t *testing.T) {
	re := regexp.MustCompile(`MAC:\s*([0-9a-fA-F:]{17})`)

	tests := []struct {
		input string
		want  string
	}{
		{"MAC: aa:bb:cc:dd:ee:ff", "aa:bb:cc:dd:ee:ff"},
		{"MAC:AA:BB:CC:DD:EE:FF", "AA:BB:CC:DD:EE:FF"},
		{"MAC:  12:34:56:78:9a:bc", "12:34:56:78:9a:bc"},
		{"esptool.py v4.7\nMAC: 01:02:03:04:05:06\nDone", "01:02:03:04:05:06"},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			matches := re.FindStringSubmatch(tt.input)
			if len(matches) < 2 {
				t.Fatalf("no match found in %q", tt.input)
			}
			if matches[1] != tt.want {
				t.Errorf("got %q, want %q", matches[1], tt.want)
			}
		})
	}
}

func TestMACNormalization(t *testing.T) {
	input := "AA:BB:CC:DD:EE:FF"
	want := "aa:bb:cc:dd:ee:ff"

	got := strings.ToLower(input)
	if got != want {
		t.Errorf("ToLower(%q) = %q, want %q", input, got, want)
	}
}

func TestNewMACReader(t *testing.T) {
	reader := NewMACReader("/dev/ttyUSB0")
	if reader.port != "/dev/ttyUSB0" {
		t.Errorf("port = %s, want /dev/ttyUSB0", reader.port)
	}
}
