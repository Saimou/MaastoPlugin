#include "SplashScreen.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>

// ----------------------------------------------------------------
// Aforismilista — arvotaan satunnaisesti joka käynnistyksellä
// ----------------------------------------------------------------
static const QStringList s_aphorisms = {
    "\"Joka toiselle hyvän suopi, se itselle siunaust luopi.\" - Suomalainen sananlasku",
    "\"Älä enää tuhlaa aikaa riitelemällä, millainen hyvän ihmisen tulisi olla; ole sitä.\" - Marcus Aurelius",
    "\"Pahuuden, jonka tunnistaa itsessään, rankaisee sitä ankarammin toisissa.\" - T.G. von Hippel",
    "\"Pahuus juo itse suurimman osan omasta myrkystään.\" - Seneca",
    "\"Joka toiselle kuoppaa kaivaa, se itse siihen lankeaa.\" - Suomalainen sananlasku",
    "\"Ei pahaa pahalla paranneta.\" - Herodotos",
    "\"Joskus tulee ajatelleeksi, että ihmistä luodessaan Jumala jossain määrin yliarvioi kykynsä.\" - Oscar Wilde",
    "\"Omatunto on sisäinen ääni, joka varoittaa meitä siitä, että joku voi nähdä.\" - H.L. Mencken",
    "\"Hyvälle ihmiselle maailma on hyvä.\" - Mohandas Gandhi",
    "\"Mustankipeä kaiken kuulee.\" - Lawrence Durrell",
    "\"Ei sydänt' innoita jo saatu, vaan vain uus.\" - Gustaf Filip Creutz",
    "\"Mustasukkainen löytää aina enemmän kuin etsii.\" - Madeleine de Scudery",
    "\"Mustasukkaisuudessa on aina enemmän itserakkautta kuin rakkautta.\" - La Rochefoucauld",
    "\"Mustasukkaisuus syntyy aina rakkauden seurassa mutta ei aina kuole sen mukana.\" - La Rochefoucauld",
    "\"Ainoa todellinen viisaus on tieto siitä, että et tiedä yhtään mitään.\" - Sokrates",
    "\"Ainoa todellinen poikkeavuus on kyvyttömyys rakkauteen.\" - Anais Nin",
    "\"On parempi kärsiä vääryyttä itse, kuin sitä toisille tehdä.\" - Minna Canth",
    "\"Aloita jokainen päivä positiivisin ajatuksin ja kiitollisin sydämin.\" - Roy T. Bennett",
    "\"Tytön suukot ovat kuin pulloon säilötyt oliivit - kun saat ensimmäisen heltiämään, loput tulevat helposti.\" - Chr. News",
    "\"Nykyään melkein kaikki osaavat lukea, mutta vain harvat ajatella.\" - Alfredo Ottaviani",
    "\"Totuus, joka haavoittaa, ei ole hiukkaakaan parempi, kuin valhe, joka haavoittaa.\" - Mark Twain",
    "\"Sitä on juuri niin kaunis kuin on olevinaan.\" - Brigitte Bardot",
    "\"Elämä on laiffii.\" - Matti Nykänen",
    "\"Sanat kuluvat käytössä / hirsiseinä harmaantuu / ja kynttilänsydän palaa loppuun / elämä kuluu opetellessa / elämän vaikeaa taitoa.\" - Mikko Kilpi",
    "\"Aika on suuri opettaja, mutta valitettavasti se tappaa kaikki oppilaansa.\" - Hector Berlioz",
    "\"Minne Jumala rakentaa kirkon - sinne paholainen rakentaa kappelin.\" - Kv. sanonta",
    "\"Hyvät ihmiset kärsivät eniten.\" - Kiinalainen sananlasku",
    "\"Missä on rakkautta, siellä on elämää.\" - Mahatma Gandhi",
    "\"Jos haluat lastesi kulkevan oikeaan suuntaan, kulje sinne itsekin.\" - Amissien viisaus",
    "\"Sillä parempi on viisaus kuin helmet, eivät mitkään kalleudet vedä sille vertaa.\" - Sananlaskut, luku 8",
    "\"Hän, jolla ei ole rahaa, on köyhä. Hän, jolla on vain rahaa, on vieläkin köyhempi.\" - Amissien viisaus",
    "\"Tietäminen on helppoa. Se tuo kuitenkin mukanaan meille paljon vaikeuksia.\" - Amissien viisaus",
    "\"Amissien yhteisön elämä ja kulttuuri voi tuoda suojelusta, mutta ei koskaan pelastusta.\" - Entisen amissin viisaus",
    "\"Ennen vartioi kapan kirppuja kuin uskotonta akkaa.\" - Suomalainen sananlasku",
    "\"Yksi on vieres, toinen on mieles ja kolomannen laulu kuuluu.\" - Suomalainen sananlasku",
    "\"Mustasukkainen ei ole se, joka rakastaa, vaan se, joka tahtoo, että häntä rakastetaan.\" - Rahel Varnhagen",
    "\"Epäluulo on kirves rakkauden puun juurella.\" - Venäläinen sananlasku",
    "\"Tiedätkös, mustasukkaisuus on typerintä, mitä olla saattaa. Se rumentaa ihmistä, ja tekee elämän kauhean ikäväksi sekä itselle että toiselle.\" - Minna Canth",
    "\"Ihminen voi luvata tottelevaisuuden ja uskollisuuden, mutta voiko luvata rakkautensa kestävän?\" - Henri Amiel",
    "\"Romanttinen rakkaus on lasten karamelli.\" - Arja Tiainen",
    "\"Vain vailla toivoa ken rakastaa, hän tuntee rakkauden.\" - Friedrich von Schiller",
    "\"Ei sovi suopetäjä korpikuusen kumppaniksi.\" - Suomalainen sananlasku",
    "\"Nuoruudessa lyhyt elämä palaa kirkkaana.\" - Stephen Baxter",
    "\"Oli mahdotonta tajuta, kuinka lyhyt aika se oli. Tuntui siltä, kuin nuoruus kestäisi hyvin pitkään, ikuisesti. Mutta se oli vain välähdys.\" - Chris Pavone",
    "\"Vanhat ihmiset elävät muistoissaan, nuoret elävät toivossa.\" - Gayla Reid",
    "\"Nuoruuden suurta siunausta ja suurta julmuutta on se, että tuntuu, kuin aikaa olisi rajattomasti.\" - Catherynne M. Valente",
    "\"Et valitse perhettäsi. Perheesi on Jumalan lahja sinulle, kuten sinä olet perheellesi.\" - Desmond Tutu",
    "\"Jos emme muutu, emme kasva. Jos emme kasva, emme oikeastaan elä ollenkaan.\" - Gail Sheehy",
    "\"Silmä silmästä johtaa vain kasvavaan sokeuteen.\" - Margaret Atwood",
    "\"Mielikuvituksen voima tekee meistä rajattomia.\" - John Muir",
    "\"Tämä on monen lukeneen henkilön osa: he ovat lukeneet itsensä tyhmiksi.\" - Schopenhauer",
    "\"Olennaisissa tehtävissä äly on erittäin vähäpätöinen lahja.\" - Matemaatikko G. H. Hardy",
    "\"Typerää tehdä sama virhe kahdesti.\" - Muumipappa",
    "\"Elämäänsä viisaasti elävän ei tarvitse pelätä edes kuolemaa.\" - Buddha",
    "\"Ennemmin kuolisin seisaallaan, kuin eläisin elämäni polvillani.\" - Ernesto Che Guevara",
    "\"Ilo on rakkauden verkko, jolla pyydystetään sieluja.\" - Äiti Teresa",
    "\"Muutamat hädän hetket opettavat ihmiselle viisautta enemmän kuin vuosikymmenien tasaiset olot.\" - Maria Jotuni",
    "\"Ruukku tihkuu sitä, mitä se sisältää.\" - Kiinalainen sananparsi",
    "\"Hän joka tietää kaikki vastaukset ei ole kysynyt kaikkia kysymyksiä.\" - Konfutse",
};

// Tekstialueen koordinaatit kuvakoordinaateissa (768x512)
static constexpr int kTextX1 =  94;
static constexpr int kTextY1 =  44;
static constexpr int kTextX2 = 378;
static constexpr int kTextY2 = 176;
static constexpr int kTextW  = kTextX2 - kTextX1;  // 284
static constexpr int kTextH  = kTextY2 - kTextY1;  // 132
static constexpr int kMaxLines = 4;

// "Sivistynyt Prismamies sivistää:" -teksti
static constexpr int    kLabelFontSize = 22;
static const    QString kLabelText     = "Sivistynyt Prismamies sivistää:";
static constexpr int    kLabelMargin   = 10;
static constexpr int    kLabelPadding  = 3;

// ----------------------------------------------------------------
// Konstruktori
// ----------------------------------------------------------------
SplashScreen::SplashScreen( QWidget *parent )
    : QDialog( parent,
               Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint )
{
    // Arvotaan aforismi
    const int idx = static_cast<int>(
        QRandomGenerator::global()->bounded( static_cast<quint32>( s_aphorisms.size() ) ) );
    m_aphorism = s_aphorisms.at( idx );

    // Ladataan taustakuva
    m_pixmap.load( ":/CC/plugin/qMaastoPlugin/images/prismamies_sivistaa50.png" );

    // Kiinteä koko — kuvan mukaan
    const int w = m_pixmap.isNull() ? 768 : m_pixmap.width();
    const int h = m_pixmap.isNull() ? 512 : m_pixmap.height();
    setFixedSize( w, h );

    // Keskitetään vanhempi-widgetin päälle
    if ( parent )
    {
        const QRect pg = parent->geometry();
        move( pg.center() - QPoint( w / 2, h / 2 ) );
    }
    else
    {
        // Ei vanhempaa — keskitetään ruudulle
        const QRect sg = QApplication::primaryScreen()->availableGeometry();
        move( sg.center() - QPoint( w / 2, h / 2 ) );
    }

    // Sulkeutuu automaattisesti 3 sekunnin kuluttua
    QTimer::singleShot( 4000, this, &QDialog::accept );
}

// ----------------------------------------------------------------
// Hiiriklikkaus — sulkee heti
// ----------------------------------------------------------------
void SplashScreen::mousePressEvent( QMouseEvent * /*event*/ )
{
    accept();
}

// ----------------------------------------------------------------
// fitText — etsii sopivan fonttikoon
// ----------------------------------------------------------------
SplashScreen::FitResult SplashScreen::fitText(
    const QString &text, int maxW, int maxH, int maxLines ) const
{
    for ( int size = 18; size >= 7; --size )
    {
        QFont font( "Arial", size, QFont::Bold );
        QFontMetrics fm( font );

        // Rivitetään teksti manuaalisesti sanoja pilkkomalla
        QStringList words = text.split( ' ', Qt::SkipEmptyParts );
        QStringList lines;
        QString     current;

        for ( const QString &word : words )
        {
            const QString candidate = current.isEmpty() ? word : current + ' ' + word;
            if ( fm.horizontalAdvance( candidate ) <= maxW )
            {
                current = candidate;
            }
            else
            {
                if ( !current.isEmpty() )
                    lines.append( current );
                current = word;
            }
        }
        if ( !current.isEmpty() )
            lines.append( current );

        // Tarkistetaan mahtuuko
        const int totalH = fm.height() * lines.size()
                         + fm.leading() * ( lines.size() - 1 );
        if ( lines.size() <= maxLines && totalH <= maxH )
            return { size, lines };
    }

    // Ei mahdu edes 7pt:lla — palautetaan 7pt katkottuna maxLines riviin
    QFont font( "Arial", 7, QFont::Bold );
    QFontMetrics fm( font );
    QStringList words = text.split( ' ', Qt::SkipEmptyParts );
    QStringList lines;
    QString     current;
    for ( const QString &word : words )
    {
        if ( static_cast<int>( lines.size() ) >= maxLines - 1 )
        {
            // Viimeinen rivi — lisätään loput
            current += ( current.isEmpty() ? "" : " " ) + word;
        }
        else
        {
            const QString candidate = current.isEmpty() ? word : current + ' ' + word;
            if ( fm.horizontalAdvance( candidate ) <= maxW )
                current = candidate;
            else
            {
                if ( !current.isEmpty() )
                    lines.append( current );
                current = word;
            }
        }
    }
    if ( !current.isEmpty() )
        lines.append( current );
    return { 7, lines };
}

// ----------------------------------------------------------------
// paintEvent — piirtää kuvan + tekstit
// ----------------------------------------------------------------
void SplashScreen::paintEvent( QPaintEvent * /*event*/ )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setRenderHint( QPainter::TextAntialiasing );

    // 1. Taustakuva
    if ( !m_pixmap.isNull() )
        p.drawPixmap( 0, 0, m_pixmap );

    p.setPen( Qt::black );

    // 2. "Sivistynyt Prismamies sivistää:" — oikeaan alakulmaan, 22pt bold, valkoinen pohja
    {
        QFont labelFont( "Arial", kLabelFontSize, QFont::Bold );
        p.setFont( labelFont );
        QFontMetrics fm( labelFont );
        const int lw = fm.horizontalAdvance( kLabelText );
        const int lh = fm.height();
        const int lx = width()  - lw - kLabelMargin;
        const int ly = height() - lh - kLabelMargin;

        // Valkoinen tausta tekstin takana
        p.fillRect( lx - kLabelPadding,
                    ly - kLabelPadding,
                    lw + kLabelPadding * 2,
                    lh + kLabelPadding * 2,
                    Qt::white );

        // Teksti valkoisen pohjan päälle
        p.setPen( Qt::black );
        p.drawText( lx, ly + fm.ascent(), kLabelText );
    }

    // 3. Aforismi — määritellylle alueelle, automaattinen fonttikoko
    {
        const FitResult fit = fitText( m_aphorism, kTextW, kTextH, kMaxLines );

        QFont aphoFont( "Arial", fit.fontSize, QFont::Bold );
        p.setFont( aphoFont );
        QFontMetrics fm( aphoFont );

        const int lineH   = fm.height();
        const int leading = fm.leading();
        const int totalH  = lineH * fit.lines.size()
                          + leading * ( static_cast<int>( fit.lines.size() ) - 1 );

        // Pystysuunnassa keskitetään tekstialueelle
        int y = kTextY1 + ( kTextH - totalH ) / 2 + fm.ascent();

        for ( const QString &line : fit.lines )
        {
            p.drawText( kTextX1, y, line );
            y += lineH + leading;
        }
    }
}
