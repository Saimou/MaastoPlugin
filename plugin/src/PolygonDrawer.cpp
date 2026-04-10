#include "PolygonDrawer.h"

#include "ccPointCloud.h"
#include "ccPolyline.h"
#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"
#include "ccGLWindowSignalEmitter.h"

PolygonDrawer::PolygonDrawer( ccMainAppInterface *app, QObject *parent )
    : QObject( parent )
    , m_app( app )
    , m_glWindow( nullptr )
    , m_vertices( nullptr )
    , m_polyline( nullptr )
    , m_previousPolyline( nullptr )
    , m_previousGLWindow( nullptr )
    , m_drawing( false )
{
}

PolygonDrawer::~PolygonDrawer()
{
    // Siivoa kesken jäänyt piirto
    stopDrawing();

    // Siivoa valmis polygon joka jäi näkyviin GL-ikkunaan
    // m_previousGLWindow tallentaa ikkunan johon polygon rekisteröitiin
    if ( m_previousGLWindow && m_previousPolyline )
        m_previousGLWindow->removeFromOwnDB( m_previousPolyline );

    // Poistetaan vain polyline — vertices on addChild:na polylinella
    // joten se tuhotaan automaattisesti polyline:n mukana
    delete m_previousPolyline;
    m_previousPolyline = nullptr;
    m_previousGLWindow = nullptr;
}

void PolygonDrawer::startDrawing()
{
    if ( m_drawing )
        stopDrawing();

    m_glWindow = m_app->getActiveGLWindow();
    if ( !m_glWindow )
    {
        m_app->dispToConsole( "MaastoPlugin: ei aktiivista 3D-ikkunaa",
                              ccMainAppInterface::ERR_CONSOLE_MESSAGE );
        emit drawingFinished();
        return;
    }

    // Luo uusi polygon-rakenne
    // Edellinen polygon poistetaan vasta kun ensimmäinen piste piirretään
    m_vertices = new ccPointCloud( "MaastoPolygon_verts" );
    m_vertices->setEnabled( false );   // ei renderöidä pistepilvenä

    m_polyline = new ccPolyline( m_vertices );
    m_polyline->addChild( m_vertices );  // polyline omistaa vertices:in
    m_polyline->set2DMode( true );       // 2D screen-space overlay
    m_polyline->setForeground( true );   // piirretään foreground-passissa
    m_polyline->setColor( ccColor::red );
    m_polyline->showColors( true );
    m_polyline->setWidth( 1 );
    m_polyline->showVertices( true );
    m_polyline->setVertexMarkerWidth( 6 );
    m_polyline->setDisplay( m_glWindow );

    m_glWindow->addToOwnDB( m_polyline );

    // Aktivoi mouse-signaalit
    m_glWindow->setPickingMode( ccGLWindowInterface::NO_PICKING );
    m_glWindow->setInteractionMode( ccGLWindowInterface::INTERACT_SEND_ALL_SIGNALS );

    // Kytke hiiri-signaalit
    connect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::leftButtonClicked,
             this, &PolygonDrawer::onLeftClick );
    connect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::rightButtonClicked,
             this, &PolygonDrawer::onRightClick );
    connect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::mouseMoved,
             this, &PolygonDrawer::onMouseMove );

    m_drawing = true;

    m_app->dispToConsole( "MaastoPlugin: Piirrä polygon — vasen click lisää kulma, oikea sulkee",
                          ccMainAppInterface::STD_CONSOLE_MESSAGE );
}

void PolygonDrawer::stopDrawing()
{
    if ( !m_drawing )
        return;

    disconnectFromWindow();

    // Palauta normaali tila
    if ( m_glWindow )
    {
        m_glWindow->setInteractionMode( ccGLWindowInterface::MODE_TRANSFORM_CAMERA );
        m_glWindow->setPickingMode( ccGLWindowInterface::DEFAULT_PICKING );
        m_glWindow->doReleaseMouse();
    }

    // Siivoa kesken jäänyt polygon
    if ( m_polyline )
    {
        if ( m_glWindow )
            m_glWindow->removeFromOwnDB( m_polyline );
        delete m_polyline;         // poistaa myös m_vertices (addChild)
        m_polyline = nullptr;
        m_vertices = nullptr;
    }

    m_glWindow = nullptr;
    m_drawing  = false;
}

void PolygonDrawer::disconnectFromWindow()
{
    if ( m_glWindow && m_glWindow->signalEmitter() )
    {
        disconnect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::leftButtonClicked,
                    this, &PolygonDrawer::onLeftClick );
        disconnect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::rightButtonClicked,
                    this, &PolygonDrawer::onRightClick );
        disconnect( m_glWindow->signalEmitter(), &ccGLWindowSignalEmitter::mouseMoved,
                    this, &PolygonDrawer::onMouseMove );
    }
}

void PolygonDrawer::onLeftClick( int x, int y )
{
    if ( !m_drawing || !m_glWindow || !m_vertices )
        return;

    // Muunna pikselikoordinaateista 2D GL-koordinaatteihin (origo ikkunan keskellä)
    QPointF pos = m_glWindow->toCenteredGLCoordinates( x, y );
    CCVector3 P( static_cast<PointCoordinateType>( pos.x() ),
                 static_cast<PointCoordinateType>( pos.y() ),
                 0 );

    unsigned vertCount = m_vertices->size();

    if ( vertCount == 0 )
    {
        // Ensimmäinen kulma — poista edellinen polygon vasta nyt
        if ( m_previousPolyline )
        {
            if ( m_previousGLWindow )
                m_previousGLWindow->removeFromOwnDB( m_previousPolyline );
            delete m_previousPolyline;
            m_previousPolyline = nullptr;
            m_previousGLWindow = nullptr;
        }

        // Lisätään kaksi pistettä — toinen on rubber-band piste
        if ( !m_vertices->reserve( 2 ) )
            return;
        m_vertices->addPoint( P );   // kiinteä kulma
        m_vertices->addPoint( P );   // rubber-band seuraa-piste
        m_polyline->addPointIndex( 0, 2 );
    }
    else
    {
        // Nth kulma: kiinnitetään viimeinen piste ja lisätään uusi rubber-band
        CCVector3 *lastP = const_cast<CCVector3*>(
            m_vertices->getPointPersistentPtr( vertCount - 1 ) );
        *lastP = P;

        if ( !m_vertices->reserve( vertCount + 1 ) )
            return;
        m_vertices->addPoint( P );
        m_polyline->addPointIndex( vertCount );
        m_polyline->setClosed( true );  // viiva ensimmäisestä viimeiseen
    }

    m_glWindow->doGrabMouse();
    m_glWindow->redraw( true, false );
}

void PolygonDrawer::onMouseMove( int x, int y, Qt::MouseButtons /*buttons*/ )
{
    if ( !m_drawing || !m_glWindow || !m_vertices )
        return;

    unsigned vertCount = m_vertices->size();
    if ( vertCount < 2 )
        return;

    // Päivitä rubber-band piste (aina viimeinen) kursorin mukaan
    QPointF pos = m_glWindow->toCenteredGLCoordinates( x, y );
    CCVector3 *lastP = const_cast<CCVector3*>(
        m_vertices->getPointPersistentPtr( vertCount - 1 ) );
    lastP->x = static_cast<PointCoordinateType>( pos.x() );
    lastP->y = static_cast<PointCoordinateType>( pos.y() );
    lastP->z = 0;

    m_glWindow->redraw( true, false );
}

void PolygonDrawer::onRightClick( int /*x*/, int /*y*/ )
{
    if ( !m_drawing || !m_glWindow || !m_vertices )
        return;

    m_glWindow->doReleaseMouse();

    unsigned vertCount = m_polyline->size();

    if ( vertCount < 4 )
    {
        // Liian vähän pisteitä — peruutetaan
        m_app->dispToConsole( "MaastoPlugin: liian vähän pisteitä polygonille (min 3)",
                              ccMainAppInterface::WRN_CONSOLE_MESSAGE );
        resetCurrentPolygon();
        emit drawingFinished();
        return;
    }

    // Poista rubber-band seuraa-piste ja sulje polygon
    m_polyline->resize( vertCount - 1 );
    m_polyline->setClosed( true );
    m_glWindow->redraw( true, false );

    // Katkaise signaalit ja palauta normaali tila
    disconnectFromWindow();
    m_glWindow->setInteractionMode( ccGLWindowInterface::MODE_TRANSFORM_CAMERA );
    m_glWindow->setPickingMode( ccGLWindowInterface::DEFAULT_PICKING );

    // Tallenna suljetun polygonin 2D-kulmapisteet VolumeBuilder-käyttöön.
    // m_vertices->size() - 1 jättää rubber-band pisteen pois (se on viimeinen
    // piste ja osoittaa hiiren sijaintiin oikeaa nappia painaessa).
    m_closedVertices.clear();
    const unsigned closedN = m_vertices->size() - 1;
    m_closedVertices.reserve( closedN );
    for ( unsigned i = 0; i < closedN; ++i )
    {
        const CCVector3* p = m_vertices->getPoint( i );
        m_closedVertices.push_back( { p->x, p->y, p->z } );
    }

    // Siirrä valmis polygon "edellinen"-slottiin jotta se jää näkyviin.
    // Tallennetaan GL-ikkuna jotta destruktori osaa siivota sen myöhemmin.
    m_previousPolyline = m_polyline;
    m_previousGLWindow = m_glWindow;  // ← tallennetaan ennen nollausta
    m_polyline         = nullptr;
    m_vertices         = nullptr;     // omistajuus on polylinella (addChild)

    m_glWindow = nullptr;
    m_drawing  = false;

    m_app->dispToConsole( "MaastoPlugin: polygon suljettu",
                          ccMainAppInterface::STD_CONSOLE_MESSAGE );

    emit polygonClosed();
    emit drawingFinished();
}

void PolygonDrawer::resetCurrentPolygon()
{
    if ( m_glWindow && m_polyline )
        m_glWindow->removeFromOwnDB( m_polyline );

    delete m_polyline;   // poistaa myös m_vertices (addChild)
    m_polyline = nullptr;
    m_vertices = nullptr;
}
