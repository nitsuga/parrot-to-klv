# The image is built FROM the bundle, not alongside it: packaging/make-bundle.sh
# produces a tree that needs nothing but glibc, so the image is that tree on a
# minimal base. One artifact feeds both release deliverables, which is what stops
# them drifting apart (ADR 0010).
#
# distroless carries glibc and nothing else — no shell, no package manager. The
# bundle's wrapper script is therefore not used here; ENV below does the same
# job, so the entrypoint is the binary itself. See Dockerfile.debug when you need
# to look around inside.
FROM gcr.io/distroless/base-debian12

COPY bundle/ /opt/parrot-to-klv/

# What the bundle's wrapper sets at runtime, fixed at build time instead.
# GST_REGISTRY must point somewhere writable: the container runs as the caller's
# uid (--user), which owns no home directory here.
ENV LD_LIBRARY_PATH=/opt/parrot-to-klv/lib \
    GST_PLUGIN_SYSTEM_PATH=/opt/parrot-to-klv/lib/gstreamer-1.0 \
    GST_PLUGIN_SYSTEM_PATH_1_0=/opt/parrot-to-klv/lib/gstreamer-1.0 \
    GST_PLUGIN_SCANNER=/opt/parrot-to-klv/libexec/gstreamer-1.0/gst-plugin-scanner \
    GST_PLUGIN_SCANNER_1_0=/opt/parrot-to-klv/libexec/gstreamer-1.0/gst-plugin-scanner \
    GST_REGISTRY=/tmp/gst-registry.bin \
    GST_REGISTRY_1_0=/tmp/gst-registry.bin

ENTRYPOINT ["/opt/parrot-to-klv/bin/parrot-to-klv"]
