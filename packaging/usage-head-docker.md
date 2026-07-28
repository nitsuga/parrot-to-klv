# parrot-to-klv (container image)

Parrot drone telemetry to MISB ST 0601 KLV, in an MPEG-TS.

Runs from a container image, so the only thing you need installed is Docker or
Podman. The image is loaded from the tarball beside this file on first use and
reused after that; every run is ephemeral (`--rm`).

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
  parrot-to-klv:@VERSION@ /work/flight.mp4 -o /work/flight.ts
```

Mount read-only and split the directories if input and output live apart:

```sh
docker run --rm --network none \
  --user "$(id -u):$(id -g)" \
  -v /path/to/input:/in:ro -v /path/to/output:/out \
  parrot-to-klv:@VERSION@ /in/flight.mp4 -o /out/flight.ts
```

`--network none` is not required — the tool never uses the network, so it costs
nothing and is worth keeping.

## Troubleshooting

The image has no shell, which keeps it small and its attack surface minimal. If
you need to look inside, build the debug variant from `Dockerfile.debug` in the
source repository — same contents on a base that has one.
