/*--------------------------------*- C++ -*----------------------------------*\
| =========                 |                                                 |
| \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\    /   O peration     | Datei: genericReactionSource.H                  |
|   \\  /    A nd           | Generische datengesteuerte Reaktionsquellterme  |
|    \\/     M anipulation  | mit analytischer Jacobi-Hauptdiagonale          |
\*---------------------------------------------------------------------------*/

#ifndef genericReactionSource_H
#define genericReactionSource_H

#include "fvOption.H"
#include "cellSetOption.H"
#include "wordList.H"
#include "scalarList.H"

namespace Foam
{
namespace fv
{

class genericReactionSource
:
    public cellSetOption
{
    // Konfigurierbare Reaktionsparameter (Datengetrieben via Dictionary)
    wordList speciesNames_;
    List<scalarList> stoichiometricMatrix_; // [nReactions][nSpecies]
    List<scalar> arrheniusA_;                // Frequenzfaktoren [1/s] oder [m^3/(mol s)]
    List<scalar> arrheniusEa_;               // Aktivierungsenergie Ta = Ea / R [K]
    List<scalar> reactionDeltaH_;            // Reaktionsenthalpien [J/mol]
    List<scalar> molarMasses_;               // Molare Massen [kg/mol]
    scalar Tmin_;                            // Minimaltemperatur [K]
    scalar Tmax_;                            // Maximaltemperatur [K]

public:

    TypeName("genericReactionSource");

    // Konstruktor
    genericReactionSource
    (
        const word& name,
        const word& modelType,
        const dictionary& dict,
        const fvMesh& mesh
    );

    // Destruktor
    virtual ~genericReactionSource() = default;

    // Quellterm-Kopplung an fvMatrix (Standard & kompressibel mit Dichte)
    virtual void addSup
    (
        fvMatrix<scalar>& eqn,
        const label fieldi
    );

    virtual void addSup
    (
        const volScalarField& rho,
        fvMatrix<scalar>& eqn,
        const label fieldi
    );

    virtual bool read(const dictionary& dict);

    // Zugriffsmethoden fuer Verifikation und Introspektion
    const wordList& speciesNames() const { return speciesNames_; }
    const List<scalarList>& stoichiometricMatrix() const { return stoichiometricMatrix_; }
    const List<scalar>& arrheniusA() const { return arrheniusA_; }
    const List<scalar>& arrheniusEa() const { return arrheniusEa_; }
    const List<scalar>& reactionDeltaH() const { return reactionDeltaH_; }
    const List<scalar>& molarMasses() const { return molarMasses_; }
};

} // End namespace fv
} // End namespace Foam

#endif
