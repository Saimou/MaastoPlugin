#include "ClassDefinition.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

// ----------------------------------------------------------------
// .ptc-tiedoston formaatti:
//
// Jokainen luokka koostuu kahdesta rivistä + tyhjästä välistä:
//   Rivi 1: <koodi>\t\t<nimi>\t...
//           - parts[0] = numeerinen koodi
//           - nimi = ensimmäinen kenttä koodin jälkeen joka ei ole numero
//   Rivi 2: *\t<teksti>\t<koodi>\t<R,G,B>\t...
//           - etsi kenttä joka matchaa "N,N,N" -muodon
//
// Erikoistapaukset:
//   - Toinen rivi voi puuttua väristä (kenttä on * tai puuttuu)
//   - Ensimmäinen rivi voi sisältää ylimääräisen numeron ennen nimeä
// ----------------------------------------------------------------

static bool isColorString( const QString &s, int &r, int &g, int &b )
{
    const QRegularExpression re( R"(^(\d+),(\d+),(\d+)$)" );
    const QRegularExpressionMatch m = re.match( s.trimmed() );
    if ( !m.hasMatch() )
        return false;
    r = m.captured( 1 ).toInt();
    g = m.captured( 2 ).toInt();
    b = m.captured( 3 ).toInt();
    return true;
}

QMap<int, ClassDefinition> ClassDefinitionReader::read( const QString &filePath )
{
    QMap<int, ClassDefinition> result;

    QFile file( filePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return result;

    QTextStream in( &file );
    QStringList lines;

    // Lue kaikki rivit ensin
    while ( !in.atEnd() )
        lines << in.readLine();

    int i = 0;
    while ( i < lines.size() )
    {
        // Ohita tyhjät rivit
        if ( lines[i].trimmed().isEmpty() )
        {
            ++i;
            continue;
        }

        // --- Rivi 1: koodi + nimi ---
        const QString line1 = lines[i];
        ++i;

        // Ohita rivi 2 jos se on olemassa
        QString line2;
        if ( i < lines.size() && !lines[i].trimmed().isEmpty() )
        {
            line2 = lines[i];
            ++i;
        }

        // Parsitaan rivi 1: split tabilla
        const QStringList parts1 = line1.split( '\t', Qt::SkipEmptyParts );
        if ( parts1.isEmpty() )
            continue;

        // Ensimmäinen kenttä = luokan koodi
        bool ok = false;
        const int code = parts1[0].trimmed().toInt( &ok );
        if ( !ok )
            continue;

        // Nimi = ensimmäinen kenttä koodin jälkeen joka ei ole pelkkä numero
        QString name;
        for ( int p = 1; p < parts1.size(); ++p )
        {
            const QString candidate = parts1[p].trimmed();
            // Ohita jos kenttä on pelkkä numero (voi olla toistettu koodi)
            bool isNum = false;
            candidate.toInt( &isNum );
            if ( !isNum && !candidate.isEmpty() )
            {
                name = candidate;
                break;
            }
        }

        // --- Parsitaan rivi 2: väri ---
        QColor color; // invalid oletuksena
        if ( !line2.isEmpty() )
        {
            const QStringList parts2 = line2.split( '\t', Qt::SkipEmptyParts );
            for ( const QString &field : parts2 )
            {
                int r, g, b;
                if ( isColorString( field, r, g, b ) )
                {
                    color = QColor( r, g, b );
                    break;
                }
            }
        }

        ClassDefinition def;
        def.value = code;
        def.name  = name;
        def.color = color;
        result.insert( code, def );
    }

    return result;
}
