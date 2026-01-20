package api

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestProvisionDevice(t *testing.T) {
	t.Run("success", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path != "/admin/devices/provision" {
				t.Errorf("unexpected path: %s", r.URL.Path)
			}
			if r.Method != http.MethodPost {
				t.Errorf("unexpected method: %s", r.Method)
			}
			if r.Header.Get("Authorization") != "Bearer test-token" {
				t.Errorf("unexpected auth header: %s", r.Header.Get("Authorization"))
			}
			if r.Header.Get("Content-Type") != "application/json" {
				t.Errorf("unexpected content-type: %s", r.Header.Get("Content-Type"))
			}

			var req ProvisionRequest
			if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
				t.Errorf("decode request: %v", err)
			}
			if req.MACAddress != "aa:bb:cc:dd:ee:ff" {
				t.Errorf("unexpected mac: %s", req.MACAddress)
			}

			w.WriteHeader(http.StatusCreated)
			json.NewEncoder(w).Encode(ProvisionResponse{
				DeviceID: "device-123",
				Secret:   "secret-456",
			})
		}))
		defer server.Close()

		client := NewClient(server.URL, "test-token")
		resp, err := client.ProvisionDevice("aa:bb:cc:dd:ee:ff")

		if err != nil {
			t.Fatalf("ProvisionDevice() error = %v", err)
		}
		if resp.DeviceID != "device-123" {
			t.Errorf("DeviceID = %s, want device-123", resp.DeviceID)
		}
		if resp.Secret != "secret-456" {
			t.Errorf("Secret = %s, want secret-456", resp.Secret)
		}
	})

	t.Run("conflict", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusConflict)
		}))
		defer server.Close()

		client := NewClient(server.URL, "token")
		_, err := client.ProvisionDevice("aa:bb:cc:dd:ee:ff")

		if err == nil {
			t.Error("expected error for conflict")
		}
	})

	t.Run("server error", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusInternalServerError)
			w.Write([]byte("internal error"))
		}))
		defer server.Close()

		client := NewClient(server.URL, "token")
		_, err := client.ProvisionDevice("aa:bb:cc:dd:ee:ff")

		if err == nil {
			t.Error("expected error for server error")
		}
	})
}
