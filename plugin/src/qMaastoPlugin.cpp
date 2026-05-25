#include <QtGui>
#include <QMainWindow>
#include <QMessageBox>

#include "qMaastoPlugin.h"
#include "MaastoAction.h"
#include "ccHObjectCaster.h"
#include "ccPointCloud.h"

qMaastoPlugin::qMaastoPlugin( QObject *parent )
    : QObject( parent )
    , ccStdPluginInterface( ":/CC/plugin/qMaastoPlugin/info.json" )
    , m_action( nullptr )
    , m_dialog( nullptr )
{
}

void qMaastoPlugin::onNewSelection( const ccHObject::Container &selectedEntities )
{
    if ( m_action == nullptr )
        return;

    m_action->setEnabled( true );

    // Dialogi auki → pilvi lukittu, ei päivitetä valinnan muuttuessa
    if ( m_dialog != nullptr )
        return;
}

QList<QAction *> qMaastoPlugin::getActions()
{
    if ( !m_action )
    {
        m_action = new QAction( getName(), this );
        m_action->setToolTip( getDescription() );
        m_action->setIcon( getIcon() );

        connect( m_action, &QAction::triggered, this, [this]()
        {
            if ( m_dialog != nullptr )
            {
                // Dialogi on jo auki — nosta etualalle
                m_dialog->raise();
                m_dialog->activateWindow();
                return;
            }

            // Hae nykyinen valinta
            ccPointCloud *cloud = nullptr;
            const ccHObject::Container &selected = m_app->getSelectedEntities();
            if ( !selected.empty() )
                cloud = ccHObjectCaster::ToPointCloud( selected[0] );

            if ( cloud == nullptr )
            {
                QMessageBox::information(
                    static_cast<QWidget*>( m_app->getMainWindow() ),
                    "Maasto-plugin",
                    "Pistepilveä ei ole valittuna.\n\n"
                    "Valitse pistepilvi DB-puusta (vasen paneeli) ennen pluginin käynnistystä."
                );
                return;
            }

            // Avaa uusi dialogi
            m_dialog = MaastoPlugin::openDialog( m_app, cloud );

            // WA_DeleteOnClose: dialogi tuhotaan (ei vain piilotetaan) kun suljetaan
            // → destroyed-signaali laukeaa oikein → m_dialog nollautuu
            m_dialog->setAttribute( Qt::WA_DeleteOnClose );

            // Nollaa pointteri kun dialogi suljetaan
            connect( m_dialog, &QObject::destroyed, this, [this]()
            {
                m_dialog = nullptr;
            } );
        } );
    }

    return { m_action };
}
