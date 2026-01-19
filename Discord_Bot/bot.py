import asyncio
import sys
from dataclasses import dataclass
import socket
from typing import Dict, Optional

import discord
from aiohttp import ClientSession, web
import secrets as app_secrets
from discord import app_commands


def require_secret(key: str) -> str:
    value = getattr(app_secrets, key, None)
    if not value:
        print(f"Missing {key} in secrets.py.", file=sys.stderr)
        sys.exit(1)
    return value


BOT_TOKEN = require_secret("DISCORD_BOT_TOKEN")
CHANNEL_NAME = require_secret("DISCORD_CHANNEL_NAME")
HELP_TOKEN = require_secret("HELP_BOT_TOKEN")

HTTP_HOST = getattr(app_secrets, "HELP_BOT_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(getattr(app_secrets, "HELP_BOT_HTTP_PORT", 8080))
DISCOVERY_PORT = int(getattr(app_secrets, "HELP_BOT_DISCOVERY_PORT", 45678))
GATEWAY_PORT = int(getattr(app_secrets, "HELP_GATEWAY_PORT", 8081))
GATEWAY_URLS = list(getattr(app_secrets, "LIGHTHOUSE_GATEWAY_URLS", []))


@dataclass
class HelpRequest:
    req_id: str
    lighthouse: int
    status: str
    message_id: int
    color: Optional[str] = None
    claimed_by: Optional[int] = None


@dataclass
class HandleResult:
    handled: bool
    deduped: bool


def parse_help_message(text: str) -> Optional[Dict[str, str]]:
    if not text:
        return None
    text = text.strip()
    if not text.startswith("HELP|"):
        return None
    parts = text.split("|")
    if len(parts) < 3:
        return None
    msg_type = parts[1].upper()
    data = {"type": msg_type}
    if msg_type == "REQ" and len(parts) >= 5:
        data["req_id"] = parts[2]
        data["lighthouse"] = parts[3]
        data["timestamp"] = parts[4]
        if len(parts) >= 6:
            data["color"] = parts[5]
        return data
    if msg_type == "CANCEL" and len(parts) >= 4:
        data["req_id"] = parts[2]
        data["lighthouse"] = parts[3]
        return data
    if msg_type in {"CLAIM", "RESOLVE"} and len(parts) >= 5:
        data["req_id"] = parts[2]
        data["lighthouse"] = parts[3]
        data["user_id"] = parts[4]
        return data
    return None


class HelpRequestView(discord.ui.View):
    def __init__(self, manager: "HelpQueueManager", req_id: str, status: str, claimed_by: Optional[int]):
        super().__init__(timeout=None)
        self.manager = manager
        self.req_id = req_id

        claim_disabled = status != "open"
        resolve_disabled = status != "claimed"

        claim_button = discord.ui.Button(
            label="Claim",
            style=discord.ButtonStyle.primary,
            custom_id=f"help_claim:{req_id}",
            disabled=claim_disabled,
        )
        resolve_button = discord.ui.Button(
            label="Resolve",
            style=discord.ButtonStyle.success,
            custom_id=f"help_resolve:{req_id}",
            disabled=resolve_disabled,
        )

        async def on_claim(interaction: discord.Interaction) -> None:
            await self.manager.handle_claim(interaction, self.req_id)

        async def on_resolve(interaction: discord.Interaction) -> None:
            await self.manager.handle_resolve(interaction, self.req_id)

        claim_button.callback = on_claim
        resolve_button.callback = on_resolve

        self.add_item(claim_button)
        self.add_item(resolve_button)


class HelpQueueManager:
    def __init__(self, client: discord.Client) -> None:
        self.client = client
        self.requests: Dict[str, HelpRequest] = {}
        self.channel: Optional[discord.TextChannel] = None
        self.gateway_ip: Optional[str] = None
        self.views: Dict[str, HelpRequestView] = {}

    async def init_channel(self) -> None:
        for guild in self.client.guilds:
            for channel in guild.text_channels:
                if channel.name == CHANNEL_NAME:
                    self.channel = channel
                    return
        raise RuntimeError(f"Channel named '{CHANNEL_NAME}' not found.")

    async def post_to_gateways(self, text: str) -> None:
        urls = list(GATEWAY_URLS)
        if not urls and self.gateway_ip:
            urls.append(f"http://{self.gateway_ip}:{GATEWAY_PORT}/mesh")
        if not urls:
            print("No gateway URL available for outbound message.")
            return
        headers = {"Content-Type": "text/plain", "X-Help-Token": HELP_TOKEN}
        async with ClientSession() as session:
            for url in urls:
                try:
                    print(f"Posting to gateway {url}: {text} (token len={len(HELP_TOKEN)})")
                    resp = await session.post(url, data=text, headers=headers, timeout=5)
                    print(f"Gateway response {resp.status} for {url}")
                except Exception:
                    print(f"Gateway post failed for {url}")
                    continue

    def set_gateway_ip(self, ip: Optional[str]) -> None:
        if ip:
            self.gateway_ip = ip
            print(f"Gateway IP set to {ip}")

    async def handle_mesh_message(self, text: str, sender: Optional[str]) -> HandleResult:
        parsed = parse_help_message(text)
        if not parsed:
            return HandleResult(handled=False, deduped=False)

        msg_type = parsed["type"]
        req_id = parsed["req_id"]
        lighthouse = int(parsed["lighthouse"])

        if msg_type == "REQ":
            if req_id in self.requests:
                return HandleResult(handled=True, deduped=True)
            if not self.channel:
                await self.init_channel()
            color = parsed.get("color")
            color_line = f"\nColor: {color}" if color else ""
            content = (
                f"Help requested by Lighthouse {lighthouse}.\n"
                f"Request ID: `{req_id}`"
                f"{color_line}"
            )
            view = HelpRequestView(self, req_id, "open", None)
            message = await self.channel.send(content, view=view)
            self.views[req_id] = view
            self.requests[req_id] = HelpRequest(
                req_id=req_id,
                lighthouse=lighthouse,
                status="open",
                message_id=message.id,
                color=color,
            )
            return HandleResult(handled=True, deduped=False)

        if msg_type == "CANCEL":
            req = self.requests.get(req_id)
            if not req:
                return HandleResult(handled=True, deduped=True)
            if req.status in {"resolved", "canceled"}:
                return HandleResult(handled=True, deduped=True)
            req.status = "canceled"
            await self.update_message(req, "Canceled by lighthouse.")
            return HandleResult(handled=True, deduped=False)

        return HandleResult(handled=True, deduped=True)

    async def update_message(self, req: HelpRequest, status_line: str) -> None:
        if not self.channel:
            await self.init_channel()
        try:
            message = await self.channel.fetch_message(req.message_id)
        except discord.NotFound:
            return
        color_line = f"\nColor: {req.color}" if req.color else ""
        content = (
            f"Help requested by Lighthouse {req.lighthouse}.\n"
            f"Request ID: `{req.req_id}`"
            f"{color_line}\n"
            f"Status: {status_line}"
        )
        view = HelpRequestView(self, req.req_id, req.status, req.claimed_by)
        self.views[req.req_id] = view
        await message.edit(content=content, view=view)

    async def handle_claim(self, interaction: discord.Interaction, req_id: str) -> None:
        req = self.requests.get(req_id)
        if not req:
            await interaction.response.send_message("Request not found.", ephemeral=True)
            return
        if req.status != "open":
            await interaction.response.send_message("Request already claimed or closed.", ephemeral=True)
            return
        req.status = "claimed"
        req.claimed_by = interaction.user.id
        await self.update_message(req, f"Claimed by {interaction.user.mention}.")
        await interaction.response.send_message("Claimed. Please follow up when resolved.", ephemeral=True)
        event = f"HELP|CLAIM|{req.req_id}|{req.lighthouse}|{interaction.user.id}"
        await self.post_to_gateways(event)

    async def handle_resolve(self, interaction: discord.Interaction, req_id: str) -> None:
        req = self.requests.get(req_id)
        if not req:
            await interaction.response.send_message("Request not found.", ephemeral=True)
            return
        if req.status != "claimed":
            await interaction.response.send_message("Request is not claimed.", ephemeral=True)
            return
        if req.claimed_by != interaction.user.id:
            await interaction.response.send_message("Only the claimer can resolve this request.", ephemeral=True)
            return
        req.status = "resolved"
        await self.update_message(req, f"Resolved by {interaction.user.mention}.")
        await interaction.response.send_message("Resolved. Thanks!", ephemeral=True)
        event = f"HELP|RESOLVE|{req.req_id}|{req.lighthouse}|{interaction.user.id}"
        await self.post_to_gateways(event)


intents = discord.Intents.default()
client = discord.Client(intents=intents)
queue_manager = HelpQueueManager(client)
command_tree = app_commands.CommandTree(client)
http_started = False


async def start_http_server() -> None:
    app = web.Application()

    async def handle_mesh(request: web.Request) -> web.Response:
        token = request.headers.get("X-Help-Token")
        if token != HELP_TOKEN:
            return web.Response(status=401, text="unauthorized")
        text = await request.text()
        sender = request.headers.get("X-Help-Sender")
        queue_manager.set_gateway_ip(request.remote)
        result = await queue_manager.handle_mesh_message(text, sender)
        return web.json_response({"handled": result.handled, "deduped": result.deduped})

    app.router.add_post("/mesh", handle_mesh)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, HTTP_HOST, HTTP_PORT)
    await site.start()
    print(f"HTTP server listening on {HTTP_HOST}:{HTTP_PORT}")

    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(
        lambda: DiscoveryProtocol(HTTP_PORT, HELP_TOKEN),
        local_addr=("0.0.0.0", DISCOVERY_PORT),
    )
    print(f"UDP discovery listening on 0.0.0.0:{DISCOVERY_PORT}")


class DiscoveryProtocol(asyncio.DatagramProtocol):
    def __init__(self, http_port: int, token: str) -> None:
        self.http_port = http_port
        self.token = token
        self.transport: Optional[asyncio.transports.DatagramTransport] = None

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.transport = transport  # type: ignore[assignment]

    def datagram_received(self, data: bytes, addr) -> None:
        if not self.transport:
            return
        try:
            message = data.decode("utf-8", errors="ignore").strip()
        except Exception:
            return
        if not message.startswith("HELPBOT_DISCOVERY|"):
            return
        _, token = message.split("|", 1)
        if token != self.token:
            return
        sockname = self.transport.get_extra_info("sockname")
        local_ip = None
        if sockname and isinstance(sockname, tuple):
            local_ip = sockname[0]
        if not local_ip or local_ip == "0.0.0.0":
            local_ip = self._resolve_local_ip(addr[0])
        url = f"http://{local_ip}:{self.http_port}/mesh"
        response = f"HELPBOT_URL|{url}|{self.token}"
        self.transport.sendto(response.encode("utf-8"), addr)

    @staticmethod
    def _resolve_local_ip(peer_ip: str) -> str:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect((peer_ip, 9))
            local_ip = sock.getsockname()[0]
            sock.close()
            return local_ip
        except Exception:
            return "127.0.0.1"


@client.event
async def on_ready() -> None:
    global http_started
    await queue_manager.init_channel()
    if not http_started:
        http_started = True
        asyncio.create_task(start_http_server())
        await command_tree.sync()
    print(f"Logged in as {client.user} (id={client.user.id})")


@command_tree.command(name="audio", description="Send an audio URL to one or all lighthouses.")
@app_commands.describe(target="Lighthouse number or 'all'", url="Audio file URL")
async def send_audio(interaction: discord.Interaction, target: str, url: str) -> None:
    target_value = target.strip().lower()
    if target_value != "all" and not target_value.isdigit():
        await interaction.response.send_message("Target must be a lighthouse number or 'all'.", ephemeral=True)
        return
    if target_value.isdigit():
        number = int(target_value)
        if number < 1 or number > 30:
            await interaction.response.send_message("Lighthouse number must be 1-30.", ephemeral=True)
            return
        target_value = str(number)
    else:
        target_value = "ALL"

    payload = f"HELP|AUDIO|{target_value}|{url}"
    await queue_manager.post_to_gateways(payload)
    await interaction.response.send_message(f"Audio request sent to {target_value}.", ephemeral=True)




if __name__ == "__main__":
    client.run(BOT_TOKEN)
