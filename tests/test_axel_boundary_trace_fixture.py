"""VUH-2897: only the dedicated trace fixture opts into native boundary hooks."""
import copy
import importlib.util
import json
import tempfile
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
FOLDER=ROOT/'tools/scenario/scenarios'
FLAG='KH2COOP_AXEL_BOUNDARY_TRACE'

class AxelTraceFixtureTest(unittest.TestCase):
    def test_only_the_dedicated_scenario_sets_the_flag(self):
        opted_in=[]
        for path in FOLDER.glob('*.json'):
            data=json.loads(path.read_text(encoding='utf-8'))
            if any(FLAG in step.get('env',{}) for step in data.get('steps',[])):
                opted_in.append(path.name)
        self.assertEqual(opted_in,['net_prologue_mansion_axel_trace.json'])

    def test_route_and_native_intro_steps_match_the_base(self):
        base=json.loads((FOLDER/'net_prologue_mansion_hall.json').read_text())
        trace=json.loads((FOLDER/'net_prologue_mansion_axel_trace.json').read_text())
        expected=copy.deepcopy(base['steps'])
        for step in expected:
            if step['do']=='boot': step['env'][FLAG]='1'
        actual=trace['steps']
        self.assertEqual(actual[:2]+actual[3:-2],expected)
        self.assertEqual(trace['protect'],base['protect'])
        self.assertEqual(trace['playerDefaults'],base['playerDefaults'])
        self.assertIn('configured=1',actual[2]['expr'])
        self.assertEqual(actual[-2]['do'],'wait_until')
        self.assertEqual(actual[-2]['timeoutMs'],40000)
        self.assertIn('lossObserved=0 missedArms=0',actual[-1]['expr'])
        self.assertIn('site=load-task-start ',actual[-1]['expr'])

    def test_production_footer_predicate_rejects_missing_or_censored_trace(self):
        spec=importlib.util.spec_from_file_location('axel_trace_runner',ROOT/'tools/scenario/run.py')
        runner=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        fixture=json.loads((FOLDER/'net_prologue_mansion_axel_trace.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            old=runner.LOGS
            runner.LOGS=directory
            self.addCleanup(setattr,runner,'LOGS',old)
            ctx=runner.Context(directory)
            self.addCleanup(ctx._stop.set)
            logs=[]
            for index,pid in enumerate((920011,920012)):
                inst=runner.Instance(index,pid)
                inst.expect_exit=True
                ctx.instances.append(inst)
                logs.append(directory/f'kh2coop_inject_{pid}.log')
            good=('\n[axel-boundary] configured=1 diagnosticOnly=1\n[axel-boundary] site=load-task-start seq=1\n'
                  '[axel-boundary] site=load-finalizers-returned seq=2\n'
                  '[axel-boundary] stop reason=trace-deadline ms=40000 elapsedMs=30000 '
                  'lines=100 dropped=0 lossObserved=0 missedArms=0 windowOnly=1\n')
            for log in logs: log.write_text(good)
            self.assertTrue(ctx.eval(fixture['steps'][2]['expr']))
            self.assertTrue(ctx.eval(fixture['steps'][-1]['expr']))
            for invalid in ('',good.replace('lossObserved=0','lossObserved=1'),
                            good.replace('trace-deadline','trace-budget'),
                            good.replace('site=load-task-start','site=not-load-start'),
                            good.replace('missedArms=0','missedArms=1')):
                logs[1].write_text(invalid)
                self.assertFalse(ctx.eval(fixture['steps'][-1]['expr']),invalid)

if __name__=='__main__': unittest.main()
