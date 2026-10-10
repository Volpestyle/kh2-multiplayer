"""Independent bounded PopulationCut-schema2 receipt decoder; no process access."""
import math
import struct


def decode(raw):
    at = 0
    def take(fmt):
        nonlocal at
        size = struct.calcsize(fmt)
        if at + size > len(raw):
            raise ValueError("truncated membership proof")
        values = struct.unpack_from(fmt, raw, at)
        at += size
        return values[0] if len(values) == 1 else values
    tag, length = take("<BH")
    if tag != 48 or length != len(raw) - 3 or take("<B") != 2:
        raise ValueError("membership framing/schema")
    epoch, sequence, load, transition = take("<IQII")
    location = take("<6H")
    if not all((epoch, sequence, load, transition)) or location != (5, 6, 0, 1, 1, 0):
        raise ValueError("membership scope")
    definitions = []
    count = take("<H")
    if not 0 < count <= 64:
        raise ValueError("definition count")
    total = 0
    for _ in range(count):
        layout = bytes(take("<32B")); room = take("<6H"); group = take("<I"); header = bytes(take("<44B"))
        records = take("<H"); total += records
        if not any(layout) or room != location or not group or not 0 < records <= 256 or total > 512 or int.from_bytes(header[4:6], "little") != records:
            raise ValueError("definition content")
        record_bytes = tuple(bytes(take("<64B")) for _ in range(records))
        definition = (layout, room, group, header[:14] + header[15:], record_bytes)
        if definition in definitions:
            raise ValueError("aliased definitions")
        definitions.append(definition)
    count = take("<H")
    if not 0 < count <= 64:
        raise ValueError("occurrence count")
    rows = {}; events = set(); live_keys = set()
    for _ in range(count):
        net, definition, ordinal, obj, occurrence, birth, terminal, state = take("<HHHIQQQB")
        point = take("<4f")
        if not net or net in rows or obj != 302 or definition >= len(definitions) or state not in (0, 1, 2):
            raise ValueError("occurrence identity")
        d = definitions[definition]
        if ordinal >= len(d[-1]) or int.from_bytes(d[-1][ordinal][:4], "little") != obj or occurrence != birth or not 0 < birth <= sequence or birth in events:
            raise ValueError("birth proof")
        if not all(math.isfinite(v) for v in point) or point[3] != 1:
            raise ValueError("birth point")
        events.add(birth)
        if state:
            if not birth < terminal <= sequence or terminal in events:
                raise ValueError("retirement ordering")
            events.add(terminal)
        elif terminal:
            raise ValueError("nonterminal timestamp")
        key = (d, ordinal)
        if not state:
            if key in live_keys:
                raise ValueError("duplicate live record")
            live_keys.add(key)
        rows[net] = dict(key=key, birth=birth, terminal=terminal, state=state, point=point)
    if at != len(raw) or max(events) != sequence or len(live_keys) > 5:
        raise ValueError("membership boundary")
    for e in rows.values():
        for n in rows.values():
            if e["key"] == n["key"] and e["birth"] < n["birth"] and not (e["state"] == 2 and e["terminal"] < n["birth"]):
                raise ValueError("birth before removal")
    return dict(epoch=epoch, sequence=sequence, load=load, transition=transition, rows=rows)


class History:
    def __init__(self):
        self.cut = None

    def admit(self, raw):
        new = decode(raw)
        old = self.cut
        if old:
            if any(old[k] != new[k] for k in ("epoch", "load", "transition")) or new["sequence"] <= old["sequence"]:
                raise ValueError("history scope/order")
            for net, e in old["rows"].items():
                n = new["rows"].get(net)
                if not n or any(e[k] != n[k] for k in ("key", "birth", "point")):
                    raise ValueError("history identity/reuse")
                if e["state"] and e != n:
                    raise ValueError("terminal history replacement")
                if not e["state"] and n["state"] and n["terminal"] <= old["sequence"]:
                    raise ValueError("retirement replay")
            for net, e in new["rows"].items():
                if net not in old["rows"]:
                    prior = [n for n in new["rows"].values() if n["key"] == e["key"] and n["birth"] < e["birth"]]
                    if e["birth"] <= old["sequence"] or not prior or max(prior, key=lambda n:n["birth"])["state"] != 2:
                        raise ValueError("missing old retirement")
        elif len({e["key"] for e in new["rows"].values()}) != 5:
            raise ValueError("missing original record proof")
        self.cut = new
        return new
