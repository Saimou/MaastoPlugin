#pragma once

#include "PolygonDrawer.h"
#include <vector>

class ccMesh;
class ccPointCloud;
class ccGLWindowInterface;

// Rakentaa 3D-prismakappaleen piirretystä 2D-polygonista ja tarjoaa
// apumetodeja prisman sisällä olevien pisteiden tunnistamiseen.

class VolumeBuilder
{
public:
    // Rakentaa prisma-meshin.
    // polygon2D : suljetun polygonin 2D-kulmapisteet (PolygonDrawer::Point2D)
    // glWindow  : aktiivinen GL-ikkuna — käytetään kameran projektioon
    // nearDist  : etukannen etäisyys kamerasta metreinä (default 10)
    // farDist   : takakannen etäisyys kamerasta metreinä (default 1000)
    // Palauttaa uuden ccMesh-olion tai nullptr jos epäonnistuu.
    static ccMesh* build( const std::vector<PolygonDrawer::Point2D>& polygon2D,
                          ccGLWindowInterface*                        glWindow,
                          double                                      nearDist = 10.0,
                          double                                      farDist  = 1000.0 );

    // Etsii pistepilvestä pisteet jotka ovat prisman sisällä ja luo niistä
    // erillisen korostetun pistepilven (keltainen väri, pistekoko +2).
    // outIndices: jos != nullptr, täytetään sisällä olevien pisteiden
    //             alkuperäisillä indekseillä pistepilvessä (luokittelua varten)
    // Palauttaa uuden pilven tai nullptr jos yksikään piste ei ole sisällä.
    static ccPointCloud* highlightPointsInsideVolume(
        const std::vector<PolygonDrawer::Point2D>& polygon2D,
        ccGLWindowInterface*                        glWindow,
        ccPointCloud*                               cloud,
        double                                      nearDist,
        double                                      farDist,
        std::vector<unsigned>*                      outIndices = nullptr );
};
