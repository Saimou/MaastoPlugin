#pragma once

#include <QString>
#include <QColor>
#include <QMap>

// Yhden luokan määritys .ptc-tiedostosta
struct ClassDefinition
{
    int     value;           // luokan numeerinen koodi
    QString name;            // luokan nimi
    QColor  color;           // luokan väri (invalid jos ei määritelty)
};

// Lukee .ptc-tiedoston ja palauttaa map: luokan koodi → ClassDefinition
class ClassDefinitionReader
{
public:
    // Lukee .ptc-tiedoston.
    // Palauttaa tyhjän mapin jos tiedostoa ei löydy tai formaatti on tuntematon.
    static QMap<int, ClassDefinition> read( const QString &filePath );
};
