# Reference: Diagnosing a BitBake Build

Commands used during weeks 1–6, with the behaviour that made them necessary.
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

### `:remove` removes every occurrence

`:remove` matches by value, not by origin. If your base value and the vendor's
append both contain the same entry, removing the duplicate removes yours as
well. `KERNEL_DEVICETREE` on this machine lists the jailhouse inmate twice for
exactly this reason, and the duplicate stays: it is harmless, and the
alternative is losing the entry altogether.

A change to the machine configuration invalidates the parse cache for every
recipe. Expect a full reparse, and a rebuild of the kernel and everything
packaged against it — out-of-tree modules included.

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

**Do not run git inside a devshell.** Under pseudo, git sees the repository
as owned by someone else and refuses with `detected dubious ownership`. The
suggested fix, a `safe.directory` exception, would be written to your own
`~/.gitconfig`, since `HOME` is unchanged. Run git from an ordinary shell
instead.

### The kernel devshell

```bash
bitbake -c devshell virtual/kernel
echo $KBUILD_OUTPUT
make freescale/<board>.dtb
```

The devshell exports `KBUILD_OUTPUT`, pointing at the recipe's build
directory, so a single target builds there without `O=`. Rebuilding one
device tree takes seconds, against most of an hour for an image.

The source directory is not the recipe's own: it lives in
`tmp/work-shared/<machine>/kernel-source` and is shared with every recipe
that builds against the kernel. Anything edited there for an experiment has
to be put back, and checked from outside the devshell:

```bash
S=$(bitbake -e virtual/kernel | grep "^S=" | cut -d'"' -f2)
git -C $S status --short          # must be empty afterwards
```

`git diff` ignores untracked files. A new file added during an experiment only
shows after `git add` and `git diff --cached`.

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

How it works, from `cml1.bbclass`: `do_menuconfig` copies `.config` to
`.config.orig` before opening the editor, and `diffconfig` prints the lines
present in `.config` but not in `.config.orig`. Saving in menuconfig also
marks `do_compile` as needing to run again, because the new `.config` is newer
than the one compiled.

**A hand-written fragment must include the parent symbols.** A symbol inside a
menu that is switched off does not survive the merge:

```
CONFIG_AUXDISPLAY=y       # without this line…
CONFIG_HD44780=m          # …this one never reaches .config
```

Check the result, not the fragment:

```bash
B=$(bitbake -e virtual/kernel | grep "^B=" | cut -d'"' -f2)
grep -E "AUXDISPLAY|HD44780" $B/.config
```

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

```bash
git format-patch -N vendor..yongchun -o <layer>/recipes-kernel/linux/linux-imx/
```

`-N` (`--no-numbered`) keeps every subject as `[PATCH]`. Without it, adding a
second patch rewrites the first file's subject to `[PATCH 1/2]`, and the layer
history shows a change to a patch whose content did not change.

Patching the kernel changes its local version, because `kernel-yocto` commits
patches into the kernel's own git tree and the `-g<hash>` suffix follows. Every
module's install path under `/lib/modules/<version>/` moves with it.

### Proving the patches reproduce the work tree

```bash
bitbake -c patch virtual/kernel
git -C <work-tree> rev-parse yongchun^{tree}
git -C $S rev-parse HEAD^{tree}
```

A tree hash covers every file's content and nothing else — not commit
messages, authors or dates. Equal hashes mean the recipe's patched source is
byte-for-byte the tree that was tested by hand, which is a stronger claim than
"the patches applied".

---

## Device trees

### What was actually built

```bash
D=tmp/deploy/images/<machine>
DTC=tmp/sysroots-components/x86_64/dtc-native/usr/bin/dtc
$DTC -I dtb -O dts $D/<board>.dtb 2>/dev/null | grep -c '"<compatible>"'
```

Search for a compatible string, not a node name. Node names repeat:
`display-controller` is also the name of the SoC's display engine nodes, and
counting it said nothing about the one that mattered.

### Overlays

| Check | Why |
|---|---|
| `grep -c '__symbols__ {'` on the base dtb | Applying an overlay resolves its external labels (`&i2c3`) through the base's `__symbols__`. No symbols, no overlay |
| Phandles in the merged tree | Every reference to a node the overlay added must carry that node's `phandle` value. This is the proof the merge connected things, more direct than the presence of any one node |
| Property order | After a merge the overlay's properties come out in reverse order. Find properties by name; `grep -A` after the last one finds nothing |

An overlay built by the kernel carries `__fixups__` and `__local_fixups__`
but no `__symbols__`: the kernel build adds `-@` only to the dtbs used as
the base of a composite target. (This is inferred from the output, not
confirmed in the kernel's makefiles.) Its labels therefore do not survive into
the merged tree, and a second overlay cannot refer to them.

A composite target (`<name>-dtbs := base.dtb overlay.dtbo` in the kernel
Makefile) runs the merge at build time. Applying the same `.dtbo` in U-Boot
at boot gave an identical tree — same phandle numbers, same property order —
since both use libfdt's overlay code.

### In U-Boot

```
help fdt                 # `fdt help` aborts when no working address is set
```

There are no pipes in U-Boot's shell. `setenv` without `saveenv` lasts one
boot, which is what an experiment wants.

The board's `mmcboot` loads the device tree itself, so it cannot be used after
a manual `fdt apply`: it would load the base again over the merged one. The
steps it performs have to be run by hand up to `booti`:

```
run loadimage
run loadfdt
fdt addr ${fdt_addr_r}
fdt resize 4096
fatload mmc ${mmcdev}:${mmcpart} <free address> <overlay>.dtbo
fdt apply <free address>
run mmcargs
booti ${loadaddr} - ${fdt_addr_r}
```

The overlay's address has to clear the kernel image (`loadaddr` plus its size)
and the resized base. If `fdt apply` fails, the base in memory may already be
modified; load it again rather than boot it.

## After the fact

| | |
|---|---|
| `tmp/buildstats/<timestamp>/build_stats` | `Elapsed time`, CPU usage, rootfs size. Produced automatically when `USER_CLASSES` includes `buildstats` |
| `tmp/log/cooker/<machine>/console-latest.log` | Full console output, including builds whose terminal is gone |

Both survive a lost terminal. Neither survives deleting `tmp/`.

In buildstats, a recipe whose only entries are `*_setscene` tasks was restored
from shared state, not rebuilt. Count the real tasks before concluding that a
change caused a recipe to compile again.

---

## On the board

| | |
|---|---|
| `systemd-analyze` | Kernel and userspace split, and the target reached |
| `systemd-analyze blame` | Per-unit times, slowest first |
| `i2cdetect -y -r <bus>` | `--` means nothing responds at that address; a number means something responds and no driver holds it; `UU` means a driver holds it |
| `dmesg \| grep -i "deferred probe"` | Probes waiting on a supplier |
| `cat /sys/kernel/debug/devices_deferred` | Every device still deferred, with the supplier it waits for |
| `/sys/kernel/debug/pinctrl/*/pinmux-pins` | Which device claimed each pin, through which group. **Not** the mux value: a pin configured to the wrong function still shows as claimed by the right device |

`i2cdetect` on the same bus across boots is a cheap control. A device that
shows its address with one device tree and `UU` with another proves the
difference is the description, not the wiring.

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

The root is often not a deferral at all but a hard failure, and its errno says
what kind:

```
pcf857x 2-0027: probe with driver pcf857x failed with error -110   ← root: ETIMEDOUT
wm8962 2-001a: probe with driver wm8962 failed with error -110     ← same bus, same cause
```

Here the bus itself was dead (a pin muxed to the wrong function), so every
device on it timed out, and the devices depending on them were deferred.
`devices_deferred` lists only the victims. An `-ENXIO` instead — the code a missing
acknowledgement produced in the drivers seen so far — points at a single device
not answering on a working bus.
