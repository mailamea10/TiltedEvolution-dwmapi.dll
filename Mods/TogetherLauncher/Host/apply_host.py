"""Adds the Skyrim Together Plugin API, versions 1 and 2, to a Skyrim Together Reborn source tree.

Usage: python apply_host.py <path to the STR source tree> --protocol 1.8.3|1.8.4|1.8.5|1.8.7

This is the one patch the Together Launcher puts on each Skyrim Together release before the add-ons: they all talk
to the other players through this API, so a new Skyrim Together release needs this script to apply, not one patch
per add-on. It copies Host/Code over the tree (the Plugin API header, the Mod Message and add-on relay messages,
the client's PluginService and the server's relay services) and wires them in:

- Code/encoding/Opcodes.h: the message opcodes, appended so every existing opcode keeps its number;
- Code/encoding/Messages/ClientMessageFactory.h and ServerMessageFactory.h: the messages;
- Code/encoding/xmake.lua: the Plugin API header's folder;
- Code/client/World.cpp: PluginService's event handlers;
- Code/server/World.cpp: the server's ModMessageService and AddonNearService.

Skyrim Together 1.8.7 already has version 1, so there only the version 2 parts are added. 1.8.3 and 1.8.4 cannot
send unreliably, so there everything goes reliably (--protocol says which release the tree is). Running the script
twice changes nothing the second time.

A server built from the patched tree relays add-on messages; players on a stock server of the same release can
still join it, and patched clients on a stock server find out it does not relay them and fall back.
"""

import pathlib
import re
import shutil
import sys

CLIENT_OPCODES = ["kSendModMessageRequest", "kAddonHelloRequest", "kSendAddonNearRequest"]
SERVER_OPCODES = ["kNotifyModMessage", "kNotifyAddonCapabilities", "kNotifyAddonNear"]
CLIENT_MESSAGES = ["SendModMessageRequest", "AddonHelloRequest", "SendAddonNearRequest"]
SERVER_MESSAGES = ["NotifyModMessage", "NotifyAddonCapabilities", "NotifyAddonNear"]

CLIENT_WORLD_INCLUDES = [
    "#include <Services/PluginService.h>",
    "#include <Messages/NotifyModMessage.h>",
    "#include <Messages/NotifyAddonCapabilities.h>",
    "#include <Messages/NotifyAddonNear.h>",
    "#include <Messages/NotifyPartyInfo.h>",
    "#include <Messages/NotifyPartyJoined.h>",
    "#include <Messages/NotifyPartyLeft.h>",
    "#include <Events/ConnectedEvent.h>",
    "#include <Events/DisconnectedEvent.h>",
    "#include <Events/UpdateEvent.h>",
]
# After PartyService's own handlers, so its member list is up to date when PluginService looks at it.
CLIENT_WORLD_SINKS = [
    "m_dispatcher.sink<NotifyModMessage>().connect<&PluginService::OnNotifyModMessage>(PluginService::Get());",
    "m_dispatcher.sink<NotifyAddonNear>().connect<&PluginService::OnNotifyAddonNear>(PluginService::Get());",
    "m_dispatcher.sink<NotifyAddonCapabilities>().connect<&PluginService::OnNotifyAddonCapabilities>(PluginService::Get());",
    "m_dispatcher.sink<ConnectedEvent>().connect<&PluginService::OnConnected>(PluginService::Get());",
    "m_dispatcher.sink<DisconnectedEvent>().connect<&PluginService::OnDisconnected>(PluginService::Get());",
    "m_dispatcher.sink<UpdateEvent>().connect<&PluginService::OnUpdate>(PluginService::Get());",
    "m_dispatcher.sink<NotifyPartyInfo>().connect<&PluginService::OnPartyEvent<NotifyPartyInfo>>(PluginService::Get());",
    "m_dispatcher.sink<NotifyPartyJoined>().connect<&PluginService::OnPartyEvent<NotifyPartyJoined>>(PluginService::Get());",
    "m_dispatcher.sink<NotifyPartyLeft>().connect<&PluginService::OnPartyEvent<NotifyPartyLeft>>(PluginService::Get());",
    "m_dispatcher.sink<DisconnectedEvent>().connect<&PluginService::OnPartyEvent<DisconnectedEvent>>(PluginService::Get());",
]
SERVER_WORLD_INCLUDES = ["#include <Services/ModMessageService.h>", "#include <Services/AddonNearService.h>"]
SERVER_WORLD_SERVICES = ["ctx().emplace<ModMessageService>(*this, m_dispatcher);", "ctx().emplace<AddonNearService>(*this, m_dispatcher);"]

# 1.8.3 and 1.8.4: Send takes no packet flags.
RELIABLE_ONLY = [
    (
        "Code/client/Services/Generic/PluginService.cpp",
        "world.GetTransport().Send(request, cUnreliable ? TiltedPhoques::kUnreliableNoNagle : TiltedPhoques::kReliable);",
        "(void)cUnreliable; // this Skyrim Together release sends everything reliably\n            world.GetTransport().Send(request);",
    ),
    (
        "Code/server/Services/AddonNearService.cpp",
        "    const auto cFlags = cRequest.Unreliable ? TiltedPhoques::kUnreliableNoNagle : TiltedPhoques::kReliable;\n",
        "    // This Skyrim Together release sends everything reliably.\n",
    ),
    ("Code/server/Services/AddonNearService.cpp", "pPlayer->Send(notify, cFlags);", "pPlayer->Send(notify);"),
]


def fail(message: str) -> None:
    sys.exit(f"apply_host: {message}")


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def write(path: pathlib.Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="")


def eol(text: str) -> str:
    return "\r\n" if "\r\n" in text else "\n"


def add_enum_values(path: pathlib.Path, names: list, terminator: str) -> None:
    text = read(path)
    nl = eol(text)
    match = re.search(r"^([ \t]*)" + re.escape(terminator) + r"\b", text, re.MULTILINE)
    if not match:
        fail(f"'{terminator}' not found in {path}")
    indent = match.group(1)
    missing = [name for name in names if not re.search(r"\b" + name + r"\b", text)]
    if missing:
        addition = "".join(f"{indent}{name},{nl}" for name in missing)
        text = text[: match.start()] + addition + text[match.start() :]
        write(path, text)
    print(f"apply_host: {path.name}: {', '.join(missing) if missing else 'already has every opcode'}")


def add_to_factory(path: pathlib.Path, names: list) -> None:
    text = read(path)
    nl = eol(text)
    includes = list(re.finditer(r"^#include <Messages/[^>]+>[ \t]*\r?$", text, re.MULTILINE))
    if not includes:
        fail(f"no message includes in {path}")
    missing_includes = [f"#include <Messages/{name}.h>" for name in names if f"<Messages/{name}.h>" not in text]
    if missing_includes:
        end = includes[-1].end()
        text = text[:end] + "".join(nl + line for line in missing_includes) + text[end:]

    match = re.search(r"CreateMessageVisitor<(.*?)>;", text, re.DOTALL)
    if not match:
        fail(f"no CreateMessageVisitor list in {path}")
    listed = {name.strip() for name in match.group(1).split(",")}
    missing = [name for name in names if name not in listed]
    if missing:
        text = text[: match.end(1)] + "".join(f", {name}" for name in missing) + text[match.end(1) :]
    write(path, text)
    print(f"apply_host: {path.name}: {', '.join(missing) if missing else 'already lists every message'}")


def insert_lines_after(path: pathlib.Path, anchor_pattern: str, lines: list, indent: str = "") -> None:
    text = read(path)
    nl = eol(text)
    missing = [line for line in lines if line not in text]
    if not missing:
        print(f"apply_host: {path.name}: already wired")
        return
    match = re.search(anchor_pattern, text, re.MULTILINE)
    if not match:
        fail(f"anchor '{anchor_pattern}' not found in {path}")
    end = text.index("\n", match.end()) + 1
    text = text[:end] + "".join(f"{indent}{line}{nl}" for line in missing) + text[end:]
    write(path, text)
    print(f"apply_host: {path.name}: added {len(missing)} line(s)")


def main() -> None:
    args = sys.argv[1:]
    protocol = ""
    if "--protocol" in args:
        index = args.index("--protocol")
        protocol = args[index + 1] if index + 1 < len(args) else ""
        del args[index : index + 2]
    if len(args) != 1 or protocol not in ("1.8.3", "1.8.4", "1.8.5", "1.8.7"):
        sys.exit(__doc__)

    tree = pathlib.Path(args[0]).resolve()
    host = pathlib.Path(__file__).resolve().parent
    code = tree / "Code"
    if not (code / "encoding" / "Opcodes.h").is_file():
        fail(f"{tree} is not a Skyrim Together source tree")

    source_root = host / "Code"
    for source in source_root.rglob("*"):
        if source.is_file():
            target = code / source.relative_to(source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            print(f"apply_host: copied {target.relative_to(tree)}")

    if protocol in ("1.8.3", "1.8.4"):
        for relative, old, new in RELIABLE_ONLY:
            path = tree / relative
            text = read(path)
            if old not in text:
                fail(f"'{old.strip()}' not found in {path}")
            write(path, text.replace(old, new))
        print(f"apply_host: protocol {protocol}: everything is sent reliably")

    add_enum_values(code / "encoding" / "Opcodes.h", CLIENT_OPCODES, "kClientOpcodeMax")
    add_enum_values(code / "encoding" / "Opcodes.h", SERVER_OPCODES, "kServerOpcodeMax")
    add_to_factory(code / "encoding" / "Messages" / "ClientMessageFactory.h", CLIENT_MESSAGES)
    add_to_factory(code / "encoding" / "Messages" / "ServerMessageFactory.h", SERVER_MESSAGES)

    xmake = code / "encoding" / "xmake.lua"
    text = read(xmake)
    if "plugin_api/include" not in text:
        old = 'add_includedirs(".", "../", {public = true})'
        if old not in text:
            fail(f"'{old}' not found in {xmake}")
        write(xmake, text.replace(old, 'add_includedirs(".", "../", "../plugin_api/include", {public = true})'))
        print("apply_host: xmake.lua: the Plugin API header's folder")

    world = code / "client" / "World.cpp"
    insert_lines_after(world, r"^#include <Services/MapService\.h>", CLIENT_WORLD_INCLUDES)
    insert_lines_after(world, r"ctx\(\)\.emplace<MapService>\(", CLIENT_WORLD_SINKS, "    ")

    world = code / "server" / "World.cpp"
    insert_lines_after(world, r"^#include <Services/MapService\.h>", SERVER_WORLD_INCLUDES)
    insert_lines_after(world, r"ctx\(\)\.emplace<MapService>\(", SERVER_WORLD_SERVICES, "    ")


if __name__ == "__main__":
    main()
