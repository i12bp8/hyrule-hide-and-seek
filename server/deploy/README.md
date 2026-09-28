# Hosting the relay yourself (e.g. on a Raspberry Pi)

This runs the relay on your own machine and gives it a public `wss://` address through
**Tailscale Funnel**:

- **No open ports** on your router. Nothing on your network becomes reachable except the relay.
- **Your home IP stays hidden.** Players connect to Tailscale's servers, which pass the traffic on.
- Free HTTPS, a stable address (`https://hyrule-hide-and-seek.<your-tailnet>.ts.net`), no domain.

Both parts run in Docker and are locked down: the relay runs as an unused user id on a read-only
filesystem with no Linux capabilities, listens only on 127.0.0.1, stores nothing and doesn't log
addresses. It caps connections (400 total, 12 per address), rooms (300, 10 new per address per
minute), message size and message rate.

## Set up

On the machine (needs Docker with Compose):

```sh
git clone <this repo> && cd hyrule-hide-and-seek/server   # or copy the server/ folder over
docker compose -f deploy/compose.yml --profile funnel up -d --build
docker logs hide-and-seek-tunnel      # prints "To authenticate, visit: https://login.tailscale.com/..."
```

1. Open that link and log in (a free Tailscale account).
2. In the [Tailscale admin console](https://login.tailscale.com/admin/dns), enable **MagicDNS**
   and **HTTPS Certificates**. Funnel must be allowed in your access controls; new accounts allow it
   by default (`"nodeAttrs": [{"target": ["autogroup:member"], "attr": ["funnel"]}]`).
3. `docker restart hide-and-seek-tunnel`, then find your address:
   `docker exec hide-and-seek-tunnel tailscale funnel status`
4. Check it from any browser: `https://hyrule-hide-and-seek.<tailnet>.ts.net/` answers
   "Hyrule Hide & Seek relay".
5. Players use `wss://hyrule-hide-and-seek.<tailnet>.ts.net`. Put it in `HS_DEFAULT_SERVER` in the
   top-level `CMakeLists.txt` so every copy of the mod uses it.

Without Funnel (`docker compose -f deploy/compose.yml up -d --build`) the relay is only reachable
from the machine itself, e.g. for testing with `ws://127.0.0.1:8787`.

## Keep it running

- Both containers restart by themselves after a reboot or crash.
- Update: copy the new `server/` over, then `docker compose -f deploy/compose.yml --profile funnel up -d --build`.
- See who's playing: `docker logs -f hide-and-seek-relay` (room codes and player counts only).
- Stop: `docker compose -f deploy/compose.yml --profile funnel down`. Remove the device from the
  Tailscale admin console to shut the public address for good.

## Good to know

- Traffic goes through your home connection. A full room of 16 players needs about 0.1 Mbit/s
  down and 1.5 Mbit/s up; a room of 6 about a tenth of that. Tailscale Funnel also has bandwidth
  limits it doesn't publish; a handful of rooms at once is fine.
- A home server is only up while it's on. For something that never sleeps, the Cloudflare option
  in [../README.md](../README.md) runs the same relay on Cloudflare's network for free.
- On a Raspberry Pi the container memory limit is ignored unless memory cgroups are enabled
  (`cgroup_enable=memory` in `/boot/firmware/cmdline.txt`); the relay's own limits still apply.
