# 🤖 3DSMate – AI Chat for Nintendo 3DS

A Groq-powered AI chat app for the Nintendo 3DS, built in C with devkitARM.  
GitHub Actions automatically builds both a **`.3dsx`** (Homebrew Launcher) and a **`.cia`** (HOME Menu install).

---

## ✨ Features

- 🤖 **3DSMate** – your personal AI assistant on the 3DS
- 💾 **API key is saved** to SD card (`/3ds/3dsmate/config.ini`) — enter it once, never again
- 📁 **Multiple chats** – create, save, browse, and delete conversations
- 🔄 **Full conversation context** sent with every message
- 📦 Automatic builds via GitHub Actions → `.3dsx` + `.cia` artifacts

---

## 📦 Download & Install

### Option A – Homebrew Launcher (`.3dsx`, no CFW required)
1. Download `3DSMate.3dsx` from [Releases](../../releases/latest)
2. Copy to `/3ds/3DSMate/3DSMate.3dsx` on your SD card
3. Open via Homebrew Launcher

### Option B – HOME Menu (`.cia`, CFW + FBI required)
1. Download `3DSMate.cia` from [Releases](../../releases/latest)
2. Copy to your SD card
3. Open **FBI** → SD → install `3DSMate.cia`
4. 3DSMate appears on your HOME menu

---

## 🎮 Controls

| Screen | Button | Action |
|--------|--------|--------|
| Main Menu | **A** | Start new chat (enter a title) |
| Main Menu | **B** | Browse saved chats |
| Main Menu | **X** | Enter / change API key |
| Chat | **A** | Type and send a message |
| Chat | **X** | Clear chat history |
| Chat | **B** | Save and go back |
| Chat List | **↑↓** | Navigate |
| Chat List | **A** | Open selected chat |
| Chat List | **Y** | Delete selected chat |
| Anywhere | **START** | Quit |

---

## 🔑 API Key

1. Get a free key at https://console.groq.com
2. In the app press **X** → type your key → press OK
3. Key is saved to `sdmc:/3ds/3dsmate/config.ini` automatically

---

## 🚀 Build via GitHub Actions

Push to `main` → Actions runs automatically → find artifacts under:  
**Actions → Build 3DSMate → Artifacts**

To create a full GitHub Release:
```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## 🛠️ Local Build

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
make
```

Install `3ds-jansson` first: `sudo dkp-pacman -S 3ds-jansson`

---

## 🎨 Custom Icon & Banner

Replace these files before building to brand your own version:

| File | Size | Purpose |
|------|------|---------|
| `meta/icon.png` | 48×48 px | App icon (HOME menu + Homebrew Launcher) |
| `meta/banner.png` | 256×128 px | Banner shown in CIA installer |
| `meta/audio.wav` | any | Short jingle in CIA installer (can be silent) |

If these files are missing, the workflow generates placeholders automatically.

---

## 📁 Project Structure

```
3DSMate/
├── source/
│   └── main.c                    ← App code (C, devkitARM)
├── meta/
│   ├── icon.png                  ← 48x48 app icon (optional, auto-generated)
│   ├── banner.png                ← 256x128 CIA banner (optional, auto-generated)
│   └── audio.wav                 ← CIA jingle (optional, auto-generated)
├── .github/
│   └── workflows/
│       └── build.yml             ← Builds .3dsx + .cia
├── Makefile
└── README.md
```

---

## 📜 License

MIT – free to use, modify, and distribute.
