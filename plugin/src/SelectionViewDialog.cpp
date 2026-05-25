#include "SelectionViewDialog.h"

#include <ccMainAppInterface.h>
#include <ccGLWindowInterface.h>
#include <ccGLWindowSignalEmitter.h>
#include <ccPointCloud.h>
#include <ccHObject.h>
#include <ccGLUtils.h>

#include <QCloseEvent>
#include <QToolButton>

SelectionViewDialog::SelectionViewDialog( ccMainAppInterface *app, QWidget *parent )
    : QDialog( parent )
    , m_app( app )
    , m_glWindow( nullptr )
    , m_glWidget( nullptr )
    , m_sceneRoot( nullptr )
    , m_displayCloud( nullptr )
{
    setWindowTitle( "Valinta" );
    setMinimumSize( 500, 400 );
    resize( 650, 520 );

    m_app->createGLWindow( m_glWindow, m_glWidget );
    if ( !m_glWindow || !m_glWidget )
    {
        setEnabled( false );
        return;
    }

    m_glWindow->setPerspectiveState( false, true );
    m_glWindow->displayOverlayEntities( true, true );
    m_glWindow->setInteractionMode(
        ccGLWindowInterface::MODE_TRANSFORM_CAMERA );
    m_glWindow->setPickingMode( ccGLWindowInterface::NO_PICKING );

    m_sceneRoot = new ccHObject( "Valinta" );
    m_glWindow->addToOwnDB( m_sceneRoot );

    auto *mainLayout = new QHBoxLayout( this );
    mainLayout->setContentsMargins( 4, 4, 4, 4 );
    mainLayout->setSpacing( 4 );

    auto *buttonPanel = new QVBoxLayout();
    buttonPanel->setSpacing( 2 );
    createViewButtons( buttonPanel );
    buttonPanel->addStretch();

    mainLayout->addLayout( buttonPanel );
    mainLayout->addWidget( m_glWidget, 1 );
}

SelectionViewDialog::~SelectionViewDialog()
{
    // m_displayCloud is owned by the caller (MaastoDialog), not by us
    // So clear it from scene root before cleanup
    clearCloud();

    if ( m_glWindow )
    {
        m_app->destroyGLWindow( m_glWindow );
        m_glWindow = nullptr;
    }

    // m_sceneRoot was added to m_winDBRoot via addToOwnDB.
    // The window destructor does not delete children of m_winDBRoot,
    // so we must clean it up.
    if ( m_sceneRoot )
    {
        delete m_sceneRoot;
        m_sceneRoot = nullptr;
    }
}

void SelectionViewDialog::showCloud( ccPointCloud *cloud )
{
    if ( !m_glWindow || !m_sceneRoot || !cloud )
        return;

    clearCloud();

    m_sceneRoot->addChild( cloud );
    m_displayCloud = cloud;

    m_glWindow->zoomGlobal();
    m_glWindow->redraw();
}

void SelectionViewDialog::clearCloud()
{
    if ( m_displayCloud && m_sceneRoot )
    {
        m_sceneRoot->removeChild( m_displayCloud );
        m_displayCloud = nullptr;
    }
}

void SelectionViewDialog::closeEvent( QCloseEvent *e )
{
    Q_EMIT closed();
    QDialog::closeEvent( e );
}

void SelectionViewDialog::createViewButtons( QVBoxLayout *layout )
{
    struct ViewDef {
        QString   label;
        QString   iconText;
        CC_VIEW_ORIENTATION orientation;
    };
    const ViewDef views[] = {
        { "Ylhäältä", "T",   CC_TOP_VIEW },
        { "Alhaalta", "B",   CC_BOTTOM_VIEW },
        { "Edestä",   "F",   CC_FRONT_VIEW },
        { "Takaa",    "Bk",  CC_BACK_VIEW },
        { "Vasen",    "L",   CC_LEFT_VIEW },
        { "Oikea",    "R",   CC_RIGHT_VIEW },
    };

    for ( const auto &vd : views )
    {
        auto *btn = new QToolButton( this );
        btn->setText( vd.iconText );
        btn->setToolTip( vd.label );
        btn->setFixedSize( 32, 32 );
        connect( btn, &QToolButton::clicked, this, [this, orientation = vd.orientation]()
        {
            if ( m_glWindow )
                m_glWindow->setView( orientation );
        } );
        layout->addWidget( btn );
    }
}
