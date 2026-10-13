"""Offline build-receipt coherence; not binary protocol introspection."""
import re

SUPPORTED_PROTOCOL = 18
AVATAR_BRIDGE_VERSION = 4

ROLES = {"dll", "runtime", "server", "avatarctl", "cli"}

def validate_protocol_set(protocol, builds, files=None):
    if type(protocol) is not int or protocol != SUPPORTED_PROTOCOL or set(builds) != ROLES:
        raise ValueError("Package protocol/build roles missing. Obtain a fresh coherent package; do not mix versions.")
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
