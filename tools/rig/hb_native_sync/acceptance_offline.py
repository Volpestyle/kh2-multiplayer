"""Run source6 replay/controls with native/process/network boundaries denied."""
import argparse,json,sys
from pathlib import Path
from unittest.mock import patch

def run(packet):
    import archive_format,fixture,native_read,native_sync_controls,native_sync_scope_controls,native_sync_retained_controls,native_sync_media_controls,native_sync_budget_controls,product_controls,setup_controls
    def denied(*a,**kw):raise AssertionError('offline controls attempted native/process/network work')
    with patch('subprocess.Popen',denied),patch('socket.socket',denied),patch.object(native_read,'Reader',denied),patch.object(native_read,'Mapping',denied):
        checks=native_sync_controls.run()+native_sync_scope_controls.run(packet)+native_sync_retained_controls.run(packet)+product_controls.run()+native_sync_media_controls.run(packet)+native_sync_budget_controls.run(packet)
        source=archive_format.entries((packet/'save_sandbox_prepared/source-copy.png').read_bytes())[4][3]
        checks+=setup_controls.run(packet,source,fixture.START)
    fixture.verify(packet)
    return dict(status='PASS',liveExecuted=False,nativeProcessNetworkDenied=True,checks=checks,sourcePinsVerified=True)

if __name__=='__main__':
    sys.dont_write_bytecode=True
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    parser=argparse.ArgumentParser();parser.add_argument('--packet',type=Path,required=True);a=parser.parse_args()
    packet=a.packet.resolve();sys.path.insert(0,str(packet/'dependencies'))
    print(json.dumps(run(packet),indent=2))
