#pragma once

#include "PolygonDrawer.h"
#include <vector>

#include "CCGeom.h"

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

    // Rakentaa suorakaiteisen "seinä"-meshin kahdesta 3D-pisteestä ekstruoimalla
    // globaalin akselin suuntaan ±halfExtent metriä.
    // axis       : 'X', 'Y' tai 'Z' — ekstruusioakseli
    // halfExtent : puoli-ulottuvuus akselin suuntaan (default 10 000 m)
    // Palauttaa uuden ccMesh-olion tai nullptr jos epäonnistuu.
    static ccMesh* buildFromLine( const CCVector3& p1,
                                  const CCVector3& p2,
                                  char             axis,
                                  double           thickness,
                                  double           halfExtent = 10000.0 );

    // Etsii pistepilvestä pisteet jotka ovat "viiva+akseli"-kappaleen sisällä.
    // Testi: piste hyväksytään jos sen etäisyys viivasta (akselin suuntainen
    // komponentti jätetty pois) on ≤ halfThickness ja sen projektio viivalle
    // on välillä [0, viivan pituus].
    // outIndices: täytetään löydetyillä pisteindekseillä.
    static void highlightPointsInsideLineVolume(
        const CCVector3&       p1,
        const CCVector3&       p2,
        char                   axis,
        ccPointCloud*          cloud,
        double                 thickness,
        std::vector<unsigned>* outIndices );
};
