// mcp_server.h : MCP (Model Context Protocol) stdio server — the agent bridge
// Speaks newline-delimited JSON-RPC 2.0 on stdin/stdout. Each tool is a thin
// wrapper over the same functions the CLI uses. Run with --mcp.
#pragma once

// Run the server loop until stdin closes. Returns process exit code.
int Mcp_Run();
