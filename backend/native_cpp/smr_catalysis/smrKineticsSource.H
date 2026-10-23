/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Datei: smrKineticsSource.H                      |
|   \\  /    A nd           | Hochperformante, analytisch stabilisierte       |
|    \\/     M anipulation  | C++ Wandkatalyse (SMR / WGS auf beta-Mo2C)      |
\*---------------------------------------------------------------------------*/

#ifndef smrKineticsSource_H
#define smrKineticsSource_H

#include "fvOption.H"
#include "cellSetOption.H"

namespace Foam
{
namespace fv
{

class smrKineticsSource
:
    public cellSetOption
{
    // Kinetische Parameter (Kibret et al. 2026 / Bosanquet-Thiele)
    scalar A_smr_;      // Frequenzfaktor [1/s]
    scalar Ea_R_;       // Aktivierungsenergie / R [K]
    scalar dH_smr_;     // Reaktionsenthalpie [J/mol]
    scalar eta_cat_;    // Thiele-Diffusionskorrekturfaktor

public:

    TypeName("smrKineticsSource");

    // Konstruktor
    smrKineticsSource
    (
        const word& name,
        const word& modelType,
        const dictionary& dict,
        const fvMesh& mesh
    );

    // Destruktor
    virtual ~smrKineticsSource() = default;

    // Quellterm-Kopplung an die Energiegleichung
    virtual void addSup
    (
        const volScalarField& rho,
        fvMatrix<scalar>& eqn,
        const label fieldi
    );

    virtual bool read(const dictionary& dict);
};

} // End namespace fv
} // End namespace Foam

#endif
