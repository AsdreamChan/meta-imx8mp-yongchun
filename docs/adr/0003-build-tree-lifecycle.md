# ADR-0003: Build trees are disposable; shared state is not

## Status

Accepted (2026-09-17). Amended 2026-09-20: the first application of this
decision lost the deployed image, and the copy-out list has been corrected.

Supersedes the disk-usage estimate in
[ADR-0002](0002-scarthgap-to-wrynose-migration.md#consequences), which was
wrong by an order of magnitude.

## Context

ADR-0002 anticipated that adding a second build tree would roughly double disk
usage. Measured after the first successful vendor BSP build:

| Directory | Size |
|---|---|
| `wrynose-imx/build/tmp` | **214 GB** |
| `wrynose-imx/build/sstate-cache` | 11 GB |
| `yocto/downloads` (shared) | 14 GB |

The Scarthgap tree in its entirety was tens of gigabytes. The i.MX `tmp/`
alone is 214 GB — the vendor BSP pulls 27 repositories, `fsl-imx-xwayland`
brings Qt6 and Weston, and the parse covers 4124 recipes against Scarthgap's
963.

That has a concrete consequence on this machine. WSL2 stores the filesystem in
a VHDX on the C: partition:

```
ext4.vhdx    291 GB
C: free      100 GB
```

The free space reported inside WSL is virtual. The real ceiling is the host
partition, and it is close.

Sparse VHD mode, which ADR-0002 suggested enabling, has been disabled by
Microsoft over data-corruption risk:

```
> wsl --manage Ubuntu --set-sparse true
Sparse VHD support is currently disabled due to possible data corruption.
```

Forcing it with `--allow-unsafe` is not worth the risk on a tree that took ten
hours of wall clock to produce.

## Decision

**`tmp/` is a disposable artefact. `sstate-cache/` and `downloads/` are
assets.**

- **Delete `tmp/` once a milestone's measurements are recorded.** Its entire
  content is reconstructible from shared state and downloads.
- **Keep `sstate-cache/` and `downloads/` indefinitely.** Together they are
  25 GB against `tmp/`'s 214 GB, and they are what makes the reconstruction
  cheap.
- **Before deleting, copy out what `tmp/` holds and shared state does not**:
  the deployed image (`.wic.zst`, `.wic.bmap`), the manifest, `buildstats/`,
  and `log/cooker/<machine>/console-latest.log`.

  **Then verify the copy before deleting anything.** `cp` fails silently when
  a path is wrong, and a directory under `/tmp` does not survive a WSL
  restart. Copy to a persistent location and `ls` the destination.

  ```
  mkdir -p ~/imx-keep
  cp -Lv tmp/deploy/images/<machine>/*.wic.zst  ~/imx-keep/
  cp -Lv tmp/deploy/images/<machine>/*.wic.bmap ~/imx-keep/
  cp -Lv tmp/deploy/images/<machine>/*.manifest ~/imx-keep/
  cp -rv tmp/buildstats/$(ls -t tmp/buildstats | head -1) ~/imx-keep/buildstats
  cp -v  tmp/log/cooker/<machine>/console-latest.log ~/imx-keep/
  ls -lh ~/imx-keep/          # confirm before rm -rf tmp
  ```
- `DL_DIR` stays shared across trees — source tarballs overlap almost
  completely. `SSTATE_DIR` stays per-tree, since hashes from different
  releases will not match and sharing would make "how long does a first build
  take on this tree" unmeasurable.

Reclaiming the space is two steps, because deleting files inside WSL does not
shrink the VHDX:

```
# inside WSL
sudo fstrim -v /

# Windows, WSL shut down
diskpart
  select vdisk file="...\LocalState\ext4.vhdx"
  attach vdisk readonly
  compact vdisk
  detach vdisk
```

`fstrim` is not optional — without it the VHDX does not know which blocks are
free and `compact` achieves nothing.

## Consequences

### Accepted

- Rebuilding `tmp/` after deletion costs time — **measured at 13 minutes 21
  seconds** (801 s) on 2026-09-20, against roughly ten hours of wall clock for
  the original build across four attempts:

  ```
  Elapsed time: 801.29 seconds
  CPU usage: 24.1%
  ```

  The low CPU figure says where the time goes: unpacking shared-state
  archives and writing files, not compiling. **The decision holds with a wide
  margin** — the Revisit threshold below is one hour.

  The rebuild was not planned. The first application of this decision deleted
  the image, which forced the measurement this record said to take.
- Deleting `tmp/` also deletes the deployed images. **This has now gone wrong
  twice.** The Scarthgap manifest from before the distro change was lost to
  stale-sstate cleanup. Then, the first time this decision was applied, the
  copy-out list omitted the image itself and the copies that were made went to
  `/tmp`, where they did not survive a restart. The image, manifest,
  buildstats and console log were all lost; the rebuild cost the time this
  record assumed would be small.

  **The image is the easiest thing to forget and the most expensive to
  rebuild.** The list above now puts it first.

### Gained

- Disk pressure stops being a recurring interruption. Two trees fit
  comfortably when only one carries a populated `tmp/`.
- The habit matches how a build tree should be treated anyway. `tmp/` and
  `bblayers.conf` store absolute paths and cannot be moved; treating the tree
  as reproducible rather than precious is the same reasoning that puts the
  layer in its own repository and, later, replaces manual setup with `kas`.

### Revisit if

- A rebuild from warm shared state turns out to cost more than an hour. At
  that point keeping `tmp/` and moving the WSL distribution to a larger
  partition becomes the better trade.

## References

- [ADR-0002](0002-scarthgap-to-wrynose-migration.md) — the superseded estimate
- `sources/build-logs/2026-09-17-imx-first-build-three-failures.md` — the ten
  hours, and why it took three attempts
