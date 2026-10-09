import copy
import unittest
import population_authority_evidence as evidence


def rows():
    events = [
        dict(seq='1', invocation='1', parent='0', context='1', kind='2', exit='0', ms='100', identityValid='1', depth='0', thread='42', fiber='0', isFiber='0', stackHigh='12345', returned='0', unwind='0', authority='UNKNOWN'),
        dict(seq='2', invocation='1', parent='0', context='1', kind='2', exit='1', ms='101', identityValid='1', depth='0', thread='42', fiber='0', isFiber='0', stackHigh='12345', returned='1', unwind='0', authority='UNKNOWN'),
    ]
    return events


def log(events, omit=None, drain=None):
    result = []
    for row in events:
        result.append(evidence.PREFIX + 'event ' + ' '.join(k + '=' + v for k, v in row.items()))
        for field, size in zip(evidence.FIELDS, evidence.LENGTHS):
            if (row['seq'], field) != omit:
                result.append(evidence.PREFIX + f"bytes seq={row['seq']} field={field} value=" + '0' * size)
    result.append(evidence.PREFIX + 'drain ' + (drain or 'frame=3 mask=4095 loss=0 openObserved=0 creationAuthority=0'))
    return '\n'.join(result)


class Evidence(unittest.TestCase):
    def test_complete_graph_never_grants_authority(self):
        result = evidence.analyze(log(rows()))
        self.assertTrue(result['receiptGraphComplete'])
        self.assertEqual(result['verdict'], 'INCONCLUSIVE')
        self.assertFalse(result['creationAuthority'])
        self.assertEqual(set(result['authority'].values()), {'UNKNOWN'})

    def test_missing_each_raw_chunk_refuses_graph_completeness(self):
        for seq in ('1', '2'):
            for field in evidence.FIELDS:
                self.assertFalse(evidence.analyze(log(rows(), (seq, field)))['receiptGraphComplete'])

    def test_missing_duplicate_and_replaced_terminal(self):
        original = rows()
        for mutant in (original[:1], original + [original[1]], [original[0], dict(original[1], context='2')],
                       [original[0], dict(original[1], parent='9')], [original[0], dict(original[1], ms='99')]):
            self.assertFalse(evidence.analyze(log(mutant))['receiptGraphComplete'])

    def test_loss_or_incomplete_mask_cannot_be_trimmed(self):
        for drain in ('frame=3 mask=1 loss=0 openObserved=0 creationAuthority=0',
                      'frame=3 mask=4095 loss=4 openObserved=0 creationAuthority=0',
                      'frame=3 mask=4095 loss=0 openObserved=1 creationAuthority=0'):
            self.assertFalse(evidence.analyze(log(rows(), drain=drain))['receiptGraphComplete'])

    def test_any_authority_claim_is_retained(self):
        mutant = copy.deepcopy(rows()); mutant[1]['authority'] = 'PROVEN'
        self.assertIn('unexpected authority claim', evidence.analyze(log(mutant))['issues'])


if __name__ == '__main__':
    unittest.main()
