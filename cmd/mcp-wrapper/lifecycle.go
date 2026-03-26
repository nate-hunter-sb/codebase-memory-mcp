//go:build windows

// Version: 0.2.0
// cmd/mcp-wrapper/lifecycle.go
package main

import (
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"time"
)

// LifecycleManager handles starting and health-checking the SSE server.
type LifecycleManager struct {
	binaryPath  string
	port        string
	idleTimeout int // minutes
}

// NewLifecycleManager creates a manager for the given server binary and port.
func NewLifecycleManager(binaryPath, port string, idleTimeout int) *LifecycleManager {
	return &LifecycleManager{
		binaryPath:  binaryPath,
		port:        port,
		idleTimeout: idleTimeout,
	}
}

// IsRunning checks if something is listening on the configured port.
func (lm *LifecycleManager) IsRunning() bool {
	conn, err := net.DialTimeout("tcp", "localhost:"+lm.port, 2*time.Second)
	if err != nil {
		return false
	}
	conn.Close()
	return true
}

// Start launches the SSE server as a detached background process.
// The server will outlive the wrapper — it's shared across sessions.
// Server stderr is redirected to a log file for debugging.
func (lm *LifecycleManager) Start() error {
	cmd := exec.Command(lm.binaryPath,
		"--transport", "sse",
		"--port", lm.port,
		"--idle-timeout", fmt.Sprintf("%d", lm.idleTimeout),
	)

	// CREATE_NO_WINDOW (0x08000000): suppress the console window entirely.
	// DETACHED_PROCESS would allocate a *new* console (causing a visible flash);
	// CREATE_NO_WINDOW prevents any console from being created.
	// CREATE_NEW_PROCESS_GROUP (0x200): isolates signal handling so Ctrl+C
	// sent to the wrapper doesn't propagate to the server.
	cmd.SysProcAttr = &syscall.SysProcAttr{
		CreationFlags: 0x08000000 | 0x00000200,
	}
	cmd.Stdin = nil
	cmd.Stdout = nil

	// Redirect stderr to log file for debugging
	logPath := filepath.Join(os.TempDir(), "codebase-memory-mcp-sse.log")
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		log.Printf("warning: could not open server log file %s: %v", logPath, err)
		cmd.Stderr = nil
	} else {
		cmd.Stderr = logFile
		// Note: logFile is intentionally not closed here — the spawned process
		// inherits the handle. It will be closed when the server exits.
	}

	if err := cmd.Start(); err != nil {
		if logFile != nil {
			logFile.Close()
		}
		return fmt.Errorf("starting server: %w", err)
	}

	// Release the wrapper's handle to the log file — the spawned process
	// retains its own inherited copy. This prevents the wrapper from
	// holding the file open for its entire lifetime.
	if logFile != nil {
		logFile.Close()
	}

	log.Printf("started server PID %d on port %s (log: %s)", cmd.Process.Pid, lm.port, logPath)
	cmd.Process.Release()
	return nil
}

// EnsureRunning starts the server if it's not already listening, then waits.
func (lm *LifecycleManager) EnsureRunning(ctx context.Context) error {
	if lm.IsRunning() {
		return nil
	}

	log.Printf("server not running, starting...")
	if err := lm.Start(); err != nil {
		return err
	}

	return lm.WaitForReady(ctx)
}

// ForceRestart starts the server unconditionally (bypasses IsRunning check).
// Use this after a failed request where the port may still be in TIME_WAIT.
// If the old server is still running (transient error, not a crash), the new
// process will fail to bind the port and exit harmlessly. WaitForReady will
// then succeed because the original server is still listening.
func (lm *LifecycleManager) ForceRestart(ctx context.Context) error {
	log.Printf("force-restarting server...")
	if err := lm.Start(); err != nil {
		return err
	}
	return lm.WaitForReady(ctx)
}

// WaitForReady polls the port until the server is accepting connections.
// Uses the context deadline if set, otherwise falls back to 15 seconds.
func (lm *LifecycleManager) WaitForReady(ctx context.Context) error {
	// Use context deadline if set, otherwise default to 15s
	if _, ok := ctx.Deadline(); !ok {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, 15*time.Second)
		defer cancel()
	}

	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return fmt.Errorf("server did not become ready: %w", ctx.Err())
		case <-ticker.C:
			if lm.IsRunning() {
				log.Printf("server ready on port %s", lm.port)
				return nil
			}
		}
	}
}

// serverBinaryPath returns the expected server binary path given a directory.
func serverBinaryPath(dir string) string {
	return filepath.Join(dir, "codebase-memory-mcp.exe")
}

// FindServerBinary looks for the server binary in the same directory as the wrapper.
func FindServerBinary() (string, error) {
	exe, err := os.Executable()
	if err != nil {
		return "", fmt.Errorf("finding wrapper executable: %w", err)
	}
	dir := filepath.Dir(exe)
	candidate := serverBinaryPath(dir)
	if _, err := os.Stat(candidate); err != nil {
		return "", fmt.Errorf("server binary not found at %s", candidate)
	}
	return candidate, nil
}
