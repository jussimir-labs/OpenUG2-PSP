"""tools/min_curv.py gives trajectory_planning_helpers' opt_min_curv alpha, bit for bit."""
import math
from pathlib import Path
import sys
import unittest

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


if __name__ == '__main__':
    unittest.main()
