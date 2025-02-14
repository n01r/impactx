/* Copyright 2022-2023 The Regents of the University of California, through Lawrence
 *           Berkeley National Laboratory (subject to receipt of any required
 *           approvals from the U.S. Dept. of Energy). All rights reserved.
 *
 * This file is part of ImpactX.
 *
 * Authors: Chad Mitchell, Axel Huebl
 * License: BSD-3-Clause-LBNL
 */
#ifndef IMPACTX_EXACTSBEND_H
#define IMPACTX_EXACTSBEND_H

#include "particles/ImpactXParticleContainer.H"
#include "mixin/beamoptic.H"
#include "mixin/thick.H"
#include "mixin/nofinalize.H"

#include <AMReX_Extension.H>
#include <AMReX_REAL.H>
#include <AMReX_Print.H>      // for PrintToFile

#include <cmath>


namespace impactx
{
    struct ExactSbend
    : public elements::BeamOptic<ExactSbend>,
      public elements::Thick,
      public elements::NoFinalize
    {
        static constexpr auto name = "ExactSbend";
        static constexpr amrex::ParticleReal degree2rad = ablastr::constant::math::pi / 180.0;
        using PType = ImpactXParticleContainer::ParticleType;

        /** The body of an ideal sector bend, using the exact nonlinear transfer map.
         *  The map corresponds to the map described in:
         *  D. L. Bruhwiler et al, in Proc. of EPAC 98, pp. 1171-1173 (1998).
         *
         *  In the ultrarelativistic limit, this is equivalent to the map described in:
         *  E. Forest et al, Part. Accel. 45, pp. 65-94 (1994).
         *
         *  The model consists of a uniform bending field B_y with a hard edge.
         *  Pole faces are normal to the entry and exit velocity of the reference particle.
         *
         * @param ds Segment length in m.
         * @param phi Segment bend angle in degrees.
         * @param B Magnetic field strength in T.
         *  When B = 0 (default), the reference bending radius is defined by r0 = length / (angle in rad), corresponding to
         *  a magnetic field of B = rigidity / r0; otherwise the reference bending radius is defined by r0 = rigidity / B.
         * @param nslice number of slices used for the application of space charge
         */
        ExactSbend( amrex::ParticleReal const ds, amrex::ParticleReal const phi,
              amrex::ParticleReal const B, int const nslice )
        : Thick(ds, nslice), m_phi(phi * degree2rad), m_B(B)
        {
        }

        /** Push all particles */
        using BeamOptic::operator();

        /** This is an ExactSbend functor, so that a variable of this type can be used like an ExactSbend function.
         *
         * @param p Particle AoS data for positions and cpu/id
         * @param px particle momentum in x
         * @param py particle momentum in y
         * @param pt particle momentum in t
         * @param refpart reference particle
         */
        AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
        void operator() (
                PType& AMREX_RESTRICT p,
                amrex::ParticleReal & AMREX_RESTRICT px,
                amrex::ParticleReal & AMREX_RESTRICT py,
                amrex::ParticleReal & AMREX_RESTRICT pt,
                RefPart const & refpart) const {

            using namespace amrex::literals; // for _rt and _prt

            // access AoS data such as positions and cpu/id
            amrex::ParticleReal const x = p.pos(RealAoS::x);
            amrex::ParticleReal const y = p.pos(RealAoS::y);
            amrex::ParticleReal const t = p.pos(RealAoS::t);

            // angle of arc for the current slice
            amrex::ParticleReal const slice_phi = m_phi / nslice();

            // access reference particle values to find beta
            amrex::ParticleReal const bet = refpart.beta();

            // reference particle's orbital radius
            amrex::ParticleReal const rc = (m_B != 0_prt) ? refpart.rigidity_Tm() / m_B : m_ds / m_phi;

            // intialize output values of momenta
            amrex::ParticleReal pxout = px;
            amrex::ParticleReal pyout = py;
            amrex::ParticleReal ptout = pt;

            // assign intermediate quantities
            amrex::ParticleReal const pperp = sqrt(pow(pt,2)-2.0_prt/bet*pt-pow(py,2)+1.0_prt);
            amrex::ParticleReal const pzi = sqrt(pow(pperp,2)-pow(px,2));
            amrex::ParticleReal const rho = rc + x;
            amrex::ParticleReal const sin_phi = sin(slice_phi);
            amrex::ParticleReal const cos_phi = cos(slice_phi);

            // update momenta
            pxout = px*cos_phi + (pzi - rho/rc)*sin_phi;
            pyout = py;
            ptout = pt;

            // angle of momentum rotation
            amrex::ParticleReal const pzf = sqrt(pow(pperp,2)-pow(pxout,2));
            amrex::ParticleReal const theta = slice_phi + asin(px/pperp) - asin(pxout/pperp);

            // update position coordinates
            p.pos(RealAoS::x) = -rc + rho*cos_phi + rc*(pzf + px*sin_phi - pzi*cos_phi);
            p.pos(RealAoS::y) = y + theta*rc*py;
            p.pos(RealAoS::t) = t - theta*rc*(pt - 1.0_prt/bet) - m_phi*rc/bet;

            // assign updated momenta
            px = pxout;
            py = pyout;
            pt = ptout;

        }

        /** This pushes the reference particle.
         *
         * @param[in,out] refpart reference particle
         */
        AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
        void operator() (RefPart & AMREX_RESTRICT refpart) const {

            using namespace amrex::literals; // for _rt and _prt

            // assign input reference particle values
            amrex::ParticleReal const x = refpart.x;
            amrex::ParticleReal const px = refpart.px;
            amrex::ParticleReal const y = refpart.y;
            amrex::ParticleReal const py = refpart.py;
            amrex::ParticleReal const z = refpart.z;
            amrex::ParticleReal const pz = refpart.pz;
            amrex::ParticleReal const t = refpart.t;
            amrex::ParticleReal const pt = refpart.pt;
            amrex::ParticleReal const s = refpart.s;

            // length of the current slice
            amrex::ParticleReal const slice_ds = m_ds / nslice();

            // assign intermediate parameters
            amrex::ParticleReal const theta = m_phi / nslice();
            amrex::ParticleReal const rc = (m_B != 0_prt) ? refpart.rigidity_Tm() / m_B : m_ds / m_phi;
            amrex::ParticleReal const B = refpart.beta_gamma() /rc;

            // calculate expensive terms once
            //   TODO: use sincos function once wrapped in AMReX
            amrex::ParticleReal const sin_theta = sin(theta);
            amrex::ParticleReal const cos_theta = cos(theta);

            // advance position and momentum (bend)
            refpart.px = px*cos_theta - pz*sin_theta;
            refpart.py = py;
            refpart.pz = pz*cos_theta + px*sin_theta;
            refpart.pt = pt;

            refpart.x = x + (refpart.pz - pz)/B;
            refpart.y = y + (theta/B)*py;
            refpart.z = z - (refpart.px - px)/B;
            refpart.t = t - (theta/B)*pt;

            // advance integrated path length
            refpart.s = s + slice_ds;

        }

    private:
        amrex::ParticleReal m_phi; //! bend angle in radians
        amrex::ParticleReal m_B;  //! magnetic field in T
    };

} // namespace impactx

#endif // IMPACTX_EXACTSBEND_H
