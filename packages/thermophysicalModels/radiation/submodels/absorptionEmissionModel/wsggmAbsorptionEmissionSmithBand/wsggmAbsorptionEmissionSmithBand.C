/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2012 OpenFOAM Foundation
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

#include "wsggmAbsorptionEmissionSmithBand.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    namespace radiation
    {
        defineTypeNameAndDebug(wsggmAbsorptionEmissionSmithBand, 0);

        addToRunTimeSelectionTable
        (
            absorptionEmissionModel,
            wsggmAbsorptionEmissionSmithBand,
            dictionary
        );
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::radiation::wsggmAbsorptionEmissionSmithBand::wsggmAbsorptionEmissionSmithBand
(
    const dictionary& dict,
    const fvMesh& mesh
)
:
    absorptionEmissionModel(dict, mesh),
    coeffsDict_((dict.subDict(typeName + "Coeffs"))),
    speciesNames_(0),
    specieIndex_(label(0)),
    thermo_(mesh.lookupObject<fluidThermo>("thermophysicalProperties")),
    Yj_(nSpecies_),
    Csoot_(readScalar(coeffsDict_.lookup("Csoot")))

{
    label nBand = 0;
    const dictionary& functionDicts = dict.subDict(typeName +"Coeffs");
    forAllConstIter(dictionary, functionDicts, iter)
    {
        // safety:
        if (!iter().isDict())
        {
            continue;
        }

        const dictionary& dict = iter().dict();

        label nSpec = 0;
        const dictionary& specDicts = dict.subDict("species");
        forAllConstIter(dictionary, specDicts, iter)
        {
            const word& key = iter().keyword();
            if (nBand == 0)
            {
                speciesNames_.insert(key, nSpec);
            }
            else
            {
                if (!speciesNames_.found(key))
                {
                    FatalErrorIn
                    (
                        "Foam::radiation::wideBandAbsorptionEmission(const"
                        "dictionary& dict, const fvMesh& mesh)"
                    )   << "specie: " << key << "is not in all the bands"
                        << nl << exit(FatalError);
                }
            }
            coeffs_[nSpec][nBand].initialise(specDicts.subDict(key));
            nSpec++;
        }
        nBand++;
    }
    nBands_ = nBand;

}



// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::radiation::wsggmAbsorptionEmissionSmithBand::~wsggmAbsorptionEmissionSmithBand()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::tmp<Foam::volScalarField>
Foam::radiation::wsggmAbsorptionEmissionSmithBand::aCont(const label bandI) const
{
    const volScalarField& T = thermo_.T();
    const volScalarField& p = thermo_.p();

    const psiReactionThermo& thermo= mesh_.lookupObject<psiReactionThermo>("thermophysicalProperties");

    //access species mass fractions
    const PtrList<volScalarField>& Y = thermo.composition().Y();

    //access specie thermo data
    const PtrList<gasHThermoPhysics> & specieThermo =
        dynamic_cast<const reactingMixture<gasHThermoPhysics>&>  (thermo).speciesData();

    // fraction volume for soot term
    const volScalarField& fv =mesh_.lookupObject<volScalarField>("fv");

/*
 // to specify a soot loading before runtime
    volScalarField fv(
            IOobject
            (
                "fv",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimensionedScalar("zero",dimless,0.0)
        );
*/
/*
    // uniform soot distribution
    forAll(fv, cellI)
    {
        fv[cellI] = 1e-05;
    }
*/
/*
    // specified soot distribution

    volScalarField x(
            IOobject
            (
                "x",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimensionedScalar("zero",dimless,0.0)
        );

    scalar dx = 0.004975124; // =1/(200+1)

    for(label i=0; i < 200; i++)
    {
        x[i] = x[i-1] + dx;

    }
   // Info << x << endl;


    for(label i=0; i < 201; i++)
    {
        fv[i] = (4*x[i]*(x[i] - 1)+1.6)*1e-07;

    }
   // Info << fv << endl;
*/

    // get index of CO2 in mixture
    label indexCO2= dynamic_cast<const reactingMixture<gasHThermoPhysics>&> (thermo).species()["CO2"];

    // get index of H2O in mixture
    label indexH2O= dynamic_cast<const reactingMixture<gasHThermoPhysics>&> (thermo).species()["H2O"];

    tmp<volScalarField> ta
    (
        new volScalarField
        (
            IOobject
            (
                "a",
                mesh().time().timeName(),
                mesh(),
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh(),
            dimensionedScalar("a", dimless/dimLength, 0.0)
        )
    );

    scalarField& a = ta.ref().primitiveFieldRef();

    volScalarField partialPressure // for calculation of a in m-1 (k supplied in m-1.atm-1)
    (
        IOobject
        (
            "partialPressure",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",dimless,0.0)
    );

    volScalarField wMean // for calculation of a in m-1 (k supplied in m-1.atm-1)
    (
        IOobject
        (
            "wMean",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",dimless,0.0) // kg/kmol
    );

    volScalarField aSoot // soot fraction volume based absorption coefficient in m-1
    (
        IOobject
        (
            "aSoot",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",pow(dimLength,-1),0.0)
    );

    // calculation of partial pressure

    forAll(Y,specieI)
    {
        wMean+=Y[specieI]/specieThermo[specieI].W();
    }
    wMean=1/wMean;

    partialPressure=wMean*(p/101325/dimensionedScalar("unity",dimPressure,1.0))*(Y[indexCO2]/specieThermo[indexCO2].W()
                      +Y[indexH2O]/specieThermo[indexH2O].W());

   // Info << partialPressure << endl;

    aSoot = Csoot_*fv*T/dimensionedScalar("unity",dimTemperature,1.0)/dimensionedScalar("unity",dimLength,1.0);

    label nSpecies = speciesNames_.size();

    forAll(a, i)
    {
        for (label n=0; n<nSpecies; n++)
        {
            const absorptionCoeffs::coeffArray& b =
                coeffs_[n][bandI].coeffs(T[i]);

	    a[i]=b[0]*partialPressure[i]+aSoot[i];

        }
    }
   // Info << "aCont = " << a << endl;
    return ta;
}

Foam::tmp<Foam::volScalarField>
Foam::radiation::wsggmAbsorptionEmissionSmithBand::ggCoeff(const label bandI) const
{

    label nSpecies = speciesNames_.size();
    const volScalarField& T = thermo_.T();
    //Info << T << endl;

    tmp<volScalarField> tggCoeff
    (
        new volScalarField
        (
            IOobject
            (
                "ggCoeff",
                mesh().time().timeName(),
                mesh(),
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh(),
            dimensionedScalar("ggCoeff", dimless, 0.0)
        )
    );

    tmp<volScalarField> tggCoeffBC
    (
        new volScalarField
        (
            IOobject
            (
                "ggCoeffBC",
                mesh().time().timeName(),
                mesh(),
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh(),
            dimensionedScalar("ggCoeffBC", dimless, 0.0)
        )
    );

    scalarField& wsggmWeightingCoeff = tggCoeff.ref().primitiveFieldRef();

    forAll(wsggmWeightingCoeff, i)
    {
        for (label n=0; n<nSpecies; n++)
        {
            const absorptionCoeffs::coeffArray& b =
                coeffs_[n][bandI].coeffs(T[i]);

                    if(bandI < nBands_ -1)
                    {
		        wsggmWeightingCoeff[i] = b[1]*pow(T[i],0)*1e-1 + b[2]*pow(T[i],1)*1e-4 + b[3]*pow(T[i],2)*1e-7 + b[4]*pow(T[i],3)*1e-11;
                    }
                    else
                    {
                        wsggmWeightingCoeff[i] = 1-(b[1]*pow(T[i],0)*1e-1 + b[2]*pow(T[i],1)*1e-4 + b[3]*pow(T[i],2)*1e-7 + b[4]*pow(T[i],3)*1e-11);
                    }
        }
    }
// BOUNDARY FIELD CALCULATION

    forAll(mesh().boundary(), bid)
    {
       scalarField Tw = thermo_.T().boundaryField()[bid];

       scalarField& wsggmWeightingCoeffBC = tggCoeffBC.ref().boundaryFieldRef()[bid];

	    forAll(wsggmWeightingCoeffBC, i)
	    {
		for (label n=0; n<nSpecies; n++)
		{
		    const absorptionCoeffs::coeffArray& b =
			coeffs_[n][bandI].coeffs(Tw[i]);

                    if(bandI < nBands_ -1)
                    {
		        wsggmWeightingCoeffBC[i] = b[1]*pow(Tw[i],0)*1e-1 + b[2]*pow(Tw[i],1)*1e-4 + b[3]*pow(Tw[i],2)*1e-7 + b[4]*pow(Tw[i],3)*1e-11;
                    }
                    else
                    {
                        wsggmWeightingCoeffBC[i] = 1-(b[1]*pow(Tw[i],0)*1e-1 + b[2]*pow(Tw[i],1)*1e-4 + b[3]*pow(Tw[i],2)*1e-7 + b[4]*pow(Tw[i],3)*1e-11);
                       // Info << wsggmWeightingCoeffBC[0] << endl;
                    }


		}
	    }
    }


    return tggCoeff + tggCoeffBC;
}


Foam::tmp<Foam::volScalarField>
Foam::radiation::wsggmAbsorptionEmissionSmithBand::eCont(const label bandI) const
{

    return aCont(bandI);
}


Foam::tmp<Foam::volScalarField>
Foam::radiation::wsggmAbsorptionEmissionSmithBand::ECont(const label bandI) const
{
    tmp<volScalarField> E
    (
        new volScalarField
        (
            IOobject
            (
                "E",
                mesh().time().timeName(),
                mesh(),
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh(),
            dimensionedScalar("E", dimMass/dimLength/pow3(dimTime), 0.0)
        )
    );

    return E;
}

Foam::tmp<Foam::volScalarField>
Foam::radiation::wsggmAbsorptionEmissionSmithBand::addIntensity
(
    const label i,
    const volScalarField& ILambda
) const
{

    return ILambda;
}


void Foam::radiation::wsggmAbsorptionEmissionSmithBand::correct
(
    volScalarField& a,
    PtrList<volScalarField>& aLambda

) const
{
    a = dimensionedScalar("zero", dimless/dimLength, 0.0);
   // ggCoeff = dimensionedScalar("zero", dimless, 0.0); // new

    for (label j=0; j<nBands_; j++)
    {
        Info<< "Calculating absorption in band: " << j << endl;
        aLambda[j].primitiveFieldRef() = this->a(j);
       // Info << aLambda << endl;
        Info<< "Calculated absorption in band: " << j << endl;
        a.primitiveFieldRef() =
            aLambda[j].primitiveField();
       // Info << a << endl;

    }
Info << aLambda << endl;
}


void Foam::radiation::wsggmAbsorptionEmissionSmithBand::correctEnFrac
(
    PtrList<volScalarField>& enFrac,
    blackBodyEmission& blackBody   
) const    // blackBody object doesn't get used here... 
{
    for (label j=0; j < nBands_; j++)
    {
        //Info<< "Calculating weighting coefficient in band: " << j << endl;
        enFrac[j] = this->ggCoeff(j);
    }

}

// ************************************************************************* //
