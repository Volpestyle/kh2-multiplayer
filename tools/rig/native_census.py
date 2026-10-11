"""Read-only actor census normalization, including nullable non-combat props."""


class CensusRefused(RuntimeError):
    """The native record could not be read consistently."""


def _integer(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def actor_roots(reader, entities, *, check):
    """Bracket each actor's roots and validate combat HP; retain every row.

    reader supplies value(address, struct_format) and read(address, size).
    check is the caller's cancellation/deadline check. Exceptions propagate.
    """
    rows = []
    for actor_row in entities['actors']:
        check()
        actor = _integer(actor_row['address'])
        obj = reader.value(actor + 0x918, 'Q')
        status = reader.value(actor + 0x5C0, 'Q')
        raw = reader.read(obj, 40)
        if (status != _integer(actor_row['status']) or len(raw) != 40
                or int.from_bytes(raw[:4], 'little') != actor_row['objectId']
                or raw[4] != actor_row['objectType']):
            raise CensusRefused('native descriptor/status mismatch')
        try:
            native_name = raw[8:40].split(b'\0', 1)[0].decode('ascii')
        except UnicodeDecodeError as error:
            raise CensusRefused('invalid native descriptor name') from error
        if native_name != actor_row['name']:
            raise CensusRefused('native descriptor name mismatch')
        if reader.value(actor + 0x4DC, 'I') != actor_row['team']:
            raise CensusRefused('native team mismatch')
        enemy = (actor_row['objectType'] in (3, 4)
                 or native_name.startswith(('M_', 'B_')))
        player = (actor_row['objectType'] in (0, 1)
                  or native_name.startswith('P_EX'))
        combat = enemy or player
        if combat:
            if status < 0x10000:
                raise CensusRefused('player/enemy status block unavailable')
            if (reader.value(status, 'i') != actor_row['hp']
                    or reader.value(status + 4, 'i') != actor_row['maxHp']):
                raise CensusRefused('native player/enemy HP mismatch')
        if (reader.value(actor + 0x918, 'Q') != obj
                or reader.value(actor + 0x5C0, 'Q') != status):
            raise CensusRefused('native roots replaced')
        rows.append(dict(
            name=native_name, actor=actor, objentry=obj, status=status,
            objectId=actor_row['objectId'], objectType=actor_row['objectType'],
            team=actor_row['team'], hp=actor_row['hp'], motionId=actor_row['motionId'],
            position=[actor_row['position'][axis] for axis in ('x', 'y', 'z')],
            combatClass='enemy' if enemy else 'player' if player else 'non-combat',
            hpBlockDereferenced=combat))
    return rows
