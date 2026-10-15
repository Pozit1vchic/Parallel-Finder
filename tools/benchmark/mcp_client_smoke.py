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
                names = {tool.name for tool in tools.tools}
                assert len(names) == 13
                assert {"pf_get_status", "pf_list_results", "pf_list_classes", "pf_get_preview", "pf_get_capabilities"}.issubset(names)
                assert "pf_update_settings" not in names
                for name, args in [("pf_get_status", {}), ("pf_list_results", {"limit": 1}), ("pf_list_classes", {}),
                                   ("pf_get_capabilities", {}), ("pf_get_settings", {}), ("pf_get_sources", {}),
                                   ("pf_get_model_status", {}), ("pf_get_export_status", {}), ("pf_get_draft_preview", {})]:
                    result = await session.call_tool(name, args)
                    assert not result.isError
                    assert result.structuredContent is not None
                    if name == "pf_get_status":
                        assert isinstance(result.structuredContent["reviewBusy"], bool)
                        assert result.structuredContent["controlAllowed"] is False
                        assert result.structuredContent["exportSuccess"] is None
                invalid = await session.call_tool("pf_list_results", {"limit": 201})
                assert invalid.isError
                resources = await session.list_resources()
                assert len(resources.resources) == 4
                resource = await session.read_resource("pf://status")
                assert resource.contents
                prompts = await session.list_prompts()
                assert any(prompt.name == "review-parallels" for prompt in prompts.prompts)
                prompt = await session.get_prompt("review-parallels")
                assert prompt.messages
    print("MCP SDK integration passed")


asyncio.run(main())
