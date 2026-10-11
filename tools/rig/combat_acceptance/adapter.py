"""Profile-owned sample/dispatch projection onto sealed packet services.

Import starts nothing. Services provide original identities/handles, authenticated
transport, raw log reads, deadline-bounded native helpers and existing cleanup.
They must NOT delegate sample or dispatch to the inherited Encounter methods.
"""
import copy
from contextlib import contextmanager
import json
import time
from . import profile, summary_acquisition


class DriverCalls:
    """Real packet read/input leaves; mix into its transport/identity services.

    phase.child MUST be the installed original Reclaim.child, not phase's
    inherited fixed-timeout subprocess fallback. No source/build selection here.
    """
    def __init__(self, driver, phase, diagnostic):
        self.driver, self.phase, self.diagnostic = driver, phase, diagnostic
        if (getattr(phase.child, '__self__', None) is not driver.reclaim or
                getattr(phase.child, '__func__', None) is not type(driver.reclaim).child):
            raise profile.Refused('deadline-bounded owned Reclaim.child must be installed')

    @contextmanager
    def bounded(self, deadline_ns):
        d = self.driver
        original = d.deadline
        left = (deadline_ns - self.clock_ns()) / 1_000_000_000
        if left <= 0: raise profile.Refused('native child deadline expired')
        # Duration conversion only: QPC and monotonic may have different epochs.
        mono = time.monotonic()
        if mono >= original:raise profile.Refused('original run deadline expired')
        d.deadline = min(original, mono + left)
        try:
            yield
            if self.clock_ns() >= deadline_ns:
                raise profile.Refused('native child exceeded original sample deadline')
        finally:
            d.deadline = original

    def read_logs(self, *, deadline_ns):
        with self.bounded(deadline_ns):return self.diagnostic.raw(self.driver)

    def read_owned(self, *, deadline_ns):
        with self.bounded(deadline_ns):
            return self.diagnostic.owned(self.driver, self.owned['moduleBase'])

    def native(self, name, *, deadline_ns):
        with self.bounded(deadline_ns):
            d = self.driver
            if name == 'avatarObserve':
                return json.loads(self.phase.child([d.package/'bin/avatarctl.exe','observe',
                    '--pid',str(self.owned['pid']),'--samples','1','--interval-ms','0'],cwd=d.package))
            command = name.removesuffix('Before').removesuffix('After')
            if command not in ('peek','state','entities'):
                raise profile.Refused('undeclared native sample operation')
            args = ('--rva', profile.population.PEEK_SPEC) if command == 'peek' else ()
            return d.call(command, *args)

    def wait_tick(self, *, deadline_ns):
        self.check_stop_and_ownership(self.owned)
        left = (deadline_ns-self.clock_ns())/1_000_000_000
        if left <= 0:raise profile.Refused('summary wait expired')
        time.sleep(min(.025,left))

    def call_input(self, argv, *, deadline_ns):
        with self.bounded(deadline_ns):return self.driver.call(*argv)


class PacketAdapter:
    def __init__(self, services, declared_contract):
        self.s = services
        # Refuse stale packet configuration before any native/transport operation.
        if declared_contract != profile.adapter_contract():
            raise profile.Refused('packet sample/action contract differs from source profile')
        self.contract = copy.deepcopy(declared_contract)
        self.history = []
        self.issued = set()
        self.last_sample = None

    def __getattr__(self, name):
        return getattr(self.s, name)

    def check(self, end):
        if self.contract != profile.adapter_contract():
            raise profile.Refused('profile contract changed after adapter admission')
        self.s.check_stop_and_ownership(self.s.owned)
        if self.s.clock_ns() >= end:
            raise profile.Refused('original adapter operation deadline expired')

    def take(self, name, end, *args):
        self.check(end)
        result = getattr(self.s, name)(*args, deadline_ns=end)
        self.check(end)
        return result

    def sample(self, deadline_ns):
        end = min(deadline_ns, self.s.deadline_ns, self.s.clock_ns()+profile.SAMPLE_BUDGET_NS)
        baseline=copy.deepcopy(self.last_sample)
        if baseline:baseline.pop('summaryAcquisition',None)
        previous=baseline
        row={}
        self.check(end)
        try:
            for _ in range(64):
                self.check(end)
                row=dict(readStartedNs=self.s.clock_ns())
                before, runtime_before = self.take('read_logs', end)
                row.update(injectBefore=before, runtimeBefore=runtime_before)
                row['ownedBefore'] = self.take('read_owned', end)
                for name in ('peekBefore','stateBefore','entitiesBefore','avatarObserve',
                             'stateAfter','entitiesAfter','peekAfter'):
                    row[name] = self.take('native', end, name)
                row['readFinishedNs'] = self.s.clock_ns()
                after, runtime_after = self.take('read_logs', end)
                row.update(injectAfter=after,runtimeAfter=runtime_after,
                           ownedAfter=self.take('read_owned',end))
                self.check(end)
                row['operationFinishedNs']=self.s.clock_ns()
                profile.need(after.startswith(before),'sample log prefix replaced/truncated')
                if previous:
                    profile.need(before.startswith(previous['injectAfter']),
                                 'inter-bracket log prefix replaced/truncated')
                admitted=[r for r in profile.population.receipt_rows(after)
                          if profile.population.number(r,'kind')==0]
                # The immutable prior bracket covers BOTH gap and in-bracket
                # rows. Without it, direct acquisition uses its before prefix.
                # No last-row fast path may conceal an earlier contradictory row.
                ready=len(admitted)!=5
                if len(admitted)==5:
                    if baseline:
                        proof=summary_acquisition.qualify(row,baseline)
                        if proof:
                            row['summaryAcquisition']=proof
                            ready=True
                    else:ready=summary_acquisition.in_bracket(row)
                if ready:
                    self.last_sample=copy.deepcopy(row)
                    return row
                if baseline is None:baseline=copy.deepcopy(row)
                previous=copy.deepcopy(row)
                self.take('wait_tick', end)
            raise profile.Refused('summary acquisition attempt cap')
        except BaseException as error:
            # Retention must not rescue an expired operation or dispatch input.
            row.update(error=repr(error), deadlineNs=end)
            self.s.retain('adapter-sample-failure', row)
            raise

    def action_acked(self, action, result):
        if len(self.history) >= len(profile.ACTIONS):
            raise profile.Refused('extra action ACK')
        expected, role, argv = profile.ACTIONS[len(self.history)]
        profile.authorize(action, role, argv, self.history)
        if expected != action or result.get('ok') is not True:
            raise profile.Refused('action ACK mismatch')
        if role == self.s.role and action not in self.issued:
            raise profile.Refused('local ACK without a dispatch attempt')
        self.history.append(action)

    def dispatch_once(self, action, argv, final_gate, deadline_ns):
        profile.authorize(action, self.s.role, argv, self.history)
        if action in self.issued:
            raise profile.Refused('dispatch already spent; never repeat')
        self.check(deadline_ns)
        self.issued.add(action)  # Even a gate failure/ACK loss spends this dispatch.
        d = self.s.driver
        if any(getattr(d,n,None) is not None for n in
               ('diagnostic_dispatch','diagnostic_before_child','diagnostic_stamp')):
            raise profile.Refused('another diagnostic dispatch owns the driver hooks')
        attempt = dict(actionId=action, role=self.s.role, argv=list(argv), status='PREPARED')
        self.s.retain('attempt-'+action+'-prepared', attempt)
        stamped = []
        gated = []

        def gate():
            if gated:raise profile.Refused('repeated final input gate')
            self.check(deadline_ns)
            self.s.prepare_media(action, deadline_ns=deadline_ns)
            final_gate()  # Source native/health gate AFTER blocking media work.
            self.check(deadline_ns)
            gated.append(True)

        def stamp():
            # Called by production process_journal.wrap at actual helper spawn.
            # Media observation can read files, so do it BEFORE final QPC stamp.
            if not gated:raise profile.Refused('actual spawn before final native gate')
            self.s.media_pre_spawn(action)
            self.s.check_cancel_memory_only()
            now = self.s.clock_ns()
            if now >= deadline_ns or stamped:
                raise profile.Refused('expired or repeated actual helper stamp')
            stamped.append(now)  # Memory only; no log/network operation here.

        d.diagnostic_dispatch = tuple(argv)
        d.diagnostic_before_child = gate
        d.diagnostic_stamp = stamp
        try:
            # Services cap the real Driver/child timeout to the supplied original
            # QPC deadline. No inherited Encounter.dispatch/old action table.
            result = self.s.call_input(tuple(argv), deadline_ns=deadline_ns)
            if len(stamped) != 1 or result.get('ok') is not True:
                raise profile.Refused('actual dispatch stamp or input ACK missing')
            attempt.update(status='ACKED', dispatchNs=stamped[0], result=result)
            self.s.retain('attempt-'+action+'-acked', attempt)
            return dict(ok=True, actionId=action, dispatchNs=stamped[0], result=result)
        except BaseException as error:
            attempt.update(status='FAILED', error=repr(error), stamps=stamped)
            self.s.retain('attempt-'+action+'-failed', attempt)
            raise
        finally:
            d.diagnostic_dispatch = d.diagnostic_before_child = d.diagnostic_stamp = None
