/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2017 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "KinematicParcel.H"
#include "forceSuSp.H"
#include "IntegrationScheme.H"
#include "meshTools.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

template<class ParcelType>
Foam::label Foam::KinematicParcel<ParcelType>::maxTrackAttempts = 1;


// * * * * * * * * * * *  Protected Member Functions * * * * * * * * * * * * //

template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::setCellValues
(
    TrackData& td,
    const scalar dt,
    const label celli
)
{
    tetIndices tetIs = this->currentTetIndices();

    rhoc_ = td.rhoInterp().interpolate(this->coordinates(), tetIs);

    if (rhoc_ < td.cloud().constProps().rhoMin())
    {
        if (debug)
        {
            WarningInFunction
                << "Limiting observed density in cell " << celli << " to "
                << td.cloud().constProps().rhoMin() <<  nl << endl;
        }

        rhoc_ = td.cloud().constProps().rhoMin();
    }

    Uc_ = td.UInterp().interpolate(this->coordinates(), tetIs);

    muc_ = td.muInterp().interpolate(this->coordinates(), tetIs);

    // Apply dispersion components to carrier phase velocity
    Uc_ = td.cloud().dispersion().update
    (
        dt,
        celli,
        U_,
        Uc_,
        UTurb_,
        tTurb_
    );
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::cellValueSourceCorrection
(
    TrackData& td,
    const scalar dt,
    const label celli
)
{
    Uc_ += td.cloud().UTrans()[celli]/massCell(celli);
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::calc
(
    TrackData& td,
    const scalar dt,
    const label celli
)
{
    // Define local properties at beginning of time step
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    const scalar np0 = nParticle_;
    const scalar mass0 = mass();

    // Reynolds number
    const scalar Re = this->Re(U_, d_, rhoc_, muc_);


    // Sources
    //~~~~~~~~

    // Explicit momentum source for particle
    vector Su = Zero;

    // Linearised momentum source coefficient
    scalar Spu = 0.0;

    // Momentum transfer from the particle to the carrier phase
    vector dUTrans = Zero;


    // Motion
    // ~~~~~~

    // Calculate new particle velocity
    this->U_ = calcVelocity(td, dt, celli, Re, muc_, mass0, Su, dUTrans, Spu);


    // Accumulate carrier phase source terms
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (td.cloud().solution().coupled())
    {
        // Update momentum transfer
        td.cloud().UTrans()[celli] += np0*dUTrans;

        // Update momentum transfer coefficient
        td.cloud().UCoeff()[celli] += np0*Spu;
    }
}


template<class ParcelType>
template<class TrackData>
const Foam::vector Foam::KinematicParcel<ParcelType>::calcVelocity
(
    TrackData& td,
    const scalar dt,
    const label celli,
    const scalar Re,
    const scalar mu,
    const scalar mass,
    const vector& Su,
    vector& dUTrans,
    scalar& Spu
) const
{
    typedef typename TrackData::cloudType cloudType;
    typedef typename cloudType::parcelType parcelType;
    typedef typename cloudType::forceType forceType;

    const forceType& forces = td.cloud().forces();

    // Momentum source due to particle forces
    const parcelType& p = static_cast<const parcelType&>(*this);
    const forceSuSp Fcp = forces.calcCoupled(p, dt, mass, Re, mu);
    const forceSuSp Fncp = forces.calcNonCoupled(p, dt, mass, Re, mu);
    const forceSuSp Feff = Fcp + Fncp;
    const scalar massEff = forces.massEff(p, mass);


    // New particle velocity
    //~~~~~~~~~~~~~~~~~~~~~~

    // Update velocity - treat as 3-D
    const vector abp = (Feff.Sp()*Uc_ + (Feff.Su() + Su))/massEff;
    const scalar bp = Feff.Sp()/massEff;

    Spu = dt*Feff.Sp();

    IntegrationScheme<vector>::integrationResult Ures =
        td.cloud().UIntegrator().integrate(U_, dt, abp, bp);

    vector Unew = Ures.value();

    // note: Feff.Sp() and Fc.Sp() must be the same
    dUTrans += dt*(Feff.Sp()*(Ures.average() - Uc_) - Fcp.Su());

    // Apply correction to velocity and dUTrans for reduced-D cases
    const polyMesh& mesh = td.cloud().pMesh();
    meshTools::constrainDirection(mesh, mesh.solutionD(), Unew);
    meshTools::constrainDirection(mesh, mesh.solutionD(), dUTrans);

    return Unew;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class ParcelType>
Foam::KinematicParcel<ParcelType>::KinematicParcel
(
    const KinematicParcel<ParcelType>& p
)
:
    ParcelType(p),
    active_(p.active_),
    typeId_(p.typeId_),
    nParticle_(p.nParticle_),
    d_(p.d_),
    dTarget_(p.dTarget_),
    U_(p.U_),
    rho_(p.rho_),
    age_(p.age_),
    tTurb_(p.tTurb_),
    UTurb_(p.UTurb_),
    rhoc_(p.rhoc_),
    Uc_(p.Uc_),
    muc_(p.muc_)
{}


template<class ParcelType>
Foam::KinematicParcel<ParcelType>::KinematicParcel
(
    const KinematicParcel<ParcelType>& p,
    const polyMesh& mesh
)
:
    ParcelType(p, mesh),
    active_(p.active_),
    typeId_(p.typeId_),
    nParticle_(p.nParticle_),
    d_(p.d_),
    dTarget_(p.dTarget_),
    U_(p.U_),
    rho_(p.rho_),
    age_(p.age_),
    tTurb_(p.tTurb_),
    UTurb_(p.UTurb_),
    rhoc_(p.rhoc_),
    Uc_(p.Uc_),
    muc_(p.muc_)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class ParcelType>
template<class TrackData>
bool Foam::KinematicParcel<ParcelType>::move
(
    TrackData& td,
    const scalar trackTime
)
{
    typename TrackData::cloudType::parcelType& p =
        static_cast<typename TrackData::cloudType::parcelType&>(*this);

    td.switchProcessor = false;
    td.keepParticle = true;

    const polyMesh& mesh = td.cloud().pMesh();
    const polyBoundaryMesh& pbMesh = mesh.boundaryMesh();
    const scalarField& cellLengthScale = td.cloud().cellLengthScale();
    const scalar maxCo = td.cloud().solution().maxCo();

    while (td.keepParticle && !td.switchProcessor && p.stepFraction() < 1)
    {
        // Apply correction to position for reduced-D cases
        p.constrainToMeshCentre();

        // Cache the current position, cell and step-fraction
        const point start = p.position();
        const label celli = p.cell();
        const scalar sfrac = p.stepFraction();

        // Total displacement over the time-step
        const vector s = trackTime*U_;

        // Cell length scale
        const scalar l = cellLengthScale[p.cell()];

        // Fraction of the displacement to track in this loop. This is limited
        // to ensure that the both the time and distance tracked is less than
        // maxCo times the total value.
        scalar f = 1 - p.stepFraction();
        f = min(f, maxCo);
        f = min(f, maxCo*l/max(SMALL*l, mag(s)));
        if (p.active())
        {
            // Track to the next face
            p.trackToFace(f*s, f, td);
        }
        else
        {
            // Abandon the track, and move to the end of the sub-step. If the
            // the mesh is moving, this will implicitly move the parcel.
            if (mesh.moving())
            {
                WarningInFunction
                    << "Tracking was abandoned on a moving mesh. Parcels may "
                    << "move unphysically as a result." << endl;
            }
            p.stepFraction() += f;
        }

        const scalar dt = (p.stepFraction() - sfrac)*trackTime;

        // Avoid problems with extremely small timesteps
        if (dt > ROOTVSMALL)
        {
            // Update cell based properties
            p.setCellValues(td, dt, celli);

            if (td.cloud().solution().cellValueSourceCorrection())
            {
                p.cellValueSourceCorrection(td, dt, celli);
            }

            p.calc(td, dt, celli);
        }

        if (p.onBoundaryFace() && td.keepParticle)
        {
            if (isA<processorPolyPatch>(pbMesh[p.patch(p.face())]))
            {
                td.switchProcessor = true;
            }
        }

        p.age() += dt;

        td.cloud().functions().postMove(p, celli, dt, start, td.keepParticle);
    }

    return td.keepParticle;
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::hitFace(TrackData& td)
{
    typename TrackData::cloudType::parcelType& p =
        static_cast<typename TrackData::cloudType::parcelType&>(*this);

    td.cloud().functions().postFace(p, p.face(), td.keepParticle);
}


template<class ParcelType>
void Foam::KinematicParcel<ParcelType>::hitFace(int& td)
{}


template<class ParcelType>
template<class TrackData>
bool Foam::KinematicParcel<ParcelType>::hitPatch
(
    const polyPatch& pp,
    TrackData& td,
    const label patchi,
    const scalar trackFraction,
    const tetIndices& tetIs
)
{
    typename TrackData::cloudType::parcelType& p =
        static_cast<typename TrackData::cloudType::parcelType&>(*this);

    // Invoke post-processing model
    td.cloud().functions().postPatch
    (
        p,
        pp,
        trackFraction,
        tetIs,
        td.keepParticle
    );

    // Invoke surface film model
    if (td.cloud().surfaceFilm().transferParcel(p, pp, td.keepParticle))
    {
        // All interactions done
        return true;
    }
    else if (pp.coupled())
    {
        // Don't apply the patchInteraction models to coupled boundaries
        return false;
    }
    else
    {
        // Invoke patch interaction model
        return td.cloud().patchInteraction().correct
        (
            p,
            pp,
            td.keepParticle,
            trackFraction,
            tetIs
        );
    }
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::hitProcessorPatch
(
    const processorPolyPatch&,
    TrackData& td
)
{
    td.switchProcessor = true;
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::hitWallPatch
(
    const wallPolyPatch& wpp,
    TrackData& td,
    const tetIndices&
)
{
    // Wall interactions handled by generic hitPatch function
}


template<class ParcelType>
template<class TrackData>
void Foam::KinematicParcel<ParcelType>::hitPatch
(
    const polyPatch&,
    TrackData& td
)
{
    td.keepParticle = false;
}


template<class ParcelType>
void Foam::KinematicParcel<ParcelType>::transformProperties(const tensor& T)
{
    ParcelType::transformProperties(T);

    U_ = transform(T, U_);
}


template<class ParcelType>
void Foam::KinematicParcel<ParcelType>::transformProperties
(
    const vector& separation
)
{
    ParcelType::transformProperties(separation);
}


template<class ParcelType>
Foam::scalar Foam::KinematicParcel<ParcelType>::wallImpactDistance
(
    const vector&
) const
{
    return 0.5*d_;
}


// * * * * * * * * * * * * * * IOStream operators  * * * * * * * * * * * * * //

#include "KinematicParcelIO.C"

// ************************************************************************* //
