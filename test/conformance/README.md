# Conformance reports

Evidence that real Parrot encoders match what this project assumes — committed
here so it survives, while the recordings themselves never enter the repository.

Each `<label>.json` is generated from one recording by `parrot-conform`. It holds
sample sizes, extension ids and payload lengths, decode rates, quaternion norms,
aircraft-state ranges, and pass/fail assertions. It holds **no** position, no
absolute time, and no device identity: coordinates appear only as range checks
("latitude was within ±90"), never as values. `test/conformance_test.cpp` asserts
that — it renders a report from a fixture with known coordinates and fails if any
of them appear in the output, at any precision or in fixed-point form.

## Generating

On a machine that has the footage, with recordings in `data/` (gitignored):

```
cmake --build build
./build/parrot-conform --scan data --reports test/conformance
```

Review the JSON, then commit it. The recording stays put.

## Checking

`ctest` runs `conformance_real_data`, which regenerates each report and compares:

```
ctest --test-dir build -R conformance_real_data
```

It **skips** where there is no footage, so it is a no-op in CI and on machines
that have never held a recording. Where footage is present, a changed report
means the decoder now reads that recording differently than when the report was
committed — which is either a fix or a regression, and worth knowing which.

Files here are matched to recordings by `input_sha256`, so a report whose
recording has been replaced is detectable rather than silently wrong.
