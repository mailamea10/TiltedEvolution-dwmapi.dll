"""Builds one Together Launcher target: a Skyrim Together launcher (and that release's server) with every add-on.

Usage:
  python build_launcher.py <target id> --tree <STR source checkout> --addon <add-on folder> [--addon ...]
                           [--out <folder>] [--server] [--no-build]

<target id> is a line of targets.txt (183-1.7.104, 187-1.6.1170, ...). --tree is a git checkout of any Skyrim
Together repository; the target's commit is fetched into it from the target's source repository when missing, and
the checkout is reset to that commit, so do not point it at a tree with work you want to keep. Each --addon is an
add-on's folder (for example Mods/RagdollTogether on its release branch), containing StrAddon/apply_addon.py.

The same steps run locally and in the release workflow:
  1. check out the target's commit and pin its version, exactly what that release's servers accept;
  2. apply Host/apply_host.py (the Plugin API the add-ons talk through), then every add-on;
  3. build the launcher (and with --server the server), then check its version string and add-on exports;
  4. copy the launcher to <out>/<folder>/SkyrimTogetherReborn and the server to <out>/server-<version>/.
--no-build stops after step 2, to look at or build the patched tree by hand.
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
EXPORTS = ["RequestPluginAPI", "STRModSync_HostApiVersion", "STRRaceMenu_HostApiVersion", "STRLipSync_HostApiVersion", "STRRagdoll_HostApiVersion"]
LEGACY_FILES = ["EarlyLoad.dll", "TPProcess.exe"]
LEGACY_MAP = "GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map"


def fail(message: str) -> None:
    sys.exit(f"build_launcher: {message}")


def run(args: list, cwd: pathlib.Path, capture: bool = False) -> str:
    print(f"build_launcher: $ {' '.join(str(a) for a in args)}", flush=True)
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=capture)
    if result.returncode != 0:
        if capture:
            print(result.stdout, result.stderr)
        fail(f"'{args[0]} {args[1] if len(args) > 1 else ''}' failed in {cwd}")
    return result.stdout if capture else ""


def read_targets() -> dict:
    targets = {}
    for line in (HERE / "targets.txt").read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if len(fields) != 7:
            fail(f"targets.txt: expected 7 fields: {line}")
        target_id, source, commit, version, protocol, runtime, folder = fields
        targets[target_id] = dict(id=target_id, source=source, commit=commit, version=version, protocol=protocol, runtime=runtime,
                                  folder=folder.replace("_", " "))
    return targets


def checkout(tree: pathlib.Path, target: dict) -> None:
    if subprocess.run(["git", "cat-file", "-e", f"{target['commit']}^{{commit}}"], cwd=tree, capture_output=True).returncode != 0:
        # By commit and without that repository's tags, so the version pinned below is the only one on it.
        run(["git", "fetch", "--no-tags", target["source"], target["commit"]], tree)
    run(["git", "checkout", "--force", "--detach", target["commit"]], tree)
    run(["git", "clean", "-fdq", "Code"], tree)
    # Annotated, because git describe (which the build stamps into the binaries) prefers annotated tags over any
    # lightweight one that happens to sit on the same commit.
    # An annotated tag needs a tagger; CI runners have no git identity, so it gets a fixed one.
    run(["git", "-c", "user.name=Together Launcher", "-c", "user.email=together-launcher@users.noreply.github.com",
         "tag", "-f", "-a", "-m", "Together Launcher build version", target["version"], "HEAD"], tree)
    described = run(["git", "describe", "--tags"], tree, capture=True).strip()
    if described != target["version"]:
        others = run(["git", "tag", "--points-at", "HEAD"], tree, capture=True).split()
        fail(f"the tree describes itself as {described}, not {target['version']}: other tags on this commit "
             f"({', '.join(t for t in others if t != target['version'])}) win; delete them in this checkout")
    run(["git", "submodule", "update", "--init", "--force", "--recursive", "--depth=1"], tree)


def apply(tree: pathlib.Path, target: dict, addons: list) -> list:
    run([sys.executable, str(HERE / "Host" / "apply_host.py"), str(tree), "--protocol", target["protocol"]], tree)
    applied = []
    for addon in addons:
        script = addon / "StrAddon" / "apply_addon.py"
        if not script.is_file():
            fail(f"{addon} has no StrAddon/apply_addon.py")
        run([sys.executable, str(script), str(tree), "--protocol", target["protocol"]], tree)
        applied.append(addon.name)
    return applied


def build(tree: pathlib.Path, target: dict, with_server: bool) -> None:
    config = ["xmake", "config", "--plat=windows", "--arch=x64", "--mode=releasedbg", "--yes"]
    # Releases that build both AE runtimes from one commit choose with this option; the others have one commit each.
    if 'option("skyrim_version")' in (tree / "xmake.lua").read_text(encoding="utf-8"):
        config.append("--skyrim_version=" + ("1.6.1170" if target["runtime"] == "1.6.1170" else "latest"))
    run(config, tree)

    targets = ["SkyrimTogetherServer", "SkyrimServerRunner"] if with_server else []
    if target["runtime"] == "1.5.97":
        if any('target("ImmersiveElf")' in p.read_text(encoding="utf-8", errors="ignore") for p in (tree / "Code").rglob("xmake.lua")):
            targets.append("ImmersiveElf")
        targets += ["TPProcess", "SkyrimImmersiveLauncherLegacyNoConsole"]
    else:
        targets.append("SkyrimImmersiveLauncher")
    for name in targets:
        run(["xmake", "build", "-y", name], tree)


def check(exe: pathlib.Path, target: dict) -> None:
    data = exe.read_bytes()
    versions = {m.group(0).decode() for m in re.finditer(rb"v1\.8\.[0-9][\w.\-]{0,32}", data)}
    if target["version"] not in versions:
        fail(f"{exe} reports {sorted(versions)}, not {target['version']}")
    missing = [name for name in EXPORTS if name.encode() + b"\0" not in data]
    if missing:
        fail(f"{exe} does not export {missing}")
    print(f"build_launcher: {exe.name} reports {target['version']} and exports {', '.join(EXPORTS)}")


def collect(tree: pathlib.Path, target: dict, out: pathlib.Path, with_server: bool, applied: list) -> None:
    built = tree / "build" / "windows" / "x64" / "releasedbg"
    dest = out / target["folder"] / "SkyrimTogetherReborn"
    dest.mkdir(parents=True, exist_ok=True)
    if target["runtime"] == "1.5.97":
        shutil.copy2(built / "legacy-noconsole" / "SkyrimTogether.exe", dest)
        for name in LEGACY_FILES:
            shutil.copy2(built / name, dest)
        shutil.copy2(tree / LEGACY_MAP, dest)
    else:
        shutil.copy2(built / "SkyrimTogether.exe", dest)
    check(dest / "SkyrimTogether.exe", target)

    with (out / "LAUNCHER_MANIFEST.txt").open("a", encoding="utf-8") as manifest:
        manifest.write(f"{target['folder']}: Skyrim Together {target['version']} at {target['commit']}, runtime {target['runtime']}\n")
        for name in applied:
            manifest.write(f"  add-on: {name}\n")

    if with_server:
        server = out / f"server-{target['version']}" / "SkyrimTogetherReborn"
        server.mkdir(parents=True, exist_ok=True)
        for name in ("SkyrimTogetherServer.exe", "STServer.dll"):
            shutil.copy2(built / name, server)
        print(f"build_launcher: server copied to {server}")
    print(f"build_launcher: launcher copied to {dest}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target")
    parser.add_argument("--tree", required=True, type=pathlib.Path)
    parser.add_argument("--addon", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("launcher-out"))
    parser.add_argument("--server", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    targets = read_targets()
    if args.target not in targets:
        fail(f"unknown target {args.target}; targets.txt has {', '.join(targets)}")
    target = targets[args.target]
    tree = args.tree.resolve()
    if not (tree / ".git").exists():
        fail(f"{tree} is not a git checkout")

    checkout(tree, target)
    applied = apply(tree, target, [a.resolve() for a in args.addon])
    if args.no_build:
        return
    build(tree, target, args.server)
    collect(tree, target, args.out.resolve(), args.server, applied)


if __name__ == "__main__":
    main()
