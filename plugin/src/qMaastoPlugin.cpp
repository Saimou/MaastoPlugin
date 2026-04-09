#include <QtGui>

#include "qMaastoPlugin.h"
#include "MaastoAction.h"

qMaastoPlugin::qMaastoPlugin( QObject *parent )
    : QObject( parent )
    , ccStdPluginInterface( ":/CC/plugin/qMaastoPlugin/info.json" )
    , m_action( nullptr )
{
}

void qMaastoPlugin::onNewSelection( const ccHObject::Container &selectedEntities )
{
    if ( m_action == nullptr )
    {
        return;
    }

    // Nappi on aina aktiivinen riippumatta valinnasta
    m_action->setEnabled( true );
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
            MaastoPlugin::performAction( m_app );
        } );
    }

    return { m_action };
}
