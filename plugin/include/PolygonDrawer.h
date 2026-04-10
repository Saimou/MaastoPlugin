#pragma once

#include <QObject>
#include <vector>

class ccMainAppInterface;
class ccGLWindowInterface;
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
// Vanha polygon jää näkyviin kunnes uusi piirto aloitetaan.

class PolygonDrawer : public QObject
{
    Q_OBJECT

public:
    explicit PolygonDrawer( ccMainAppInterface *app, QObject *parent = nullptr );
    ~PolygonDrawer() override;

    // Aloittaa piirto-tilan. Hakee aktiivisen GL-ikkunan.
    void startDrawing();

    // Lopettaa piirto-tilan ja palauttaa GL-ikkunan normaaliin tilaan.
    // Kutsutaan kun nappi togglautuu pois tai polygon suljetaan.
    void stopDrawing();

    bool isDrawing() const { return m_drawing; }

signals:
    // Laukeaa kun polygon on suljettu oikealla hiiren napilla.
    // vertices sisältää kulmat 2D-screenikoordinaateissa.
    void polygonClosed();

    // Laukeaa kun piirto lopetetaan — nappi voidaan palauttaa OFF-tilaan.
    void drawingFinished();

private slots:
    void onLeftClick( int x, int y );
    void onRightClick( int x, int y );
    void onMouseMove( int x, int y, Qt::MouseButtons buttons );

private:
    void resetCurrentPolygon();
    void disconnectFromWindow();

    ccMainAppInterface  *m_app;
    ccGLWindowInterface *m_glWindow;

    // Nykyinen polygon jota piirretään (rubber-band)
    ccPointCloud        *m_vertices;
    ccPolyline          *m_polyline;

    // Edellinen valmis polygon — jää näkyviin kunnes uusi aloitetaan
    ccPolyline          *m_previousPolyline;
    ccPointCloud        *m_previousVertices;

    bool                 m_drawing;
};
