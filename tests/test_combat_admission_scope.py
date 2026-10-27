"""VUH-2897: the combat-admission barrier's scope stays exactly where it was reviewed.

S3 relaxation: the barrier's held-mask proof admits only the host's room publication (announced
ahead of the census, which the hold keeps closed) and the progress sample that precedes it;
SafeNativeGameplay, the census and every other mask==0 gate stay closed.
One detour owns 3BF2E0 (MinHook allows one per target); the Axel trace observes through it.
"""
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'inject/src'
OWNERS = {'CombatAdmission.hpp', 'CombatAdmissionNative.hpp'}


def function_body(text, signature):
    start = text.index(signature)
    depth, i = 0, text.index('{', start)
    for j in range(i, len(text)):
        depth += {'{': 1, '}': -1}.get(text[j], 0)
        if depth == 0:
            return text[start:j + 1]
    raise AssertionError(signature)


class CombatAdmissionScopeTest(unittest.TestCase):
    def sources(self):
        for path in sorted(SRC.iterdir()):
            if path.suffix in {'.cpp', '.hpp', '.inl'} and path.name not in OWNERS:
                yield path, path.read_text(encoding='utf-8', errors='replace')

    def test_relaxation_is_used_only_by_publication_and_its_progress_sample(self):
        users = {}
        for path, text in self.sources():
            for match in re.finditer(r'combatadmission::(PublicationHoldQualifies|PublicationChallenge)\(', text):
                users.setdefault(path.name, set()).add(match.group(1))
        self.assertEqual(users, {'EnemySync.cpp': {'PublicationHoldQualifies', 'PublicationChallenge'},
                                 'ProgressSync.cpp': {'PublicationHoldQualifies'}})
        enemy = (SRC / 'EnemySync.cpp').read_text(encoding='utf-8')
        begin = function_body(enemy, 'bool HostBeginInstance() {')
        self.assertEqual(begin.count('combatadmission::PublicationHoldQualifies('), 1)
        # The one other use announces the held room before the census gate, which the hold keeps
        # closed (live run 20261027-033229: every census refused "gameplay gate", no publication).
        outside = enemy.replace(begin, '')
        self.assertEqual(outside.count('combatadmission::PublicationHoldQualifies('), 1)
        at = outside.index('combatadmission::PublicationHoldQualifies(')
        end = outside.index(';', at)
        self.assertTrue(outside[at:end].rstrip().endswith('(void)HostBeginInstance()'), outside[at:end])
        following = re.sub(r'\s+', ' ', outside[end + 1:outside.index('CaptureNativeCensus(', end)])
        self.assertEqual(following.strip(), 'if (!g_inst.live) { DrainPendingSpawnTrace(false); return; } auto census =')
        progress = (SRC / 'ProgressSync.cpp').read_text(encoding='utf-8')
        tick = function_body(progress, 'void Tick(std::uint32_t frame, bool host, bool client) {')
        self.assertIn('combatadmission::PublicationHoldQualifies()', tick)

    def test_ordinary_safety_predicates_are_not_relaxed(self):
        enemy = (SRC / 'EnemySync.cpp').read_text(encoding='utf-8')
        for signature in ('bool SafeNativeGameplay() {', 'bool SafeRecoveryGameplay() {'):
            body = function_body(enemy, signature)
            self.assertNotIn('combatadmission', body, signature)
            self.assertIn('CONTROLLABLE', function_body(enemy, 'bool SafeNativeGameplay() {'))
        progress = (SRC / 'ProgressSync.cpp').read_text(encoding='utf-8')
        self.assertNotIn('combatadmission', function_body(progress, 'bool SafeGameplay('))
        warp = (SRC / 'Warp.cpp').read_text(encoding='utf-8')
        self.assertNotIn('combatadmission', function_body(warp, 'bool SafeToWarp() {'))

    def test_one_detour_owns_3bf2e0(self):
        trace = (SRC / 'AxelBoundaryTrace.hpp').read_text(encoding='utf-8')
        self.assertNotIn('0x3BF2E0', trace)
        owner = (SRC / 'CombatAdmissionNative.hpp').read_text(encoding='utf-8')
        self.assertEqual(len(re.findall(r'MH_CreateHook\(', owner)), 1)
        self.assertIn('base + 0x3BF2E0', owner)
        hooks = [path.name for path, text in self.sources() if re.search(r'0x3BF2E0', text, re.I)]
        self.assertEqual(hooks, [])

    def test_barrier_is_default_on(self):
        owner = (SRC / 'CombatAdmissionNative.hpp').read_text(encoding='utf-8')
        self.assertIn("value[0] == '0'", owner)  # only an explicit 0 disables it
        machine = (SRC / 'CombatAdmission.hpp').read_text(encoding='utf-8')
        self.assertIn('bool enabled = true;', machine)


if __name__ == '__main__':
    unittest.main()
