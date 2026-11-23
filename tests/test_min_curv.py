"""tools/min_curv.py gives trajectory_planning_helpers' opt_min_curv alpha, bit for bit."""
import math
from pathlib import Path
import sys
import unittest
import weakref

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
try:
    import numpy as np
    import trajectory_planning_helpers as tph
    import min_curv
except ImportError:   # a source kit before its setup
    tph = None


def track(n, closed, wobble):
    t = np.linspace(0, 2 * math.pi, n, endpoint=not closed) if closed else np.linspace(0, 1.4 * math.pi, n)
    r = 300 + 40 * np.sin(3 * t) + wobble * np.sin(11 * t)
    return np.column_stack((r * np.cos(t), 0.7 * r * np.sin(t)))


@unittest.skipIf(tph is None, 'numpy/trajectory_planning_helpers missing')
class MinCurv(unittest.TestCase):
    def check(self, closed, n, wobble):
        P = track(n, closed, wobble)
        if closed:
            _, _, A, nv = tph.calc_splines.calc_splines(path=np.vstack([P, P[:1]]))
            ps = pe = None
        else:
            head = lambda a, b: math.atan2(-(b[0] - a[0]), b[1] - a[1])   # noqa: E731
            ps, pe = head(P[0], P[1]), head(P[-2], P[-1])
            _, _, A, nv = tph.calc_splines.calc_splines(path=P, psi_s=ps, psi_e=pe)
            nv = np.vstack([nv, nv[-1:]]) if len(nv) == len(P) - 1 else nv
        widths = 2.5 + np.abs(np.sin(np.arange(len(P)) * .37))
        ref = np.column_stack((P, widths, widths[::-1]))
        for kb in (1 / 8.0, 1.0):
            a = tph.opt_min_curv.opt_min_curv(reftrack=ref, normvectors=nv, A=A, kappa_bound=kb, w_veh=0.0,
                                              closed=closed, psi_s=ps, psi_e=pe)[0]
            b = min_curv.opt_min_curv(reftrack=ref, normvectors=nv, A=A, kappa_bound=kb, w_veh=0.0,
                                      closed=closed, psi_s=ps, psi_e=pe)[0]
            self.assertEqual(a.tobytes(), b.tobytes())

    def test_closed(self):
        self.check(True, 180, 6.0)

    def test_open(self):
        self.check(False, 150, 9.0)

    def test_curvature_retry_reuses_geometry_and_matches_fresh_bound(self):
        from unittest import mock

        P = track(180, True, 6.0)
        _, _, A, nv = tph.calc_splines.calc_splines(path=np.vstack([P, P[:1]]))
        ref = np.column_stack((P, np.full(len(P), 2.5), np.full(len(P), 2.5)))
        inputs = dict(reftrack=ref, normvectors=nv, A=A, w_veh=0.0)
        expected, _ = min_curv.opt_min_curv(**inputs, kappa_bound=1.0)
        solve = min_curv.solve_qp
        invert = np.linalg.inv
        inverted = []

        def capture_inverse(matrix):
            inverse = invert(matrix)
            inverted.append(weakref.ref(inverse))
            return inverse

        def first_infeasible(*args):
            self.assertEqual(len(inverted), 1)
            self.assertIsNone(inverted[0](), 'dense inverse is still resident when solving the QP')
            if attempts.call_count == 1:
                raise ValueError('infeasible constraint')
            return solve(*args)

        with mock.patch.object(min_curv, 'solve_qp', side_effect=first_infeasible) as attempts, \
             mock.patch.object(np.linalg, 'inv', side_effect=capture_inverse) as inversions:
            result, _ = min_curv.opt_min_curv(**inputs, kappa_bound=.125, retry_kappa_bound=1.0)
        self.assertEqual(attempts.call_count, 2)
        self.assertEqual(inversions.call_count, 1)
        self.assertEqual(expected.tobytes(), result.tobytes())

    def test_time_limit(self):
        """A QP past its time limit raises QPTimeout instead of holding the
        generation for ever; within it, quadprog's own answer."""
        import quadprog
        import time
        from unittest import mock
        rng = np.random.default_rng(3)
        n = 400
        M = rng.standard_normal((n, n))
        H, a = M @ M.T + n * np.eye(n), rng.standard_normal(n)
        C, b = np.hstack((np.eye(n), -np.eye(n))), -np.full(2 * n, .01)
        self.assertEqual(min_curv.solve_qp(H, a, C, b).tobytes(), quadprog.solve_qp(H, a, C, b, 0)[0].tobytes())

        def stuck(*args):          # a solver that does not come back (it cycles)
            time.sleep(5)
        with mock.patch.object(min_curv.quadprog, 'solve_qp', stuck), self.assertRaises(min_curv.QPTimeout):
            min_curv.solve_qp(H, a, C, b, seconds=0.2)


if __name__ == '__main__':
    unittest.main()
