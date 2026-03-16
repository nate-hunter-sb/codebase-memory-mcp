//go:build windows

// Version: 0.2.0
// cmd/mcp-wrapper/lifecycle_test.go
package main

import (
	"context"
	"fmt"
	"net"
	"path/filepath"
	"testing"
	"time"
)

func TestIsRunning_PortOpen(t *testing.T) {
	ln, err := net.Listen("tcp", "localhost:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	port := ln.Addr().(*net.TCPAddr).Port
	lm := NewLifecycleManager("dummy", fmt.Sprintf("%d", port), 30)

	if !lm.IsRunning() {
		t.Error("expected IsRunning=true when port is listening")
	}
}

func TestIsRunning_PortClosed(t *testing.T) {
	// Bind and immediately close to get a definitely-unused port
	ln, err := net.Listen("tcp", "localhost:0")
	if err != nil {
		t.Fatal(err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	ln.Close()

	lm := NewLifecycleManager("dummy", fmt.Sprintf("%d", port), 30)

	if lm.IsRunning() {
		t.Error("expected IsRunning=false when port is not listening")
	}
}

func TestWaitForReady_AlreadyRunning(t *testing.T) {
	ln, err := net.Listen("tcp", "localhost:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	port := ln.Addr().(*net.TCPAddr).Port
	lm := NewLifecycleManager("dummy", fmt.Sprintf("%d", port), 30)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	if err := lm.WaitForReady(ctx); err != nil {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestWaitForReady_Timeout(t *testing.T) {
	ln, err := net.Listen("tcp", "localhost:0")
	if err != nil {
		t.Fatal(err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	ln.Close()

	lm := NewLifecycleManager("dummy", fmt.Sprintf("%d", port), 30)

	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	err = lm.WaitForReady(ctx)
	if err == nil {
		t.Error("expected timeout error")
	}
}

func TestServerBinaryPath(t *testing.T) {
	// Use filepath.Join for platform-agnostic paths
	dir := filepath.Join("C:", "some", "path")
	got := serverBinaryPath(dir)
	want := filepath.Join("C:", "some", "path", "codebase-memory-mcp.exe")
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}
