"""Offline build-receipt coherence; not binary protocol introspection."""
import re
from pathlib import Path

PROTOCOL_HEADER = "common/include/kh2coop/Protocol.hpp"

def source_protocol(root):
    """Read the canonical header, also bundled in the portable package."""
    try:
        header = (Path(root)/PROTOCOL_HEADER).read_text(encoding="utf-8-sig")
    except OSError as error:
        raise ValueError("Source protocol declaration unavailable. Obtain a fresh complete package.") from error
    versions = re.findall(r"^\s*inline constexpr std::uint16_t PROTOCOL_VERSION\s*=\s*(\d+)\s*;", header, re.MULTILINE)
    if len(versions) != 1 or not 0 < int(versions[0]) <= 65535:
        raise ValueError("Source protocol declaration unavailable or ambiguous.")
    return int(versions[0])

AVATAR_BRIDGE_VERSION = 4

ROLES = {"dll", "runtime", "server", "avatarctl", "cli"}

def validate_protocol_set(protocol, builds, files=None, *, supported_protocol=None):
    if supported_protocol is None:
        supported_protocol = source_protocol(Path(__file__).resolve().parents[2])
    if type(protocol) is not int or protocol != supported_protocol or set(builds) != ROLES:
        raise ValueError("Package protocol differs from source or build roles missing. Obtain a fresh coherent package; do not mix versions.")
    cohorts = set()
    for role, row in builds.items():
        if type(row.get("protocol")) is not int or row["protocol"] != protocol:
            raise ValueError(f"Mixed protocols: {role} does not match package protocol {protocol}.")
        if not re.fullmatch(r"[0-9a-f]{40}", row.get("sourceRevision", "")) or not isinstance(row.get("buildId"), str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", row["buildId"]):
            raise ValueError(f"Missing reviewed build provenance for {role}.")
        if not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", "")):
            raise ValueError(f"Missing product hash for {role}.")
        cohorts.add((row["sourceRevision"], row["buildId"]))
        if files is not None and files.get(row.get("path")) != row["sha256"]:
            raise ValueError(f"Build receipt hash disagrees with packaged {role} bytes.")
    if len(cohorts) != 1:
        raise ValueError("Mixed source/build cohorts. Obtain products from one reviewed build.")
    return protocol
