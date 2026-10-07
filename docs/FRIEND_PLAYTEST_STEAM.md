# KH2 Co-op — private friend preview over Steam (DRAFT, not released)

> Draft for a future Steam-beta package (release10). It is not shipped yet, and it is not accurate for
> release09, which uses Tailscale. It becomes valid only after the single-account listener probe and the
> two-account Steam run both pass (`docs/STEAM_TRANSPORT.md`).

## What you need

- Your own Windows copy of KINGDOM HEARTS HD 1.5+2.5 ReMIX on Steam (Global), the exact build the launcher
  accepts. Epic and other builds are refused.
- Steam running and signed in as you. That's all for networking: no Tailscale, no VPN, no router or firewall
  changes, no port forwarding. Traffic goes through Valve's relays. The other player never sees your IP
  address, and you never see theirs.
- Your host's SteamID64, or your friends' SteamID64s if you are the host. This is a 17-digit number. It
  is not your profile name, friend code or profile URL.
- Up to three players: the host and one or two friends.

## Start and join

1. Unzip to a new folder beside your game folder, **not inside it**, and keep all files together.
   Double-click **Start KH2 Co-op.cmd**. Nothing is installed and administrator access isn't needed.
2. Browse to the folder that contains **KINGDOM HEARTS II FINAL MIX.exe**, choose **Steam (beta)**, then
   click **Start game**. When save protection is confirmed, the launcher shows **Your SteamID**. Copy it and
   send it to the other player over chat.
3. Host:
   - choose **host**;
   - paste one or two friend SteamIDs, separated by a comma;
   - only those accounts can join this session. To add someone later, Disconnect, add them, then Connect
     again.
4. Friend:
   - choose **join** and your slot (**friend1**, or **friend2** if your host assigns it);
   - paste the host's SteamID.
5. Load your existing save normally. **Do not save during the preview.** Meet in the agreed room, wait for the
   host's ready signal, check the ready box, then click **Connect**. Look for the verified roster and RTT.
6. When you're done, click **Disconnect**, then **Exit & close game**.

## If it doesn't connect

(Exact launcher wording to be confirmed against the release10 build.)


- No SteamID shown, or the launcher says the Steam broker is not ready: make sure Steam is running and online, wait up to a minute
  after the title screen, then try again. Starting the game outside the launcher doesn't enable Steam co-op.
- The host never sees you join: the host must paste your exact SteamID64. Check that no digit is missing.
- A refusal that mentions relay-only or ICE: your connection was refused on purpose because it would
  not go through Valve's relays. Don't change Steam settings to work around it; report it with the package
  logs.
- It dropped mid-session: click **Connect** again. Each Connect starts a fresh link through Steam; your game
  stays open. If the host leaves, the session ends for everyone.

## Saves, warnings and removal

Unchanged from the Tailscale preview:
- SaveGuard redirects save writes into the package sandbox. Never copy sandbox files over your saves.
- Antivirus may warn about the unsigned DLL. Stop and contact your host; don't add exclusions yourself.
- To uninstall, delete the folder and the ZIP after closing the game and the launcher.

The Steam option uses your game's own Steam session. It adds no Steam app, no second Steam login, and no
account or firewall change.

## Preview limits

The gameplay limits of the Tailscale preview still apply: enemies, combat, cutscenes, reconnecting and the
courtyard workaround all behave the same. Steam adds:
- its own relay latency, typically tens of milliseconds;
- the remote player's character may freeze for up to 3 seconds during a short network hiccup before
  continuing. A longer outage hides it until the connection recovers.

Separate-PC play over Steam is untested until the two-account run passes.
