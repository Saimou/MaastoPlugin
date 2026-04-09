#include "MaastoAction.h"

#include "ccMainAppInterface.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace MaastoPlugin
{
    void performAction( ccMainAppInterface *appInterface )
    {
        if ( appInterface == nullptr )
        {
            Q_ASSERT( false );
            return;
        }

        // Avaa dialogi-ikkuna
        QDialog dialog( appInterface->getMainWindow() );
        dialog.setWindowTitle( "MaastoPlugin" );
        dialog.setMinimumWidth( 300 );

        QVBoxLayout *layout = new QVBoxLayout( &dialog );

        QLabel *label = new QLabel( "MaastoPlugin", &dialog );
        label->setAlignment( Qt::AlignCenter );
        layout->addWidget( label );

        QPushButton *button = new QPushButton( "Paina minua!", &dialog );
        layout->addWidget( button );

        // Napin painallus tulostaa "hello world!" konsoliin
        QObject::connect( button, &QPushButton::clicked, [&]()
        {
            appInterface->dispToConsole(
                "hello world!",
                ccMainAppInterface::STD_CONSOLE_MESSAGE
            );
            dialog.accept();
        } );

        dialog.exec();
    }
}
