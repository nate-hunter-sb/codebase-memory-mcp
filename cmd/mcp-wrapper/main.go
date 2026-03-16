// Version: 0.2.0
// cmd/mcp-wrapper/main.go
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
)

func main() {
	port := flag.String("port", "6274", "SSE server port")
	idleTimeout := flag.Int("idle-timeout", 30, "Server idle timeout in minutes")
	serverBinary := flag.String("server-binary", "", "Path to codebase-memory-mcp binary (default: same directory as wrapper)")
	flag.Parse()

	// Suppress log timestamps for cleaner stderr (Claude Code captures stderr)
	log.SetFlags(0)
	log.SetPrefix("[mcp-wrapper] ")

	// Find server binary
	binary := *serverBinary
	if binary == "" {
		var err error
		binary, err = FindServerBinary()
		if err != nil {
			log.Fatalf("cannot find server binary: %v\nUse --server-binary to specify the path", err)
		}
	}
	log.Printf("server binary: %s", binary)
	log.Printf("server port: %s, idle timeout: %dm", *port, *idleTimeout)

	// Set up context with signal handling (SIGINT / Ctrl+C)
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt)
	defer cancel()

	// Create components
	lifecycle := NewLifecycleManager(binary, *port, *idleTimeout)
	serverURL := fmt.Sprintf("http://localhost:%s/mcp", *port)
	proxy := NewProxy(serverURL, lifecycle)

	// Run the proxy loop (blocks until stdin closes or signal received)
	if err := proxy.Run(ctx); err != nil {
		log.Fatalf("proxy error: %v", err)
	}
}
