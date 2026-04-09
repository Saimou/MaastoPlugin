#pragma once

#include "ccStdPluginInterface.h"

namespace MaastoPlugin { class MaastoDialog; }

class qMaastoPlugin : public QObject, public ccStdPluginInterface
{
    Q_OBJECT
    Q_INTERFACES( ccPluginInterface ccStdPluginInterface )

    Q_PLUGIN_METADATA( IID "cccorp.cloudcompare.plugin.qMaastoPlugin"
                       FILE "../info.json" )

public:
    explicit qMaastoPlugin( QObject *parent = nullptr );
    ~qMaastoPlugin() override = default;

    void onNewSelection( const ccHObject::Container &selectedEntities ) override;
    QList<QAction *> getActions() override;

private:
    QAction                    *m_action;
    MaastoPlugin::MaastoDialog *m_dialog;
};
