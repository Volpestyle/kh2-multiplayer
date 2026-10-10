"""Owned no-console control only, never imports the game/launcher."""
import os,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
root=Path(sys.argv[1]);mode=sys.argv[2]
if mode=='none':sys.stdout=sys.stderr=None
elif mode=='invalid':
    for fd in (1,2):
        try:os.close(fd)
        except OSError:pass
assert 'logging' not in sys.modules
from winplumb.bootstrap import bootstrap,configure_logging,child_stdio
log,invalid=bootstrap(root/'worker.raw.log');assert invalid
configure_logging(root/'worker.raw.log')
import json,logging,subprocess
(root/'waiting.json').write_text(json.dumps({'pid':os.getpid(),'recovered':invalid}),encoding='utf-8')
print('WAITING_AFTER_FILE')
logging.error('FILE_HANDLER_ONLY')
subprocess.run([sys.executable,'-c',"import os;os.write(2,b'CHILD_STDERR_FULL\\n')"],check=True,**child_stdio(log))
print('UNICODE_✓',file=sys.stderr)
raise RuntimeError('FINAL_TRACEBACK_MARKER')
