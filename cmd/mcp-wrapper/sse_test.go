// Version: 0.2.0
// cmd/mcp-wrapper/sse_test.go
package main

import (
	"strings"
	"testing"
)

func TestParseSSEResponse_SingleEvent(t *testing.T) {
	input := "event: message\ndata: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n\n"
	got, err := ParseSSEResponse(strings.NewReader(input))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	want := `{"jsonrpc":"2.0","id":1,"result":{}}`
	if string(got) != want {
		t.Errorf("got %q, want %q", string(got), want)
	}
}

func TestParseSSEResponse_NoDataEvents(t *testing.T) {
	input := "event: message\n\n"
	_, err := ParseSSEResponse(strings.NewReader(input))
	if err == nil {
		t.Error("expected error for SSE stream with no data events")
	}
}

func TestParseSSEResponse_EmptyStream(t *testing.T) {
	input := ""
	_, err := ParseSSEResponse(strings.NewReader(input))
	if err == nil {
		t.Error("expected error for empty SSE stream")
	}
}

func TestParseSSEResponse_MultipleDataEvents(t *testing.T) {
	input := "event: message\ndata: {\"first\":true}\n\nevent: message\ndata: {\"last\":true}\n\n"
	got, err := ParseSSEResponse(strings.NewReader(input))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	want := `{"last":true}`
	if string(got) != want {
		t.Errorf("got %q, want %q", string(got), want)
	}
}

func TestParseSSEResponse_DataOnly(t *testing.T) {
	input := "data: {\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"ok\":true}}\n\n"
	got, err := ParseSSEResponse(strings.NewReader(input))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	want := `{"jsonrpc":"2.0","id":2,"result":{"ok":true}}`
	if string(got) != want {
		t.Errorf("got %q, want %q", string(got), want)
	}
}

func TestParseSSEResponse_CommentLinesIgnored(t *testing.T) {
	input := ": keep-alive\ndata: {\"id\":1}\n\n"
	got, err := ParseSSEResponse(strings.NewReader(input))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if string(got) != `{"id":1}` {
		t.Errorf("got %q", string(got))
	}
}

func TestParseSSEResponse_DataNoSpace(t *testing.T) {
	// SSE spec allows "data:value" (no space after colon)
	input := "data:{\"id\":1}\n\n"
	got, err := ParseSSEResponse(strings.NewReader(input))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if string(got) != `{"id":1}` {
		t.Errorf("got %q", string(got))
	}
}
