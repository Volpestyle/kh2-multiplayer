"""Run actual runtime/relay diagnostics with --pid 0; never use a game/CLI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(bin_dir, root):
    root.mkdir(parents=True, exist_ok=False)
    suffix = ".exe" if (bin_dir / "kh2coop_runtime_scaffold.exe").exists() else ""
    runtime = bin_dir / ("kh2coop_runtime_scaffold" + suffix)
    fixture = bin_dir / ("kh2coop_runtime_desync_smoke" + suffix)
    config = root / "runtime.ini"
    config.write_text("game_build=runtime-desync-smoke\nmod_hash=\ncontent_hash=none\n", encoding="utf-8")
    inject_log = root / "registered-inject.log"
    inject_bytes = b"owned offline source file: no native observations\n"
    inject_log.write_bytes(inject_bytes)
    fixture_log, runtime_log = root / "fixture.log", root / "runtime.log"
    fixture_cmd = [str(fixture), str(root / "aggregate"), "17813"]
    runtime_cmd = [str(runtime), "--config", str(config), "--network", "--server", "127.0.0.1",
                   "--port", "17813", "--role", "1", "--peer-id", "offline-runtime", "--pid", "0",
                   "--no-camera", "--max-ticks", "600", "--inject-log", str(inject_log),
                   "--desync-dir", str(root / "local")]
    children = []
    try:
        with fixture_log.open("wb") as output:
            peer = subprocess.Popen(fixture_cmd, stdout=output, stderr=subprocess.STDOUT)
            children.append(peer)
            end = time.monotonic() + 10
            while "READY 17813" not in fixture_log.read_text(errors="replace"):
                if peer.poll() is not None or time.monotonic() >= end:
                    raise AssertionError("fixture did not become ready; see fixture.log")
                time.sleep(0.02)
            with runtime_log.open("wb") as local_output:
                local = subprocess.Popen(runtime_cmd, stdout=local_output, stderr=subprocess.STDOUT)
                children.append(local)
                assert peer.wait(timeout=25) == 0, "fixture failed; see fixture.log"
                assert local.wait(timeout=20) == 0, "runtime failed; see runtime.log"
    finally:
        # These are exclusively owned test children. No desktop/rig process lookup.
        for child in children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)

    manifests = list((root / "aggregate").glob("*/*/manifest.json"))
    assert len(manifests) == 1, "exactly one automatic report expected"
    manifest_path = manifests[0]
    manifest = json.loads(manifest_path.read_text())
    assert manifest["collectionStatus"] == "partial", "unattached screenshot must remain partial"
    assert len(manifest["peers"]) == 3 and all(p["done"] for p in manifest["peers"])
    participant = next(p for p in manifest["peers"] if p["slot"] == 1)
    assert not participant["error"], "runtime's contribution must be admitted"
    for descriptor in participant["artifacts"]:
        file = manifest_path.parent / descriptor["file"]
        assert descriptor["fileWritten"] and descriptor["integrity"]
        assert descriptor["bytes"] == descriptor["receivedBytes"] == file.stat().st_size
        assert descriptor["sha256"] == descriptor["receivedSha256"] == digest(file)
    metadata, tail, inject, png = participant["artifacts"]
    assert all(a["status"] == "complete" for a in (metadata, tail, inject))
    assert png["status"] == "unavailable" and png["bytes"] == 0
    assert "no current attached PID" in png["error"]
    observed = (manifest_path.parent / metadata["file"]).read_text()
    assert "attachedPid=0" in observed and "nativeBootstrapReadiness=unverified" in observed
    assert "BEFORE" in observed and "AFTER" in observed
    raw_tail = (manifest_path.parent / tail["file"]).read_bytes()
    assert b"Booting runtime scaffold" in raw_tail and b"Network: SessionState" in raw_tail
    assert (manifest_path.parent / inject["file"]).read_bytes() == inject_bytes
    runtime_text = runtime_log.read_text(errors="replace")
    assert "Desync collection started" in runtime_text and "Desync upload finished" in runtime_text
    assert "Waiting for KH2 process" in runtime_text and "Attached to KH2 process" not in runtime_text
    result = {"status": "pass", "scope": "actual runtime reporting while attachment is impossible",
              "gameInteraction": False, "nativeAcceptance": False, "remoteAcceptance": False,
              "runtimeCommand": runtime_cmd, "fixtureCommand": fixture_cmd,
              "runtimeSha256": digest(runtime), "fixtureSha256": digest(fixture),
              "manifest": str(manifest_path), "manifestSha256": digest(manifest_path),
              "runtimeParticipant": participant}
    (root / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin-dir", required=True, type=Path)
    parser.add_argument("--out-root", required=True, type=Path)
    args = parser.parse_args()
    result = run(args.bin_dir.resolve(), args.out_root.resolve())
    print(json.dumps({"status": result["status"], "manifest": result["manifest"]}))
