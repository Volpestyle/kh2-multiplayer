"""Offline session controls; no native enumeration or launch."""
import unittest
from unittest.mock import patch
from . import require_current_session
from .binding import require_session

class Controls(unittest.TestCase):
    def test_matching_session(self):
        row=dict(pid=7, session=1)
        self.assertIs(require_session(1,[row],7,1),row)
    def test_invalid_consent(self):
        for value in (None,True,0,-1,0xffffffff,'1',1.0):
            with self.subTest(value=value),self.assertRaises(AssertionError):
                require_session(value,[dict(pid=7,session=1)],7,1)
    def test_missing_or_changed_console(self):
        for active in (0,0xffffffff,2):
            with self.subTest(active=active),self.assertRaises(AssertionError):
                require_session(1,[dict(pid=7,session=1)],7,active)
    def test_missing_duplicate_or_foreign_driver(self):
        for rows in ([],[dict(pid=8,session=1)],[dict(pid=7,session=0)],[dict(pid=7,session=1)]*2):
            with self.subTest(rows=rows),self.assertRaises(AssertionError):
                require_session(1,rows,7,1)
    def test_fresh_gate_before_every_launch(self):
        with patch('tools.rig.pc2_session.os.getpid',return_value=7),patch('tools.rig.pc2_session.snapshot',side_effect=[[dict(pid=7,session=1)]]*3) as census,patch('tools.rig.pc2_session.active_console_session',side_effect=[1,1,2]):
            require_current_session(1) # admission
            require_current_session(1) # first launch
            with self.assertRaises(AssertionError):require_current_session(1) # second launch refuses
            self.assertEqual(census.call_count,3)
    def test_native_snapshot_failure_refuses(self):
        with patch('tools.rig.pc2_session.snapshot',side_effect=RuntimeError('unavailable')):
            with self.assertRaises(RuntimeError):require_current_session(1)

if __name__=='__main__':unittest.main()
