package partition

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

type Entry struct {
	Name    string
	Type    string
	SubType string
	Offset  int
	Size    int
}

type Table struct {
	entries []Entry
}

func ParseFile(path string) (*Table, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open partition table: %w", err)
	}
	defer file.Close()

	var entries []Entry
	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		entry, err := parseLine(line)
		if err != nil {
			continue // Skip malformed lines
		}

		entries = append(entries, entry)
	}

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("scan partition table: %w", err)
	}

	return &Table{entries: entries}, nil
}

func (t *Table) FindByName(name string) (*Entry, error) {
	for _, e := range t.entries {
		if e.Name == name {
			return &e, nil
		}
	}
	return nil, fmt.Errorf("partition %q not found", name)
}

func (t *Table) FindBySubType(subType string) (*Entry, error) {
	for _, e := range t.entries {
		if e.SubType == subType {
			return &e, nil
		}
	}
	return nil, fmt.Errorf("partition with subtype %q not found", subType)
}

func parseLine(line string) (Entry, error) {
	// Format: Name, Type, SubType, Offset, Size, [Flags]
	parts := strings.Split(line, ",")
	if len(parts) < 5 {
		return Entry{}, fmt.Errorf("invalid line: %s", line)
	}

	offset, err := parseHexOrInt(strings.TrimSpace(parts[3]))
	if err != nil {
		return Entry{}, fmt.Errorf("parse offset: %w", err)
	}

	size, err := parseHexOrInt(strings.TrimSpace(parts[4]))
	if err != nil {
		return Entry{}, fmt.Errorf("parse size: %w", err)
	}

	return Entry{
		Name:    strings.TrimSpace(parts[0]),
		Type:    strings.TrimSpace(parts[1]),
		SubType: strings.TrimSpace(parts[2]),
		Offset:  offset,
		Size:    size,
	}, nil
}

func parseHexOrInt(s string) (int, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, nil
	}

	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		val, err := strconv.ParseInt(s[2:], 16, 64)
		return int(val), err
	}

	val, err := strconv.ParseInt(s, 10, 64)
	return int(val), err
}
