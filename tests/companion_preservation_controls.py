"""Small offline source/fixture preservation checks, not compiled C++ tests."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
from companion_retained_fixture import generate
BASE='19aae6cd00e4b36207772f5ca7ca47db3f3d4bc6'
ROOT=Path(__file__).resolve().parents[1]
def original(path):return subprocess.check_output(['git','show',BASE+':'+path],cwd=ROOT).decode('utf-8').replace('\r\n','\n')
def current(path):return (ROOT/path).read_text(encoding='utf-8')
def body(text,name,next_name):return text[text.index(name):text.index(next_name,text.index(name))]
def run(logs):
    checks=[]
    for path in ['inject/src/EntityHook.cpp','inject/src/DamagePolicy.hpp','inject/src/DamagePolicy.cpp']:
        assert original(path)==current(path),path
        checks.append(path+' byte-equivalent after CRLF normalization')
    path='inject/src/CombatCausalTrace.hpp';old=original(path);new=current(path)
    assert body(old,'inline bool PlayerEnemyHit','inline std::uint64_t Digest')==body(new,'inline bool PlayerEnemyHit','// Separate host-only')
    checks.append('PlayerEnemyHit unchanged')
    guard='if(l&&!r.locallyQualified&&e.before.victim.hp!=e.after.victim.hp)Retire(s,now,Reason::UnknownDelta);'
    assert guard in old and guard in new;checks.append('UnknownDelta guard unchanged')
    path='tests/CombatCausalTraceTest.cpp';old=original(path);new=current(path)
    assert body(old,' for(unsigned mutant=0;mutant<15;',' local.Hit(s,hit,3);')==body(new,' for(unsigned mutant=0;mutant<15;',' local.Hit(s,hit,3);')
    checks.append('existing 15 player mutants unchanged')
    with tempfile.TemporaryDirectory() as tmp:
        header=Path(tmp)/'fixture.hpp';generate(logs,header)
        assert header.read_bytes()==(ROOT/'tests/CompanionRetainedFixture.hpp').read_bytes()
        assert header.with_suffix('.json').read_bytes()==(ROOT/'tests/CompanionRetainedFixture.json').read_bytes()
    checks.append('both retained cases regenerate byte-identical')
    print(json.dumps(dict(ok=True,checks=checks,nativeExecuted=False,compiledControls='PENDING_STEAM_NATIVE_SLOT'),indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('logs',nargs=2,type=Path);run(parser.parse_args().logs)
