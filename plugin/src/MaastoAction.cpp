#include "MaastoAction.h"

#include "ccMainAppInterface.h"
#include "ccHObjectCaster.h"
#include "ccPointCloud.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
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
        , m_listWidget( nullptr )
        , m_targetClassComboBox( nullptr )
        , m_colorComboBox( nullptr )
        , m_updatingCloud( false )
    {
        setWindowTitle( "MaastoPlugin" );
        setMinimumWidth( 320 );
        setWindowFlags( windowFlags() | Qt::Window );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // --- Scalar field ---
        layout->addWidget( new QLabel( "Scalar field:", this ) );
        m_valuesComboBox = new QComboBox( this );
        layout->addWidget( m_valuesComboBox );

        // --- Arvot (checkbox-monivalinta) ---
        layout->addWidget( new QLabel( "Arvot:", this ) );
        m_listWidget = new QListWidget( this );
        m_listWidget->setMinimumHeight( 150 );
        layout->addWidget( m_listWidget );

        // --- Luokittele luokkaan ---
        layout->addWidget( new QLabel( "Luokittele luokkaan:", this ) );
        m_targetClassComboBox = new QComboBox( this );
        layout->addWidget( m_targetClassComboBox );

        // --- Värjää pisteet ---
        layout->addWidget( new QLabel( "Värjää pisteet:", this ) );
        m_colorComboBox = new QComboBox( this );
        layout->addWidget( m_colorComboBox );

        // Päivitä arvolista ja kohdeluokka kun valuesComboBox muuttuu
        connect( m_valuesComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                populateValueList( fieldName );
                populateTargetClassComboBox();
            } );

        // Aseta värjäys kun colorComboBox muuttuu
        connect( m_colorComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                applyColorField( fieldName );
            } );

        // --- Toimintonappi icon.png:llä (alimmaisena) ---
        QPushButton *actionButton = new QPushButton( this );
        actionButton->setIcon( QIcon( ":/CC/plugin/qMaastoPlugin/images/icon.png" ) );
        actionButton->setIconSize( QSize( 96, 96 ) );
        actionButton->setFixedSize( QSize( 128, 128 ) );
        layout->addWidget( actionButton, 0, Qt::AlignCenter );
    }

    void MaastoDialog::updateCloud( ccPointCloud *cloud )
    {
        if ( m_updatingCloud )
            return;
        m_updatingCloud = true;

        QString keepValues      = m_valuesComboBox->currentText();
        QString keepTargetClass = m_targetClassComboBox->currentText();
        QString keepColor       = m_colorComboBox->currentText();

        m_cloud = cloud;

        populateComboBox( m_valuesComboBox, keepValues );
        populateTargetClassComboBox( keepTargetClass );
        populateComboBox( m_colorComboBox, keepColor );

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
        {
            populateValueList( comboBox->currentText() );
            populateTargetClassComboBox();
        }

        if ( comboBox == m_colorComboBox )
            applyColorField( comboBox->currentText() );
    }

    void MaastoDialog::populateValueList( const QString &fieldName )
    {
        m_listWidget->clear();

        const QStringList values = getScalarFieldValues( m_cloud, fieldName );
        for ( const QString &val : values )
        {
            QListWidgetItem *item = new QListWidgetItem( val, m_listWidget );
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            item->setCheckState( Qt::Unchecked );
        }
    }

    void MaastoDialog::populateTargetClassComboBox( const QString &keepValue )
    {
        m_targetClassComboBox->blockSignals( true );
        m_targetClassComboBox->clear();

        // Samat arvot kuin Arvot-listassa — haetaan valitun scalar-kentän mukaan
        const QStringList values = getScalarFieldValues( m_cloud, m_valuesComboBox->currentText() );
        m_targetClassComboBox->addItems( values );
        m_targetClassComboBox->blockSignals( false );

        if ( values.isEmpty() )
            return;

        // Säilytä edellinen valinta jos mahdollista
        int keepIdx = values.indexOf( keepValue );
        m_targetClassComboBox->setCurrentIndex( keepIdx >= 0 ? keepIdx : 0 );
    }

    void MaastoDialog::applyColorField( const QString &fieldName )
    {
        if ( m_cloud == nullptr || fieldName.isEmpty() )
            return;

        int idx = m_cloud->getScalarFieldIndexByName( fieldName.toStdString().c_str() );
        if ( idx < 0 )
            return;

        // Aseta scalar field aktiiviseksi ja kytke SF-väritys päälle.
        // showColors(false) tarvitaan jotta Properties-ikkunan Colors vaihtuu
        // RGB:stä Scalar field -tilaan (sama mitä Properties-paneeli tekee)
        m_cloud->setCurrentDisplayedScalarField( idx );
        m_cloud->showColors( false );
        m_cloud->showSF( true );
        m_cloud->prepareDisplayForRefresh();

        // updateUI() päivittää Properties-paneelin reaaliajassa.
        // m_updatingCloud-lippu estää sen triggeröimän onNewSelection()-silmukan.
        m_updatingCloud = true;
        m_appInterface->updateUI();
        m_updatingCloud = false;

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
