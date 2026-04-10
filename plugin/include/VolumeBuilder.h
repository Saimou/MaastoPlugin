#pragma once

#include "PolygonDrawer.h"

class ccMesh;
class ccGLWindowInterface;

// Rakentaa 3D-prismakappaleen piirretystä 2D-polygonista.
//
// Prisma syntyy projisoimalla polygonin kulmat kahteen syvyyteen
// kameran katselusuunnassa:
//   - Etukansi: nearDist metriä kamerasta
//   - Takakansi: farDist metriä kamerasta
//
// Asetukset (nearDist ja farDist) lisätään myöhemmin dialogin SpinBox-kenttiin.

class VolumeBuilder
{
public:
    // Rakentaa prisma-meshin.
    // polygon2D : suljetun polygonin 2D-kulmapisteet (PolygonDrawer::Point2D)
    // glWindow  : aktiivinen GL-ikkuna — käytetään kameran projektioon
    // nearDist  : etukannen etäisyys kamerasta metreinä (default 10)
    // farDist   : takakannen etäisyys kamerasta metreinä (default 50)
    // Palauttaa uuden ccMesh-olion tai nullptr jos epäonnistuu.
    static ccMesh* build( const std::vector<PolygonDrawer::Point2D>& polygon2D,
                          ccGLWindowInterface*                        glWindow,
                          double                                      nearDist = 10.0,
                          double                                      farDist  = 50.0 );
};
