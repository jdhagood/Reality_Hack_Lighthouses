# Lighthouse Discord Bot

This bot is only needed to manage a Discord bot user and to verify messages.
The lighthouses post to Discord using the bot token and channel ID.

## 1) Create the bot in Discord

1. Go to https://discord.com/developers/applications and create a new application.
2. In "Bot", click "Add Bot".
3. Copy the bot token.
4. In "OAuth2" -> "URL Generator":
   - Scopes: `bot`
   - Permissions: `Send Messages`, `Read Message History`
5. Open the generated URL and invite the bot to your server.

## 2) Configure environment variables

Create a `.env` file in this folder:

```
DISCORD_BOT_TOKEN=Bot YOUR_TOKEN_HERE
DISCORD_CHANNEL_ID=YOUR_CHANNEL_ID
```

Note: `DISCORD_BOT_TOKEN` must include the `Bot ` prefix.

## 3) Run the bot (Python)

```
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
python bot.py
```

The bot logs to stdout and posts a startup message to the channel.
