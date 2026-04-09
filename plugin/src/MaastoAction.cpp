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
        , m_valuesComboBox( nullptr )
        , m_colorComboBox( nullptr )
        , m_listWidget( nullptr )
        , m_updatingCloud( false )
    {
        setWindowTitle( "MaastoPlugin" );
        setMinimumWidth( 320 );
        setWindowFlags( windowFlags() | Qt::Window );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // --- Scalar field (arvolista) ---
        layout->addWidget( new QLabel( "Scalar field:", this ) );
        m_valuesComboBox = new QComboBox( this );
        layout->addWidget( m_valuesComboBox );

        // --- Arvolista ---
        layout->addWidget( new QLabel( "Arvot:", this ) );
        m_listWidget = new QListWidget( this );
        m_listWidget->setMinimumHeight( 150 );
        layout->addWidget( m_listWidget );

        // --- Värjäyskenttä ---
        layout->addWidget( new QLabel( "Värjää pisteet:", this ) );
        m_colorComboBox = new QComboBox( this );
        layout->addWidget( m_colorComboBox );

        // Päivitä arvolista kun valuesComboBox muuttuu
        connect( m_valuesComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                populateValueList( fieldName );
            } );

        // Aseta värjäys kun colorComboBox muuttuu
        connect( m_colorComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                applyColorField( fieldName );
            } );
    }

    void MaastoDialog::updateCloud( ccPointCloud *cloud )
    {
        // Estä uudelleensyöttö: CC saattaa triggeröidä onNewSelection()
        // updateUI():n seurauksena, mikä kutsuisi tätä uudelleen
        if ( m_updatingCloud )
            return;
        m_updatingCloud = true;

        QString keepValues = m_valuesComboBox->currentText();
        QString keepColor  = m_colorComboBox->currentText();

        m_cloud = cloud;

        populateComboBox( m_valuesComboBox, keepValues );
        populateComboBox( m_colorComboBox,  keepColor  );

        m_updatingCloud = false;
    }

    void MaastoDialog::populateComboBox( QComboBox *comboBox, const QString &keepField )
    {
        comboBox->blockSignals( true );
        comboBox->clear();

        QStringList names = getScalarFieldNames( m_cloud );
        comboBox->addItems( names );
        comboBox->blockSignals( false );

        if ( names.isEmpty() )
        {
            if ( comboBox == m_valuesComboBox )
                m_listWidget->clear();
            return;
        }

        // Yritä säilyttää edellinen valinta
        int keepIdx = names.indexOf( keepField );
        if ( keepIdx >= 0 )
        {
            comboBox->setCurrentIndex( keepIdx );
        }
        else
        {
            // Oletusvalinta: Classification jos löytyy, muuten ensimmäinen
            int classIdx = names.indexOf(
                QRegularExpression( "Classification",
                                    QRegularExpression::CaseInsensitiveOption ) );
            comboBox->setCurrentIndex( classIdx >= 0 ? classIdx : 0 );
        }

        if ( comboBox == m_valuesComboBox )
            populateValueList( comboBox->currentText() );

        if ( comboBox == m_colorComboBox )
            applyColorField( comboBox->currentText() );
    }

    void MaastoDialog::populateValueList( const QString &fieldName )
    {
        m_listWidget->clear();
        m_listWidget->addItems( getScalarFieldValues( m_cloud, fieldName ) );
    }

    void MaastoDialog::applyColorField( const QString &fieldName )
    {
        if ( m_cloud == nullptr || fieldName.isEmpty() )
            return;

        int idx = m_cloud->getScalarFieldIndexByName( fieldName.toStdString().c_str() );
        if ( idx < 0 )
            return;

        // Aseta scalar field aktiiviseksi ja kytke SF-väritys päälle
        m_cloud->setCurrentDisplayedScalarField( idx );
        m_cloud->showSF( true );
        m_cloud->prepareDisplayForRefresh();

        // Päivitä vain 3D-näkymä — updateUI() poistettu koska se triggeroi
        // onNewSelection() → updateCloud() → applyColorField() -silmukan
        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // openDialog
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
