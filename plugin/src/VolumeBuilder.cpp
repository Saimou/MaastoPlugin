#include "VolumeBuilder.h"
#include "PolygonDrawer.h"

#include "ccPointCloud.h"
#include "ccMesh.h"
#include "ccGLWindowInterface.h"
#include "ccViewportParameters.h"

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

    // ---- Nimi ----
    ++s_counter;
    mesh->setName( QString( "MaastoVolume_%1" ).arg( s_counter ) );

    return mesh;
}
