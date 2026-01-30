package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
	"unicode"

	secretmanager "cloud.google.com/go/secretmanager/apiv1"
	"cloud.google.com/go/secretmanager/apiv1/secretmanagerpb"
)

// MeasurementSchema represents the backend schema format
type MeasurementSchema struct {
	ID   uint32 `json:"id"`
	Name string `json:"name"`
	Type string `json:"type"`
	Unit string `json:"unit"`
}

// MEASUREMENT_TRAIT has 4 fields: ID, TYPE, NAME, UNIT
const measurementTraitFieldCount = 4

type SchemaRequest struct {
	Measurements map[string]MeasurementSchema `json:"measurements"`
}

type SchemaResponse struct {
	App     string `json:"app"`
	Version string `json:"version"`
	Message string `json:"message"`
}

// Simple CamelCase to human-readable string converter
func toHumanReadable(s string) string {
	if s == "IAQ" {
		return "Indoor Air Quality"
	}
	var result []rune
	for i, r := range s {
		if i > 0 && unicode.IsUpper(r) && (i+1 < len(s) && unicode.IsLower(rune(s[i+1]))) {
			result = append(result, ' ')
		}
		result = append(result, r)
	}
	return string(result)
}

// Normalize units to match backend expectations
func normalizeUnit(unit string) string {
	switch unit {
	case "°C":
		return "celsius"
	case "%":
		return "percent"
	default:
		return unit
	}
}

func main() {
	var (
		appName    = flag.String("app", "probe", "Application name")
		version    = flag.String("version", "", "Firmware version (required)")
		apiURL     = flag.String("api-url", "https://telemetry-api-cn4vxdwjxq-uw.a.run.app", "Backend API URL")
		projectID  = flag.String("project", "", "GCP project ID (required for Secret Manager)")
		secretName = flag.String("secret", "github-actions-api-key", "Secret Manager secret name")
		schemaFile = flag.String("schema", "", "Path to schema JSON file (optional, generates if not provided)")
		dryRun     = flag.Bool("dry-run", false, "Generate schema but don't upload")
		outputFile = flag.String("o", "", "Write generated schema to a file instead of stdout")
	)
	flag.Parse()

	if *version == "" && !*dryRun {
		log.Fatal("Error: -version is required unless in dry-run mode")
	}

	// Generate or load schema
	var schema SchemaRequest
	var err error

	if *schemaFile != "" {
		schema, err = loadSchema(*schemaFile)
		if err != nil {
			log.Fatalf("Failed to load schema: %v", err)
		}
	} else {
		// Generate schema from measurement definitions
		schema, err = generateSchema()
		if err != nil {
			log.Fatalf("Failed to generate schema: %v", err)
		}
	}

	// Validate schema
	if len(schema.Measurements) == 0 {
		log.Fatal("Error: Schema has no measurements")
	}

	schemaJSON, err := json.MarshalIndent(schema, "", "  ")
	if err != nil {
		log.Fatalf("Failed to marshal schema to JSON: %v", err)
	}

	// Print or write schema
	if *outputFile != "" {
		err := os.WriteFile(*outputFile, schemaJSON, 0644)
		if err != nil {
			log.Fatalf("Failed to write schema to %s: %v", *outputFile, err)
		}
		fmt.Printf("✓ Schema written to %s\n", *outputFile)
		return
	} else {
		fmt.Println("Generated schema:")
		fmt.Println(string(schemaJSON))
		fmt.Println()
	}

	if *dryRun {
		fmt.Println("Dry run - not uploading")
		return
	}

	// Get API key from Secret Manager
	if *projectID == "" {
		log.Fatal("Error: -project is required for upload")
	}

	apiKey, err := getSecretValue(*projectID, *secretName)
	if err != nil {
		log.Fatalf("Failed to get API key from Secret Manager: %v", err)
	}
	fmt.Println("✓ Retrieved API key from Secret Manager")

	// Upload schema
	url := fmt.Sprintf("%s/admin/schemas/%s/%s", *apiURL, *appName, *version)
	if err := uploadSchema(url, apiKey, schema); err != nil {
		log.Fatalf("Failed to upload schema: %v", err)
	}

	fmt.Printf("✓ Schema uploaded successfully for %s v%s\n", *appName, *version)
}

// getSecretValue retrieves a secret from GCP Secret Manager using Application Default Credentials
func getSecretValue(projectID, secretName string) (string, error) {
	ctx := context.Background()

	client, err := secretmanager.NewClient(ctx)
	if err != nil {
		return "", fmt.Errorf("failed to create Secret Manager client: %w", err)
	}
	defer client.Close()

	name := fmt.Sprintf("projects/%s/secrets/%s/versions/latest", projectID, secretName)

	req := &secretmanagerpb.AccessSecretVersionRequest{
		Name: name,
	}

	result, err := client.AccessSecretVersion(ctx, req)
	if err != nil {
		return "", fmt.Errorf("failed to access secret %s: %w", secretName, err)
	}

	return string(result.Payload.Data), nil
}

func generateSchema() (SchemaRequest, error) {
	// Read measurement.hpp to extract measurement definitions
	// Try multiple possible paths (relative to repo root or ci directory)
	possiblePaths := []string{
		"components/library/sensor_base/include/sensor/measurement.hpp",
		"../components/library/sensor_base/include/sensor/measurement.hpp",
		"../../components/library/sensor_base/include/sensor/measurement.hpp",
	}

	var data []byte
	var err error

	for _, path := range possiblePaths {
		data, err = os.ReadFile(path)
		if err == nil {
			break
		}
	}

	if err != nil {
		return SchemaRequest{}, fmt.Errorf("failed to read measurement.hpp (tried %v): %w", possiblePaths, err)
	}

	// First, parse enum definition to map enum names to values
	lines := strings.Split(string(data), "\n")
	enumNameToValue := make(map[string]uint32)
	enumValue := uint32(0)
	inEnum := false

	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.Contains(trimmed, "enum class MeasurementId") {
			inEnum = true
			continue
		}
		if inEnum {
			if strings.Contains(trimmed, "Count") || strings.Contains(trimmed, "};") {
				break
			}
			// Skip comments and empty lines
			if strings.HasPrefix(trimmed, "//") || trimmed == "" {
				continue
			}
			// Parse enum entries like "Temperature," or "Timestamp = 1,"
			if strings.Contains(trimmed, ",") {
				entry := strings.TrimSpace(strings.Split(trimmed, ",")[0])
				// Remove any trailing comments
				if idx := strings.Index(entry, "//"); idx >= 0 {
					entry = strings.TrimSpace(entry[:idx])
				}
				// Check for explicit value assignment (e.g., "Timestamp = 1")
				var enumName string
				if idx := strings.Index(entry, "="); idx >= 0 {
					enumName = strings.TrimSpace(entry[:idx])
					valueStr := strings.TrimSpace(entry[idx+1:])
					if val, err := strconv.ParseUint(valueStr, 10, 32); err == nil {
						enumValue = uint32(val)
					}
				} else {
					enumName = entry
				}
				if enumName != "" {
					enumNameToValue[enumName] = enumValue
					enumValue++
				}
			}
		}
	}

	measurements := make(map[string]MeasurementSchema)

	// Manual overrides for human-readable names
	nameOverrides := map[string]string{
		"co2": "CO2 Equivalent",
		"voc": "Volatile Organic Compounds",
	}

	for _, line := range lines {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "MEASUREMENT_TRAIT(") {
			continue
		}

		// Parse: MEASUREMENT_TRAIT(Temperature, float, "temperature", "°C");
		parts := strings.Split(line, ",")
		if len(parts) < measurementTraitFieldCount {
			continue
		}

		// Extract ID (first param) - this is the enum name
		idStr := strings.TrimSpace(strings.TrimPrefix(parts[0], "MEASUREMENT_TRAIT("))

		// Extract TYPE (second param)
		typeStr := strings.TrimSpace(parts[1])

		// Extract NAME (third param, remove quotes)
		nameStr := strings.TrimSpace(parts[2])
		nameStr = strings.Trim(nameStr, `"`)

		// Extract UNIT (fourth param, remove quotes and closing paren)
		unitStr := strings.TrimSpace(parts[3])
		unitStr = strings.TrimSuffix(unitStr, ");")
		unitStr = strings.Trim(unitStr, `"`)

		// Skip Count enum value
		if idStr == "Count" {
			continue
		}

		// Get enum value from the map we built
		enumID, ok := enumNameToValue[idStr]
		if !ok {
			log.Printf("Warning: Could not find enum value for %s, skipping", idStr)
			continue
		}

		measurementID := enumID

		// Map C++ types to backend types
		backendType := mapType(typeStr)

		// Generate human-readable name
		humanName := toHumanReadable(idStr)
		if override, exists := nameOverrides[nameStr]; exists {
			humanName = override
		}

		measurements[nameStr] = MeasurementSchema{
			ID:   measurementID,
			Name: humanName,
			Type: backendType,
			Unit: normalizeUnit(unitStr),
		}
	}

	return SchemaRequest{Measurements: measurements}, nil
}

func mapType(cppType string) string {
	cppType = strings.TrimSpace(cppType)
	switch cppType {
	case "float", "double":
		return "float"
	case "int32_t", "int64_t", "uint32_t", "uint64_t", "uint8_t":
		return "int"
	case "bool":
		return "bool"
	default:
		return "float" // Default fallback
	}
}

func loadSchema(path string) (SchemaRequest, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return SchemaRequest{}, err
	}

	var schema SchemaRequest
	if err := json.Unmarshal(data, &schema); err != nil {
		return SchemaRequest{}, err
	}

	return schema, nil
}

func uploadSchema(url, apiKey string, schema SchemaRequest) error {
	jsonData, err := json.Marshal(schema)
	if err != nil {
		return fmt.Errorf("failed to marshal schema: %w", err)
	}

	req, err := http.NewRequest("POST", url, bytes.NewBuffer(jsonData))
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Authorization", fmt.Sprintf("Bearer %s", apiKey))
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)

	if resp.StatusCode != http.StatusCreated {
		return fmt.Errorf("upload failed with status %d: %s", resp.StatusCode, string(body))
	}

	// Parse response
	var result SchemaResponse
	if err := json.Unmarshal(body, &result); err == nil {
		fmt.Printf("Response: %s\n", result.Message)
	}

	return nil
}
