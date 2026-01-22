import asyncio
import sys
from dataclasses import dataclass
import os
import socket
import tempfile
import time
import uuid
import wave
from typing import Dict, Optional

import discord
import numpy as np
from aiohttp import ClientSession, web
import secrets as app_secrets
from discord import app_commands
from piper.voice import PiperVoice


def require_secret(key: str) -> str:
    value = getattr(app_secrets, key, None)
    if not value:
        print(f"Missing {key} in secrets.py.", file=sys.stderr)
        sys.exit(1)
    return value


BOT_TOKEN = require_secret("DISCORD_BOT_TOKEN")
CHANNEL_NAME = require_secret("DISCORD_CHANNEL_NAME")
HELP_TOKEN = require_secret("HELP_BOT_TOKEN")
GUILD_ID = getattr(app_secrets, "DISCORD_GUILD_ID", None)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def resolve_path(path: Optional[str]) -> Optional[str]:
    if not path:
        return None
    if os.path.isabs(path):
        return path
    return os.path.normpath(os.path.join(BASE_DIR, path))


HTTP_HOST = getattr(app_secrets, "HELP_BOT_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(getattr(app_secrets, "HELP_BOT_HTTP_PORT", 8080))
DISCOVERY_PORT = int(getattr(app_secrets, "HELP_BOT_DISCOVERY_PORT", 45678))
GATEWAY_PORT = int(getattr(app_secrets, "HELP_GATEWAY_PORT", 8081))

AUDIO_CACHE_DIR = getattr(app_secrets, "AUDIO_CACHE_DIR", None)
if not AUDIO_CACHE_DIR:
    AUDIO_CACHE_DIR = os.path.join(tempfile.gettempdir(), "lighthouse_audio_cache")
os.makedirs(AUDIO_CACHE_DIR, exist_ok=True)
GATEWAY_URLS = list(getattr(app_secrets, "LIGHTHOUSE_GATEWAY_URLS", []))

TTS_MODEL_PATH = resolve_path(getattr(app_secrets, "TTS_MODEL_PATH", None))
TTS_CONFIG_PATH = resolve_path(getattr(app_secrets, "TTS_CONFIG_PATH", None))
TTS_USE_CUDA = bool(getattr(app_secrets, "TTS_USE_CUDA", False))


@dataclass
class HelpRequest:
    req_id: str
    lighthouse: int
    status: str
    message_id: int
    channel_id: int
    color: Optional[str] = None
    claimed_by: Optional[int] = None
    reason: Optional[str] = None


class HelpDetailsModal(discord.ui.Modal):
    def __init__(self, manager: "HelpQueueManager", req_id: str) -> None:
        super().__init__(title="Describe the help needed")
        self.manager = manager
        self.req_id = req_id
        self.details = discord.ui.TextInput(
            label="Description",
            style=discord.TextStyle.paragraph,
            max_length=400,
            required=True,
        )
        self.add_item(self.details)

    async def on_submit(self, interaction: discord.Interaction) -> None:
        await self.manager.handle_details(interaction, self.req_id, str(self.details.value))


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
    if msg_type == "PONG" and len(parts) >= 5:
        data["ping_id"] = parts[2]
        data["lighthouse"] = parts[3]
        data["timestamp"] = parts[4]
        return data
    return None


@dataclass
class LighthouseStatus:
    lighthouse_id: str
    ip: str
    mac: str
    firmware: str
    uptime_s: int
    last_seen: float
    online: bool = True


class LighthouseRegistry:
    def __init__(self) -> None:
        self.entries: Dict[str, LighthouseStatus] = {}
        self.transport: Optional[asyncio.DatagramTransport] = None

    def set_transport(self, transport: asyncio.DatagramTransport) -> None:
        self.transport = transport

    def update_from_packet(self, packet: str, addr) -> Optional[LighthouseStatus]:
        parts = packet.strip().split("|")
        if len(parts) < 6 or parts[0] != "LHREG":
            return None
        lighthouse_id = parts[1]
        ip = parts[2]
        mac = parts[3]
        firmware = parts[4]
        try:
            uptime_s = int(parts[5])
        except ValueError:
            uptime_s = 0
        now = time.time()
        status = self.entries.get(lighthouse_id)
        if status is None:
            status = LighthouseStatus(
                lighthouse_id=lighthouse_id,
                ip=addr[0],
                mac=mac,
                firmware=firmware,
                uptime_s=uptime_s,
                last_seen=now,
                online=True,
            )
            self.entries[lighthouse_id] = status
        else:
            status.ip = addr[0]
            status.mac = mac
            status.firmware = firmware
            status.uptime_s = uptime_s
            status.last_seen = now
            status.online = True
        return status

    def mark_offline(self, offline_after_s: int) -> None:
        now = time.time()
        for status in self.entries.values():
            status.online = (now - status.last_seen) <= offline_after_s

    def send_to_lighthouse(self, lighthouse_id: str, payload: str, port: int) -> bool:
        if not self.transport:
            return False
        status = self.entries.get(lighthouse_id)
        if not status:
            return False
        self.transport.sendto(payload.encode("utf-8"), (status.ip, port))
        return True


class HelpRequestView(discord.ui.View):
    def __init__(
        self,
        manager: "HelpQueueManager",
        req_id: str,
        status: str,
        claimed_by: Optional[int],
        show_details: bool = True,
        show_claim: bool = True,
        show_resolve: bool = True,
    ):
        super().__init__(timeout=None)
        self.manager = manager
        self.req_id = req_id

        details_disabled = status != "pending"
        claim_disabled = status != "open"
        resolve_disabled = status != "claimed"

        if show_details:
            details_button = discord.ui.Button(
                label="Add Details",
                style=discord.ButtonStyle.secondary,
                custom_id=f"help_details:{req_id}",
                disabled=details_disabled,
            )

            async def on_details(interaction: discord.Interaction) -> None:
                await interaction.response.send_modal(HelpDetailsModal(self.manager, self.req_id))

            details_button.callback = on_details
            self.add_item(details_button)

        if show_claim:
            claim_button = discord.ui.Button(
                label="Claim",
                style=discord.ButtonStyle.primary,
                custom_id=f"help_claim:{req_id}",
                disabled=claim_disabled,
            )

            async def on_claim(interaction: discord.Interaction) -> None:
                await self.manager.handle_claim(interaction, self.req_id)

            claim_button.callback = on_claim
            self.add_item(claim_button)

        if show_resolve:
            resolve_button = discord.ui.Button(
                label="Resolve",
                style=discord.ButtonStyle.success,
                custom_id=f"help_resolve:{req_id}",
                disabled=resolve_disabled,
            )

            async def on_resolve(interaction: discord.Interaction) -> None:
                await self.manager.handle_resolve(interaction, self.req_id)

            resolve_button.callback = on_resolve
            self.add_item(resolve_button)


class HelpQueueManager:
    def __init__(self, client: discord.Client) -> None:
        self.client = client
        self.requests: Dict[str, HelpRequest] = {}
        self.channel: Optional[discord.TextChannel] = None
        self.lighthouse_channels: Dict[int, discord.TextChannel] = {}
        self.gateway_ip: Optional[str] = None
        self.views: Dict[str, HelpRequestView] = {}
        self.ping_waiters: Dict[str, set[int]] = {}

    async def init_channel(self) -> None:
        for guild in self.client.guilds:
            channel = discord.utils.get(guild.text_channels, name=CHANNEL_NAME)
            if channel is None:
                overwrites = self._admin_only_overwrites(guild)
                channel = await guild.create_text_channel(CHANNEL_NAME, overwrites=overwrites)
            else:
                await channel.edit(overwrites=self._admin_only_overwrites(guild))
            self.channel = channel

            for lh_id in range(1, 31):
                name = f"lh-{lh_id:02d}"
                role_name = f"Team-{lh_id:02d}"
                team_role = discord.utils.get(guild.roles, name=role_name)
                if team_role is None:
                    team_role = await guild.create_role(name=role_name, mentionable=True)
                overwrites = self._team_channel_overwrites(guild, team_role)
                lh_channel = discord.utils.get(guild.text_channels, name=name)
                if lh_channel is None:
                    lh_channel = await guild.create_text_channel(name, overwrites=overwrites)
                else:
                    await lh_channel.edit(overwrites=overwrites)
                self.lighthouse_channels[lh_id] = lh_channel
            return
        raise RuntimeError(f"Channel named '{CHANNEL_NAME}' not found.")

    @staticmethod
    def _admin_only_overwrites(guild: discord.Guild) -> Dict[discord.abc.Snowflake, discord.PermissionOverwrite]:
        overwrites: Dict[discord.abc.Snowflake, discord.PermissionOverwrite] = {
            guild.default_role: discord.PermissionOverwrite(view_channel=False)
        }
        for role in guild.roles:
            if role.permissions.administrator:
                overwrites[role] = discord.PermissionOverwrite(view_channel=True)
        return overwrites

    @classmethod
    def _team_channel_overwrites(
        cls,
        guild: discord.Guild,
        team_role: discord.Role,
    ) -> Dict[discord.abc.Snowflake, discord.PermissionOverwrite]:
        overwrites = cls._admin_only_overwrites(guild)
        overwrites[team_role] = discord.PermissionOverwrite(view_channel=True)
        return overwrites

    async def get_lighthouse_channel(self, lighthouse: int) -> discord.TextChannel:
        channel = self.lighthouse_channels.get(lighthouse)
        if channel:
            return channel
        await self.init_channel()
        channel = self.lighthouse_channels.get(lighthouse)
        if not channel:
            raise RuntimeError(f"Channel for lighthouse {lighthouse} not found.")
        return channel

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

    def get_base_url(self) -> Optional[str]:
        if not self.gateway_ip:
            return None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect((self.gateway_ip, 9))
            local_ip = sock.getsockname()[0]
            sock.close()
            return f"http://{local_ip}:{HTTP_PORT}"
        except Exception:
            return None

    async def handle_mesh_message(self, text: str, sender: Optional[str]) -> HandleResult:
        parsed = parse_help_message(text)
        if not parsed:
            return HandleResult(handled=False, deduped=False)

        msg_type = parsed["type"]
        if msg_type == "PONG":
            ping_id = parsed.get("ping_id")
            lighthouse = int(parsed["lighthouse"])
            waiter = self.ping_waiters.get(ping_id)
            if waiter is None:
                return HandleResult(handled=True, deduped=True)
            already = lighthouse in waiter
            waiter.add(lighthouse)
            return HandleResult(handled=True, deduped=already)

        req_id = parsed["req_id"]
        lighthouse = int(parsed["lighthouse"])

        if msg_type == "REQ":
            if req_id in self.requests:
                return HandleResult(handled=True, deduped=True)
            channel = await self.get_lighthouse_channel(lighthouse)
            color = parsed.get("color")
            color_line = f"\nColor: {color}" if color else ""
            content = (
                f"Help requested by Lighthouse {lighthouse}.\n"
                f"Request ID: `{req_id}`"
                f"{color_line}\n"
                "Status: Waiting for details."
            )
            view = HelpRequestView(self, req_id, "pending", None, show_claim=False, show_resolve=False)
            message = await channel.send(content, view=view)
            self.views[req_id] = view
            self.requests[req_id] = HelpRequest(
                req_id=req_id,
                lighthouse=lighthouse,
                status="pending",
                message_id=message.id,
                channel_id=channel.id,
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
        channel = self.channel
        if not channel:
            await self.init_channel()
            channel = self.channel
        try:
            channel_obj = self.client.get_channel(req.channel_id)
            if not isinstance(channel_obj, discord.TextChannel):
                channel_obj = channel
            if channel_obj is None:
                return
            message = await channel_obj.fetch_message(req.message_id)
        except discord.NotFound:
            return
        color_line = f"\nColor: {req.color}" if req.color else ""
        reason_line = f"\nReason: {req.reason}" if req.reason else ""
        content = (
            f"Help requested by Lighthouse {req.lighthouse}.\n"
            f"Request ID: `{req.req_id}`"
            f"{color_line}"
            f"{reason_line}\n"
            f"Status: {status_line}"
        )
        view = HelpRequestView(self, req.req_id, req.status, req.claimed_by, show_details=False)
        self.views[req.req_id] = view
        await message.edit(content=content, view=view)

    async def handle_claim(self, interaction: discord.Interaction, req_id: str) -> None:
        req = self.requests.get(req_id)
        if not req:
            await interaction.response.send_message("Request not found.", ephemeral=True)
            return
        if req.status == "pending":
            await interaction.response.send_message("Please add details before claiming.", ephemeral=True)
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

    async def handle_details(self, interaction: discord.Interaction, req_id: str, reason: str) -> None:
        req = self.requests.get(req_id)
        if not req:
            await interaction.response.send_message("Request not found.", ephemeral=True)
            return
        if req.status != "pending":
            await interaction.response.send_message("Details already provided.", ephemeral=True)
            return
        clean_reason = reason.replace("|", "/").strip()
        if not clean_reason:
            await interaction.response.send_message("Reason cannot be empty.", ephemeral=True)
            return
        req.reason = clean_reason
        req.status = "open"
        lh_channel = self.client.get_channel(req.channel_id)
        if isinstance(lh_channel, discord.TextChannel):
            try:
                lh_message = await lh_channel.fetch_message(req.message_id)
                await lh_message.edit(content="Details submitted. Staff will respond in notifications.", view=None)
            except discord.NotFound:
                pass

        if not self.channel:
            await self.init_channel()
        notify_channel = self.channel
        if not notify_channel:
            await interaction.response.send_message("Notifications channel not available.", ephemeral=True)
            return

        color_line = f"\nColor: {req.color}" if req.color else ""
        reason_line = f"\nReason: {req.reason}" if req.reason else ""
        content = (
            f"Help requested by Lighthouse {req.lighthouse}.\n"
            f"Request ID: `{req.req_id}`"
            f"{color_line}"
            f"{reason_line}\n"
            "Status: Open (details provided)."
        )
        view = HelpRequestView(self, req.req_id, req.status, req.claimed_by, show_details=False)
        notify_message = await notify_channel.send(content, view=view)
        self.views[req.req_id] = view
        req.message_id = notify_message.id
        req.channel_id = notify_channel.id

        await interaction.response.send_message("Details added. Request is now in the queue.", ephemeral=True)
        event = f"HELP|DETAILS|{req.req_id}|{req.lighthouse}|{clean_reason}"
        await self.post_to_gateways(event)


class AudioRegistry:
    def __init__(self) -> None:
        self.entries: Dict[str, Dict[str, str]] = {}

    def add_file(self, path: str, filename: str) -> str:
        audio_id = uuid.uuid4().hex
        ext = os.path.splitext(filename)[1].lower().lstrip(".")
        self.entries[audio_id] = {
            "type": "file",
            "path": path,
            "filename": filename,
            "ext": ext,
        }
        return audio_id

    def add_url(self, url: str) -> str:
        audio_id = uuid.uuid4().hex
        clean = url.split("?", 1)[0].lower()
        ext = os.path.splitext(clean)[1].lstrip(".")
        self.entries[audio_id] = {"type": "url", "url": url, "ext": ext}
        return audio_id

    def get(self, audio_id: str) -> Optional[Dict[str, str]]:
        return self.entries.get(audio_id)


class TTSManager:
    def __init__(self, model_path: Optional[str], config_path: Optional[str], use_cuda: bool) -> None:
        self.model_path = model_path
        self.config_path = config_path
        self.use_cuda = use_cuda
        self._voice: Optional[PiperVoice] = None

    def is_configured(self) -> bool:
        return bool(self.model_path)

    def _load_voice(self) -> PiperVoice:
        if not self.model_path:
            raise RuntimeError("TTS model path is not configured.")
        if self._voice is None:
            self._voice = PiperVoice.load(
                self.model_path,
                config_path=self.config_path,
                use_cuda=self.use_cuda,
            )
        return self._voice

    def synthesize_to_file(self, text: str, output_path: str) -> None:
        voice = self._load_voice()
        with wave.open(output_path, "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(voice.config.sample_rate)
            for chunk in voice.synthesize(text):
                audio = (chunk.audio_float_array * 32767.0).astype(np.int16)
                wav_file.writeframes(audio.tobytes())


intents = discord.Intents.default()
client = discord.Client(intents=intents)
queue_manager = HelpQueueManager(client)
audio_registry = AudioRegistry()
tts_manager = TTSManager(TTS_MODEL_PATH, TTS_CONFIG_PATH, TTS_USE_CUDA)
command_tree = app_commands.CommandTree(client)
http_started = False
lighthouse_registry = LighthouseRegistry()


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

    async def handle_audio(request: web.Request) -> web.StreamResponse:
        audio_id = request.match_info.get("audio_id")
        if not audio_id:
            return web.Response(status=404, text="not found")
        if "." in audio_id:
            audio_id = audio_id.split(".", 1)[0]
        entry = audio_registry.get(audio_id)
        if not entry:
            return web.Response(status=404, text="not found")
        if entry["type"] == "file":
            return web.FileResponse(entry["path"])

        url = entry["url"]
        async with ClientSession() as session:
            async with session.get(url) as resp:
                if resp.status != 200:
                    return web.Response(status=502, text="upstream error")
                stream = web.StreamResponse(status=200)
                stream.content_type = resp.headers.get("Content-Type", "audio/mpeg")
                await stream.prepare(request)
                async for chunk in resp.content.iter_chunked(4096):
                    await stream.write(chunk)
                await stream.write_eof()
                return stream

    app.router.add_post("/mesh", handle_mesh)
    app.router.add_get("/audio/{audio_id}", handle_audio)
    app.router.add_get("/audio/{audio_id}.{ext}", handle_audio)
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

    registration_host = getattr(app_secrets, "REGISTRATION_HOST", "0.0.0.0")
    registration_port = int(getattr(app_secrets, "REGISTRATION_PORT", 9010))
    await loop.create_datagram_endpoint(
        lambda: RegistrationProtocol(lighthouse_registry),
        local_addr=(registration_host, registration_port),
    )
    print(f"UDP registration listening on {registration_host}:{registration_port}")


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


class RegistrationProtocol(asyncio.DatagramProtocol):
    def __init__(self, registry: LighthouseRegistry) -> None:
        self.registry = registry

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.registry.set_transport(transport)  # type: ignore[arg-type]

    def datagram_received(self, data: bytes, addr) -> None:
        try:
            packet = data.decode("utf-8", errors="ignore").strip()
        except Exception:
            return
        status = self.registry.update_from_packet(packet, addr)
        if status:
            response = f"LHACK|{status.lighthouse_id}|{int(time.time())}"
            if self.registry.transport:
                self.registry.transport.sendto(response.encode("utf-8"), addr)


@client.event
async def on_ready() -> None:
    global http_started
    await queue_manager.init_channel()
    if not http_started:
        http_started = True
        asyncio.create_task(start_http_server())
        if GUILD_ID:
            guild = discord.Object(id=int(GUILD_ID))
            command_tree.copy_global_to(guild=guild)
            await command_tree.sync(guild=guild)
            print(f"Synced commands to guild {GUILD_ID}")
        else:
            await command_tree.sync()
    print(f"Logged in as {client.user} (id={client.user.id})")


@command_tree.command(name="audio", description="Send an audio file or URL to the lighthouse mailbox.")
@app_commands.describe(target="Lighthouse number or 'all'", url="Audio file URL", file="Audio file attachment")
async def send_audio(
    interaction: discord.Interaction,
    target: str,
    url: Optional[str] = None,
    file: Optional[discord.Attachment] = None,
) -> None:
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

    audio_url = url
    audio_id = None
    if not audio_url:
        if file is None:
            await interaction.response.send_message("Provide an audio URL or attach a file.", ephemeral=True)
            return
        name = (file.filename or "").lower()
        if not (name.endswith(".mp3") or name.endswith(".wav")):
            await interaction.response.send_message("Only .mp3 or .wav attachments are supported.", ephemeral=True)
            return
        audio_path = os.path.join(AUDIO_CACHE_DIR, f"{uuid.uuid4().hex}_{file.filename}")
        await file.save(audio_path)
        audio_id = audio_registry.add_file(audio_path, file.filename)
    else:
        clean_url = audio_url.split("?", 1)[0].lower()
        if not (clean_url.endswith(".mp3") or clean_url.endswith(".wav")):
            await interaction.response.send_message("Only .mp3 or .wav URLs are supported.", ephemeral=True)
            return
        audio_id = audio_registry.add_url(audio_url)

    base_url = queue_manager.get_base_url()
    if not base_url:
        await interaction.response.send_message("Gateway not connected yet; try again after a help request.", ephemeral=True)
        return
    ext = audio_registry.get(audio_id).get("ext") if audio_id else None
    suffix = f".{ext}" if ext else ""
    audio_url = f"{base_url}/audio/{audio_id}{suffix}"

    payload = f"HELP|MAIL|{target_value}|{audio_url}"
    await queue_manager.post_to_gateways(payload)
    await interaction.response.send_message(f"Mailbox audio queued for {target_value}.", ephemeral=True)

@command_tree.command(name="announcement", description="Generate a TTS announcement and send it to lighthouses.")
@app_commands.describe(target="Lighthouse number or 'all'", message="Announcement text")
async def send_announcement(
    interaction: discord.Interaction,
    target: str,
    message: str,
) -> None:
    if not tts_manager.is_configured():
        await interaction.response.send_message("TTS model is not configured.", ephemeral=True)
        return
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

    text = (message or "").strip()
    if not text:
        await interaction.response.send_message("Announcement text cannot be empty.", ephemeral=True)
        return

    await interaction.response.send_message("Generating announcement audio...", ephemeral=True)

    audio_path = os.path.join(AUDIO_CACHE_DIR, f"{uuid.uuid4().hex}_announcement.wav")
    loop = asyncio.get_running_loop()
    try:
        await loop.run_in_executor(None, tts_manager.synthesize_to_file, text, audio_path)
    except Exception as exc:
        await interaction.followup.send(f"TTS generation failed: {exc}", ephemeral=True)
        return

    audio_id = audio_registry.add_file(audio_path, "announcement.wav")
    base_url = queue_manager.get_base_url()
    if not base_url:
        await interaction.followup.send("Gateway not connected yet; try again after a help request.", ephemeral=True)
        return
    audio_url = f"{base_url}/audio/{audio_id}.wav"
    payload = f"HELP|MAIL|{target_value}|{audio_url}"
    await queue_manager.post_to_gateways(payload)
    await interaction.followup.send(f"Announcement queued in mailbox for {target_value}.", ephemeral=True)


@command_tree.command(name="ping_lighthouses", description="Ping the mesh and report which lighthouses are online.")
@app_commands.describe(timeout_seconds="How long to wait for responses (seconds)")
async def ping_lighthouses(
    interaction: discord.Interaction,
    timeout_seconds: Optional[int] = 4,
) -> None:
    if not GATEWAY_URLS and not queue_manager.gateway_ip:
        await interaction.response.send_message(
            "Gateway not connected yet; try again after a lighthouse connects.",
            ephemeral=True,
        )
        return
    timeout = max(1, min(int(timeout_seconds or 4), 15))
    ping_id = uuid.uuid4().hex[:8]
    queue_manager.ping_waiters[ping_id] = set()
    await interaction.response.send_message(
        f"Pinging lighthouses (timeout {timeout}s)...",
        ephemeral=True,
    )
    await queue_manager.post_to_gateways(f"HELP|PING|{ping_id}")
    await asyncio.sleep(timeout)
    online = sorted(queue_manager.ping_waiters.pop(ping_id, set()))
    offline = [lh for lh in range(1, 31) if lh not in online]
    online_text = ", ".join(f"{lh:02d}" for lh in online) if online else "none"
    offline_text = ", ".join(f"{lh:02d}" for lh in offline) if offline else "none"
    await interaction.followup.send(
        f"Online: {online_text}\nOffline: {offline_text}",
        ephemeral=True,
    )



if __name__ == "__main__":
    client.run(BOT_TOKEN)
