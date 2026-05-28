#pragma once

#include <QDialog>
#include <QPixmap>
#include <QString>

// ----------------------------------------------------------------
// SplashScreen
// Käynnistyssivu joka näytetään 3 sekunniksi kun plugin avataan.
// Näyttää kuvan päällä satunnaisen aforismin määritellylle alueelle
// sekä "Prismamies sivistää:" -tekstin oikeaan alakulmaan.
// Sulkeutuu automaattisesti 3s jälkeen tai hiiriklikkauksella.
// ----------------------------------------------------------------
class SplashScreen : public QDialog
{
    Q_OBJECT

public:
    explicit SplashScreen( QWidget *parent = nullptr );

protected:
    void paintEvent( QPaintEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;

private:
    // Sopivan fonttikoon laskeminen sanalle wordwrap-logiikalla.
    // Palauttaa (fontSize, rivitetty teksti) jolla teksti mahtuu
    // leveydelle maxW, max maxLines riville.
    struct FitResult
    {
        int          fontSize;
        QStringList  lines;
    };
    FitResult fitText( const QString &text, int maxW, int maxH, int maxLines ) const;

    QPixmap m_pixmap;   // taustakuva 768x512
    QString m_aphorism; // arvottu aforismi
};
