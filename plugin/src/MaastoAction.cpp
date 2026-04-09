#include "MaastoAction.h"

#include "ccMainAppInterface.h"
#include "ccHObjectCaster.h"
#include "ccPointCloud.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QLabel>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace MaastoPlugin
{
    // ----------------------------------------------------------------
    // Apufunktiot
    // ----------------------------------------------------------------

    QStringList getScalarFieldNames( ccPointCloud *cloud )
    {
        QStringList names;
        if ( cloud == nullptr )
            return names;

        for ( unsigned i = 0; i < cloud->getNumberOfScalarFields(); ++i )
            names << QString( cloud->getScalarFieldName( i ) );

        names.sort( Qt::CaseInsensitive );
        return names;
    }

    QStringList getScalarFieldValues( ccPointCloud *cloud, const QString &fieldName )
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

        QSet<float> uniqueValues;
        for ( std::size_t i = 0; i < sf->currentSize(); ++i )
            uniqueValues.insert( static_cast<float>( sf->getValue( i ) ) );

        QList<float> sorted = uniqueValues.values();
        std::sort( sorted.begin(), sorted.end() );

        for ( float val : sorted )
        {
            if ( val == static_cast<float>( static_cast<int>( val ) ) )
                result << QString::number( static_cast<int>( val ) );
            else
                result << QString::number( static_cast<double>( val ), 'f', 6 );
        }

        return result;
    }

    // ----------------------------------------------------------------
    // MaastoDialog
    // ----------------------------------------------------------------

    MaastoDialog::MaastoDialog( ccMainAppInterface *appInterface, QWidget *parent )
        : QDialog( parent )
        , m_appInterface( appInterface )
        , m_cloud( nullptr )
        , m_comboBox( nullptr )
        , m_listWidget( nullptr )
    {
        setWindowTitle( "MaastoPlugin" );
        setMinimumWidth( 320 );
        // Ei-modaali: käyttäjä voi käyttää CC:tä dialogin ollessa auki
        setWindowFlags( windowFlags() | Qt::Window );

        QVBoxLayout *layout = new QVBoxLayout( this );

        QLabel *sfLabel = new QLabel( "Scalar field:", this );
        layout->addWidget( sfLabel );

        m_comboBox = new QComboBox( this );
        layout->addWidget( m_comboBox );

        QLabel *valuesLabel = new QLabel( "Arvot:", this );
        layout->addWidget( valuesLabel );

        m_listWidget = new QListWidget( this );
        m_listWidget->setMinimumHeight( 150 );
        layout->addWidget( m_listWidget );

        // Päivitä arvolista kun combobox muuttuu
        connect( m_comboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                populateValueList( fieldName );
            } );
    }

    void MaastoDialog::updateCloud( ccPointCloud *cloud )
    {
        // Tallenna nykyinen valinta ennen päivitystä
        QString currentField = m_comboBox->currentText();

        m_cloud = cloud;

        // Päivitä combobox — säilytä valinta jos mahdollista
        populateComboBox( currentField );
    }

    void MaastoDialog::populateComboBox( const QString &keepField )
    {
        // Estetään turhat currentTextChanged-signaalit päivityksen aikana
        m_comboBox->blockSignals( true );
        m_comboBox->clear();

        QStringList names = getScalarFieldNames( m_cloud );
        m_comboBox->addItems( names );
        m_comboBox->blockSignals( false );

        if ( names.isEmpty() )
        {
            m_listWidget->clear();
            return;
        }

        // Yritä säilyttää edellinen valinta
        int keepIdx = names.indexOf( keepField );
        if ( keepIdx >= 0 )
        {
            m_comboBox->setCurrentIndex( keepIdx );
        }
        else
        {
            // Oletusvalinta: Classification jos löytyy, muuten ensimmäinen
            int classIdx = names.indexOf(
                QRegularExpression( "Classification",
                                    QRegularExpression::CaseInsensitiveOption ) );
            m_comboBox->setCurrentIndex( classIdx >= 0 ? classIdx : 0 );
        }

        populateValueList( m_comboBox->currentText() );
    }

    void MaastoDialog::populateValueList( const QString &fieldName )
    {
        m_listWidget->clear();
        m_listWidget->addItems( getScalarFieldValues( m_cloud, fieldName ) );
    }

    // ----------------------------------------------------------------
    // openDialog — avaa tai nostaa etualalle
    // ----------------------------------------------------------------

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud )
    {
        MaastoDialog *dialog = new MaastoDialog(
            appInterface,
            static_cast<QWidget*>( appInterface->getMainWindow() )
        );

        dialog->updateCloud( cloud );
        dialog->show();
        dialog->raise();
        dialog->activateWindow();

        return dialog;
    }

} // namespace MaastoPlugin
