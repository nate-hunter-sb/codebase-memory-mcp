// Version: 0.2.0
// cmd/mcp-wrapper/sse.go
package main

import (
	"bufio"
	"fmt"
	"io"
	"strings"
)

// ParseSSEResponse reads an SSE stream and extracts the JSON-RPC payload.
// Returns the data from the last "data:" line encountered.
// Handles both "data: value" (with space) and "data:value" (without space)
// per the SSE specification. Comment lines (starting with ":") are ignored.
func ParseSSEResponse(r io.Reader) ([]byte, error) {
	scanner := bufio.NewScanner(r)
	var lastData []byte

	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "data:") {
			// Strip "data:" prefix, then optional leading space
			value := strings.TrimPrefix(line, "data:")
			value = strings.TrimPrefix(value, " ")
			lastData = []byte(value)
		}
	}

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("reading SSE stream: %w", err)
	}

	if lastData == nil {
		return nil, fmt.Errorf("no data events in SSE response")
	}

	return lastData, nil
}
