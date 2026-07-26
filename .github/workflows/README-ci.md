# CI setup — the one secret `build.yml` needs

`build.yml` clones **libmisbklv**, which is a separate *private* repository. The
automatic `GITHUB_TOKEN` is scoped to this repository alone and cannot read it,
so the runner needs a credential of its own.

**Use a read-only deploy key**, not a personal SSH key: a deploy key grants read
access to exactly one repository, where a personal key carries whatever its owner
can reach. A key that lives in a CI secret is a key that any workflow — including
one added in a pull request — can use.

One-time setup:

```bash
# 1. A dedicated keypair, kept out of ~/.ssh so it is never used interactively.
ssh-keygen -t ed25519 -N '' -C 'parrot-to-klv CI' -f /tmp/misbklv-ci

# 2. Public half -> a read-only deploy key on the dependency.
gh repo deploy-key add /tmp/misbklv-ci.pub \
  --repo nitsuga/libmisbklv --title 'parrot-to-klv CI (read-only)'

# 3. Private half -> a secret on this repository.
gh secret set MISBKLV_DEPLOY_KEY --repo nitsuga/parrot-to-klv < /tmp/misbklv-ci

# 4. Both halves are now where they belong; remove the local copies.
shred -u /tmp/misbklv-ci /tmp/misbklv-ci.pub
```

Omit `--allow-write` (the default is read-only) — CI only ever clones.

## Why not the alternatives

- **A fine-grained PAT** with read access to libmisbklv, plus a
  `url.<https>.insteadOf` rewrite, also works and avoids SSH entirely. It is a
  reasonable second choice, but a PAT is account-scoped and expires, so it is
  more to hold correctly than a per-repo key that does not.
- **Vendoring or a submodule** would remove the credential problem, at the cost
  of the exact-commit pin in `CMakeLists.txt` being the single record of which
  libmisbklv this project builds against.

## Rotating or revoking

Delete the deploy key (`gh repo deploy-key list --repo nitsuga/libmisbklv`, then
`delete <id>`) and re-run the steps above. Nothing else references the key.

## When libmisbklv goes public

Drop the "Authorise the private dependency" step, and switch
`GIT_REPOSITORY` in `CMakeLists.txt` from `git@github.com:` to
`https://github.com/` so the clone needs no credential at all.
