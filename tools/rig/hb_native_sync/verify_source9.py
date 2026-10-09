"""Verify the landing projection against the independently reviewed source9 seal."""
import argparse,ast,difflib,hashlib,json
from pathlib import Path

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(packet):
    landing=Path(__file__).resolve().parent
    source=packet/'source/tools/rig/tt_hb_worldmap'
    proof=json.loads((landing/'source9-provenance.json').read_text(encoding='utf-8'))
    assert sha(packet/'pins.json')==proof['source9PinsSha256'],'different source9 seal'
    pins=json.loads((packet/'pins.json').read_text(encoding='utf-8'))
    for row in pins['files']:
        path=packet/row['path']
        assert path.stat().st_size==row['bytes']and sha(path)==row['sha256'],row['path']
    for row in proof['files']:
        before,after=source/row['path'],landing/row['path']
        assert sha(before)==row['source9Sha256']and sha(after)==row['landingSha256'],row['path']
        assert (before.read_bytes()==after.read_bytes())is row['byteIdentical']
    for name,projection in proof['projections'].items():
        before=(source/name).read_text(encoding='utf-8');after=(landing/name).read_text(encoding='utf-8')
        def functions(text):
            result={}
            def walk(node,prefix=''):
                for child in getattr(node,'body',[]):
                    if isinstance(child,(ast.FunctionDef,ast.AsyncFunctionDef)):
                        result[prefix+child.name]=ast.get_source_segment(text,child)
                        walk(child,prefix+child.name+'.')
            walk(ast.parse(text));return result
        old,new=functions(before),functions(after)
        for key,row in projection['byteIdenticalFunctions'].items():
            assert old[key]==new[key]and hashlib.sha256(new[key].encode()).hexdigest()==row['sha256'],key
        if name=='fixture.py':
            assert before[before.index('def install('):before.index('    def route(')]==after[after.index('def install('):after.index('    def route(')]
            assert proof['acceptedRouteClauseByteIdentical']in before and proof['acceptedRouteClauseByteIdentical']in after
    diff=[]
    for name in proof['projections']:
        diff+=difflib.unified_diff((source/name).read_text().splitlines(True),(landing/name).read_text().splitlines(True),fromfile='source9/'+name,tofile='landing/'+name)
    assert ''.join(diff)==(landing/'source9-landing.diff').read_text(encoding='utf-8'),'projection diff changed'
    return dict(status='PASS',source9Files=len(proof['files']),byteIdenticalFiles=sum(r['byteIdentical']for r in proof['files']),projectedFiles=list(proof['projections']),nativeExecuted=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-packet',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(verify(args.source_packet.resolve())))
