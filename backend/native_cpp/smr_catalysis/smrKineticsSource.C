/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Datei: smrKineticsSource.C                      |
|   \\  /    A nd           | Implementierung mit Jacobi-Hauptdiagonale       |
|    \\/     M anipulation  | für absolute numerische Konvergenz              |
\*---------------------------------------------------------------------------*/

#include "smrKineticsSource.H"
#include "addToRunTimeSelectionTable.H"
#include "volFields.H"

namespace Foam
{
namespace fv
{

defineTypeNameAndDebug(smrKineticsSource, 0);
addToRunTimeSelectionTable(option, smrKineticsSource, dictionary);

smrKineticsSource::smrKineticsSource
(
    const word& name,
    const word& modelType,
    const dictionary& dict,
    const fvMesh& mesh
)
:
    cellSetOption(name, modelType, dict, mesh),
    A_smr_(7.67e6),
    Ea_R_(12748.9),
    dH_smr_(2.061e5),
    eta_cat_(0.902)
{
    read(dict);
}

bool smrKineticsSource::read(const dictionary& dict)
{
    if (cellSetOption::read(dict))
    {
        coeffs_.readIfPresent("A_smr", A_smr_);
        coeffs_.readIfPresent("Ea_R", Ea_R_);
        coeffs_.readIfPresent("dH_smr", dH_smr_);
        coeffs_.readIfPresent("eta_cat", eta_cat_);
        return true;
    }
    return false;
}

void smrKineticsSource::addSup
(
    const volScalarField& rho,
    fvMatrix<scalar>& eqn,
    const label fieldi
)
{
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    const volScalarField& Y_CH4 = mesh_.lookupObject<volScalarField>("CH4");
    const volScalarField& Cp = mesh_.lookupObject<volScalarField>("thermo:Cp");
    const scalarField& V = mesh_.V();

    scalarField& heSource = eqn.source();
    scalarField& heDiag   = eqn.diag();

    forAll(cells_, i)
    {
        const label cellI = cells_[i];
        const scalar Tc = max(300.0, T[cellI]);
        const scalar y_ch4 = max(0.0, Y_CH4[cellI]);

        if (Tc > 600.0 && y_ch4 > 1e-5)
        {
            // Molare Konzentration [mol/m^3]
            const scalar c_ch4 = (rho[cellI] * y_ch4) / 1.6043e-2;
            const scalar k_rate = eta_cat_ * A_smr_ * Foam::exp(-Ea_R_ / Tc);
            const scalar q_sink = -k_rate * c_ch4 * dH_smr_; // [W/m^3]

            // Analytische Jacobi-Ableitung d(q_sink)/dh = (dq/dT) / Cp < 0
            const scalar dq_dh = q_sink * (Ea_R_ / (Tc * Tc * max(1000.0, Cp[cellI])));

            // Implizite Stabilisierung der Matrix-Diagonale
            heSource[cellI] -= (q_sink - dq_dh * eqn.psi()[cellI]) * V[cellI];
            heDiag[cellI]   += dq_dh * V[cellI];
        }
    }
}

} // End namespace fv
} // End namespace Foam
