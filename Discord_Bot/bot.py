import os
import sys

import discord


def require_env(key: str) -> str:
    value = os.getenv(key)
    if not value:
        print(f"Missing {key} in environment.", file=sys.stderr)
        sys.exit(1)
    return value


BOT_TOKEN = require_env("DISCORD_BOT_TOKEN")
CHANNEL_ID = int(require_env("DISCORD_CHANNEL_ID"))


intents = discord.Intents.default()
intents.message_content = True

client = discord.Client(intents=intents)


@client.event
async def on_ready() -> None:
    print(f"Logged in as {client.user} (id={client.user.id})")
    channel = client.get_channel(CHANNEL_ID)
    if channel:
        await channel.send("Lighthouse bot online test message.")
    else:
        print("Channel not found. Check DISCORD_CHANNEL_ID.")


@client.event
async def on_message(message: discord.Message) -> None:
    if message.author == client.user:
        return
    print(f"[{message.channel}] {message.author}: {message.content}")


if __name__ == "__main__":
    client.run(BOT_TOKEN)
