package prompt_test

import (
	"bytes"
	"strings"
	"testing"

	"measurement-probe/tools/setup/internal/prompt"
)

func TestPrompter_Select(t *testing.T) {
	t.Parallel()

	choices := []prompt.Choice{
		{ID: "first", Display: "First Option"},
		{ID: "second", Display: "Second Option"},
		{ID: "third", Display: "Third Option"},
	}

	tests := []struct {
		name       string
		input      string
		defaultIdx int
		wantID     string
	}{
		{
			name:       "empty input returns default",
			input:      "\n",
			defaultIdx: 0,
			wantID:     "first",
		},
		{
			name:       "select first option",
			input:      "1\n",
			defaultIdx: 1,
			wantID:     "first",
		},
		{
			name:       "select second option",
			input:      "2\n",
			defaultIdx: 0,
			wantID:     "second",
		},
		{
			name:       "select last option",
			input:      "3\n",
			defaultIdx: 0,
			wantID:     "third",
		},
		{
			name:       "invalid input returns default",
			input:      "invalid\n",
			defaultIdx: 1,
			wantID:     "second",
		},
		{
			name:       "out of range high returns default",
			input:      "99\n",
			defaultIdx: 2,
			wantID:     "third",
		},
		{
			name:       "out of range zero returns default",
			input:      "0\n",
			defaultIdx: 0,
			wantID:     "first",
		},
		{
			name:       "negative number returns default",
			input:      "-1\n",
			defaultIdx: 1,
			wantID:     "second",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			input := strings.NewReader(tt.input)
			output := &bytes.Buffer{}
			p := prompt.New(input, output)

			got := p.Select("Test prompt", choices, tt.defaultIdx)

			if got != tt.wantID {
				t.Errorf("Select() = %q, want %q", got, tt.wantID)
			}
		})
	}
}

func TestPrompter_Select_OutputFormat(t *testing.T) {
	t.Parallel()

	choices := []prompt.Choice{
		{ID: "opt1", Display: "Option One"},
		{ID: "opt2", Display: "Option Two"},
	}

	input := strings.NewReader("\n")
	output := &bytes.Buffer{}
	p := prompt.New(input, output)

	p.Select("Choose", choices, 0)

	got := output.String()

	// Verify output contains numbered options
	if !strings.Contains(got, "1. Option One (default)") {
		t.Errorf("output missing first option with default marker: %s", got)
	}
	if !strings.Contains(got, "2. Option Two") {
		t.Errorf("output missing second option: %s", got)
	}
	if !strings.Contains(got, "Choose [1-2, default=1]:") {
		t.Errorf("output missing prompt: %s", got)
	}
}

func TestPrompter_Section(t *testing.T) {
	t.Parallel()

	output := &bytes.Buffer{}
	p := prompt.New(strings.NewReader(""), output)

	p.Section("Test Section")

	got := output.String()
	want := "\n[Test Section]\n"

	if got != want {
		t.Errorf("Section() output = %q, want %q", got, want)
	}
}

func TestPrompter_Print(t *testing.T) {
	t.Parallel()

	output := &bytes.Buffer{}
	p := prompt.New(strings.NewReader(""), output)

	p.Print("Hello %s, number %d", "world", 42)

	got := output.String()
	want := "Hello world, number 42"

	if got != want {
		t.Errorf("Print() output = %q, want %q", got, want)
	}
}

func TestPrompter_Println(t *testing.T) {
	t.Parallel()

	output := &bytes.Buffer{}
	p := prompt.New(strings.NewReader(""), output)

	p.Println("Line 1")
	p.Println("Line 2")

	got := output.String()
	want := "Line 1\nLine 2\n"

	if got != want {
		t.Errorf("Println() output = %q, want %q", got, want)
	}
}
