# parrot-to-klv

Parrot drone telemetry to MISB ST 0601 KLV, in an MPEG-TS.

Self-contained: everything it needs is in this directory. Nothing to install, no
root, no changes to your system. Ubuntu 22.04 or 24.04, x86-64.

## Running it

```sh
./parrot-to-klv flight.mp4 -o flight.ts     # convert
./parrot-to-klv flight.mp4 --klv            # inspect, convert nothing
```

Run it from anywhere — `./parrot-to-klv`, an absolute path, or a symlink on your
`PATH`. It finds its own libraries relative to itself. Move or copy the whole
directory; do not move the executable out of it on its own.

Uninstall by deleting the directory.
