"""Copied practice05 event; historical text does not authenticate applySeq.
The compiled control establishes parent linkage using actual callbacks.
"""
import argparse
from pathlib import Path
from companion_retained_fixture import generate

def practice_fixture(log, out):
    generate([log], out)
    text = out.read_text(encoding='utf-8')
    text = text.replace('Generated from retained causal08/10 text. Regenerate with companion_retained_fixture.py.',
                        'Generated from retained practice05 text. Regenerate with practice_player_retained_fixture.py.')
    out.write_text(text.replace('RetainedCompanion', 'RetainedPracticePlayer'), encoding='utf-8')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('log', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    practice_fixture(a.log, a.out)
