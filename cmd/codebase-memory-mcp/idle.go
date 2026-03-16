// Version: 0.4.10
package main

import (
	"log"
	"net/http"
	"sync"
	"time"
)

// IdleTracker cancels a context after a period of inactivity.
// Call Touch() on each request to reset the countdown.
type IdleTracker struct {
	timeout time.Duration
	timer   *time.Timer
	cancel  func()
	mu      sync.Mutex
}

// NewIdleTracker creates a tracker that calls cancel after timeout of no activity.
func NewIdleTracker(timeout time.Duration, cancel func()) *IdleTracker {
	it := &IdleTracker{
		timeout: timeout,
		cancel:  cancel,
	}
	it.timer = time.AfterFunc(timeout, func() {
		log.Printf("codebase-memory-mcp: idle for %v, shutting down", timeout)
		cancel()
	})
	return it
}

// Touch resets the idle timer. Call this on every request.
func (it *IdleTracker) Touch() {
	it.mu.Lock()
	defer it.mu.Unlock()
	it.timer.Reset(it.timeout)
}

// WrapHTTP returns middleware that resets the idle timer on each HTTP request.
func (it *IdleTracker) WrapHTTP(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		it.Touch()
		next.ServeHTTP(w, r)
	})
}
