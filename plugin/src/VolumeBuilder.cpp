#include "VolumeBuilder.h"
#include "PolygonDrawer.h"

#include "ccPointCloud.h"
#include "ccMesh.h"
#include "ccGLWindowInterface.h"
#include "ccViewportParameters.h"
#include "ManualSegmentationTools.h"

#include <QString>
#include <cmath>

// ----------------------------------------------------------------
// Projisoi yksi 2D centered GL -piste 3D-maailmaan etäisyydellä
// distFromCamera kameran katselusuunnassa.
// ----------------------------------------------------------------
static bool unprojectToWorld( float                px,
                              float                py,
                              ccGLWindowInterface* win,
                              double               distFromCamera,
                              CCVector3d&          outWorld )
{
    ccGLCameraParameters camera;
    win->getGLCameraParameters( camera );

    // Centered GL → corner GL (origo vasemmassa alalaidassa, y ylöspäin)
    const double halfW = camera.viewport[2] * 0.5;
    const double halfH = camera.viewport[3] * 0.5;
    const double cornerX = static_cast<double>( px ) + halfW;
    const double cornerY = static_cast<double>( py ) + halfH;

    // Unprojectoidaan lähitaso ja kaukotaso
    CCVector3d rayNear, rayFar;
    if ( !camera.unproject( CCVector3d( cornerX, cornerY, 0.0 ), rayNear ) )
        return false;
    if ( !camera.unproject( CCVector3d( cornerX, cornerY, 1.0 ), rayFar ) )
        return false;

    CCVector3d rayDir = rayFar - rayNear;
    const double rayLen = rayDir.norm();
    if ( rayLen < 1e-10 )
        return false;
    rayDir /= rayLen;

    // Kameran katselusuunta (forward)
    const ccViewportParameters& vp = win->getViewportParameters();
    CCVector3d forward = vp.getViewDir();

    // Skaalaa säde niin että pisteen syvyys kameran suunnassa on distFromCamera
    const double cosAngle = rayDir.dot( forward );
    if ( std::abs( cosAngle ) < 1e-6 )
        return false;

    const double t = distFromCamera / cosAngle;
    outWorld = rayNear + rayDir * t;
    return true;
}

// ----------------------------------------------------------------
// VolumeBuilder::build
// ----------------------------------------------------------------
ccMesh* VolumeBuilder::build( const std::vector<PolygonDrawer::Point2D>& polygon2D,
                              ccGLWindowInterface*                        glWindow,
                              double                                      nearDist,
                              double                                      farDist )
{
    static int s_counter = 0;

    const unsigned N = static_cast<unsigned>( polygon2D.size() );
    if ( N < 3 || !glWindow )
        return nullptr;

    // ---- Projisoi kulmat 3D-maailmaan ----
    std::vector<CCVector3d> nearPts( N );
    std::vector<CCVector3d> farPts( N );

    for ( unsigned i = 0; i < N; ++i )
    {
        const float px = polygon2D[i][0];
        const float py = polygon2D[i][1];
        if ( !unprojectToWorld( px, py, glWindow, nearDist, nearPts[i] ) )
            return nullptr;
        if ( !unprojectToWorld( px, py, glWindow, farDist,  farPts[i]  ) )
            return nullptr;
    }

    // ---- Luo pistepilvi (2N pistettä) ----
    ccPointCloud* cloud = new ccPointCloud( "MaastoVolume_verts" );
    if ( !cloud->reserve( 2 * N ) )
    {
        delete cloud;
        return nullptr;
    }

    for ( unsigned i = 0; i < N; ++i )
        cloud->addPoint( CCVector3::fromArray( nearPts[i].u ) );
    for ( unsigned i = 0; i < N; ++i )
        cloud->addPoint( CCVector3::fromArray( farPts[i].u ) );

    // ---- Vertex-värit alpha-läpinäkyvyydellä ----
    // alpha 100/255 ≈ 40% läpinäkyvä harmaa
    if ( cloud->reserveTheRGBTable() )
    {
        const ColorCompType r = 200, g = 200, b = 200, a = 100;
        for ( unsigned i = 0; i < 2 * N; ++i )
            cloud->addColor( r, g, b, a );
        cloud->showColors( true );
    }

    // ---- Luo mesh ----
    // Trianglit: etukansi (N-2) + takakansi (N-2) + seinät (N*2)
    const unsigned triCount = ( N - 2 ) * 2 + N * 2;

    ccMesh* mesh = new ccMesh( cloud );
    mesh->addChild( cloud );

    if ( !mesh->reserve( triCount ) )
    {
        delete mesh;
        return nullptr;
    }

    // Etukansi — fan triangulation (pisteet 0..N-1)
    for ( unsigned i = 1; i + 1 < N; ++i )
        mesh->addTriangle( 0, i, i + 1 );

    // Takakansi — käänteinen järjestys (pisteet N..2N-1)
    for ( unsigned i = 1; i + 1 < N; ++i )
        mesh->addTriangle( N, N + i + 1, N + i );

    // Seinät — kaksi kolmiota per kulmaväli
    for ( unsigned i = 0; i < N; ++i )
    {
        const unsigned j = ( i + 1 ) % N;
        mesh->addTriangle( i,     j,     N + i );
        mesh->addTriangle( j,     N + j, N + i );
    }

    // ---- Normaaleille ----
    mesh->computeNormals( true );
    mesh->showColors( true );

    // ---- Nimi ----
    ++s_counter;
    mesh->setName( QString( "MaastoVolume_%1" ).arg( s_counter ) );

    return mesh;
}

// ----------------------------------------------------------------
// VolumeBuilder::highlightPointsInsideVolume
// ----------------------------------------------------------------
ccPointCloud* VolumeBuilder::highlightPointsInsideVolume(
    const std::vector<PolygonDrawer::Point2D>& polygon2D,
    ccGLWindowInterface*                        glWindow,
    ccPointCloud*                               cloud,
    double                                      nearDist,
    double                                      farDist,
    std::vector<unsigned>*                      outIndices )
{
    static int s_highlightCounter = 0;

    if ( polygon2D.empty() || !glWindow || !cloud || cloud->size() == 0 )
        return nullptr;

    // ---- Kameran parametrit ----
    ccGLCameraParameters camera;
    glWindow->getGLCameraParameters( camera );

    const ccViewportParameters& vp = glWindow->getViewportParameters();
    const CCVector3d forward = vp.getViewDir();

    // Kameran world-space sijainti (object-centered vs viewer-centered)
    CCVector3d eyePos = vp.getCameraCenter();
    if ( vp.objectCenteredView )
    {
        CCVector3d PC = vp.getCameraCenter() - vp.getPivotPoint();
        vp.viewMat.inverse().apply( PC );
        eyePos = vp.getPivotPoint() + PC;
    }

    // ---- Polygon corner GL -koordinaateissa (ManualSegmentationTools käyttää näitä) ----
    const double halfW = camera.viewport[2] * 0.5;
    const double halfH = camera.viewport[3] * 0.5;

    std::vector<CCVector2> poly2D;
    poly2D.reserve( polygon2D.size() );
    for ( const auto& pt : polygon2D )
    {
        // centered GL → corner GL
        poly2D.push_back( CCVector2(
            static_cast<PointCoordinateType>( pt[0] + halfW ),
            static_cast<PointCoordinateType>( pt[1] + halfH )
        ) );
    }

    // ---- Käy pistepilvi läpi ja kerää sisällä olevat ----
    std::vector<unsigned> insideIndices;

    for ( unsigned i = 0; i < cloud->size(); ++i )
    {
        const CCVector3* P = cloud->getPoint( i );
        CCVector3d Pd( static_cast<double>( P->x ),
                       static_cast<double>( P->y ),
                       static_cast<double>( P->z ) );

        // Syvyystesti: pisteen syvyys kameran katselusuunnassa
        const double depth = ( Pd - eyePos ).dot( forward );
        if ( depth < nearDist || depth > farDist )
            continue;

        // 2D-projektiotesti: projisoidaan piste kameran näkymään (corner GL)
        CCVector3d P2D;
        if ( !camera.project( Pd, P2D ) )
            continue;

        const CCVector2 p2(
            static_cast<PointCoordinateType>( P2D.x ),
            static_cast<PointCoordinateType>( P2D.y )
        );

        if ( !CCCoreLib::ManualSegmentationTools::isPointInsidePoly( p2, poly2D ) )
            continue;

        insideIndices.push_back( i );
    }

    if ( insideIndices.empty() )
        return nullptr;

    // Täytä outIndices (highlight-pilven luonti on siirretty MaastoDialog:iin)
    if ( outIndices )
    {
        outIndices->insert( outIndices->end(),
                            insideIndices.begin(),
                            insideIndices.end() );
    }

    // Highlight-pilven luonti on siirretty MaastoDialog::createFilteredHighlight():iin
    // jotta sitä voidaan päivittää dynaamisesti Arvot-valinnan mukaan
    return nullptr;
}

// ----------------------------------------------------------------
// VolumeBuilder::buildFromLine
// Rakentaa boksi-meshin (8 kulmapistettä, 6 tahkoa) kahdesta 3D-pisteestä.
//
// Akselin (axisDir) suunnassa ekstruusio rajaantuu pistepilven bounding boxin
// globaaleihin rajoihin (axisMin … axisMax) — ei kiinteään halfExtent-arvoon.
// Paksuussuunta on thickDir = (lineDir × axisDir).normalized().
// Muoto rakentuu KOKONAAN oikealle puolelle viivasta (katsottuna P1→P2):
//   viiva on muodon vasen reuna, oikea reuna on viiva + thickDir * thickness.
// ----------------------------------------------------------------
ccMesh* VolumeBuilder::buildFromLine( const CCVector3& p1,
                                      const CCVector3& p2,
                                      char             axis,
                                      double           thickness,
                                      double           axisMin,
                                      double           axisMax )
{
    static int s_lineCounter = 0;

    CCVector3d p1d( static_cast<double>( p1.x ),
                    static_cast<double>( p1.y ),
                    static_cast<double>( p1.z ) );
    CCVector3d p2d( static_cast<double>( p2.x ),
                    static_cast<double>( p2.y ),
                    static_cast<double>( p2.z ) );

    // Ekstruusioakseli
    CCVector3d axisDir( 0.0, 0.0, 0.0 );
    if      ( axis == 'X' || axis == 'x' ) axisDir.x = 1.0;
    else if ( axis == 'Y' || axis == 'y' ) axisDir.y = 1.0;
    else                                   axisDir.z = 1.0;

    // Viivan suuntavektori
    const CCVector3d lineVec = p2d - p1d;
    const double lineLen = lineVec.norm();
    if ( lineLen < 1e-10 )
        return nullptr;
    const CCVector3d lineDirNorm = lineVec / lineLen;

    // Paksuussuunta: oikea puoli katsottuna P1→P2
    // thickDir = lineDir × axisDir (oikeakätinen koordinaattijärjestelmä)
    CCVector3d thickDir = lineDirNorm.cross( axisDir );
    const double thickDirLen = thickDir.norm();
    if ( thickDirLen < 1e-10 )
        return nullptr;  // viiva yhdensuuntainen akselin kanssa
    thickDir /= thickDirLen;

    const CCVector3d thickOffset = thickDir * thickness;  // koko paksuus oikealle

    // Lasketaan BB-rajoista akseli-offsetit kullekin päätepisteelle erikseen.
    // axisDir on yksikkövektori, joten:
    //   p + axisDir * (axisMax - p.axisCoord)  →  akselin maksimiraja
    //   p + axisDir * (axisMin - p.axisCoord)  →  akselin minimiraja
    auto axisCoord = [&]( const CCVector3d& p ) -> double {
        if ( axis == 'X' || axis == 'x' ) return p.x;
        if ( axis == 'Y' || axis == 'y' ) return p.y;
        return p.z;
    };

    const double p1AxisMaxOff = axisMax - axisCoord( p1d );
    const double p1AxisMinOff = axisMin - axisCoord( p1d );
    const double p2AxisMaxOff = axisMax - axisCoord( p2d );
    const double p2AxisMinOff = axisMin - axisCoord( p2d );

    // 8 kulmapistettä boksille
    // Indeksit:  p1-pää: 0-3,  p2-pää: 4-7
    //   axisMax + thickOffset = 0/4  (oikea reuna, akselin max)
    //   axisMax + 0           = 1/5  (viivan reuna, akselin max)
    //   axisMin + thickOffset = 2/6  (oikea reuna, akselin min)
    //   axisMin + 0           = 3/7  (viivan reuna, akselin min)
    const CCVector3d verts[8] = {
        p1d + axisDir * p1AxisMaxOff + thickOffset,  // 0
        p1d + axisDir * p1AxisMaxOff,                // 1  ← viivan reuna
        p1d + axisDir * p1AxisMinOff + thickOffset,  // 2
        p1d + axisDir * p1AxisMinOff,                // 3  ← viivan reuna
        p2d + axisDir * p2AxisMaxOff + thickOffset,  // 4
        p2d + axisDir * p2AxisMaxOff,                // 5  ← viivan reuna
        p2d + axisDir * p2AxisMinOff + thickOffset,  // 6
        p2d + axisDir * p2AxisMinOff,                // 7  ← viivan reuna
    };

    ccPointCloud *cloud = new ccPointCloud( "MaastoLineVolume_verts" );
    if ( !cloud->reserve( 8 ) )
    {
        delete cloud;
        return nullptr;
    }
    for ( int i = 0; i < 8; ++i )
        cloud->addPoint( CCVector3::fromArray( verts[i].u ) );

    if ( cloud->reserveTheRGBTable() )
    {
        const ColorCompType r = 200, g = 200, b = 200, a = 100;
        for ( unsigned i = 0; i < 8; ++i )
            cloud->addColor( r, g, b, a );
        cloud->showColors( true );
    }

    // 12 kolmiota (6 tahkoa × 2)
    // Tahkot: +thick, -thick, +axis, -axis, p1-pää, p2-pää
    ccMesh *mesh = new ccMesh( cloud );
    mesh->addChild( cloud );
    if ( !mesh->reserve( 12 ) )
    {
        delete mesh;
        return nullptr;
    }

    // +thick-tahko: 0,2,4 ja 2,6,4
    mesh->addTriangle( 0, 2, 4 );
    mesh->addTriangle( 2, 6, 4 );
    // -thick-tahko: 1,5,3 ja 3,5,7
    mesh->addTriangle( 1, 5, 3 );
    mesh->addTriangle( 3, 5, 7 );
    // +axis-tahko: 0,1,4 ja 1,5,4
    mesh->addTriangle( 0, 1, 4 );
    mesh->addTriangle( 1, 5, 4 );
    // -axis-tahko: 2,6,3 ja 3,6,7
    mesh->addTriangle( 2, 6, 3 );
    mesh->addTriangle( 3, 6, 7 );
    // p1-pää: 0,3,2 ja 0,1,3
    mesh->addTriangle( 0, 3, 2 );
    mesh->addTriangle( 0, 1, 3 );
    // p2-pää: 4,6,7 ja 4,7,5
    mesh->addTriangle( 4, 6, 7 );
    mesh->addTriangle( 4, 7, 5 );

    mesh->computeNormals( true );
    mesh->showColors( true );

    ++s_lineCounter;
    mesh->setName( QString( "MaastoLineVolume_%1" ).arg( s_lineCounter ) );

    return mesh;
}

// ----------------------------------------------------------------
// VolumeBuilder::highlightPointsInsideLineVolume
// Testaa jokaisen pisteen suhteessa viiva+akseli-kappaleeseen.
//
// Muoto on kokonaan viivan OIKEALLA puolella (katsottuna P1→P2):
// thickDir = (lineDir × axisDir).normalized() osoittaa oikealle.
//
// Piste hyväksytään jos:
//   1. Projektio viivalle (p1→p2) on välillä [0, viivan pituus]
//   2. v · thickDir ∈ [0, thickness]  (viivan reunasta oikealle)
// ----------------------------------------------------------------
void VolumeBuilder::highlightPointsInsideLineVolume(
    const CCVector3&       p1,
    const CCVector3&       p2,
    char                   axis,
    ccPointCloud*          cloud,
    double                 thickness,
    std::vector<unsigned>* outIndices )
{
    if ( !cloud || cloud->size() == 0 || !outIndices )
        return;

    // Ekstruusioakselin suuntavektori (yksikkövektori)
    CCVector3d axisDir( 0.0, 0.0, 0.0 );
    if      ( axis == 'X' || axis == 'x' ) axisDir.x = 1.0;
    else if ( axis == 'Y' || axis == 'y' ) axisDir.y = 1.0;
    else                                   axisDir.z = 1.0;

    // Viivan suuntavektori ja pituus
    CCVector3d p1d( static_cast<double>( p1.x ),
                    static_cast<double>( p1.y ),
                    static_cast<double>( p1.z ) );
    CCVector3d p2d( static_cast<double>( p2.x ),
                    static_cast<double>( p2.y ),
                    static_cast<double>( p2.z ) );

    const CCVector3d lineVec = p2d - p1d;
    const double lineLen = lineVec.norm();

    if ( lineLen < 1e-10 )
        return;  // degeneroitunut viiva

    const CCVector3d lineDirNorm = lineVec / lineLen;

    // Paksuussuunta: kohtisuoraan sekä viivaan että akseliin
    // thickDir = lineDir × axisDir  (ristiintulo)
    CCVector3d thickDir = lineDirNorm.cross( axisDir );
    const double thickDirLen = thickDir.norm();

    if ( thickDirLen < 1e-10 )
    {
        // Viiva on samansuuntainen kuin akseli — kaikki pisteet viivan
        // "putkessa" kelpaavat; käytetään varasuuntana mitä tahansa
        // kohtisuoraa suuntaa. Tässä poikkeustilanteessa ei voi
        // paksuustestiä tehdä mielekkäästi — hyväksytään kaikki
        // projektiotestissä läpipäässeet pisteet.
        for ( unsigned i = 0; i < cloud->size(); ++i )
        {
            const CCVector3 *P = cloud->getPoint( i );
            CCVector3d Pd( static_cast<double>( P->x ),
                           static_cast<double>( P->y ),
                           static_cast<double>( P->z ) );
            const double t = ( Pd - p1d ).dot( lineDirNorm );
            if ( t >= 0.0 && t <= lineLen )
                outIndices->push_back( i );
        }
        return;
    }

    thickDir /= thickDirLen;  // normalisoi

    for ( unsigned i = 0; i < cloud->size(); ++i )
    {
        const CCVector3 *P = cloud->getPoint( i );
        CCVector3d Pd( static_cast<double>( P->x ),
                       static_cast<double>( P->y ),
                       static_cast<double>( P->z ) );

        const CCVector3d v = Pd - p1d;

        // 1. Projektio viivan suunnassa: t ∈ [0, lineLen]
        const double t = v.dot( lineDirNorm );
        if ( t < 0.0 || t > lineLen )
            continue;

        // 2. Etäisyys thickDir-suunnassa: piste on viivan ja oikean reunan välissä
        //    d = 0 → viivan reunalla, d = thickness → oikea reuna
        const double d = v.dot( thickDir );
        if ( d >= 0.0 && d <= thickness )
            outIndices->push_back( i );
    }
}
