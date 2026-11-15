/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Datei: genericReactionSource.C                  |
|   \\  /    A nd           | Generische datengesteuerte Reaktionsquellterme  |
|    \\/     M anipulation  | mit analytischer Jacobi-Hauptdiagonale          |
\*---------------------------------------------------------------------------*/

#include "genericReactionSource.H"
#include "addToRunTimeSelectionTable.H"
#include "volFields.H"
#include <algorithm>

namespace Foam
{
namespace fv
{

defineTypeNameAndDebug(genericReactionSource, 0);
addToRunTimeSelectionTable(option, genericReactionSource, dictionary);

genericReactionSource::genericReactionSource
(
    const word& name,
    const word& modelType,
    const dictionary& dict,
    const fvMesh& mesh
)
:
    cellSetOption(name, modelType, dict, mesh),
    speciesNames_(),
    stoichiometricMatrix_(),
    arrheniusA_(),
    arrheniusEa_(),
    reactionDeltaH_(),
    molarMasses_(),
    Tmin_(250.0),
    Tmax_(3500.0)
{
    read(dict);
}

bool genericReactionSource::read(const dictionary& dict)
{
    if (cellSetOption::read(dict))
    {
        coeffs_.readIfPresent("speciesNames", speciesNames_);
        coeffs_.readIfPresent("stoichiometricMatrix", stoichiometricMatrix_);
        coeffs_.readIfPresent("arrheniusA", arrheniusA_);
        coeffs_.readIfPresent("arrheniusEa", arrheniusEa_);
        coeffs_.readIfPresent("reactionDeltaH", reactionDeltaH_);
        coeffs_.readIfPresent("molarMasses", molarMasses_);
        coeffs_.readIfPresent("Tmin", Tmin_);
        coeffs_.readIfPresent("Tmax", Tmax_);

        // Default molar masses if not provided (0.028 kg/mol standard)
        if (molarMasses_.size() != speciesNames_.size())
        {
            molarMasses_.setSize(speciesNames_.size(), 0.028);
        }

        // Validate dimension consistency across reactions
        const label nReactions = arrheniusA_.size();
        if (arrheniusEa_.size() != nReactions || reactionDeltaH_.size() != nReactions)
        {
            FatalErrorInFunction
                << "Size mismatch in kinetic parameters: arrheniusA (" << nReactions
                << "), arrheniusEa (" << arrheniusEa_.size()
                << "), reactionDeltaH (" << reactionDeltaH_.size() << ")"
                << exit(FatalError);
        }

        return true;
    }
    return false;
}

void genericReactionSource::addSup
(
    fvMatrix<scalar>& eqn,
    const label fieldi
)
{
    const volScalarField rho
    (
        IOobject
        (
            "rho",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("rho", dimDensity, 1.0)
    );

    addSup(rho, eqn, fieldi);
}

void genericReactionSource::addSup
(
    const volScalarField& rho,
    fvMatrix<scalar>& eqn,
    const label fieldi
)
{
    const word& fieldName = eqn.psi().name();
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    const volScalarField& Cp = mesh_.lookupObject<volScalarField>("thermo:Cp");
    const scalarField& V = mesh_.V();

    scalarField& source = eqn.source();
    scalarField& diag   = eqn.diag();

    const label nSpecies = speciesNames_.size();
    const label nReactions = arrheniusA_.size();

    // Find if current field is a participating species
    label speciesIndex = -1;
    for (label s = 0; s < nSpecies; ++s)
    {
        if (fieldName == speciesNames_[s])
        {
            speciesIndex = s;
            break;
        }
    }

    const bool isEnergyField = (fieldName == "he" || fieldName == "h" || fieldName == "e");

    if (speciesIndex == -1 && !isEnergyField)
    {
        // Field not handled by this reaction source
        return;
    }

    // Lookup species mass fraction fields (non-owning pointers to mesh-registered fields)
    List<const volScalarField*> Y(nSpecies, static_cast<const volScalarField*>(nullptr));
    for (label s = 0; s < nSpecies; ++s)
    {
        if (mesh_.foundObject<volScalarField>(speciesNames_[s]))
        {
            Y[s] = &mesh_.lookupObject<volScalarField>(speciesNames_[s]);
        }
    }

    // Iterate over cells in designated cellSet
    forAll(cells_, i)
    {
        const label cellI = cells_[i];
        const scalar Tc = Foam::min(Tmax_, Foam::max(Tmin_, T[cellI]));
        const scalar rhoc = rho[cellI];

        if (isEnergyField)
        {
            scalar totalQ = 0.0;
            scalar totalDqDh = 0.0;
            const scalar cpVal = max(100.0, Cp[cellI]);

            for (label r = 0; r < nReactions; ++r)
            {
                scalar k_rate = arrheniusA_[r] * Foam::exp(-arrheniusEa_[r] / Tc);
                scalar concProduct = 1.0;

                if (r < stoichiometricMatrix_.size())
                {
                    const scalarList& stoich = stoichiometricMatrix_[r];
                    for (label s = 0; s < nSpecies && s < stoich.size(); ++s)
                    {
                        if (stoich[s] < -1e-6)
                        {
                            const scalar yVal = (Y[s] != nullptr) ? max(0.0, (*Y[s])[cellI]) : 0.0;
                            const scalar molarMass = max(1e-4, molarMasses_[s]);
                            const scalar conc = (rhoc * yVal) / molarMass;
                            const scalar order = -stoich[s];
                            concProduct *= Foam::pow(conc, order);
                        }
                    }
                }

                // Reaction rate r [mol/(m^3 s)]
                const scalar rateProgress = k_rate * concProduct;
                const scalar deltaH = reactionDeltaH_[r];
                // Heat source/sink [W/m^3]
                const scalar q_reaction = -rateProgress * deltaH;

                totalQ += q_reaction;

                // Analytical Jacobian: dq/dT = q * (Ea / T^2), dq/dh = (dq/dT) / Cp
                const scalar dq_dh = q_reaction * (arrheniusEa_[r] / (Tc * Tc * cpVal));
                if (dq_dh < 0.0)
                {
                    totalDqDh += dq_dh;
                }
            }

            source[cellI] += (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
            diag[cellI]   -= totalDqDh * V[cellI]; // Add positive quantity to diagonal
        }
        else if (speciesIndex >= 0)
        {
            // Species mass balance equation
            const label s = speciesIndex;
            const scalar molarMass_s = max(1e-4, molarMasses_[s]);
            scalar totalMassSource = 0.0;
            scalar totalDiagJac = 0.0;

            for (label r = 0; r < nReactions; ++r)
            {
                if (r >= stoichiometricMatrix_.size()) continue;
                const scalarList& stoich = stoichiometricMatrix_[r];
                if (s >= stoich.size()) continue;

                const scalar nu_sr = stoich[s];
                if (Foam::mag(nu_sr) < 1e-6) continue;

                scalar k_rate = arrheniusA_[r] * Foam::exp(-arrheniusEa_[r] / Tc);
                scalar concProduct = 1.0;

                for (label k = 0; k < nSpecies && k < stoich.size(); ++k)
                {
                    if (stoich[k] < -1e-6)
                    {
                        const scalar yVal = (Y[k] != nullptr) ? max(0.0, (*Y[k])[cellI]) : 0.0;
                        const scalar molarMass_k = max(1e-4, molarMasses_[k]);
                        const scalar conc = (rhoc * yVal) / molarMass_k;
                        const scalar order = -stoich[k];
                        concProduct *= Foam::pow(conc, order);
                    }
                }

                const scalar rateProgress = k_rate * concProduct;
                // Mass production rate [kg/(m^3 s)] = nu_sr * M_s * rateProgress
                const scalar massRate = nu_sr * molarMass_s * rateProgress;
                totalMassSource += massRate;

                // If consumed in reaction r, compute implicit diagonal contribution
                if (nu_sr < -1e-6 && Y[s] != nullptr)
                {
                    const scalar yVal = max(1e-6, (*Y[s])[cellI]);
                    const scalar dRate_dY = (-nu_sr) * rateProgress / yVal;
                    const scalar dMass_dY = nu_sr * molarMass_s * dRate_dY; // Negative
                    totalDiagJac += dMass_dY;
                }
            }

            // Implicit diagonal stabilization: diag -= dMass_dY * V (since dMass_dY < 0, adds positive to diag)
            source[cellI] += (totalMassSource - totalDiagJac * eqn.psi()[cellI]) * V[cellI];
            diag[cellI]   -= totalDiagJac * V[cellI];
        }
    }
}

} // End namespace fv
} // End namespace Foam
