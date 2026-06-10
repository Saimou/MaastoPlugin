#pragma once

#include <QObject>
#include <vector>
#include <array>

#include "ccGLWindowInterface.h"

class ccMainAppInterface;
class ccPointCloud;
class ccPolyline;
class QPushButton;

// Hallitsee interaktiivisen polygon-piirron CloudComparen 3D-ikkunassa.
//
// Piirto-työnkulku:
//   1. startDrawing()  — aktivoi tilan, kytketään GL-ikkunan mouse-signaaleihin
//   2. Vasen click     — lisää kulmapisteistä, piirtää punaisen viivan
//   3. Hiiri liikkuu   — rubber-band: viimeinen piste seuraa kursoria
//   4. Oikea click     — sulkee polygonin, palautetaan normaali tila
//   5. stopDrawing()   — siivoaa tilan (kutsutaan myös nappia togglaamalla OFF)
//
// Vanha polygon jää näkyviin kunnes ensimmäinen piste uuteen piirretään.

class PolygonDrawer : public QObject
{
    Q_OBJECT

public:
    // 2D-kulmapisteen tallennusmuoto: {x, y, z} centered GL koordinaateissa
    using Point2D = std::array<float, 3>;

    explicit PolygonDrawer( ccMainAppInterface *app, QObject *parent = nullptr );
    ~PolygonDrawer() override;

    // targetWindow: ikkuna johon piirretään. Jos nullptr, käytetään aktiivista ikkunaa.
    void startDrawing( ccGLWindowInterface *targetWindow = nullptr );
    void stopDrawing();

    // Poistaa viimeksi valmiin polygonin GL-ikkunasta (kutsuttavissa ulkoapäin)
    void clearCompletedPolygon();

    bool isDrawing() const { return m_drawing; }

    // Palauttaa viimeksi suljetun polygonin 2D-kulmapisteet (centered GL coords).
    // Tyhjä jos polygonia ei ole vielä suljettu.
    const std::vector<Point2D>& getClosedVertices() const { return m_closedVertices; }

    // Palauttaa ikkunan johon viimeisin polygon piirrettiin (asetettu onRightClick():ssä)
    ccGLWindowInterface *drawnInWindow() const { return m_previousGLWindow; }

signals:
    void polygonClosed();
    void drawingFinished();

private slots:
    void onLeftClick( int x, int y );
    void onRightClick( int x, int y );
    void onMouseMove( int x, int y, Qt::MouseButtons buttons );

private:
    void resetCurrentPolygon();
    void disconnectFromWindow();
    // Poistaa valmiin polygonin GL-ikkunasta (kutsutaan kun kameraa liikutetaan)
    void clearPreviousPolygon();

    ccMainAppInterface  *m_app;
    ccGLWindowInterface *m_glWindow;

    ccPointCloud        *m_vertices;
    ccPolyline          *m_polyline;

    ccPolyline          *m_previousPolyline;
    ccGLWindowInterface *m_previousGLWindow;

    // Suljetun polygonin 2D-kulmapisteet VolumeBuilder-käyttöön
    std::vector<Point2D> m_closedVertices;

    bool                 m_drawing;

    // Tallennetaan piirtoikkunan alkuperäinen picking-moodi ennen kuin
    // startDrawing() asettaa NO_PICKING — palautetaan stopDrawing():ssa
    // jotta esim. View 2:n POINT_PICKING ei tuhoudu piirtämisen jälkeen.
    ccGLWindowInterface::PICKING_MODE m_savedPickingMode;
};
