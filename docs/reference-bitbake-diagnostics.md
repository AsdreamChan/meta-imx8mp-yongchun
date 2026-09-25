# Reference: Diagnosing a BitBake Build

Commands used during weeks 1–5, with the behaviour that made them necessary.
Every entry here was run against a real problem; nothing is included for
completeness.

---

## Where a value came from

```bash
bitbake -e <recipe> | grep -B12 "^VARIABLE="
```

The comment block before an assignment lists every operation that touched the
variable, with file and line number. This is the single most useful command
for understanding metadata.

```
# $S [2 operations]
#   set /…/poky/meta/conf/bitbake.conf:410
#     "${WORKDIR}/${BP}"
# pre-expansion value:
#   "${WORKDIR}/${BP}"
S="/…/hello/1.0/hello-1.0"
```

**Make `-B` large enough.** A variable with several `?=` candidates can have a
dozen entries; `-B8` shows only the last few, which makes the wrong one look
authoritative. `DEFAULTTUNE` on a Raspberry Pi has ten candidates, of which
only the first takes effect.

**The output is expanded.** It answers "what is the value" and "who set it",
not "is the recipe portable". A recipe using `${COMMON_LICENSE_DIR}` and one
with the path written out look identical here.

### Override assignments outrank plain ones

```
#   set?      imx-image-core.bb:42                ""
#   set       yongchun-image.bb:9                 ""
#   override[mx8-nxp-bsp]:set  imx-image-core.bb:43  "docker"
# pre-expansion value:
#   "docker"
```

`DOCKER = ""` in a higher-priority layer loses to `DOCKER:mx8-nxp-bsp` in a
lower one. To displace an override assignment, use the same tag or a more
specific one. Layer priority does not enter into it.

```bash
bitbake -e <recipe> | grep "^OVERRIDES=" | tr ':' '\n'
```

lists the tags in effect, coarsest first. On an i.MX8M Plus board that runs
`mx8-generic-bsp` → `mx8-nxp-bsp` → `mx8m-*` → `mx8mp-*` → `<machine>`.

### An append is a separate layer from the base value

```
#   set                   imx8mp-evk.inc:22
#   :append[use-nxp-bsp]  imx8mp-lpddr4-frdm.conf:25
#   set                   yongchun.conf:17
```

Assigning the variable replaces the base value and leaves the append in place;
the append is applied at expansion time, on top of whatever the base is. The
result is your value plus the vendor's ten entries.

Removing it takes `:remove` with the same override tag.

---

## Before and after a configuration change

```bash
bitbake -e | grep "^DISTRO_FEATURES=" > /tmp/before.txt
# make the change
bitbake -e | grep "^DISTRO_FEATURES=" > /tmp/after.txt
diff /tmp/before.txt /tmp/after.txt
```

Worth the thirty seconds before any build, because **configuration mistakes
here fail silently**. A distro configuration in the wrong directory does not
error — it leaves `DISTRO_FEATURES` in a state that is neither the old value
nor the intended one. Two `:remove` lines instead of one with two values means
the second silently replaces the first.

The diagnostic that separates the two cases: if a variable the required file
should have set **has no value at all**, the file was never loaded. If it has
a value but the change did not apply, the assignment lost.

---

## What depends on what

```bash
bitbake -g <target>
grep '\-> "<recipe>' task-depends.dot | grep -v '^"<recipe>'
rm -f task-depends.dot pn-buildlist
```

Answers "why is this package in my image". The files land in the current
working directory, not under `tmp/`.

**Do not add `-n` to the first grep.** The line-number prefix breaks the
`^` anchor in the second one, and every line matches.

Reading the output: `image.do_rootfs -> pkg.do_package_write_deb` means the
image installs the package from the package feed. `do_rootfs` never depends on
`do_install` — the rootfs is assembled by the package manager, which is why a
package can arrive as a runtime dependency of something else entirely.

---

## Is a package available at all

```bash
bitbake -s | grep -E "^<name> "
```

Cheaper than discovering at build time that a recipe lives in a layer that is
not in `bblayers.conf`.

### Two causes of "Nothing RPROVIDES"

```
ERROR: Nothing RPROVIDES 'foo' (but …/my-image.bb RDEPENDS on or otherwise requires it)
```

Distinguished by whether a `but was skipped:` line follows:

| Message | Cause |
|---|---|
| with `but was skipped: <reason>` | The recipe exists and was excluded — restricted licence, `COMPATIBLE_MACHINE`, a missing `DISTRO_FEATURE` |
| without | No layer provides it, or the name is wrong |

When several packages are blocked for the same reason, **the error names
whichever the resolver reached first**. Two runs of the same failure can name
different packages. Chasing the specific name wastes time; the dependency
chain printed underneath is the useful part.

---

## Layers

| | |
|---|---|
| `bitbake-layers show-layers` | Paths and priorities. Collection names are the first column — `LAYERDEPENDS` wants those, not directory names |
| `bitbake-layers show-overlayed` | Every recipe provided by more than one layer, across the whole project. Shows skip reasons, including which `DISTRO_FEATURES` were missing |
| `bitbake-layers show-recipes "<glob>"` | Which layer provides a recipe, and its version |
| `bitbake-layers show-appends` | Every `.bbappend` and the recipe it applies to. The way to confirm a new one is seen at all |
| `bitbake-layers create-layer <path>` | Skeleton. Refuses an existing directory — create elsewhere and move `conf/` in |
| `bitbake-layers add-layer <path>` | Use an absolute path |

`.bbappend` files accumulate rather than compete. A recipe can carry appends
from several layers at once, all of them applied — unlike recipes, where the
highest-priority layer wins outright.

---

## Inside a recipe's build

```bash
bitbake -c listtasks <recipe>
bitbake -c devshell <recipe>
```

`devshell` drops into the task environment. `echo $CC` shows the full
cross-compiler invocation, including the security flags the distro adds and a
`--sysroot` pointing at that recipe's own isolated sysroot.

**`id` reports root inside `do_install`-family tasks and inside `devshell`.**
That is pseudo, the fakeroot layer: it intercepts syscalls so files can be
recorded as owned by root without the build needing privileges. `ps` reports
the real user. **`id` is the one that lies.**

Do not touch anything outside the build tree from a devshell — pseudo's
database only covers that tree.

```bash
bitbake -c cleansstate <recipe>
```

Deletes the recipe's work directory and its shared-state artefacts, forcing a
rebuild of that one recipe. This is the fix when an interrupted build leaves
pseudo's database inconsistent:

```
KeyError: 'getpwuid(): uid not found: 1000'
Path … is owned by uid 1000, gid 1000, which doesn't match any user/group on target.
This may be due to host contamination.
```

The message's guess is wrong. It is pseudo, not host contamination.

---

## Is the build actually running

```bash
ls -lt tmp/work/*/*/*/temp/log.do_* | head
```

If the newest task log predates the current invocation, no task has run —
BitBake is still in setup. More reliable than watching the progress bar or
checking `ps`, since a process can be alive and making no progress.

The earliest warning that a machine is running out of memory:

```
NOTE: No reply from server in 30s (for command ping at …)
```

BitBake's own process cannot reach its server. Treat it as a signal to reduce
parallelism, not as a transient.

**Only one BitBake at a time per build directory.** A second invocation waits
on the lock silently — no message, no error, just a command that appears to
hang.

---

## Kernel configuration and patches

```bash
bitbake -c listtasks virtual/kernel | grep -E "configme|menuconfig|diffconfig"
```

The fastest way to tell what kind of kernel recipe you have. If
`do_kernel_configme` is present, the recipe inherits `kernel-yocto` and
configuration fragments arriving through `SRC_URI` are merged automatically.

`bitbake -e | grep "^INHERIT"` does **not** answer this — it lists global
inherits only, not what a recipe inherits itself.

### Generating a fragment

```bash
bitbake -c menuconfig virtual/kernel      # change the option, save, exit
bitbake -c diffconfig virtual/kernel      # prints the path of the fragment
```

`diffconfig` writes a file containing only the delta, with whatever
dependencies the change pulled in. More reliable than writing `.cfg` by hand.

Prefer a fragment over a replacement defconfig. `KBUILD_DEFCONFIG` points at a
defconfig inside the kernel tree that the vendor maintains; fragments layer on
top and survive vendor kernel updates.

### Patches

`do_patch` fails — not warns — on a patch without an `Upstream-Status` line:

```
QA Issue: Missing Upstream-Status in patch … [patch-status]
```

Put the line in the commit message in the work tree, not in the `.patch` file.
The next `git format-patch` regenerates the file and a hand-edited line is
lost.

| Value | Meaning |
|---|---|
| `Pending` | Not submitted yet |
| `Submitted [where]` | Sent, awaiting response |
| `Accepted` | Upstream has it — **droppable at the next vendor update** |
| `Backport [commit]` | Taken from upstream |
| `Denied` | Upstream rejected it |
| `Inappropriate [reason]` | Local configuration, hardware-specific, a workaround |

```bash
git rev-list --count A..B
```

**Before `format-patch`, always.** A malformed range silently means "every
commit from the beginning" — in a kernel repository that is over a million
files written to disk.

Patching the kernel changes its local version, because `kernel-yocto` commits
patches into the kernel's own git tree and the `-g<hash>` suffix follows. Every
module's install path under `/lib/modules/<version>/` moves with it.

## After the fact

| | |
|---|---|
| `tmp/buildstats/<timestamp>/build_stats` | `Elapsed time`, CPU usage, rootfs size. Produced automatically when `USER_CLASSES` includes `buildstats` |
| `tmp/log/cooker/<machine>/console-latest.log` | Full console output, including builds whose terminal is gone |

Both survive a lost terminal. Neither survives deleting `tmp/`.

---

## On the board

| | |
|---|---|
| `systemd-analyze` | Kernel and userspace split, and the target reached |
| `systemd-analyze blame` | Per-unit times, slowest first |
| `i2cdetect -y -r <bus>` | `--` means nothing responds at that address; `UU` means a driver holds it |
| `dmesg \| grep -i "deferred probe"` | Probes waiting on a supplier |

**`.device` units are waiting; `.service` units are working.** A storage
device unit taking three and a half seconds is the card becoming ready, and no
amount of parallelism shortens it. The distinction decides whether an
optimisation is even possible.

**Removing a service does not save the time it took.** Removing a 3.161 s unit
reduced userspace by 2.0 s, because another unit that had been overlapping with
it surfaced. Measure after each change rather than adding up what each is
supposed to save.

### Deferred-probe chains have one root and many symptoms

```
adp5585 1-0034: error -ENXIO: Failed to read device ID        ← root
  regulator-dvdd_0 : supplier 1-0034 not ready
    i2c 1-003c     : supplier regulator-hmisc-vddio_0 not ready
      32c00000.bus:camera : deferred probe pending: (reason unknown)
```

The least informative line — `(reason unknown)` — belongs to the node furthest
downstream. Read upward.
