#include <QtGui>

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

    // Päivitä dialogi jos se on auki
    if ( m_dialog == nullptr )
        return;

    ccPointCloud *cloud = nullptr;
    if ( !selectedEntities.empty() )
        cloud = ccHObjectCaster::ToPointCloud( selectedEntities[0] );

    m_dialog->updateCloud( cloud );
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

            // Avaa uusi dialogi
            m_dialog = MaastoPlugin::openDialog( m_app, cloud );

            // Nollaa pointteri kun dialogi suljetaan
            connect( m_dialog, &QObject::destroyed, this, [this]()
            {
                m_dialog = nullptr;
            } );
        } );
    }

    return { m_action };
}
