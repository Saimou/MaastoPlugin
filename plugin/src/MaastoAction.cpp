#include "MaastoAction.h"

#include "ccMainAppInterface.h"
#include "ccHObjectCaster.h"
#include "ccPointCloud.h"

#include <QMainWindow>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace MaastoPlugin
{
    // Hakee scalar-kenttien nimet pistepilveltä aakkosjärjestyksessä.
    // Palauttaa tyhjän listan jos pilveä ei ole.
    static QStringList getScalarFieldNames( ccPointCloud *cloud )
    {
        QStringList names;
        if ( cloud == nullptr )
            return names;

        for ( unsigned i = 0; i < cloud->getNumberOfScalarFields(); ++i )
        {
            names << QString( cloud->getScalarFieldName( i ) );
        }
        names.sort( Qt::CaseInsensitive );
        return names;
    }

    // Hakee uniikit arvot scalar-kentästä ja palauttaa ne lajiteltuna.
    static QStringList getScalarFieldValues( ccPointCloud *cloud, const QString &fieldName )
    {
        QStringList result;
        if ( cloud == nullptr || fieldName.isEmpty() )
            return result;

        int idx = cloud->getScalarFieldIndexByName( fieldName.toStdString().c_str() );
        if ( idx < 0 )
            return result;

        CCCoreLib::ScalarField *sf = cloud->getScalarField( idx );
        if ( sf == nullptr )
            return result;

        // Kerää uniikit arvot
        QSet<float> uniqueValues;
        for ( std::size_t i = 0; i < sf->currentSize(); ++i )
        {
            uniqueValues.insert( static_cast<float>( sf->getValue( i ) ) );
        }

        // Lajittele numerojärjestyksessä
        QList<float> sorted = uniqueValues.values();
        std::sort( sorted.begin(), sorted.end() );

        // Muunna merkkijonoiksi — kokonaisluku jos arvo on tasan int
        for ( float val : sorted )
        {
            if ( val == static_cast<float>( static_cast<int>( val ) ) )
                result << QString::number( static_cast<int>( val ) );
            else
                result << QString::number( static_cast<double>( val ), 'f', 6 );
        }

        return result;
    }

    void performAction( ccMainAppInterface *appInterface )
    {
        if ( appInterface == nullptr )
        {
            Q_ASSERT( false );
            return;
        }

        // Hae valittu pistepilvi (jos on)
        ccPointCloud *cloud = nullptr;
        const ccHObject::Container &selected = appInterface->getSelectedEntities();
        if ( !selected.empty() )
        {
            cloud = ccHObjectCaster::ToPointCloud( selected[0] );
        }

        // Dialogi
        QDialog dialog( static_cast<QWidget*>( appInterface->getMainWindow() ) );
        dialog.setWindowTitle( "MaastoPlugin" );
        dialog.setMinimumWidth( 320 );

        QVBoxLayout *layout = new QVBoxLayout( &dialog );

        // --- Nappi ---
        QPushButton *button = new QPushButton( "Paina minua!", &dialog );
        layout->addWidget( button );

        // --- Scalar field -otsikko + combobox ---
        QLabel *sfLabel = new QLabel( "Scalar field:", &dialog );
        layout->addWidget( sfLabel );

        QComboBox *comboBox = new QComboBox( &dialog );
        layout->addWidget( comboBox );

        // --- Arvolista ---
        QLabel *valuesLabel = new QLabel( "Arvot:", &dialog );
        layout->addWidget( valuesLabel );

        QListWidget *listWidget = new QListWidget( &dialog );
        listWidget->setMinimumHeight( 150 );
        layout->addWidget( listWidget );

        // --- Täytä combobox ---
        QStringList sfNames = getScalarFieldNames( cloud );
        comboBox->addItems( sfNames );

        // Aseta oletusvalinta: Classification jos löytyy, muuten ensimmäinen
        if ( !sfNames.isEmpty() )
        {
            int classIdx = sfNames.indexOf( QRegularExpression(
                "Classification", QRegularExpression::CaseInsensitiveOption ) );
            if ( classIdx >= 0 )
                comboBox->setCurrentIndex( classIdx );
            else
                comboBox->setCurrentIndex( 0 );

            // Täytä lista oletusvalinnalla
            listWidget->addItems( getScalarFieldValues( cloud, comboBox->currentText() ) );
        }

        // --- Päivitä lista kun combobox muuttuu ---
        QObject::connect( comboBox, &QComboBox::currentTextChanged,
            [&]( const QString &fieldName )
            {
                listWidget->clear();
                listWidget->addItems( getScalarFieldValues( cloud, fieldName ) );
            } );

        // --- Napin painallus tulostaa "hello world!" konsoliin ---
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
