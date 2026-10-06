/usr/bin/python3 - "$1" <<'PY'
import pathlib,json,hashlib,subprocess,sys
p=pathlib.Path(sys.argv[1])
meta=json.loads((p/"relay.json").read_text())
hashes={str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in p.rglob("*") if f.is_file()}
print(json.dumps({"hashes":hashes,"exit":json.loads((p/"exit.json").read_text()),"relayPs":subprocess.run(["/bin/ps","-p",str(meta["pid"]),"-o","pid=,command="],capture_output=True,text=True).stdout,"supervisorPs":subprocess.run(["/bin/ps","-p",str(meta["supervisorPid"]),"-o","pid=,command="],capture_output=True,text=True).stdout,"socket":subprocess.run(["/usr/sbin/lsof","-nP","-iUDP:27795"],capture_output=True,text=True).stdout}))
PY
