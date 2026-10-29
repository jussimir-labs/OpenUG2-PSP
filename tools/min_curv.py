"""Minimum-curvature QP for the racing lines (tools/racing_line.py).

A modified copy of opt_min_curv() from trajectory_planning_helpers 0.79
(Alexander Heilmeier, Tim Stahl, Alexander Wischnewski, Levent Ögretmen,
Institute of Automotive Technology, TU Munich), licensed under the GNU
Lesser General Public License v3.0 (THIRD_PARTY.md; the license text ships
with the library in the runtime). Modified on 29 October 2026 for the
NFSU2 PSP generator:

- the products by the row-selection matrices A_ex_b and A_ex_c (one entry
  per row) and by the diagonal matrices (x', y', the curvature parts) are
  taken by indexing instead of dense matrix products. Each of those dense
  products has one non-zero term per entry, so it is that term (+0.0, as
  the BLAS sum starting at zero leaves a -0.0): the same numbers;
- an open track's last row, with more than one term, still comes from a
  dense product (of the last two rows: the kernel and sum of the whole
  product's, checked against it);
- the curvature error after the solution, which the generator does not
  use, is not computed (0.0 is returned in its place).

The QP and its constraints are the original's: the same alpha.
"""
import math

import numpy as np
import quadprog


def _rows(sel, A_inv, closed):
    """sel @ A_inv for a selection matrix (sel[i, 4i + k] = c, open: its last row dense)."""
    n = sel.shape[0]
    cols = [int(np.flatnonzero(sel[i])[0]) for i in range(n - (0 if closed else 1))]
    out = np.empty((n, A_inv.shape[1]))
    out[:len(cols)] = A_inv[cols] * sel[np.arange(len(cols)), cols][:, None].astype(float) + 0.0
    if not closed:
        # (the BLAS's sum for that row as in the whole product: two rows go
        # through the same matrix kernel; a single row takes a vector path,
        # summed in another order)
        out[-1:] = np.matmul(sel[-2:], A_inv)[-1:]
    return out


def _diag(m):
    return np.diagonal(m).copy()


def _dd(a, b):
    """diag @ diag."""
    return np.diag(_diag(a) * _diag(b) + 0.0)


def _dm(d, m):
    """diag @ matrix."""
    return _diag(d)[:, None] * m + 0.0


def opt_min_curv(reftrack: np.ndarray, normvectors: np.ndarray, A: np.ndarray, kappa_bound: float, w_veh: float,
                 closed: bool = True, psi_s: float = None, psi_e: float = None, fix_s: bool = False,
                 fix_e: bool = False) -> tuple:
    """opt_min_curv() of trajectory_planning_helpers: the lateral shift of
    every reference point (alpha) minimising the summed curvature inside the
    track's widths, curvature within kappa_bound. Returns (alpha, 0.0)."""
    no_points = reftrack.shape[0]
    no_splines = no_points if closed else no_points - 1
    if no_points != normvectors.shape[0]:
        raise RuntimeError("Array size of reftrack should be the same as normvectors!")
    if (no_points * 4 != A.shape[0] and closed) or (no_splines * 4 != A.shape[0] and not closed) \
            or A.shape[0] != A.shape[1]:
        raise RuntimeError("Spline equation system matrix A has wrong dimensions!")

    A_ex_b = np.zeros((no_points, no_splines * 4), dtype=int)
    for i in range(no_splines):
        A_ex_b[i, i * 4 + 1] = 1
    if not closed:
        A_ex_b[-1, -4:] = np.array([0, 1, 2, 3])
    A_ex_c = np.zeros((no_points, no_splines * 4), dtype=int)
    for i in range(no_splines):
        A_ex_c[i, i * 4 + 2] = 2
    if not closed:
        A_ex_c[-1, -4:] = np.array([0, 0, 2, 6])

    A_inv = np.linalg.inv(A)
    T_c = _rows(A_ex_c, A_inv, closed)
    T_b = _rows(A_ex_b, A_inv, closed)

    M_x = np.zeros((no_splines * 4, no_points))
    M_y = np.zeros((no_splines * 4, no_points))
    for i in range(no_splines):
        j = i * 4
        if i < no_points - 1:
            M_x[j, i] = normvectors[i, 0]
            M_x[j + 1, i + 1] = normvectors[i + 1, 0]
            M_y[j, i] = normvectors[i, 1]
            M_y[j + 1, i + 1] = normvectors[i + 1, 1]
        else:
            M_x[j, i] = normvectors[i, 0]
            M_x[j + 1, 0] = normvectors[0, 0]
            M_y[j, i] = normvectors[i, 1]
            M_y[j + 1, 0] = normvectors[0, 1]

    q_x = np.zeros((no_splines * 4, 1))
    q_y = np.zeros((no_splines * 4, 1))
    for i in range(no_splines):
        j = i * 4
        if i < no_points - 1:
            q_x[j, 0] = reftrack[i, 0]
            q_x[j + 1, 0] = reftrack[i + 1, 0]
            q_y[j, 0] = reftrack[i, 1]
            q_y[j + 1, 0] = reftrack[i + 1, 1]
        else:
            q_x[j, 0] = reftrack[i, 0]
            q_x[j + 1, 0] = reftrack[0, 0]
            q_y[j, 0] = reftrack[i, 1]
            q_y[j + 1, 0] = reftrack[0, 1]
    if not closed:
        q_x[-2, 0] = math.cos(psi_s + math.pi / 2)
        q_y[-2, 0] = math.sin(psi_s + math.pi / 2)
        q_x[-1, 0] = math.cos(psi_e + math.pi / 2)
        q_y[-1, 0] = math.sin(psi_e + math.pi / 2)

    x_prime = np.eye(no_points, no_points) * np.matmul(T_b, q_x)
    y_prime = np.eye(no_points, no_points) * np.matmul(T_b, q_y)
    x_prime_sq = np.power(x_prime, 2)
    y_prime_sq = np.power(y_prime, 2)
    x_prime_y_prime = -2 * _dd(x_prime, y_prime)
    curv_den = np.power(x_prime_sq + y_prime_sq, 1.5)
    curv_part = np.divide(1, curv_den, out=np.zeros_like(curv_den), where=curv_den != 0)
    curv_part_sq = np.power(curv_part, 2)
    P_xx = _dd(curv_part_sq, y_prime_sq)
    P_yy = _dd(curv_part_sq, x_prime_sq)
    P_xy = _dd(curv_part_sq, x_prime_y_prime)

    T_nx = np.matmul(T_c, M_x)
    T_ny = np.matmul(T_c, M_y)
    Pxx_Tnx, Pxy_Tnx, Pxy_Tny, Pyy_Tny = _dm(P_xx, T_nx), _dm(P_xy, T_nx), _dm(P_xy, T_ny), _dm(P_yy, T_ny)
    H_x = np.matmul(T_nx.T, Pxx_Tnx)
    H_xy = np.matmul(T_ny.T, Pxy_Tnx)
    H_y = np.matmul(T_ny.T, Pyy_Tny)
    H = H_x + H_xy + H_y
    H = (H + H.T) / 2

    f_x = 2 * np.matmul(np.matmul(q_x.T, T_c.T), Pxx_Tnx)
    f_xy = np.matmul(np.matmul(q_x.T, T_c.T), Pxy_Tny) + np.matmul(np.matmul(q_y.T, T_c.T), Pxy_Tnx)
    f_y = 2 * np.matmul(np.matmul(q_y.T, T_c.T), Pyy_Tny)
    f = np.squeeze(f_x + f_xy + f_y)

    Q_x = _dd(curv_part, y_prime)
    Q_y = _dd(curv_part, x_prime)
    E_kappa = _dm(Q_y, T_ny) - _dm(Q_x, T_nx)
    k_kappa_ref = _dm(Q_y, np.matmul(T_c, q_y)) - _dm(Q_x, np.matmul(T_c, q_x))
    con_ge = np.ones((no_points, 1)) * kappa_bound - k_kappa_ref
    con_le = -(np.ones((no_points, 1)) * -kappa_bound - k_kappa_ref)
    con_stack = np.append(con_ge, con_le)

    dev_max_right = reftrack[:, 2] - w_veh / 2
    dev_max_left = reftrack[:, 3] - w_veh / 2
    if not closed and fix_s:
        dev_max_left[0] = 0.05
        dev_max_right[0] = 0.05
    if not closed and fix_e:
        dev_max_left[-1] = 0.05
        dev_max_right[-1] = 0.05
    if np.any(-dev_max_right > dev_max_left) or np.any(-dev_max_left > dev_max_right):
        raise RuntimeError("Problem not solvable, track might be too small to run with current safety distance!")
    G = np.vstack((np.eye(no_points), -np.eye(no_points), E_kappa, -E_kappa))
    h = np.append(dev_max_right, dev_max_left)
    h = np.append(h, con_stack)
    alpha_mincurv = quadprog.solve_qp(H, -f, -G.T, -h, 0)[0]
    return alpha_mincurv, 0.0
