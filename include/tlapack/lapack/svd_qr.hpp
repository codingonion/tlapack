/// @file svd_qr.hpp
/// @author Thijs Steel, KU Leuven, Belgium
/// Adapted from @see
/// https://github.com/Reference-LAPACK/lapack/tree/master/SRC/zgesvd.f
//
// Copyright (c) 2021-2023, University of Colorado Denver. All rights reserved.
//
// This file is part of <T>LAPACK.
// <T>LAPACK is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#ifndef TLAPACK_SVD_QR_HH
#define TLAPACK_SVD_QR_HH

#include <algorithm>

#include "tlapack/base/utils.hpp"
#include "tlapack/blas/gemm.hpp"
#include "tlapack/blas/iamax.hpp"
#include "tlapack/blas/lartg.hpp"
#include "tlapack/blas/rot.hpp"
#include "tlapack/blas/swap.hpp"
#include "tlapack/lapack/gebrd.hpp"
#include "tlapack/lapack/singularvalues22.hpp"
#include "tlapack/lapack/svd22.hpp"
#include "tlapack/lapack/ungbr.hpp"

namespace tlapack {

/**
 * Computes the singular values and, optionally, the right and/or
 * left singular vectors from the singular value decomposition (SVD) of
 * a real N-by-N (upper or lower) bidiagonal matrix B using the implicit
 * zero-shift QR algorithm. The SVD of B has the form
 *      B = Q * S * P**T
 * where S is the diagonal matrix of singular values, Q is an orthogonal
 * matrix of left singular vectors, and P is an orthogonal matrix of
 * right singular vectors.  If left singular vectors are requested, this
 * subroutine actually returns U*Q instead of Q, and, if right singular
 * vectors are requested, this subroutine returns P**T*VT instead of
 * P**T, for given real input matrices U and VT.  When U and VT are the
 * orthogonal matrices that reduce a general matrix A to bidiagonal
 * form:  A = U*B*VT, as computed by gebrd, then
 *      A = (U*Q) * S * (P**T*VT)
 * is the SVD of A.
 *
 * See "Computing  Small Singular Values of Bidiagonal Matrices With
 * Guaranteed High Relative Accuracy," by J. Demmel and W. Kahan,
 * LAPACK Working Note #3 (or SIAM J. Sci. Statist. Comput. vol. 11,
 * no. 5, pp. 873-912, Sept 1990) and
 * "Accurate singular values and differential qd algorithms," by
 * B. Parlett and V. Fernando, Technical Report CPAM-554, Mathematics
 * Department, University of California at Berkeley, July 1992
 * for a detailed description of the algorithm.
 *
 * @return  0 if success
 *
 * @param[in] uplo
 *      Uplo::Upper, B is upper bidiagonal
 *      Uplo::Lower, B is lower bidiagonal
 *
 * @param[in] want_u bool
 *
 * @param[in] want_vt bool
 *
 * @param[in,out] d Real vector of length n.
 *      On entry, diagonal elements of the bidiagonal matrix B.
 *      On exit, the singular values of B in decreasing order.
 *
 * @param[in,out] e Real vector of length n-1.
 *      On entry, off-diagonal elements of the bidiagonal matrix B.
 *      On exit, the singular values of B in decreasing order.
 *
 * @param[in,out] U nu-by-m matrix.
 *      On entry, an nu-by-n unitary matrix.
 *      On exit, U is overwritten by U * Q.
 *
 * @param[in,out] Vt n-by-nvt matrix.
 *      On entry, an n-by-nvt unitary matrix.
 *      On exit, Vt is overwritten by P^H * Vt.
 *
 * @ingroup computational
 */
template <class matrix_t, class d_t, class e_t>
int svd_qr(Uplo uplo,
           bool want_u,
           bool want_vt,
           d_t& d,
           e_t& e,
           matrix_t& U,
           matrix_t& Vt)
{
    using idx_t = size_type<matrix_t>;
    using pair = pair<idx_t, idx_t>;
    using T = type_t<matrix_t>;
    using real_t = real_type<T>;

    // constants
    const real_t one(1);
    const real_t zero(0);
    const idx_t n = size(d);
    const real_t eps = ulp<real_t>();
    const real_t small_num = safe_min<real_t>() / ulp<real_t>();
    const real_t tol = real_t(10.0) * eps;

    // Quick return
    if (n == 0) return 0;

    // If the matrix is lower bidiagonal, apply a sequence of rotations
    // to make it upper bidiagonal.
    if (uplo == Uplo::Lower) {
        real_t c, s, r;

        for (idx_t i = 0; i < n - 1; ++i) {
            lartg(d[i], e[i], c, s, r);
            d[i] = r;
            e[i] = s * d[i + 1];
            d[i + 1] = c * d[i + 1];

            // Update singular vectors if desired
            if (want_u) {
                auto u1 = col(U, i);
                auto u2 = col(U, i + 1);
                rot(u1, u2, c, s);
            }
        }
    }

    idx_t itmax = 30 * n;

    // istart and istop determine the active block
    idx_t istart = 0;
    idx_t istop = n;

    // Main loop
    for (idx_t iter = 0; iter <= itmax; ++iter) {
        if (iter == itmax) {
            // The QR algorithm failed to converge, return with error.
            return istop;
        }

        if (istop <= 1) {
            // All eigenvalues have been found, exit and return 0.
            break;
        }

        // Find active block
        for (idx_t i = istop - 1; i > istart; --i) {
            if (abs(e[i - 1]) <= tol * abs(d[i])) {
                e[i - 1] = zero;
                istart = i;
                break;
            }
        }

        // A singular value has split off, reduce istop and start the loop again
        if (istart == istop - 1) {
            istop = istop - 1;
            istart = 0;
            continue;
        }

        // A 2x2 block has split off, handle separately
        if (istart + 1 == istop - 1) {
            real_t csl, snl, csr, snr, sigmn, sigmx;
            svd22(d[istart], e[istart], d[istart + 1], sigmn, sigmx, csl, snl,
                  csr, snr);
            d[istart] = sigmx;
            d[istart + 1] = sigmn;
            e[istart] = zero;

            // Update singular vectors if desired
            if (want_u) {
                auto u1 = col(U, istart);
                auto u2 = col(U, istart + 1);
                rot(u1, u2, csl, snl);
            }
            if (want_vt) {
                auto vt1 = row(Vt, istart);
                auto vt2 = row(Vt, istart + 1);
                rot(vt1, vt2, csr, snr);
            }

            istop = istop - 2;
            istart = 0;
            continue;
        }

        // TODO: choose direction

        // TODO: figure out convergence tests

        // Compute shift.  First, test if shifting would ruin relative
        // accuracy, and if so set the shift to zero.
        real_t shift;
        if (false) {  // TODO
            shift = zero;
        }
        else {
            // Compute the shift from 2-by-2 block at end of matrix
            real_t sstart = abs(d[istart]);
            real_t temp;
            singularvalues22(d[istop - 1], e[istop - 1], d[istop], shift, temp);

            // Test if shift negligible, and if so set to zero
            if (sstart > zero and pow(shift / sstart, 2) < eps) shift = zero;
        }

        if (shift == zero) {
            // If shift = 0, do simplified QR iteration, this is better for the
            // relative accuracy of small singular values
            real_t r, cs, sn, oldcs, oldsn;
            cs = one;
            sn = zero;
            oldcs = one;
            oldsn = zero;
            for (idx_t i = istart; i < istop - 1; ++i) {
                lartg(d[i] * cs, e[i], cs, sn, r);
                if (i > istart) e[i - 1] = oldsn * r;
                lartg(oldcs * r, d[i + 1] * sn, oldcs, oldsn, d[i]);

                // Update singular vectors if desired
                if (want_u) {
                    auto u1 = col(U, i);
                    auto u2 = col(U, i + 1);
                    rot(u1, u2, oldcs, oldsn);
                }
                if (want_vt) {
                    auto vt1 = row(Vt, i);
                    auto vt2 = row(Vt, i + 1);
                    rot(vt1, vt2, cs, sn);
                }
            }
            real_t h = d[istop - 1] * cs;
            d[istop - 1] = h * oldcs;
            e[istop - 2] = h * oldsn;
        }
        else {
            // Use nonzero shift
            real_t f = (abs(d[istart]) - shift) *
                       (copysign(one, d[istart]) + shift / d[istart]);
            real_t g = e[istart];
            for (idx_t i = istart; i < istop - 1; ++i) {
                real_t r, csl, snl, csr, snr;
                lartg(f, g, csr, snr, r);
                if (i > istart) e[i - 1] = r;
                f = csr * d[i] + snr * e[i];
                e[i] = csr * e[i] - snr * d[i];
                g = snr * d[i + 1];
                d[i + 1] = csr * d[i + 1];

                lartg(f, g, csl, snl, r);
                d[i] = r;
                f = csl * e[i] + snl * d[i + 1];
                d[i + 1] = csl * d[i + 1] - snl * e[i];
                if (i + 1 < istop - 1) {
                    g = snl * e[i + 1];
                    e[i + 1] = csl * e[i + 1];
                }

                // Update singular vectors if desired
                if (want_u) {
                    auto u1 = col(U, i);
                    auto u2 = col(U, i + 1);
                    rot(u1, u2, csl, snl);
                }
                if (want_vt) {
                    auto vt1 = row(Vt, i);
                    auto vt2 = row(Vt, i + 1);
                    rot(vt1, vt2, csr, snr);
                }
            }
            e[istop - 2] = f;
        }
    }

    // All singular values converged, so make them positive
    for (idx_t i = 0; i < n; ++i) {
        if (d[i] < zero) {
            d[i] = -d[i];
            if (want_vt) {
                auto vt1 = row(Vt, i);
                scal(-one, vt1);
            }
        }
    }

    // Sort the singular values into decreasing order.
    for (idx_t i = 0; i < n - 1; ++i) {
        auto d2 = slice(d, pair{i, n});
        idx_t imax = i + iamax(d2);
        if (imax != i) {
            std::swap(d[imax], d[i]);

            if (want_u) {
                auto u1 = col(U, imax);
                auto u2 = col(U, i);
                tlapack::swap(u1, u2);
            }
            if (want_vt) {
                auto vt1 = row(Vt, imax);
                auto vt2 = row(Vt, i);
                tlapack::swap(vt1, vt2);
            }
        }
    }

    return 0;
}

}  // namespace tlapack

#endif  // TLAPACK_SVD_QR_HH
