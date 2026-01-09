// Package prompt provides user interaction utilities for CLI tools.
package prompt

import (
	"bufio"
	"fmt"
	"io"
	"strconv"
	"strings"
)

// Prompter handles user interaction for configuration.
type Prompter struct {
	reader *bufio.Reader
	writer io.Writer
}

// New creates a prompter with the given input/output streams.
func New(r io.Reader, w io.Writer) *Prompter {
	return &Prompter{
		reader: bufio.NewReader(r),
		writer: w,
	}
}

// Choice represents a menu option.
type Choice struct {
	ID      string
	Display string
}

// Select displays options and returns the selected ID.
func (p *Prompter) Select(prompt string, choices []Choice, defaultIdx int) string {
	for i, c := range choices {
		mark := ""
		if i == defaultIdx {
			mark = " (default)"
		}
		fmt.Fprintf(p.writer, "    %d. %s%s\n", i+1, c.Display, mark)
	}

	fmt.Fprintf(p.writer, "%s [1-%d, default=%d]: ", prompt, len(choices), defaultIdx+1)

	input, _ := p.reader.ReadString('\n')
	input = strings.TrimSpace(input)

	if input == "" {
		return choices[defaultIdx].ID
	}

	idx, err := strconv.Atoi(input)
	if err != nil || idx < 1 || idx > len(choices) {
		return choices[defaultIdx].ID
	}

	return choices[idx-1].ID
}

// Section prints a section header.
func (p *Prompter) Section(title string) {
	fmt.Fprintf(p.writer, "\n[%s]\n", title)
}

// Print writes a formatted message.
func (p *Prompter) Print(format string, args ...any) {
	fmt.Fprintf(p.writer, format, args...)
}

// Println writes a message with newline.
func (p *Prompter) Println(args ...any) {
	fmt.Fprintln(p.writer, args...)
}
