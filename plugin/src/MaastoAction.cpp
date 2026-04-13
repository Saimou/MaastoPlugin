#include "MaastoAction.h"
#include "PolygonDrawer.h"
#include "VolumeBuilder.h"
#include "ClassDefinition.h"

#include "ccMainAppInterface.h"
#include "ccHObjectCaster.h"
#include "ccMesh.h"
#include "ccPointCloud.h"
#include "ccScalarField.h"
#include "ccColorScale.h"
#include "ccHObject.h"
#include "ccColorTypes.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileDialog>
#include <QHeaderView>
#include <QDir>
#include <QRegularExpression>
#include <QIcon>
#include <QMap>
#include <map>
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

    MaastoDialog::~MaastoDialog()
    {
        // Palauta pistepilvi alkutilaan ennen tuhoamista
        removePtcColors();
        resetVisibility();
    }

    MaastoDialog::MaastoDialog( ccMainAppInterface *appInterface, QWidget *parent )
        : QDialog( parent )
        , m_appInterface( appInterface )
        , m_cloud( nullptr )
        , m_readFileButton( nullptr )
        , m_valuesComboBox( nullptr )
        , m_listWidget( nullptr )
        , m_selectAllButton( nullptr )
        , m_targetClassComboBox( nullptr )
        , m_colorComboBox( nullptr )
        , m_visibilityListWidget( nullptr )
        , m_selectAllVisButton( nullptr )
        , m_updatingCloud( false )
        , m_updatingVisibility( false )
        , m_ptcColorsApplied( false )
        , m_ptcFileLabel( nullptr )
        , m_polygonDrawer( new PolygonDrawer( appInterface, this ) )
        , m_polygonButton( nullptr )
        , m_dualPolygonCheckbox( nullptr )
        , m_nearDistSpinBox( nullptr )
        , m_farDistSpinBox( nullptr )
    {
        setWindowTitle( "MaastoPlugin" );
        setMinimumWidth( 380 );
        setWindowFlags( windowFlags() | Qt::Window );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // --- Read class definition file -nappi (ylimpänä) ---
        m_readFileButton = new QPushButton( "Read class definition file", this );
        layout->addWidget( m_readFileButton );

        // Polkunäyttö Read-napin alle (piilotettu kunnes tiedosto on ladattu)
        m_ptcFileLabel = new QLabel( "", this );
        m_ptcFileLabel->setVisible( false );
        m_ptcFileLabel->setWordWrap( true );
        m_ptcFileLabel->setStyleSheet( "color: gray; font-size: 9pt;" );
        layout->addWidget( m_ptcFileLabel );

        connect( m_readFileButton, &QPushButton::clicked, this, [this]()
        {
            const QString file = QFileDialog::getOpenFileName(
                this, "Open class definition", "",
                "PTC files (*.ptc);;All files (*.*)" );
            if ( !file.isEmpty() )
                loadPtcFile( file );
        } );

        // --- Scalar field ---
        layout->addWidget( new QLabel( "Scalar field:", this ) );
        m_valuesComboBox = new QComboBox( this );
        layout->addWidget( m_valuesComboBox );

        // --- Luokat (checkbox-monivalinta) + toggle-nappi ---
        {
            QHBoxLayout *arvoHeader = new QHBoxLayout();
            arvoHeader->addWidget( new QLabel( "Luokat:", this ) );
            m_selectAllButton = new QPushButton( "Valitse kaikki", this );
            m_selectAllButton->setCheckable( true );
            m_selectAllButton->setFixedHeight( 22 );
            arvoHeader->addWidget( m_selectAllButton );
            layout->addLayout( arvoHeader );
        }
        m_listWidget = new QTreeWidget( this );
        m_listWidget->setMinimumHeight( 150 );
        m_listWidget->setHeaderHidden( true );
        m_listWidget->setRootIsDecorated( false );
        layout->addWidget( m_listWidget );

        // --- Luokittele luokkaan ---
        layout->addWidget( new QLabel( "Luokittele luokkaan:", this ) );
        m_targetClassComboBox = new QComboBox( this );
        layout->addWidget( m_targetClassComboBox );

        // --- Pisteiden väritys (RGB + scalar-kentät) ---
        layout->addWidget( new QLabel( "Pisteiden väritys:", this ) );
        m_colorComboBox = new QComboBox( this );
        layout->addWidget( m_colorComboBox );

        // --- Näkyvät luokat -lista (visibility mask) ---
        {
            QHBoxLayout *visHeader = new QHBoxLayout();
            visHeader->addWidget( new QLabel( "Näkyvät luokat:", this ) );
            m_selectAllVisButton = new QPushButton( "Valitse kaikki", this );
            m_selectAllVisButton->setCheckable( true );
            m_selectAllVisButton->setChecked( true );   // oletuksena kaikki valittuna
            m_selectAllVisButton->setFixedHeight( 22 );
            visHeader->addWidget( m_selectAllVisButton );
            layout->addLayout( visHeader );
        }
        m_visibilityListWidget = new QTreeWidget( this );
        m_visibilityListWidget->setMinimumHeight( 120 );
        m_visibilityListWidget->setHeaderHidden( true );
        m_visibilityListWidget->setRootIsDecorated( false );
        layout->addWidget( m_visibilityListWidget );

        // Päivitä arvolista ja kohdeluokka kun valuesComboBox muuttuu
        connect( m_valuesComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                populateValueList( fieldName );
                populateTargetClassComboBox();
            } );

        // Aseta värjäys + päivitä näkyvyyslista kun colorComboBox muuttuu
        connect( m_colorComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                applyColorField( fieldName );
                populateVisibilityList( fieldName );
            } );

        // Toggle: Valitse kaikki / Poista valinnat
        connect( m_selectAllButton, &QPushButton::toggled,
            [this]( bool checked )
            {
                m_selectAllButton->setText( checked ? "Poista valinnat" : "Valitse kaikki" );
                const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
                m_listWidget->blockSignals( true );
                for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
                    m_listWidget->topLevelItem( i )->setCheckState( 0, state );
                m_listWidget->blockSignals( false );
            } );

        // Jos käyttäjä klikkaa yksittäistä riviä: palauta nappi OFF + päivitä highlightit
        connect( m_listWidget, &QTreeWidget::itemChanged,
            [this]( QTreeWidgetItem* )
            {
                m_selectAllButton->blockSignals( true );
                m_selectAllButton->setChecked( false );
                m_selectAllButton->setText( "Valitse kaikki" );
                m_selectAllButton->blockSignals( false );
                refreshHighlights();
            } );

        // Näkyvät luokat: toggle Valitse kaikki / Poista valinnat
        connect( m_selectAllVisButton, &QPushButton::toggled,
            [this]( bool checked )
            {
                m_selectAllVisButton->setText( checked ? "Poista valinnat" : "Valitse kaikki" );
                const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
                m_visibilityListWidget->blockSignals( true );
                for ( int i = 0; i < m_visibilityListWidget->topLevelItemCount(); ++i )
                    m_visibilityListWidget->topLevelItem( i )->setCheckState( 0, state );
                m_visibilityListWidget->blockSignals( false );
                applyVisibilityFilter();
            } );

        // Yksittäisen rivin klikkaus: päivitä nappi + visibility
        connect( m_visibilityListWidget, &QTreeWidget::itemChanged,
            [this]( QTreeWidgetItem* )
            {
                if ( m_updatingVisibility )
                    return;
                m_selectAllVisButton->blockSignals( true );
                m_selectAllVisButton->setChecked( false );
                m_selectAllVisButton->setText( "Valitse kaikki" );
                m_selectAllVisButton->blockSignals( false );
                applyVisibilityFilter();
            } );

        // --- Napbirivi alimmaisena ---
        QHBoxLayout *buttonRow = new QHBoxLayout();

        m_polygonButton = new QPushButton( "Piirrä polygon", this );
        m_polygonButton->setCheckable( true );
        buttonRow->addWidget( m_polygonButton );

        QPushButton *actionButton = new QPushButton( this );
        actionButton->setIcon( QIcon( ":/CC/plugin/qMaastoPlugin/images/icon.png" ) );
        actionButton->setIconSize( QSize( 96, 96 ) );
        actionButton->setFixedSize( QSize( 128, 128 ) );

        // Vaihda ikoni kun nappi painetaan alas / vapautetaan
        connect( actionButton, &QPushButton::pressed, this, [actionButton]()
        {
            actionButton->setIcon(
                QIcon( ":/CC/plugin/qMaastoPlugin/images/icon_pressed.png" ) );
        } );
        connect( actionButton, &QPushButton::released, this, [actionButton]()
        {
            actionButton->setIcon(
                QIcon( ":/CC/plugin/qMaastoPlugin/images/icon.png" ) );
        } );
        buttonRow->addWidget( actionButton );

        layout->addLayout( buttonRow );

        // Iso nappi käynnistää luokittelun
        connect( actionButton, &QPushButton::clicked, this, [this]()
        {
            performClassification();
        } );

        // --- Kahden polygonin luokittelu ---
        m_dualPolygonCheckbox = new QCheckBox( "Kahden polygonin luokittelu", this );
        layout->addWidget( m_dualPolygonCheckbox );

        // Kun checkbox muuttuu → päivitä highlight
        connect( m_dualPolygonCheckbox, &QCheckBox::toggled, this, [this]()
        {
            refreshHighlights();
        } );

        // --- Etäisyysasetukset ---
        QFormLayout *distForm = new QFormLayout();
        distForm->setContentsMargins( 0, 4, 0, 4 );

        m_nearDistSpinBox = new QDoubleSpinBox( this );
        m_nearDistSpinBox->setRange( 0.1, 9999.0 );
        m_nearDistSpinBox->setSingleStep( 0.5 );
        m_nearDistSpinBox->setValue( 10.0 );
        m_nearDistSpinBox->setSuffix( " m" );
        m_nearDistSpinBox->setDecimals( 1 );
        distForm->addRow( "Lähin etäisyys:", m_nearDistSpinBox );

        m_farDistSpinBox = new QDoubleSpinBox( this );
        m_farDistSpinBox->setRange( 0.1, 9999.0 );
        m_farDistSpinBox->setSingleStep( 0.5 );
        m_farDistSpinBox->setValue( 1000.0 );
        m_farDistSpinBox->setSuffix( " m" );
        m_farDistSpinBox->setDecimals( 1 );
        distForm->addRow( "Pisin etäisyys:", m_farDistSpinBox );

        layout->addLayout( distForm );

        // Validointi: lähin < pisin
        connect( m_nearDistSpinBox, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
            [this]( double val )
            {
                if ( val >= m_farDistSpinBox->value() )
                    m_farDistSpinBox->setValue( val + 1.0 );
            } );
        connect( m_farDistSpinBox, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
            [this]( double val )
            {
                if ( val <= m_nearDistSpinBox->value() )
                    m_nearDistSpinBox->setValue( val - 1.0 );
            } );

        // Polygon-piirto: nappi toggleataan ON/OFF
        connect( m_polygonButton, &QPushButton::toggled, this, [this]( bool checked )
        {
            if ( checked )
                m_polygonDrawer->startDrawing();
            else
                m_polygonDrawer->stopDrawing();
        } );

        // Kun polygon suljetaan: rakenna 3D-kappale ja aloita uusi piirto
        connect( m_polygonDrawer, &PolygonDrawer::polygonClosed, this, [this]()
        {
            // Rakenna prisma-mesh piirretystä polygonista
            ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
            if ( win && !m_polygonDrawer->getClosedVertices().empty() )
            {
                ccMesh *mesh = VolumeBuilder::build(
                    m_polygonDrawer->getClosedVertices(),
                    win,
                    m_nearDistSpinBox->value(),
                    m_farDistSpinBox->value()
                );
                if ( mesh )
                {
                    m_appInterface->addToDB( mesh );
                    m_meshObjects.push_back( mesh );

                    // Kerää prisman sisällä olevien pisteiden indeksit
                    if ( m_cloud != nullptr )
                    {
                        std::vector<unsigned> indices;
                        VolumeBuilder::highlightPointsInsideVolume(
                            m_polygonDrawer->getClosedVertices(),
                            win,
                            m_cloud,
                            m_nearDistSpinBox->value(),
                            m_farDistSpinBox->value(),
                            &indices
                        );
                        if ( !indices.empty() )
                        {
                            for ( unsigned idx : indices )
                                m_indexHitCount[idx]++;
                        }
                        else
                        {
                            m_appInterface->dispToConsole(
                                "MaastoPlugin: prisman sisällä ei pisteitä",
                                ccMainAppInterface::STD_CONSOLE_MESSAGE );
                        }
                    }

                    // Päivitä highlight-pilvet valittujen arvojen mukaan
                    refreshHighlights();
                }
                else
                {
                    m_appInterface->dispToConsole(
                        "MaastoPlugin: 3D-kappaleen luonti epäonnistui",
                        ccMainAppInterface::WRN_CONSOLE_MESSAGE );
                }
            }

            // Aloita uusi piirto automaattisesti
            m_polygonDrawer->startDrawing();
        } );
    }

    void MaastoDialog::updateCloud( ccPointCloud *cloud )
    {
        if ( m_updatingCloud )
            return;
        m_updatingCloud = true;

        // Palauta vanha pilvi alkutilaan ennen vaihtoa
        removePtcColors();
        resetVisibility();

        // Jos pilvi vaihtuu, nollaa luokkamääritykset ja polkunäyttö
        if ( m_cloud != cloud )
        {
            m_classDefinitions.clear();
            m_classCounts.clear();
            m_ptcFileLabel->setText( "" );
            m_ptcFileLabel->setVisible( false );
        }

        QString keepValues      = m_valuesComboBox->currentText();
        QString keepTargetClass = m_targetClassComboBox->currentText();
        QString keepColor       = m_colorComboBox->currentText();

        m_cloud = cloud;

        // Automaattinen .ptc-lataus jos ei ladattu
        if ( m_classDefinitions.isEmpty() )
            tryAutoLoadPtcFile();

        populateComboBox( m_valuesComboBox, keepValues );
        populateTargetClassComboBox( keepTargetClass );
        populateColorComboBox( keepColor );

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

    void MaastoDialog::computeClassCounts( const QString &fieldName )
    {
        m_classCounts.clear();
        if ( !m_cloud || fieldName.isEmpty() )
            return;

        int sfIdx = m_cloud->getScalarFieldIndexByName( fieldName.toStdString().c_str() );
        if ( sfIdx < 0 )
            return;

        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf )
            return;

        for ( unsigned i = 0; i < m_cloud->size(); ++i )
            m_classCounts[ static_cast<int>( sf->getValue( i ) ) ]++;
    }

    void MaastoDialog::populateValueList( const QString &fieldName )
    {
        // Laske pisteiden määrät ennen listan täyttöä
        computeClassCounts( fieldName );

        // Tallenna nykyiset valinnat ennen tyhjennystä
        QSet<QString> checkedValues;
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
            if ( m_listWidget->topLevelItem( i )->checkState( 0 ) == Qt::Checked )
                checkedValues.insert( m_listWidget->topLevelItem( i )->text( 0 ) );

        m_listWidget->blockSignals( true );
        m_listWidget->clear();

        const QStringList values = getScalarFieldValues( m_cloud, fieldName );

        // Näytetään Name + Color sarakkeet jos Scalar field on Classification ja .ptc on ladattu
        const bool isClassif = ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
                               && !m_classDefinitions.isEmpty();

        // Count-sarake näytetään aina kun pilvi on valittu
        const bool hasCloud = ( m_cloud != nullptr ) && !values.isEmpty();

        if ( hasCloud )
        {
            if ( isClassif )
            {
                m_listWidget->setColumnCount( 4 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Name", "Color", "Count" } );
                m_listWidget->header()->setStretchLastSection( false );
                m_listWidget->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
                m_listWidget->header()->setSectionResizeMode( 1, QHeaderView::Stretch );
                m_listWidget->header()->setSectionResizeMode( 2, QHeaderView::Fixed );
                m_listWidget->header()->resizeSection( 2, 40 );
                m_listWidget->header()->setSectionResizeMode( 3, QHeaderView::ResizeToContents );
            }
            else
            {
                m_listWidget->setColumnCount( 2 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Count" } );
                m_listWidget->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
                m_listWidget->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
            }
        }
        else
        {
            m_listWidget->setColumnCount( 1 );
            m_listWidget->setHeaderHidden( true );
        }

        for ( const QString &val : values )
        {
            QTreeWidgetItem *item = new QTreeWidgetItem( m_listWidget );
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            item->setCheckState( 0, checkedValues.contains( val )
                                    ? Qt::Checked : Qt::Unchecked );
            item->setText( 0, val );

            const int intVal = val.toInt();

            if ( isClassif && m_classDefinitions.contains( intVal ) )
            {
                const ClassDefinition &def = m_classDefinitions[intVal];
                item->setText( 1, def.name );
                if ( def.color.isValid() )
                {
                    QPixmap px( 20, 20 );
                    px.fill( def.color );
                    item->setIcon( 2, QIcon( px ) );
                }
            }

            // Count-sarake: oikea sarakeindeksi riippuu isClassif:stä
            if ( hasCloud )
            {
                const int countCol = isClassif ? 3 : 1;
                const int count = m_classCounts.value( intVal, 0 );
                item->setText( countCol, QString::number( count ) );
                item->setTextAlignment( countCol, Qt::AlignRight | Qt::AlignVCenter );
            }
        }

        m_listWidget->blockSignals( false );
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

    void MaastoDialog::populateColorComboBox( const QString &keepField )
    {
        m_colorComboBox->blockSignals( true );
        m_colorComboBox->clear();

        // RGB aina ensimmäisenä
        m_colorComboBox->addItem( "RGB" );

        // Scalar-kentät aakkosjärjestyksessä
        const QStringList names = getScalarFieldNames( m_cloud );
        m_colorComboBox->addItems( names );

        m_colorComboBox->blockSignals( false );

        // Säilytä edellinen valinta jos mahdollista
        int keepIdx = m_colorComboBox->findText( keepField );
        if ( keepIdx < 0 )
        {
            // Oletusvalinta: Classification jos löytyy, muuten RGB (indeksi 0)
            keepIdx = m_colorComboBox->findText(
                "Classification", Qt::MatchFixedString );
            if ( keepIdx < 0 )
                keepIdx = 0; // RGB
        }
        m_colorComboBox->setCurrentIndex( keepIdx );

        applyColorField( m_colorComboBox->currentText() );
        populateVisibilityList( m_colorComboBox->currentText() );
    }

    void MaastoDialog::applyColorField( const QString &fieldName )
    {
        if ( m_cloud == nullptr || fieldName.isEmpty() )
            return;

        if ( fieldName == "RGB" )
        {
            removePtcColors();
            m_cloud->showColors( true );
            m_cloud->showSF( false );
        }
        else if ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0
                  && !m_classDefinitions.isEmpty() )
        {
            // .ptc-värit kirjoitetaan suoraan vertex-taulukkoon
            applyPtcColors();
            return;  // applyPtcColors hoitaa refreshin
        }
        else
        {
            removePtcColors();
            int idx = m_cloud->getScalarFieldIndexByName( fieldName.toStdString().c_str() );
            if ( idx < 0 )
                return;
            m_cloud->setCurrentDisplayedScalarField( idx );
            m_cloud->showColors( false );
            m_cloud->showSF( true );
        }

        m_cloud->prepareDisplayForRefresh();
        m_updatingCloud = true;
        m_appInterface->updateUI();
        m_updatingCloud = false;
        m_appInterface->refreshAll();
    }

    void MaastoDialog::applyPtcColors()
    {
        if ( !m_cloud || m_classDefinitions.isEmpty() )
            return;

        const QString sfName = m_colorComboBox->currentText();
        if ( sfName.compare( "Classification", Qt::CaseInsensitive ) != 0 )
            return;

        int sfIdx = m_cloud->getScalarFieldIndexByName( sfName.toStdString().c_str() );
        if ( sfIdx < 0 )
            return;

        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf )
            return;

        const unsigned n = m_cloud->size();

        // Varaa RGBA-taulukko (tai tyhjennä vanha)
        if ( !m_cloud->resizeTheRGBTable( false ) )
            return;

        // Kirjoita jokaisen pisteen väri suoraan vertex-taulukkoon
        for ( unsigned i = 0; i < n; ++i )
        {
            const int classVal = static_cast<int>( sf->getValue( i ) );
            if ( m_classDefinitions.contains( classVal )
                 && m_classDefinitions[classVal].color.isValid() )
            {
                const QColor &c = m_classDefinitions[classVal].color;
                m_cloud->setPointColor( i, ccColor::Rgba(
                    static_cast<ColorCompType>( c.red() ),
                    static_cast<ColorCompType>( c.green() ),
                    static_cast<ColorCompType>( c.blue() ),
                    ccColor::MAX ) );
            }
            else
            {
                // Harmaa tuntemattomille luokille
                m_cloud->setPointColor( i, ccColor::Rgba(
                    128, 128, 128, ccColor::MAX ) );
            }
        }

        m_cloud->showColors( true );
        m_cloud->showSF( false );
        m_cloud->prepareDisplayForRefresh();
        m_ptcColorsApplied = true;

        m_updatingCloud = true;
        m_appInterface->updateUI();
        m_updatingCloud = false;
        m_appInterface->refreshAll();
    }

    void MaastoDialog::removePtcColors()
    {
        if ( !m_ptcColorsApplied || !m_cloud )
            return;

        // Poista vertex-värit ja palauta SF-väritys
        m_cloud->unallocateColors();
        m_cloud->showColors( false );
        m_cloud->showSF( true );
        m_cloud->prepareDisplayForRefresh();
        m_ptcColorsApplied = false;
    }

    // ----------------------------------------------------------------
    // performClassification
    // ----------------------------------------------------------------

    void MaastoDialog::performClassification()
    {
        if ( !m_cloud || m_indexHitCount.empty() )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: ei luokiteltavia pisteitä — piirrä ensin polygon",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        const QString sfName    = m_valuesComboBox->currentText();
        const QString targetStr = m_targetClassComboBox->currentText();
        if ( sfName.isEmpty() || targetStr.isEmpty() )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: valitse scalar field ja kohdeluokka",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        int sfIdx = m_cloud->getScalarFieldIndexByName( sfName.toStdString().c_str() );
        if ( sfIdx < 0 )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: scalar fieldiä ei löydy",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }
        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf ) return;

        // Kerää valitut arvot Arvot-listasta (☑-rivit)
        const QSet<float> selectedValues = getCheckedValues();

        if ( selectedValues.isEmpty() )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: valitse vähintään yksi arvo Arvot-listasta",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        const ScalarType targetValue = static_cast<ScalarType>( targetStr.toFloat() );

        // Kahden polygonin tilassa luokitellaan vain pisteet jotka ovat ≥2 prisman sisällä
        const int minHits = ( m_dualPolygonCheckbox && m_dualPolygonCheckbox->isChecked() ) ? 2 : 1;

        // Luokittele: m_indexHitCount perusteella suodatettuna
        unsigned count = 0;
        for ( const auto& kv : m_indexHitCount )
        {
            const unsigned idx  = kv.first;
            const int      hits = kv.second;
            if ( hits < minHits || idx >= m_cloud->size() )
                continue;
            const float val = static_cast<float>( sf->getValue( idx ) );
            if ( selectedValues.contains( val ) )
            {
                sf->setValue( idx, targetValue );
                ++count;
            }
        }

        sf->computeMinAndMax();
        m_cloud->prepareDisplayForRefresh();

        // Poista 3D-kappaleet ja highlight-pilvet DB:stä
        for ( ccHObject *obj : m_meshObjects )
            m_appInterface->removeFromDB( obj, true );
        m_meshObjects.clear();

        removeHighlightObjects();
        m_indexHitCount.clear();

        m_appInterface->dispToConsole(
            QString( "MaastoPlugin: luokiteltu %1 pistettä → %2" )
                .arg( count ).arg( targetStr ),
            ccMainAppInterface::STD_CONSOLE_MESSAGE );

        // Päivitä Count-sarake luokittelun jälkeen
        populateValueList( sfName );

        m_appInterface->updateUI();
        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // getCheckedValues
    // ----------------------------------------------------------------

    QSet<float> MaastoDialog::getCheckedValues() const
    {
        QSet<float> values;
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
        {
            const QTreeWidgetItem *item = m_listWidget->topLevelItem( i );
            if ( item->checkState( 0 ) == Qt::Checked )
                values.insert( item->text( 0 ).toFloat() );
        }
        return values;
    }

    // ----------------------------------------------------------------
    // createFilteredHighlight
    // ----------------------------------------------------------------

    ccPointCloud* MaastoDialog::createFilteredHighlight( const QSet<float>& selectedValues )
    {
        static int s_counter = 0;

        if ( !m_cloud || m_indexHitCount.empty() || selectedValues.isEmpty() )
            return nullptr;

        const QString sfName = m_valuesComboBox->currentText();
        if ( sfName.isEmpty() )
            return nullptr;

        int sfIdx = m_cloud->getScalarFieldIndexByName( sfName.toStdString().c_str() );
        if ( sfIdx < 0 )
            return nullptr;

        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf )
            return nullptr;

        // Kahden polygonin tilassa näytetään vain pisteet jotka ovat ≥2 prisman sisällä
        const int minHits = ( m_dualPolygonCheckbox && m_dualPolygonCheckbox->isChecked() ) ? 2 : 1;

        // Kerää pisteet joiden hitCount ≥ minHits ja arvo on valituissa arvoissa
        std::vector<unsigned> matchIndices;
        for ( const auto& kv : m_indexHitCount )
        {
            const unsigned idx   = kv.first;
            const int      hits  = kv.second;
            if ( hits < minHits || idx >= m_cloud->size() )
                continue;
            const float val = static_cast<float>( sf->getValue( idx ) );
            if ( selectedValues.contains( val ) )
                matchIndices.push_back( idx );
        }

        if ( matchIndices.empty() )
            return nullptr;

        ++s_counter;
        ccPointCloud *highlighted = new ccPointCloud(
            QString( "Highlighted_%1" ).arg( s_counter ) );

        if ( !highlighted->reserve( static_cast<unsigned>( matchIndices.size() ) ) )
        {
            delete highlighted;
            return nullptr;
        }

        for ( unsigned idx : matchIndices )
            highlighted->addPoint( *m_cloud->getPoint( idx ) );

        // Keltainen väri
        if ( highlighted->reserveTheRGBTable() )
        {
            for ( std::size_t i = 0; i < matchIndices.size(); ++i )
                highlighted->addColor( ccColor::Rgba( 255, 255, 0, 255 ) );
            highlighted->showColors( true );
        }

        const unsigned char origSize = m_cloud->getPointSize();
        highlighted->setPointSize( origSize > 0 ? origSize + 2 : 3 );

        return highlighted;
    }

    // ----------------------------------------------------------------
    // removeHighlightObjects
    // ----------------------------------------------------------------

    void MaastoDialog::removeHighlightObjects()
    {
        for ( ccHObject *obj : m_highlightObjects )
            m_appInterface->removeFromDB( obj, true );
        m_highlightObjects.clear();
    }

    // ----------------------------------------------------------------
    // refreshHighlights
    // ----------------------------------------------------------------

    void MaastoDialog::refreshHighlights()
    {
        removeHighlightObjects();

        if ( !m_cloud || m_indexHitCount.empty() )
        {
            m_appInterface->refreshAll();
            return;
        }

        const QSet<float> selectedValues = getCheckedValues();
        ccPointCloud *highlighted = createFilteredHighlight( selectedValues );

        if ( highlighted )
        {
            m_cloud->addChild( highlighted );
            m_appInterface->addToDB(
                highlighted,
                false,  // updateZoom
                false,  // autoExpandDBTree
                false,  // checkDimensions
                false   // autoRedraw
            );
            m_highlightObjects.push_back( highlighted );
        }

        // Ei kutsuta updateUI():ta tässä — se triggeröisi onNewSelection() → updateCloud()
        // → populateColorComboBox() → applyColorField() → updateUI() silmukan
        // joka tyhjentäisi m_indexHitCount:n tai estäisi luokittelun
        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // populateVisibilityList
    // ----------------------------------------------------------------

    void MaastoDialog::populateVisibilityList( const QString &fieldName )
    {
        // Tallenna nykyiset pois-valinnat ennen tyhjennystä
        QSet<QString> uncheckedValues;
        for ( int i = 0; i < m_visibilityListWidget->topLevelItemCount(); ++i )
            if ( m_visibilityListWidget->topLevelItem( i )->checkState( 0 ) == Qt::Unchecked )
                uncheckedValues.insert( m_visibilityListWidget->topLevelItem( i )->text( 0 ) );

        m_updatingVisibility = true;
        m_visibilityListWidget->clear();

        if ( fieldName == "RGB" || m_cloud == nullptr || fieldName.isEmpty() )
        {
            m_updatingVisibility = false;
            resetVisibility();
            return;
        }

        const QStringList values = getScalarFieldValues( m_cloud, fieldName );
        bool allChecked = uncheckedValues.isEmpty();

        for ( const QString &val : values )
        {
            QTreeWidgetItem *item = new QTreeWidgetItem( m_visibilityListWidget );
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            item->setCheckState( 0, uncheckedValues.contains( val )
                                    ? Qt::Unchecked : Qt::Checked );
            item->setText( 0, val );
        }

        // Päivitä toggle-napin tila
        m_selectAllVisButton->blockSignals( true );
        if ( allChecked )
        {
            m_selectAllVisButton->setChecked( true );
            m_selectAllVisButton->setText( "Poista valinnat" );
        }
        else
        {
            m_selectAllVisButton->setChecked( false );
            m_selectAllVisButton->setText( "Valitse kaikki" );
        }
        m_selectAllVisButton->blockSignals( false );

        m_updatingVisibility = false;

        // Päivitä visibility-tila valintojen mukaan
        applyVisibilityFilter();
    }

    // ----------------------------------------------------------------
    // applyVisibilityFilter
    // ----------------------------------------------------------------

    void MaastoDialog::applyVisibilityFilter()
    {
        if ( m_cloud == nullptr )
            return;

        const QString sfName = m_colorComboBox->currentText();
        if ( sfName == "RGB" || sfName.isEmpty() )
        {
            resetVisibility();
            return;
        }

        // Kerää näkyvät arvot
        QSet<float> visibleValues;
        bool allChecked = true;
        for ( int i = 0; i < m_visibilityListWidget->topLevelItemCount(); ++i )
        {
            const QTreeWidgetItem *item = m_visibilityListWidget->topLevelItem( i );
            if ( item->checkState( 0 ) == Qt::Checked )
                visibleValues.insert( item->text( 0 ).toFloat() );
            else
                allChecked = false;
        }

        // Jos kaikki valittuna → poista maski (nolla lisämuistia)
        if ( allChecked || visibleValues.isEmpty() )
        {
            resetVisibility();
            return;
        }

        int sfIdx = m_cloud->getScalarFieldIndexByName( sfName.toStdString().c_str() );
        if ( sfIdx < 0 )
        {
            resetVisibility();
            return;
        }
        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf )
        {
            resetVisibility();
            return;
        }

        // Alusta visibility mask
        if ( !m_cloud->resetVisibilityArray() )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: visibility maskin alustus epäonnistui",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        auto& vis = m_cloud->getTheVisibilityArray();
        for ( unsigned i = 0; i < m_cloud->size(); ++i )
        {
            const float val = static_cast<float>( sf->getValue( i ) );
            vis[i] = visibleValues.contains( val )
                     ? CCCoreLib::POINT_VISIBLE
                     : CCCoreLib::POINT_HIDDEN;
        }

        m_cloud->prepareDisplayForRefresh();

        m_updatingCloud = true;
        m_appInterface->updateUI();
        m_updatingCloud = false;
        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // resetVisibility
    // ----------------------------------------------------------------

    void MaastoDialog::resetVisibility()
    {
        if ( m_cloud == nullptr )
            return;

        if ( m_cloud->isVisibilityTableInstantiated() )
        {
            m_cloud->unallocateVisibilityArray();
            m_cloud->prepareDisplayForRefresh();
            m_appInterface->refreshAll();
        }
    }

    // ----------------------------------------------------------------
    // loadPtcFile
    // ----------------------------------------------------------------

    void MaastoDialog::loadPtcFile( const QString &filePath )
    {
        QMap<int, ClassDefinition> defs = ClassDefinitionReader::read( filePath );
        if ( defs.isEmpty() )
        {
            m_appInterface->dispToConsole(
                QString( "MaastoPlugin: .ptc-tiedoston lukeminen epäonnistui: %1" ).arg( filePath ),
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        m_classDefinitions = defs;

        // Näytä polku Read-napin alla
        m_ptcFileLabel->setText( filePath );
        m_ptcFileLabel->setVisible( true );

        // Päivitä Luokat-lista
        populateValueList( m_valuesComboBox->currentText() );

        // Päivitä värit jos Pisteiden väritys on Classification
        const QString colorField = m_colorComboBox->currentText();
        if ( colorField.compare( "Classification", Qt::CaseInsensitive ) == 0 )
            applyPtcColors();
    }

    // ----------------------------------------------------------------
    // tryAutoLoadPtcFile
    // ----------------------------------------------------------------

    void MaastoDialog::tryAutoLoadPtcFile()
    {
        if ( !m_cloud )
            return;

        // Hae parent-objektin nimi — muoto: "tiedosto.laz (/polku/kansioon)"
        ccHObject *parent = m_cloud->getParent();
        if ( !parent )
            return;

        // Parsitaan polku sulkujen sisältä
        const QRegularExpression re( R"(\((.+)\)$)" );
        const QRegularExpressionMatch match = re.match( parent->getName() );
        if ( !match.hasMatch() )
            return;

        const QString folderPath = match.captured( 1 );
        QDir dir( folderPath );
        if ( !dir.exists() )
            return;

        // Etsi ensimmäinen .ptc-tiedosto kansiosta
        const QStringList ptcFiles = dir.entryList( { "*.ptc" }, QDir::Files );
        if ( ptcFiles.isEmpty() )
            return;

        const QString ptcPath = dir.absoluteFilePath( ptcFiles.first() );
        loadPtcFile( ptcPath );
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
