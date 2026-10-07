"""Offline actual PartyLeaf refusal/restore controls; no WinAPI/process use."""
import json,sys
from pathlib import Path
sys.dont_write_bytecode=True
from party_leaf import PartyLeaf, Refused, ORIGINAL, TARGET_RVA

def run(mode):
    row=bytearray(ORIGINAL);calls=[];logs=[];identity_ok=True
    kit_log='x [playerkit] native-Sora clone puppets REFUSED while KH2COOP_PLAYER_KIT=0x5a is set' if mode=='player_kit' else 'plain log'
    if mode=='wrong_original':row[1]=18
    def identity():
        if not identity_ok:raise Refused('identity lost')
    def poke(rva,value):
        assert rva==TARGET_RVA and value in (0,1)
        calls.append((rva,value));row[1]=value
        if mode=='post_write_exception' and value==0:raise TimeoutError('child may have written')
        if mode=='failed_no_write' and value==0:row[1]=1;return {'ok':False}
        return {'ok':True}
    leaf=PartyLeaf(lambda:bytes(row),identity,poke,lambda *x:logs.append(x),lambda:kit_log)
    replace_error=restore_error=None
    try:leaf.replace()
    except Exception as e:replace_error=type(e).__name__
    if mode=='changed_other':row[2]=7
    if mode=='changed_target':row[1]=2
    if mode=='identity_loss':identity_ok=False
    try:leaf.restore()
    except Exception as e:restore_error=type(e).__name__
    if mode in ('success','post_write_exception'):assert row==ORIGINAL and calls==[(TARGET_RVA,0),(TARGET_RVA,1)] and restore_error is None
    if mode in ('wrong_original','player_kit'):assert replace_error and calls==[]
    if mode=='failed_no_write':assert replace_error and row==ORIGINAL and calls==[(TARGET_RVA,0)]
    if mode=='changed_other':assert restore_error and row[1]==1 and row[2]==7 and calls[-1]==(TARGET_RVA,1)
    if mode in ('changed_target','identity_loss'):assert restore_error and calls==[(TARGET_RVA,0)]
    try:leaf.replace()
    except Refused:pass
    else:
        if mode not in ('wrong_original','player_kit'):raise AssertionError('repeat admitted')
    return {'mode':mode,'calls':calls,'row':list(row),'replaceError':replace_error,'restoreError':restore_error}
if __name__=='__main__':
    rows=[run(m) for m in ('success','wrong_original','failed_no_write','post_write_exception','changed_other','changed_target','identity_loss','player_kit')]
    print(json.dumps({'ok':True,'controls':rows,'scope':'actual leaf with synthetic memory/CLI only'},indent=2))
