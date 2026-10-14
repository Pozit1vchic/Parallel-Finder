"""Actual official SDK handshake and app-tool calls; no user-cache operations."""
import asyncio
import os
import sys
import httpx
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def main():
    async with httpx.AsyncClient(headers={"Authorization": "Bearer " + os.environ["PF_MCP_TEST_TOKEN"]}, timeout=10) as http:
        async with streamable_http_client(sys.argv[1], http_client=http) as (read, write, _):
            async with ClientSession(read, write) as session:
                initialized = await session.initialize()
                assert initialized.serverInfo.name == "parallel-finder"
                tools = await session.list_tools()
                assert {tool.name for tool in tools.tools} == {"pf_get_status", "pf_list_results", "pf_list_classes"}
                for name, args in [("pf_get_status", {}), ("pf_list_results", {"limit": 1}), ("pf_list_classes", {})]:
                    result = await session.call_tool(name, args)
                    assert not result.isError
                    assert result.structuredContent is not None
                    if name == "pf_get_status":
                        assert isinstance(result.structuredContent["reviewBusy"], bool)
                        assert result.structuredContent["controlAllowed"] is False
                        assert result.structuredContent["exportSuccess"] is None
                invalid = await session.call_tool("pf_list_results", {"limit": 201})
                assert invalid.isError
    print("MCP SDK integration passed")


asyncio.run(main())
