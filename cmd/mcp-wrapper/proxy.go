// Version: 0.2.0
// cmd/mcp-wrapper/proxy.go
package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

// jsonrpcMessage is used for minimal inspection of JSON-RPC messages.
type jsonrpcMessage struct {
	ID     json.RawMessage `json:"id,omitempty"`
	Method string          `json:"method,omitempty"`
}

// isNotification returns true if the message is a JSON-RPC notification (has method but no id).
func isNotification(msg []byte) bool {
	var m jsonrpcMessage
	if err := json.Unmarshal(msg, &m); err != nil {
		return false
	}
	return m.Method != "" && (m.ID == nil || string(m.ID) == "null")
}

// isInitializeRequest returns true if the message is an MCP "initialize" request.
func isInitializeRequest(msg []byte) bool {
	var m jsonrpcMessage
	if err := json.Unmarshal(msg, &m); err != nil {
		return false
	}
	return m.Method == "initialize" && m.ID != nil && string(m.ID) != "null"
}

// Proxy bridges stdio JSON-RPC to an HTTP/SSE MCP server.
type Proxy struct {
	serverURL string
	lifecycle *LifecycleManager
	sessionID string
	initReq   []byte // stored initialize request for re-init after server restart
	mu        sync.Mutex
	client    *http.Client
}

// NewProxy creates a proxy that forwards to the given server URL.
// lifecycle may be nil for testing (assumes server is already running).
func NewProxy(serverURL string, lifecycle *LifecycleManager) *Proxy {
	return &Proxy{
		serverURL: serverURL,
		lifecycle: lifecycle,
		client: &http.Client{
			Timeout: 5 * time.Minute,
		},
	}
}

// ForwardRequest sends a JSON-RPC message to the SSE server via HTTP POST.
// Returns the JSON-RPC response bytes, or nil for notifications (HTTP 202).
func (p *Proxy) ForwardRequest(ctx context.Context, jsonrpc []byte) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, "POST", p.serverURL, bytes.NewReader(jsonrpc))
	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json, text/event-stream")

	p.mu.Lock()
	if p.sessionID != "" {
		req.Header.Set("Mcp-Session-Id", p.sessionID)
	}
	p.mu.Unlock()

	resp, err := p.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	// Capture session ID from response
	if sid := resp.Header.Get("Mcp-Session-Id"); sid != "" {
		p.mu.Lock()
		p.sessionID = sid
		p.mu.Unlock()
	}

	// 202 Accepted = notification acknowledged, no body
	if resp.StatusCode == http.StatusAccepted {
		return nil, nil
	}

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("server returned HTTP %d: %s", resp.StatusCode, body)
	}

	// Parse based on content type
	ct := resp.Header.Get("Content-Type")
	if strings.Contains(ct, "text/event-stream") {
		return ParseSSEResponse(resp.Body)
	}

	// Plain JSON response
	return io.ReadAll(resp.Body)
}

// Run starts the proxy loop using os.Stdin and os.Stdout.
func (p *Proxy) Run(ctx context.Context) error {
	return p.RunWithIO(ctx, os.Stdin, os.Stdout)
}

// RunWithIO is the testable version of Run — accepts custom reader/writer.
func (p *Proxy) RunWithIO(ctx context.Context, stdin io.Reader, stdout io.Writer) error {
	scanner := bufio.NewScanner(stdin)
	// MCP messages can be large (code snippets, index results)
	scanner.Buffer(make([]byte, 0, 64*1024), 10*1024*1024)

	for scanner.Scan() {
		line := scanner.Bytes()
		if len(bytes.TrimSpace(line)) == 0 {
			continue
		}

		// Make a copy — scanner reuses the buffer
		msg := make([]byte, len(line))
		copy(msg, line)

		// Ensure server is running (skip if no lifecycle manager — testing)
		if p.lifecycle != nil {
			if err := p.lifecycle.EnsureRunning(ctx); err != nil {
				return fmt.Errorf("server not available: %w", err)
			}
		}

		// Store initialize request for re-init after server restarts
		if isInitializeRequest(msg) {
			p.mu.Lock()
			p.initReq = make([]byte, len(msg))
			copy(p.initReq, msg)
			p.mu.Unlock()
		}

		notification := isNotification(msg)

		// Forward to server
		resp, err := p.ForwardRequest(ctx, msg)
		if err != nil {
			// Server may have died — try restart and retry
			resp, err = p.handleServerFailure(ctx, msg)
			if err != nil {
				// Can't recover — send JSON-RPC error to Claude Code
				if !notification {
					errResp := p.makeErrorResponse(msg, err)
					stdout.Write(errResp)
					stdout.Write([]byte("\n"))
				}
				log.Printf("request failed: %v", err)
				continue // Don't kill the proxy — keep trying for next request
			}
		}

		// Write response to stdout (only for requests, not notifications)
		if !notification && resp != nil && len(resp) > 0 {
			stdout.Write(resp)
			stdout.Write([]byte("\n"))
		}
	}

	return scanner.Err()
}

// handleServerFailure attempts to restart the server and retry the request.
// Uses ForceRestart to bypass IsRunning() which may false-positive due to TCP TIME_WAIT.
func (p *Proxy) handleServerFailure(ctx context.Context, originalRequest []byte) ([]byte, error) {
	if p.lifecycle == nil {
		return nil, fmt.Errorf("server unavailable and no lifecycle manager")
	}

	log.Printf("request failed, attempting server restart...")

	// Force restart — don't trust IsRunning() after a failure (TCP TIME_WAIT)
	if err := p.lifecycle.ForceRestart(ctx); err != nil {
		return nil, fmt.Errorf("server restart failed: %w", err)
	}

	// Clear session — new server has no session state
	p.mu.Lock()
	p.sessionID = ""
	initReq := p.initReq
	p.mu.Unlock()

	// Re-initialize the new server (unless the failed request IS the initialize)
	if initReq != nil && !isInitializeRequest(originalRequest) {
		log.Printf("re-initializing new server instance...")
		if _, err := p.ForwardRequest(ctx, initReq); err != nil {
			return nil, fmt.Errorf("re-initialization failed: %w", err)
		}
		// Send the "initialized" notification
		notif := []byte(`{"jsonrpc":"2.0","method":"notifications/initialized"}`)
		p.ForwardRequest(ctx, notif) // fire and forget
	}

	// Retry original request
	return p.ForwardRequest(ctx, originalRequest)
}

// makeErrorResponse creates a JSON-RPC error response for the given request.
// Uses json.Marshal to properly escape error messages containing special characters.
func (p *Proxy) makeErrorResponse(request []byte, originalErr error) []byte {
	var m jsonrpcMessage
	id := json.RawMessage("null")
	if err := json.Unmarshal(request, &m); err == nil && m.ID != nil {
		id = m.ID
	}

	escapedMsg, _ := json.Marshal(fmt.Sprintf("wrapper: %s", originalErr.Error()))

	return []byte(fmt.Sprintf(`{"jsonrpc":"2.0","id":%s,"error":{"code":-32603,"message":%s}}`, id, escapedMsg))
}
