package api

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"
)

type ProvisionRequest struct {
	MACAddress string `json:"mac_address"`
}

type ProvisionResponse struct {
	DeviceID   string `json:"device_id"`
	MACAddress string `json:"mac_address"`
	Secret     string `json:"secret"`
}

type Client struct {
	baseURL    string
	authToken  string
	httpClient *http.Client
}

func NewClient(baseURL, authToken string) *Client {
	return &Client{
		baseURL:   baseURL,
		authToken: authToken,
		httpClient: &http.Client{
			Timeout: 30 * time.Second,
		},
	}
}

func (c *Client) ProvisionDevice(macAddress string) (*ProvisionResponse, error) {
	reqBody := ProvisionRequest{
		MACAddress: macAddress,
	}

	jsonBody, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	url := c.baseURL + "/admin/devices/provision"
	req, err := http.NewRequest(http.MethodPost, url, bytes.NewReader(jsonBody))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+c.authToken)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("execute request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
	}

	if resp.StatusCode == http.StatusConflict {
		return nil, fmt.Errorf("device already provisioned (MAC: %s)", macAddress)
	}

	if resp.StatusCode != http.StatusCreated {
		return nil, fmt.Errorf("provision failed (status %d): %s", resp.StatusCode, string(body))
	}

	var provResp ProvisionResponse
	if err := json.Unmarshal(body, &provResp); err != nil {
		return nil, fmt.Errorf("parse response: %w", err)
	}

	return &provResp, nil
}
