"""One canonical u8 replacement and one qualified restore; no native OS writer."""
ROW_RVA = 0x9ACDF4
TARGET_RVA = ROW_RVA + 1
ORIGINAL = bytes([0, 1, 2, 0x12])
class Refused(RuntimeError): pass

# The DLL logs this exactly when the game process has a non-"0" KH2COOP_PLAYER_KIT (VUH-1513):
# a selector-0 friend would then resolve to the local kit, so the leaf refuses before writing.
PLAYER_KIT_MARK = '[playerkit] native-Sora clone puppets REFUSED'

class PartyLeaf:
    def __init__(self, read_row, identity, poke, record, read_log=None):
        self.read_row, self.identity, self.poke, self.record = read_row, identity, poke, record
        self.read_log = read_log
        self.attempted = False
        self.restore_attempted = False
        self.original = None

    def replace(self):
        if self.attempted: raise Refused('replacement already spent')
        if self.read_log is not None and PLAYER_KIT_MARK in self.read_log():
            raise Refused('player kit set in the target process; selector-0 puppet refused (VUH-1513/VUH-1519)')
        self.identity()
        first, second = self.read_row(), self.read_row()
        self.record('replacement_pre', {'first': list(first), 'second': list(second)})
        if first != ORIGINAL or second != first: raise Refused('original row not exact 00/01/02/12')
        self.original = first
        self.identity()
        self.attempted = True  # spend BEFORE child creation; a timeout may already have written
        result = self.poke(TARGET_RVA, 0)
        self.record('replacement_cli', result)
        self.identity()
        actual = self.read_row()
        self.record('replacement_post', {'row': list(actual)})
        if not result.get('ok') or actual != bytes([0, 0, 2, 0x12]): raise Refused('replacement/readback failed')

    def restore(self):
        if not self.attempted: return {'notAttempted': True}
        if self.restore_attempted: raise Refused('restore already spent')
        self.restore_attempted = True
        self.identity()  # independent of gameplay safety and experimental deadline
        row = self.read_row()
        self.record('restore_pre', {'row': list(row)})
        if len(row) != 4 or row[1] not in (0, 1): raise Refused('restore target no longer our 00 or original 01')
        result = {'ok': True, 'alreadyOriginal': row[1] == 1}
        if row[1] == 0:
            self.identity()
            result = self.poke(TARGET_RVA, 1)
            self.record('restore_cli', result)
        self.identity()
        after = self.read_row()
        self.record('restore_post', {'row': list(after), 'exactOriginal': after == self.original})
        if not result.get('ok') or after != self.original: raise Refused('restore full row differs; other bytes NEVER overwritten')
        return {'exactOriginal': True, 'row': list(after), 'cli': result}
