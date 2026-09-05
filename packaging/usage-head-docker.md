# parrot-to-klv (container image)

Parrot drone telemetry to MISB ST 0601 KLV, in an MPEG-TS.

Runs from a container image, so the only thing you need installed is Docker or
Podman. The image is loaded from the tarball beside this file on first use and
reused after that; every run is ephemeral (`--rm`).

This deliverable contains the `@ARCH@` image. Its architecture-qualified tag is
`parrot-to-klv:@VERSION@-@ARCH@`, so it cannot collide with the other
architecture's image when both have been loaded on one machine.

## Running it

```sh
./parrot-to-klv flight.mp4 -o flight.ts     # convert
./parrot-to-klv flight.mp4 --klv            # inspect, convert nothing
./parrot-to-klv                             # prompts, if run in a terminal
```

`./parrot-to-klv` is a small script around `docker run`. It takes the arguments
the tool takes; anything you leave out it asks for — but only when there is a
terminal to ask on, so it is safe in scripts and cron.

## Files, directories and permissions

The container can only see the directories your files are actually in: the input
directory is mounted **read-only**, the output directory read-write, and nothing
else is exposed. It runs as your user, so output files belong to you rather than
to root.

Give paths as you normally would — relative or absolute, they are resolved
before the container starts.

## Running `docker run` yourself

The script is a convenience, not a requirement. The equivalent by hand:

```sh
docker run --rm --network none \
  --user "$(id -u):$(id -g)" \
  -v /path/to/flights:/work \
  parrot-to-klv:@VERSION@-@ARCH@ /work/flight.mp4 -o /work/flight.ts
```

Mount read-only and split the directories if input and output live apart:

```sh
docker run --rm --network none \
  --user "$(id -u):$(id -g)" \
  -v /path/to/input:/in:ro -v /path/to/output:/out \
  parrot-to-klv:@VERSION@-@ARCH@ /in/flight.mp4 -o /out/flight.ts
```

`--network none` is not required — the tool never uses the network, so it costs
nothing and is worth keeping.

## Long-running live sessions (compose)

The directory beside this file also holds `compose.yml` and `.env.example`:
an `ingest` service that captures an RTSP source around the clock, one
finalized capture per session.

```sh
docker load -i parrot-to-klv-@VERSION@-@ARCH@-image.tar.gz  # once; or run ./parrot-to-klv once
cp .env.example .env   # then edit RTSP_URI (and SINK if not a file) and set HOST_UID/HOST_GID to your IDs
mkdir -p ./captures    # or whatever OUTPUT_DIR names — Compose must not create it (root-owned)
HOST_UID=$(id -u) HOST_GID=$(id -g) docker compose up -d  # so captures belong to you, not root
```

`RTSP_URI` has no default — set it in `.env`. Everything else does: `SINK`
defaults to `/out/capture.ts`, which lands in `./captures` on the host
(`OUTPUT_DIR`); with `--reconnect` always on, sessions roll to numbered
captures next to it. `IDLE_TIMEOUT` (default 10, `0` disables) ends a session
after that long with no video delivered, and `MAX_EMPTY_SESSIONS` (default 5)
ends the process with exit 1 after that many sessions in a row that opened yet
delivered nothing — and with `restart: unless-stopped` the service is then
relaunched, so the bound covers one process lifetime and the counter resets.
Sessions that deliver video count as non-empty even with no
telemetry, so video-only captures are kept.

Recovery from a dropped source is the tool's own `--reconnect` loop; Compose
owns availability across runs — the service runs at boot and after any exit,
and only `docker compose stop` or `down` keeps it stopped. Each bound reached
is one run ending, not the service ending.

Create `OUTPUT_DIR` before the first `up`: if the directory does not exist,
the daemon creates it root-owned, and the service — running as you, not as
root — cannot write to it. Like the launcher, the service runs as your user
(`user: HOST_UID:HOST_GID`, the compose spelling of the launcher's
`--user "$(id -u):$(id -g)"`), so captures belong to you. `HOST_UID` and
`HOST_GID` are required with no fallback: set them in `.env` (replace the
example values with the output of `id -u`/`id -g`) or pin them per invocation
as above. An unset ID fails `docker compose config` loudly instead of
silently writing captures as the wrong user. The `HOST_` names avoid bash's
readonly `UID`, which `export` cannot set.

The service requires host networking so `--iface` can name a real host NIC;
bridge networking exposes only the container's `eth0` and cannot select egress
on a multi-homed host. Uncomment the `--iface` lines in `compose.yml` and set
`UDP_IFACE` to the host interface in `.env` when multicast output needs an
explicit route. Host networking is Linux-only, matching the supported targets.

Stopping drains, it does not cut mid-capture: `docker compose stop` sends
SIGTERM, which the tool handles the same as Ctrl-C (SIGINT) — one handler for
both signals, so the loops notice within milliseconds and each session
finalizes its capture before the process exits. `stop_grace_period` gives that
330 seconds: closing a session can remux the rest of a video file after the
final KLV packet, which libmisbklv bounds at 300 seconds
(`gst_insert.cpp` `kFinishDrainTimeout`), with 30 seconds of headroom. Logs go to the
service's json-file log (`docker compose logs ingest`, kept to five 10 MB
files); captures stay in `OUTPUT_DIR`.

## Troubleshooting

The image has no shell, which keeps it small and its attack surface minimal. If
you need to look inside, build the debug variant from `Dockerfile.debug` in the
source repository — same contents on a base that has one.
