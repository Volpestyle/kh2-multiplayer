# KH2 Co-op — Steam private preview

This package flow has offline review approval and passed its PC2 launch/title/exit
UX check (UX07, 2026-10-08). The underlying two-account Steam Step 2 also passed.
Host/join via the package UI remains untested live.

You need Steam running, signed in to your account owning KINGDOM HEARTS HD
1.5+2.5 ReMIX, Windows, and the exact supported Steam Global KH2 build. No
Tailscale, VPN, port forwarding or firewall changes are needed. Connections must
use Valve relays; there is no direct-IP fallback. Up to three players can play.

1. Unzip into a new folder beside the game folder, not inside it. Keep the files
   together. Open **Start KH2 Co-op.cmd** from your normal Windows desktop.
2. Browse to the folder containing **KINGDOM HEARTS II FINAL MIX.exe**. Leave
   **Steam (beta)** selected. If `steam_appid.txt` is missing, check the permission
   box to create it beside KH2 with app ID 2552430. Existing correct files are
   preserved; a different app ID is refused. No administrator access is needed.
3. Click **Start game**. Wait for save protection and your actual **SteamID**.
   Check that it is your account, then tick the account confirmation. You can
   enter an expected SteamID to catch the wrong account. This must be a 17-digit
   SteamID64, not a profile name, URL or friend code.
4. Load your existing save manually. **Do not save during this preview.** If KH2
   shows a corrupt-save or deletion prompt, stop and contact the host. Do not
   dismiss it or copy someone else's save into your account.
5. Host: choose **host**, paste one or two friends' actual SteamIDs separated by
   commas, and confirm that only those accounts may join. When your room is ready,
   check the ready box and click **Start hosting**. Wait for **Hosting — roster
   verified**, then **Copy invitation** and share that text with your friends.
6. Friend: choose **join**, select the slot the host assigned, and paste the host
   SteamID or invitation (`kh2coop:steam:` followed by the host ID). Confirm your
   account and readiness, then click **Join**. The host must separately allowlist
   your actual SteamID. The invitation alone grants no admission.
7. Wait for **Connected — roster verified**. The HUD can show connection latency.
   To finish, click **Disconnect**, then **Exit & close game**. Disconnect leaves
   the game open. Closing stops only the game and helpers started here.

The allowlist is fixed for a hosting session. To change it, Disconnect, edit and
confirm it, then Start hosting again. Disconnecting as host ends the friends'
session. Steam overlay invites, a friends picker and Steam's **Join game** button
are not supported; use the copied invitation text.

If Steam is unavailable, start it and sign in to the account owning KH2, using
the same normal desktop session as this launcher. Close this game and start
again. Broker startup can take up to 60 seconds. If authentication or Valve relay
is unavailable, check Steam online status and retry a fresh launch. These failures
cannot always be distinguished; preserve the logs when reporting a problem.

If the account is wrong, close this game, switch accounts in Steam, then start
again. If joining fails, verify the host ID and the host's explicit allowlist.
A generic timeout does not prove which side failed. Privacy/ICE failures are
refused; do not change network settings to bypass them.

Short transport interruptions use bounded reconnection. **Host left** is terminal:
there is no automatic rejoin; ask the host before joining a new session. Session
errors remain visible in the launcher. Logs stay in this package. Sessions have a
30-minute limit.

SaveGuard redirects save writes into a sandbox; never copy those files over your
saves. The launcher removes only an unchanged app-ID file it created, after
confirmed owned game closure. Changed or pre-existing app-ID files are preserved.
After an uncertain launch, leave the file in place until the game is closed and
check the launch receipt. Never delete your saves as a troubleshooting step.

The existing preview gameplay limits still apply. Stop on antivirus warnings and
contact the host rather than adding exclusions. To uninstall, close the game and
launcher, then delete the extracted package and ZIP.
