package partition

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseLine(t *testing.T) {
	tests := []struct {
		name    string
		line    string
		want    Entry
		wantErr bool
	}{
		{
			name: "hex offsets",
			line: "nvs, data, nvs, 0x9000, 0x4000,",
			want: Entry{Name: "nvs", Type: "data", SubType: "nvs", Offset: 0x9000, Size: 0x4000},
		},
		{
			name: "decimal offsets",
			line: "app, app, factory, 65536, 1048576,",
			want: Entry{Name: "app", Type: "app", SubType: "factory", Offset: 65536, Size: 1048576},
		},
		{
			name: "with spaces",
			line: "  nvs  ,  data  ,  nvs  ,  0x9000  ,  0x4000  ,",
			want: Entry{Name: "nvs", Type: "data", SubType: "nvs", Offset: 0x9000, Size: 0x4000},
		},
		{
			name:    "too few fields",
			line:    "nvs, data, nvs",
			wantErr: true,
		},
		{
			name:    "invalid offset",
			line:    "nvs, data, nvs, invalid, 0x4000,",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := parseLine(tt.line)
			if (err != nil) != tt.wantErr {
				t.Errorf("parseLine() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if !tt.wantErr && got != tt.want {
				t.Errorf("parseLine() = %+v, want %+v", got, tt.want)
			}
		})
	}
}

func TestParseHexOrInt(t *testing.T) {
	tests := []struct {
		input   string
		want    int
		wantErr bool
	}{
		{"0x1000", 0x1000, false},
		{"0X1000", 0x1000, false},
		{"4096", 4096, false},
		{"", 0, false},
		{"  0x100  ", 0x100, false},
		{"invalid", 0, true},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			got, err := parseHexOrInt(tt.input)
			if (err != nil) != tt.wantErr {
				t.Errorf("parseHexOrInt(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
				return
			}
			if got != tt.want {
				t.Errorf("parseHexOrInt(%q) = %d, want %d", tt.input, got, tt.want)
			}
		})
	}
}

func TestParseFile(t *testing.T) {
	content := `# ESP-IDF Partition Table
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x4000,
otadata,  data, ota,     0xd000,  0x2000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
`
	tmpDir := t.TempDir()
	path := filepath.Join(tmpDir, "partitions.csv")
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}

	table, err := ParseFile(path)
	if err != nil {
		t.Fatalf("ParseFile() error = %v", err)
	}

	if len(table.entries) != 3 { // factory line has "1M" which fails parsing
		t.Errorf("ParseFile() got %d entries, want 3 (valid ones)", len(table.entries))
	}
}

func TestTableFindByName(t *testing.T) {
	table := &Table{
		entries: []Entry{
			{Name: "nvs", Type: "data", SubType: "nvs", Offset: 0x9000, Size: 0x4000},
			{Name: "app", Type: "app", SubType: "factory", Offset: 0x10000, Size: 0x100000},
		},
	}

	t.Run("found", func(t *testing.T) {
		entry, err := table.FindByName("nvs")
		if err != nil {
			t.Errorf("FindByName() error = %v", err)
		}
		if entry.Offset != 0x9000 {
			t.Errorf("FindByName() offset = 0x%X, want 0x9000", entry.Offset)
		}
	})

	t.Run("not found", func(t *testing.T) {
		_, err := table.FindByName("nonexistent")
		if err == nil {
			t.Error("FindByName() expected error for nonexistent partition")
		}
	})
}

func TestTableFindBySubType(t *testing.T) {
	table := &Table{
		entries: []Entry{
			{Name: "nvs", Type: "data", SubType: "nvs", Offset: 0x9000, Size: 0x4000},
			{Name: "ota_0", Type: "app", SubType: "ota_0", Offset: 0x10000, Size: 0x100000},
		},
	}

	entry, err := table.FindBySubType("ota_0")
	if err != nil {
		t.Errorf("FindBySubType() error = %v", err)
	}
	if entry.Name != "ota_0" {
		t.Errorf("FindBySubType() name = %s, want ota_0", entry.Name)
	}
}
