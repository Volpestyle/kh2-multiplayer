import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('native_census', ROOT / 'tools/rig/native_census.py')
CENSUS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CENSUS)
FIXTURE = json.loads((Path(__file__).parent / 'fixtures/native_census_nullable_props.json').read_text())


class Memory:
    """Synthetic memory projection of retained actor descriptors."""

    def __init__(self, row, *, torn=False, unreadable=False, hp_delta=0):
        self.row = row
        self.actor = int(row['address'], 16)
        self.obj = 0x20000
        self.status = int(row['status'], 16)
        self.torn = torn
        self.unreadable = unreadable
        self.hp_delta = hp_delta
        self.status_reads = 0
        self.hp_reads = 0

    def value(self, address, fmt):
        if address == self.actor + 0x918:
            return self.obj
        if address == self.actor + 0x5C0:
            self.status_reads += 1
            return self.status + (8 if self.torn and self.status_reads > 1 else 0)
        if address == self.actor + 0x4DC:
            return self.row['team']
        if address in (self.status, self.status + 4):
            self.hp_reads += 1
            if address < 0x10000 or self.unreadable:
                raise OSError('unavailable HP read')
            return self.row['hp'] + self.hp_delta if address == self.status else self.row['maxHp']
        raise AssertionError((address, fmt))

    def read(self, address, size):
        assert address == self.obj and size == 40
        row = self.row
        return (row['objectId'].to_bytes(4, 'little') + bytes([row['objectType']])
                + bytes(3) + row['name'].encode().ljust(32, b'\0'))


class NativeCensusTests(unittest.TestCase):
    def invoke(self, row, **options):
        memory = Memory(row, **options)
        result = CENSUS.actor_roots(memory, {'actors': [row]}, check=lambda: None)
        return result, memory

    def enemy(self):
        row = copy.deepcopy(FIXTURE['actors'][2])
        row.update(name='M_EX020', objectId=302, objectType=4,
                   status='0x40000', hp=20, maxHp=20)
        return row

    def test_retained_players_require_hp(self):
        for row in FIXTURE['actors'][:2]:
            with self.subTest(actor=row['address']):
                result, memory = self.invoke(row)
                self.assertEqual(result[0]['combatClass'], 'player')
                self.assertEqual(memory.hp_reads, 2)

    def test_valid_enemy_is_still_an_enemy(self):
        result, memory = self.invoke(self.enemy())
        self.assertEqual(result[0]['combatClass'], 'enemy')
        self.assertTrue(result[0]['hpBlockDereferenced'])
        self.assertEqual(memory.hp_reads, 2)

    def test_torn_enemy_refuses(self):
        with self.assertRaisesRegex(CENSUS.CensusRefused, 'roots replaced'):
            self.invoke(self.enemy(), torn=True)

    def test_unreadable_enemy_refuses(self):
        with self.assertRaises(OSError):
            self.invoke(self.enemy(), unreadable=True)

    def test_null_enemy_refuses(self):
        row = self.enemy()
        row['status'] = '0x0'
        with self.assertRaisesRegex(CENSUS.CensusRefused, 'status block unavailable'):
            self.invoke(row)

    def test_hp_mismatch_refuses(self):
        with self.assertRaisesRegex(CENSUS.CensusRefused, 'HP mismatch'):
            self.invoke(self.enemy(), hp_delta=1)

    def test_enemy_name_cannot_hide_as_prop(self):
        row = self.enemy()
        row.update(objectType=22, status='0x0')
        with self.assertRaises(CENSUS.CensusRefused):
            self.invoke(row)

    def test_cancellation_propagates_before_reads(self):
        def cancel():
            raise TimeoutError('cancelled')
        with self.assertRaises(TimeoutError):
            CENSUS.actor_roots(None, {'actors': [self.enemy()]}, check=cancel)


def prop_test(row):
    def test(self):
        result, memory = self.invoke(row)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['combatClass'], 'non-combat')
        self.assertEqual(result[0]['status'], 0)
        self.assertFalse(result[0]['hpBlockDereferenced'])
        self.assertEqual(memory.hp_reads, 0)
    return test


for index, actor in enumerate(FIXTURE['actors'][2:]):
    setattr(NativeCensusTests, f'test_retained_nullable_prop_{index}', prop_test(actor))


if __name__ == '__main__':
    unittest.main()
