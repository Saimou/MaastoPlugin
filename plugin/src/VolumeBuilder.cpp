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
