#include "MaastoAction.h"
#include "PolygonDrawer.h"
#include "SettingsDialog.h"
#include "VolumeBuilder.h"
#include "ClassDefinition.h"

#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"
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
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileDialog>
#include <QHeaderView>
#include <QDir>
#include <QRegularExpression>
#include <QCheckBox>
#include <QIcon>
#include <QMap>
#include <map>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>

#include "ccGLWindowSignalEmitter.h"

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
        // Pura "Näytä vain valinta" -tila ennen tuhoamista
        if ( m_showOnlyMode && m_cloud )
        {
            m_cloud->setVisible( true );
            for ( ccHObject *obj : m_highlightObjects )
                obj->setVisible( true );
        }
        removeSelectionOnlyCloud();

        // Palauta pistepilvi alkutilaan ennen tuhoamista
        removePtcColors();
        resetVisibility();
    }

    MaastoDialog::MaastoDialog( ccMainAppInterface *appInterface, QWidget *parent )
        : QDialog( parent )
        , m_appInterface( appInterface )
        , m_cloud( nullptr )
        , m_ptcFilePath( "" )
        , m_valuesComboBox( nullptr )
        , m_listWidget( nullptr )
        , m_selectAllButton( nullptr )
        , m_showAllButton( nullptr )
        , m_targetClassComboBox( nullptr )
        , m_colorComboBox( nullptr )
        , m_visibilityListWidget( nullptr )
        , m_selectAllVisButton( nullptr )
        , m_updatingCloud( false )
        , m_updatingVisibility( false )
        , m_updatingShow( false )
        , m_ptcColorsApplied( false )
        , m_highlightPointSize( 5 )
        , m_highlightColor( Qt::yellow )
        , m_measurePointSize( 8 )
        , m_measurePointColor( Qt::red )
        , m_polygonDrawer( new PolygonDrawer( appInterface, this ) )
        , m_polygonButton( nullptr )
        , m_clearSelectionButton( nullptr )
        , m_showOnlyButton( nullptr )
        , m_fileButton( nullptr )
        , m_minPolygonCountSpinBox( nullptr )
        , m_nearDistSpinBox( nullptr )
        , m_farDistSpinBox( nullptr )
        , m_measureNearButton( nullptr )
        , m_measureFarButton( nullptr )
        , m_measureState( 0 )
        , m_measuringNear( true )
        , m_measuredX( 0.0 )
        , m_measuredY( 0.0 )
        , m_measuredZ( 0.0 )
        , m_measureHighlight( nullptr )
        , m_showOnlyMode( false )
        , m_selectionOnlyCloud( nullptr )
        , m_drawLineButton( nullptr )
        , m_lineAxisComboBox( nullptr )
        , m_lineThicknessSpinBox( nullptr )
        , m_copyLineRightButton( nullptr )
        , m_extendLineToBBoxCheckBox( nullptr )
        , m_linePickState( 0 )
        , m_lineP1( 0.0f, 0.0f, 0.0f )
        , m_lineP2( 0.0f, 0.0f, 0.0f )
        , m_linePoint1Highlight( nullptr )
        , m_linePoint2Highlight( nullptr )
    {
        // Käytä 3D-ikkunan pistekokoa highlight- ja mittauspiste-defaultina
        {
            ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
            if ( win )
            {
                const int baseSize = static_cast<int>(
                    win->getViewportParameters().defaultPointSize );
                m_highlightPointSize = baseSize + 1;
                m_measurePointSize   = baseSize + 3;
            }
        }

        setWindowTitle( "MaastoPlugin" );
        setMinimumWidth( 380 );
        setWindowFlags( windowFlags() | Qt::Window );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // --- Yläpalkki: Asetukset-nappi vasemmalla, polku-label vieressä ---
        {
            QHBoxLayout *topRow = new QHBoxLayout();
            m_fileButton = new QPushButton( "Asetukset", this );
            m_fileButton->setFixedWidth( 80 );
            topRow->addWidget( m_fileButton );

            topRow->addStretch();
            layout->addLayout( topRow );
        }

        connect( m_fileButton, &QPushButton::clicked, this, [this]()
        {
            const QString currentPath = m_ptcFilePath;
            SettingsDialog dlg( m_highlightPointSize, m_highlightColor, currentPath,
                                m_measurePointSize, m_measurePointColor, this );

            // Kun käyttäjä valitsee .ptc-tiedoston asetuksissa → lataa heti
            connect( &dlg, &SettingsDialog::ptcFileLoaded,
                this, [this]( const QString &path ) { loadPtcFile( path ); } );

            if ( dlg.exec() == QDialog::Accepted )
            {
                m_highlightPointSize = dlg.pointSize();
                m_highlightColor     = dlg.color();
                m_measurePointSize   = dlg.measurePointSize();
                m_measurePointColor  = dlg.measurePointColor();
                refreshHighlights();
            }
        } );

        // --- Scalar field ---
        layout->addWidget( new QLabel( "Scalar field:", this ) );
        m_valuesComboBox = new QComboBox( this );
        layout->addWidget( m_valuesComboBox );

        // --- Luokat (checkbox-monivalinta) + toggle-napit ---
        {
            QHBoxLayout *arvoHeader = new QHBoxLayout();
            arvoHeader->addWidget( new QLabel( "Luokittele luokista:", this ) );
            m_selectAllButton = new QPushButton( "Valitse kaikki", this );
            m_selectAllButton->setCheckable( true );
            m_selectAllButton->setFixedHeight( 22 );
            arvoHeader->addWidget( m_selectAllButton );
            m_showAllButton = new QPushButton( "Hide all", this );
            m_showAllButton->setCheckable( true );
            m_showAllButton->setChecked( true );
            m_showAllButton->setFixedHeight( 22 );
            arvoHeader->addWidget( m_showAllButton );
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

        // Toggle: Show all / Hide all  (Show-sarake)
        connect( m_showAllButton, &QPushButton::toggled,
            [this]( bool checked )
            {
                m_showAllButton->setText( checked ? "Hide all" : "Show all" );
                const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
                m_updatingShow = true;
                m_listWidget->blockSignals( true );
                for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
                    m_listWidget->topLevelItem( i )->setCheckState( 1, state );
                m_listWidget->blockSignals( false );
                m_updatingShow = false;
                applyShowFilter();
            } );

        // Toggle: Valitse kaikki / Poista valinnat  (Value-sarake)
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

        // itemChanged: erottele Value-sarake (col 0) ja Show-sarake (col 1)
        connect( m_listWidget, &QTreeWidget::itemChanged,
            [this]( QTreeWidgetItem *item, int column )
            {
                if ( column == 1 )
                {
                    // Show-sarakkeen muutos
                    if ( m_updatingShow )
                        return;
                    // Palauta Show all -nappi alkutilaan
                    m_showAllButton->blockSignals( true );
                    m_showAllButton->setChecked( false );
                    m_showAllButton->setText( "Show all" );
                    m_showAllButton->blockSignals( false );
                    applyShowFilter();
                }
                else if ( column == 0 )
                {
                    // Value-sarakkeen muutos
                    m_selectAllButton->blockSignals( true );
                    m_selectAllButton->setChecked( false );
                    m_selectAllButton->setText( "Valitse kaikki" );
                    m_selectAllButton->blockSignals( false );
                    refreshHighlights();
                }
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

        // Vasen sarake: "Piirrä polygon" + "Poista valinta" allekkain
        QVBoxLayout *polygonCol = new QVBoxLayout();

        m_polygonButton = new QPushButton( "Piirrä polygon", this );
        m_polygonButton->setCheckable( true );
        polygonCol->addWidget( m_polygonButton );

        // Piirrä viiva -nappi + akseli-combobox samalla rivillä
        {
            QHBoxLayout *lineRow = new QHBoxLayout();
            lineRow->setContentsMargins( 0, 0, 0, 0 );

            m_drawLineButton = new QPushButton( "Piirrä viiva", this );
            m_drawLineButton->setCheckable( true );
            lineRow->addWidget( m_drawLineButton );

            m_lineAxisComboBox = new QComboBox( this );
            m_lineAxisComboBox->addItem( "Z" );
            m_lineAxisComboBox->addItem( "X" );
            m_lineAxisComboBox->addItem( "Y" );
            m_lineAxisComboBox->setFixedWidth( 48 );
            lineRow->addWidget( m_lineAxisComboBox );

            m_copyLineRightButton = new QPushButton( "Kopioi oikealle", this );
            m_copyLineRightButton->setEnabled( false );  // disabloitu kunnes viiva on piirretty
            lineRow->addWidget( m_copyLineRightButton );

            polygonCol->addLayout( lineRow );
        }

        m_showOnlyButton = new QPushButton( "Näytä vain valinta", this );
        m_showOnlyButton->setCheckable( true );
        m_showOnlyButton->setEnabled( false );   // disabloitu kunnes on valinta
        polygonCol->addWidget( m_showOnlyButton );

        m_clearSelectionButton = new QPushButton( "Poista valinta", this );
        polygonCol->addWidget( m_clearSelectionButton );

        buttonRow->addLayout( polygonCol );

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

        // Iso nappi + "Luokittele"-teksti allekkain
        QVBoxLayout *actionCol = new QVBoxLayout();
        actionCol->setAlignment( Qt::AlignHCenter );
        actionCol->addWidget( actionButton );
        QLabel *actionLabel = new QLabel( "Luokittele", this );
        actionLabel->setAlignment( Qt::AlignHCenter );
        actionCol->addWidget( actionLabel );
        buttonRow->addLayout( actionCol );

        layout->addLayout( buttonRow );

        // Iso nappi käynnistää luokittelun
        connect( actionButton, &QPushButton::clicked, this, [this]()
        {
            performClassification();
        } );

        // --- Polygonien vähimmäismäärä ---
        {
            QHBoxLayout *polyCountRow = new QHBoxLayout();
            polyCountRow->addWidget( new QLabel( "Polygonien vähimmäismäärä:", this ) );
            m_minPolygonCountSpinBox = new QSpinBox( this );
            m_minPolygonCountSpinBox->setRange( 1, 10 );
            m_minPolygonCountSpinBox->setValue( 1 );
            m_minPolygonCountSpinBox->setFixedWidth( 60 );
            polyCountRow->addWidget( m_minPolygonCountSpinBox );
            polyCountRow->addStretch();
            layout->addLayout( polyCountRow );
        }

        // Kun arvo muuttuu → päivitä highlight
        connect( m_minPolygonCountSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ),
            this, [this]() { refreshHighlights(); } );

        // --- Etäisyysasetukset ---
        QFormLayout *distForm = new QFormLayout();
        distForm->setContentsMargins( 0, 4, 0, 4 );

        // Near: spinbox + Mittaa-nappi
        {
            m_nearDistSpinBox = new QDoubleSpinBox( this );
            m_nearDistSpinBox->setRange( 0.1, 9999.0 );
            m_nearDistSpinBox->setSingleStep( 0.5 );
            m_nearDistSpinBox->setValue( 10.0 );
            m_nearDistSpinBox->setSuffix( " m" );
            m_nearDistSpinBox->setDecimals( 1 );

            m_measureNearButton = new QPushButton( "Mittaa", this );
            m_measureNearButton->setCheckable( true );
            m_measureNearButton->setFixedWidth( 60 );

            QWidget     *nearContainer = new QWidget( this );
            QHBoxLayout *nearRow       = new QHBoxLayout( nearContainer );
            nearRow->setContentsMargins( 0, 0, 0, 0 );
            nearRow->addWidget( m_nearDistSpinBox );
            nearRow->addWidget( m_measureNearButton );
            distForm->addRow( "Lähin etäisyys:", nearContainer );
        }

        // Far: spinbox + Mittaa-nappi
        {
            m_farDistSpinBox = new QDoubleSpinBox( this );
            m_farDistSpinBox->setRange( 0.1, 9999.0 );
            m_farDistSpinBox->setSingleStep( 0.5 );
            m_farDistSpinBox->setValue( 1000.0 );
            m_farDistSpinBox->setSuffix( " m" );
            m_farDistSpinBox->setDecimals( 1 );

            m_measureFarButton = new QPushButton( "Mittaa", this );
            m_measureFarButton->setCheckable( true );
            m_measureFarButton->setFixedWidth( 60 );

            QWidget     *farContainer = new QWidget( this );
            QHBoxLayout *farRow       = new QHBoxLayout( farContainer );
            farRow->setContentsMargins( 0, 0, 0, 0 );
            farRow->addWidget( m_farDistSpinBox );
            farRow->addWidget( m_measureFarButton );
            distForm->addRow( "Pisin etäisyys:", farContainer );
        }

        // Viivatyökalun paksuus + "Jatka viivan pituutta" -täppä samalla rivillä
        {
            m_lineThicknessSpinBox = new QDoubleSpinBox( this );
            m_lineThicknessSpinBox->setRange( 0.01, 9999.0 );
            m_lineThicknessSpinBox->setSingleStep( 0.1 );
            m_lineThicknessSpinBox->setValue( 1.0 );
            m_lineThicknessSpinBox->setSuffix( " m" );
            m_lineThicknessSpinBox->setDecimals( 2 );

            m_extendLineToBBoxCheckBox = new QCheckBox( "Jatka viivan pituutta", this );
            m_extendLineToBBoxCheckBox->setChecked( false );

            QWidget     *thickContainer = new QWidget( this );
            QHBoxLayout *thickRow       = new QHBoxLayout( thickContainer );
            thickRow->setContentsMargins( 0, 0, 0, 0 );
            thickRow->addWidget( m_lineThicknessSpinBox );
            thickRow->addWidget( m_extendLineToBBoxCheckBox );
            distForm->addRow( "Viivatyökalun paksuus:", thickContainer );
        }

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

        // Mittaa-napit: near ja far
        connect( m_measureNearButton, &QPushButton::clicked, this, [this]( bool checked )
        {
            if ( checked )
            {
                // Jos far-mittaus käynnissä, pysäytä se ensin
                if ( m_measureState > 0 && !m_measuringNear )
                    stopMeasure();
                startMeasure( true );
            }
            else
            {
                // Nappia painettu uudelleen → hyväksy piste jos se on valittu
                if ( m_measureState == 2 && m_measuringNear )
                {
                    // Laske etäisyys kamerasta
                    ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
                    if ( win )
                    {
                        CCVector3d cam = win->getViewportParameters()
                            .computeViewMatrix().inverse().getTranslationAsVec3D();
                        double dx = m_measuredX - cam.x;
                        double dy = m_measuredY - cam.y;
                        double dz = m_measuredZ - cam.z;
                        double dist = std::sqrt( dx*dx + dy*dy + dz*dz );
                        m_nearDistSpinBox->blockSignals( true );
                        m_nearDistSpinBox->setValue( dist );
                        m_nearDistSpinBox->blockSignals( false );
                    }
                    stopMeasure();
                }
                else
                {
                    stopMeasure();
                }
            }
        } );

        connect( m_measureFarButton, &QPushButton::clicked, this, [this]( bool checked )
        {
            if ( checked )
            {
                if ( m_measureState > 0 && m_measuringNear )
                    stopMeasure();
                startMeasure( false );
            }
            else
            {
                if ( m_measureState == 2 && !m_measuringNear )
                {
                    ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
                    if ( win )
                    {
                        CCVector3d cam = win->getViewportParameters()
                            .computeViewMatrix().inverse().getTranslationAsVec3D();
                        double dx = m_measuredX - cam.x;
                        double dy = m_measuredY - cam.y;
                        double dz = m_measuredZ - cam.z;
                        double dist = std::sqrt( dx*dx + dy*dy + dz*dz );
                        m_farDistSpinBox->blockSignals( true );
                        m_farDistSpinBox->setValue( dist );
                        m_farDistSpinBox->blockSignals( false );
                    }
                    stopMeasure();
                }
                else
                {
                    stopMeasure();
                }
            }
        } );

        // "Näytä vain valinta" -toggle
        connect( m_showOnlyButton, &QPushButton::toggled, this, [this]( bool checked )
        {
            m_showOnlyMode = checked;
            if ( checked )
                enableShowOnlyMode();
            else
                disableShowOnlyMode();
        } );

        // Poista valinta: poistaa kaikki 3D-muodot ja highlight-pisteet
        connect( m_clearSelectionButton, &QPushButton::clicked, this, [this]()
        {
            clearSelection();
        } );

        // Polygon-piirto: nappi toggleataan ON/OFF
        connect( m_polygonButton, &QPushButton::toggled, this, [this]( bool checked )
        {
            if ( checked )
            {
                // Pysäytä viiva-työkalu jos käynnissä
                if ( m_linePickState > 0 )
                    stopLinePicking();
                m_drawLineButton->blockSignals( true );
                m_drawLineButton->setChecked( false );
                m_drawLineButton->blockSignals( false );

                m_polygonDrawer->startDrawing();
            }
            else
            {
                m_polygonDrawer->stopDrawing();
            }
        } );

        // Kopioi oikealle
        connect( m_copyLineRightButton, &QPushButton::clicked, this, [this]()
        {
            copyLineRight();
        } );

        // Viiva-työkalu: nappi toggleataan ON/OFF
        connect( m_drawLineButton, &QPushButton::toggled, this, [this]( bool checked )
        {
            if ( checked )
            {
                // Pysäytä polygon-piirto jos käynnissä
                m_polygonDrawer->stopDrawing();
                m_polygonButton->blockSignals( true );
                m_polygonButton->setChecked( false );
                m_polygonButton->blockSignals( false );

                startLinePicking();
            }
            else
            {
                stopLinePicking();
            }
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
        // Jos "Näytä vain valinta" on päällä, pura tila ensin (palauttaa m_cloud näkyviin)
        if ( m_showOnlyMode )
        {
            disableShowOnlyMode();
            m_showOnlyMode = false;
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
        }

        removePtcColors();
        resetVisibility();

        // Jos pilvi vaihtuu, nollaa luokkamääritykset ja polkunäyttö
        if ( m_cloud != cloud )
        {
            m_classDefinitions.clear();
            m_classCounts.clear();
            m_ptcFilePath = "";
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

        // Tallenna nykyiset valinnat (Value col 0) ennen tyhjennystä
        QSet<QString> checkedValues;
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
            if ( m_listWidget->topLevelItem( i )->checkState( 0 ) == Qt::Checked )
                checkedValues.insert( m_listWidget->topLevelItem( i )->text( 0 ) );

        // Tallenna nykyiset Show-tilat (col 1) ennen tyhjennystä
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
        {
            const QTreeWidgetItem *item = m_listWidget->topLevelItem( i );
            const QString key = item->text( 0 );
            // Vain jos Show-sarake on olemassa (columnCount >= 2)
            if ( m_listWidget->columnCount() >= 2 )
                m_showStates[key] = ( item->checkState( 1 ) == Qt::Checked );
        }

        m_listWidget->blockSignals( true );
        m_listWidget->clear();

        const QStringList values = getScalarFieldValues( m_cloud, fieldName );

        // Näytetään Name + Color sarakkeet jos Scalar field on Classification ja .ptc on ladattu
        const bool isClassif = ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
                               && !m_classDefinitions.isEmpty();

        // Count-sarake näytetään aina kun pilvi on valittu
        const bool hasCloud = ( m_cloud != nullptr ) && !values.isEmpty();

        // Sarakeindeksit: Value=0, Show=1, Name=2(classif), Color=3(classif), Count=2 tai 4
        if ( hasCloud )
        {
            if ( isClassif )
            {
                // Value | Show | Name | Color | Count
                m_listWidget->setColumnCount( 5 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Show", "Name", "Color", "Count" } );
                m_listWidget->header()->setStretchLastSection( false );
                m_listWidget->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
                m_listWidget->header()->setSectionResizeMode( 1, QHeaderView::Fixed );
                m_listWidget->header()->resizeSection( 1, 40 );
                m_listWidget->header()->setSectionResizeMode( 2, QHeaderView::Stretch );
                m_listWidget->header()->setSectionResizeMode( 3, QHeaderView::Fixed );
                m_listWidget->header()->resizeSection( 3, 40 );
                m_listWidget->header()->setSectionResizeMode( 4, QHeaderView::ResizeToContents );
            }
            else
            {
                // Value | Show | Count
                m_listWidget->setColumnCount( 3 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Show", "Count" } );
                m_listWidget->header()->setStretchLastSection( false );
                m_listWidget->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
                m_listWidget->header()->setSectionResizeMode( 1, QHeaderView::Fixed );
                m_listWidget->header()->resizeSection( 1, 40 );
                m_listWidget->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
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

            // Value-sarake (col 0): checkbox luokittelua varten
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            item->setCheckState( 0, checkedValues.contains( val )
                                    ? Qt::Checked : Qt::Unchecked );
            item->setText( 0, val );

            // Show-sarake (col 1): checkbox näkyvyyttä varten
            if ( hasCloud )
            {
                // Palauta tallennettu tila; uudet arvot näkyvissä oletuksena
                const bool showVal = m_showStates.value( val, true );
                item->setCheckState( 1, showVal ? Qt::Checked : Qt::Unchecked );
            }

            const int intVal = val.toInt();

            if ( isClassif && m_classDefinitions.contains( intVal ) )
            {
                const ClassDefinition &def = m_classDefinitions[intVal];
                item->setText( 2, def.name );
                if ( def.color.isValid() )
                {
                    QPixmap px( 20, 20 );
                    px.fill( def.color );
                    item->setIcon( 3, QIcon( px ) );
                }
            }

            // Count-sarake: oikea indeksi riippuu isClassif:stä
            if ( hasCloud )
            {
                const int countCol = isClassif ? 4 : 2;
                const int count = m_classCounts.value( intVal, 0 );
                item->setText( countCol, QString::number( count ) );
                item->setTextAlignment( countCol, Qt::AlignRight | Qt::AlignVCenter );
            }
        }

        m_listWidget->blockSignals( false );

        // Päivitä Show all -napin tila
        if ( m_showAllButton && hasCloud )
        {
            bool anyHidden = false;
            for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
            {
                if ( m_listWidget->topLevelItem( i )->checkState( 1 ) == Qt::Unchecked )
                {
                    anyHidden = true;
                    break;
                }
            }
            m_showAllButton->blockSignals( true );
            m_showAllButton->setChecked( !anyHidden );
            m_showAllButton->setText( anyHidden ? "Show all" : "Hide all" );
            m_showAllButton->blockSignals( false );
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
            removePtcColors();  // showSF(false) — vertex-RGB koskematon
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

        int sfIdx = m_cloud->getScalarFieldIndexByName( "Classification" );
        if ( sfIdx < 0 )
            return;

        ccScalarField *sf = static_cast<ccScalarField*>( m_cloud->getScalarField( sfIdx ) );
        if ( !sf )
            return;

        sf->computeMinAndMax();

        // Absoluuttinen min/max luokkakoodeista
        const double minVal = static_cast<double>( m_classDefinitions.firstKey() );
        const double maxVal = static_cast<double>( m_classDefinitions.lastKey() );
        const double range  = ( maxVal > minVal ) ? ( maxVal - minVal ) : 1.0;

        // Rakenna absoluuttinen color scale — vertex-RGB-taulukkoa ei kosketa
        ccColorScale::Shared scale = ccColorScale::Create( "PtcClasses" );
        scale->clear();
        scale->setAbsolute( minVal, maxVal );

        // Nearest-neighbour: vaihda väri puolivälissä kahden luokkakoodin välillä.
        // Koska luokkakoodit ovat kokonaislukuja, puoliväliarvot (1.5, 2.5 jne.)
        // eivät esiinny oikeassa datassa → jokainen piste saa täsmälleen oikean värin.
        // ccColorScale::update() vaatii että ensimmäinen step = 0.0 ja viimeinen = 1.0.
        const QList<int> keys = m_classDefinitions.keys();  // QMap on järjestetty

        for ( int i = 0; i < keys.size(); ++i )
        {
            const double relPos = ( keys[i] - minVal ) / range;
            const QColor col    = m_classDefinitions[keys[i]].color.isValid()
                                  ? m_classDefinitions[keys[i]].color
                                  : QColor( 128, 128, 128 );

            if ( i == 0 )
            {
                // Ensimmäinen step täsmälleen 0.0:ssa (update() vaatii tämän)
                scale->insert( ccColorScaleElement( 0.0, col ), false );
            }
            else
            {
                // Vaihda väri puolivälissä edellisen ja tämän luokan välillä
                const double prevRelPos = ( keys[i - 1] - minVal ) / range;
                const double mid = ( prevRelPos + relPos ) / 2.0;
                scale->insert( ccColorScaleElement( mid, col ), false );
            }

            if ( i == keys.size() - 1 )
            {
                // Viimeinen step täsmälleen 1.0:ssa (update() vaatii tämän)
                scale->insert( ccColorScaleElement( 1.0, col ), false );
            }
        }

        scale->update();

        sf->setColorScale( scale );
        sf->setColorRampSteps( 256 );

        // Aktivoi SF-näyttö — vertex-RGB-taulukko jää täysin koskemattomaksi
        m_cloud->setCurrentDisplayedScalarField( sfIdx );
        m_cloud->showSF( true );
        m_cloud->showColors( false );
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

        // Vertex-RGB-taulukkoa ei ole koskaan muutettu — riittää SF-näytön sammutus
        m_cloud->showSF( false );
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

        // Luokitellaan vain pisteet jotka ovat vähintään N prisman sisällä
        const int minHits = m_minPolygonCountSpinBox ? m_minPolygonCountSpinBox->value() : 1;

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
    // clearSelection
    // ----------------------------------------------------------------

    void MaastoDialog::clearSelection()
    {
        // 1. Jos "Näytä vain valinta" on päällä, pura tila ensin
        if ( m_showOnlyMode )
        {
            disableShowOnlyMode();
            m_showOnlyMode = false;
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
        }

        // 2. Poista kaikki prism-meshit DB:stä
        for ( ccHObject *obj : m_meshObjects )
            m_appInterface->removeFromDB( obj, true );
        m_meshObjects.clear();

        // 3. Poista highlight-pistepilvet DB:stä
        removeHighlightObjects();

        // 4. Tyhjennä indeksit ja osuma-laskuri
        m_selectionIndices.clear();
        m_indexHitCount.clear();

        // 5. Disabloi "Näytä vain valinta" -nappi
        if ( m_showOnlyButton )
            m_showOnlyButton->setEnabled( false );

        // 6. Pysäytä aktiivinen polygon-piirto ja poista näkyvä polygon GL-ikkunasta
        m_polygonDrawer->stopDrawing();
        m_polygonDrawer->clearCompletedPolygon();

        // 7. Nollaa polygon-nappi ilman signaalin laukaisemista
        m_polygonButton->blockSignals( true );
        m_polygonButton->setChecked( false );
        m_polygonButton->blockSignals( false );

        // 8. Pysäytä viiva-työkalu jos käynnissä
        stopLinePicking();
        if ( m_drawLineButton )
        {
            m_drawLineButton->blockSignals( true );
            m_drawLineButton->setChecked( false );
            m_drawLineButton->blockSignals( false );
        }
        if ( m_copyLineRightButton )
            m_copyLineRightButton->setEnabled( false );

        m_appInterface->dispToConsole(
            "MaastoPlugin: valinta poistettu",
            ccMainAppInterface::STD_CONSOLE_MESSAGE );

        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // removeSelectionOnlyCloud
    // ----------------------------------------------------------------

    void MaastoDialog::removeSelectionOnlyCloud()
    {
        if ( m_selectionOnlyCloud )
        {
            m_appInterface->removeFromDB( m_selectionOnlyCloud, true );
            m_selectionOnlyCloud = nullptr;
        }
    }

    // ----------------------------------------------------------------
    // enableShowOnlyMode
    // ----------------------------------------------------------------

    void MaastoDialog::enableShowOnlyMode()
    {
        if ( !m_cloud || m_selectionIndices.empty() )
            return;

        // 1. Piilota pääpilvi
        m_cloud->setVisible( false );
        m_cloud->prepareDisplayForRefresh();

        // 2. Piilota highlight-pilvet (keltainen visualisointi)
        for ( ccHObject *obj : m_highlightObjects )
        {
            obj->setVisible( false );
            obj->prepareDisplayForRefresh();
        }

        // 3. Poista vanha väliaikainen pilvi jos on
        removeSelectionOnlyCloud();

        // 4. Rakenna uusi "vain valinta" -pilvi pääpilven visualisointia käyttäen
        const unsigned count = static_cast<unsigned>( m_selectionIndices.size() );
        m_selectionOnlyCloud = new ccPointCloud( "SelectionOnly" );
        if ( !m_selectionOnlyCloud->reserve( count ) )
        {
            delete m_selectionOnlyCloud;
            m_selectionOnlyCloud = nullptr;
            return;
        }

        for ( unsigned idx : m_selectionIndices )
            m_selectionOnlyCloud->addPoint( *m_cloud->getPoint( idx ) );

        // 5. Kopioi visualisointi pääpilveltä
        if ( m_cloud->sfShown() )
        {
            // SF-tila: kopioi scalar field arvot ja värikaava
            const int srcSfIdx = m_cloud->getCurrentDisplayedScalarFieldIndex();
            ccScalarField *srcSf = static_cast<ccScalarField*>(
                m_cloud->getScalarField( srcSfIdx ) );

            if ( srcSf )
            {
                ccScalarField *dstSf = new ccScalarField( srcSf->getName() );
                dstSf->reserve( count );
                for ( unsigned idx : m_selectionIndices )
                    dstSf->addElement( srcSf->getValue( idx ) );
                dstSf->computeMinAndMax();
                dstSf->setColorScale( srcSf->getColorScale() );
                dstSf->setColorRampSteps( srcSf->getColorRampSteps() );

                const int dstSfIdx = m_selectionOnlyCloud->addScalarField( dstSf );
                m_selectionOnlyCloud->setCurrentDisplayedScalarField( dstSfIdx );
                m_selectionOnlyCloud->showSF( true );
                m_selectionOnlyCloud->showColors( false );
            }
        }
        else if ( m_cloud->colorsShown() )
        {
            // RGB-tila: kopioi per-pisteen RGB pääpilveltä
            if ( m_cloud->hasColors() && m_selectionOnlyCloud->reserveTheRGBTable() )
            {
                for ( unsigned idx : m_selectionIndices )
                    m_selectionOnlyCloud->addColor( m_cloud->getPointColor( idx ) );
                m_selectionOnlyCloud->showColors( true );
                m_selectionOnlyCloud->showSF( false );
            }
        }

        // 6. Lisää DB:hen pääpilven lapseksi
        m_cloud->addChild( m_selectionOnlyCloud );
        m_appInterface->addToDB(
            m_selectionOnlyCloud,
            false, false, false, false );

        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // disableShowOnlyMode
    // ----------------------------------------------------------------

    void MaastoDialog::disableShowOnlyMode()
    {
        if ( !m_cloud )
            return;

        // 1. Poista "vain valinta" -pilvi
        removeSelectionOnlyCloud();

        // 2. Palauta pääpilvi näkyviin
        m_cloud->setVisible( true );
        m_cloud->prepareDisplayForRefresh();

        // 3. Palauta highlight-pilvet näkyviin
        for ( ccHObject *obj : m_highlightObjects )
        {
            obj->setVisible( true );
            obj->prepareDisplayForRefresh();
        }

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

        // Näytetään vain pisteet jotka ovat vähintään N prisman sisällä
        const int minHits = m_minPolygonCountSpinBox ? m_minPolygonCountSpinBox->value() : 1;

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
        {
            m_selectionIndices.clear();
            return nullptr;
        }

        // Tallenna indeksit "Näytä vain valinta" -tilaa varten
        m_selectionIndices = matchIndices;

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

        // Highlight-väri asetuksista
        if ( highlighted->reserveTheRGBTable() )
        {
            const ccColor::Rgba col(
                static_cast<ColorCompType>( m_highlightColor.red() ),
                static_cast<ColorCompType>( m_highlightColor.green() ),
                static_cast<ColorCompType>( m_highlightColor.blue() ),
                ccColor::MAX );
            for ( std::size_t i = 0; i < matchIndices.size(); ++i )
                highlighted->addColor( col );
            highlighted->showColors( true );
        }

        // Highlight-pisteiden koko asetuksista
        highlighted->setPointSize( static_cast<unsigned>( m_highlightPointSize ) );

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
        // Jos "Näytä vain valinta" on päällä, puretaan tila ensin puhtaasti
        if ( m_showOnlyMode )
            disableShowOnlyMode();

        removeHighlightObjects();

        if ( !m_cloud || m_indexHitCount.empty() )
        {
            // Ei valintaa — disabloi nappi ja nollaa tila
            m_selectionIndices.clear();
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
            m_showOnlyMode = false;
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

            // Aktivoi nappi kun valittuja pisteitä on
            if ( m_showOnlyButton )
                m_showOnlyButton->setEnabled( true );
        }
        else
        {
            // createFilteredHighlight palautti nullptr (tyhjä tulos)
            m_selectionIndices.clear();
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
            m_showOnlyMode = false;
        }

        // Jos "Näytä vain valinta" oli päällä, aktivoi se uudelleen päivitetyillä pisteillä
        if ( m_showOnlyMode )
            enableShowOnlyMode();

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
        m_updatingVisibility = true;
        m_visibilityListWidget->clear();

        // Classification-tilassa näkyvyyttä hallitaan Show-sarakkeella
        if ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
        {
            QTreeWidgetItem *item = new QTreeWidgetItem( m_visibilityListWidget );
            item->setFlags( Qt::ItemIsEnabled );  // ei checkable
            item->setText( 0, "Käytä ylemmän ikkunan Show-saraketta" );
            item->setForeground( 0, QBrush( Qt::gray ) );

            m_selectAllVisButton->setEnabled( false );

            m_updatingVisibility = false;
            return;
        }

        // Muissa tiloissa: palauta nappi käyttöön
        m_selectAllVisButton->setEnabled( true );

        // Tallenna nykyiset pois-valinnat ennen tyhjennystä
        QSet<QString> uncheckedValues;
        for ( int i = 0; i < m_visibilityListWidget->topLevelItemCount(); ++i )
            if ( m_visibilityListWidget->topLevelItem( i )->checkState( 0 ) == Qt::Unchecked )
                uncheckedValues.insert( m_visibilityListWidget->topLevelItem( i )->text( 0 ) );

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

        // Classification-tilassa Show-sarake hoitaa näkyvyyden
        if ( sfName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
            return;

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
    // applyShowFilter  (Show-sarake Luokat-listassa ohjaa 3D-näkyvyyttä)
    // ----------------------------------------------------------------

    void MaastoDialog::applyShowFilter()
    {
        if ( m_cloud == nullptr )
            return;

        // Kerää piilotettavat arvot (Show = Unchecked)
        QSet<float> hiddenValues;
        bool allShown = true;
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
        {
            const QTreeWidgetItem *item = m_listWidget->topLevelItem( i );
            // Show-sarake on col 1 vain kun columnCount >= 2
            if ( m_listWidget->columnCount() < 2 )
                break;
            if ( item->checkState( 1 ) == Qt::Unchecked )
            {
                hiddenValues.insert( item->text( 0 ).toFloat() );
                allShown = false;
            }
        }

        // Jos kaikki näkyvissä → poista maski
        if ( allShown || m_listWidget->topLevelItemCount() == 0 )
        {
            resetVisibility();
            return;
        }

        const QString sfName = m_valuesComboBox->currentText();
        if ( sfName.isEmpty() )
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
                "MaastoPlugin: Show-suodattimen visibility maskin alustus epäonnistui",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        auto& vis = m_cloud->getTheVisibilityArray();
        for ( unsigned i = 0; i < m_cloud->size(); ++i )
        {
            const float val = static_cast<float>( sf->getValue( i ) );
            vis[i] = hiddenValues.contains( val )
                     ? CCCoreLib::POINT_HIDDEN
                     : CCCoreLib::POINT_VISIBLE;
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

        m_ptcFilePath = filePath;

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
    // Mittaa: startMeasure / stopMeasure / removeMeasureHighlight
    // ----------------------------------------------------------------

    void MaastoDialog::removeMeasureHighlight()
    {
        if ( m_measureHighlight )
        {
            m_appInterface->removeFromDB( m_measureHighlight );  // autoDelete=true poistaa muistin
            m_measureHighlight = nullptr;
            ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
            if ( win )
                win->redraw();
        }
    }

    void MaastoDialog::startMeasure( bool isNear )
    {
        // Pysäytä viiva-työkalu jos käynnissä
        if ( m_linePickState > 0 )
        {
            stopLinePicking();
            if ( m_drawLineButton )
            {
                m_drawLineButton->blockSignals( true );
                m_drawLineButton->setChecked( false );
                m_drawLineButton->blockSignals( false );
            }
        }

        m_measuringNear = isNear;
        m_measureState  = 1;  // odottaa pistettä

        // Poista mahdollinen vanha highlight
        removeMeasureHighlight();

        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
        if ( !win )
        {
            stopMeasure();
            return;
        }

        // Aktivoi point picking
        win->setPickingMode( ccGLWindowInterface::POINT_PICKING );

        // Yhdistä itemPicked-signaali: jokainen klikkaus päivittää mittauspisteen
        // Picking ja signaali pysyvät voimassa kunnes käyttäjä painaa "Hyväksy"
        connect( win->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked,
            this, [this, win]( ccHObject *entity, unsigned /*itemIdx*/,
                               int /*x*/, int /*y*/,
                               const CCVector3 &P, const CCVector3d & /*uvw*/ )
            {
                Q_UNUSED( entity )

                // Tallenna valittu piste
                m_measuredX = static_cast<double>( P.x );
                m_measuredY = static_cast<double>( P.y );
                m_measuredZ = static_cast<double>( P.z );
                m_measureState = 2;  // piste valittu, odottaa hyväksyntää

                // Päivitä highlight-piste (poista vanha, luo uusi)
                removeMeasureHighlight();
                ccPointCloud *dot = new ccPointCloud( "MeasurePoint" );
                dot->reserve( 1 );
                dot->addPoint( P );
                dot->setColor( ccColor::Rgb(
                    static_cast<ColorCompType>( m_measurePointColor.red() ),
                    static_cast<ColorCompType>( m_measurePointColor.green() ),
                    static_cast<ColorCompType>( m_measurePointColor.blue() ) ) );
                dot->showColors( true );
                dot->showSF( false );
                dot->setPointSize( static_cast<unsigned>( m_measurePointSize ) );
                m_appInterface->addToDB( dot, false, true, false );
                m_measureHighlight = dot;
                win->redraw();

                // Laske etäisyys kamerasta ja tulosta consoleen heti
                CCVector3d cam = win->getViewportParameters()
                    .computeViewMatrix().inverse().getTranslationAsVec3D();
                double dx   = m_measuredX - cam.x;
                double dy   = m_measuredY - cam.y;
                double dz   = m_measuredZ - cam.z;
                double dist = std::sqrt( dx*dx + dy*dy + dz*dz );
                const QString label = m_measuringNear ? "Lähin etäisyys" : "Pisin etäisyys";
                m_appInterface->dispToConsole(
                    QString( "MaastoPlugin: %1 = %2 m" ).arg( label ).arg( dist, 0, 'f', 2 ),
                    ccMainAppInterface::STD_CONSOLE_MESSAGE );

                // Päivitä napin teksti → "Hyväksy"
                QPushButton *btn = m_measuringNear ? m_measureNearButton : m_measureFarButton;
                if ( btn )
                    btn->setText( "Hyväksy" );
            },
            Qt::UniqueConnection
        );
    }

    void MaastoDialog::stopMeasure()
    {
        // Irrota signaali tarvittaessa
        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
        if ( win )
        {
            disconnect( win->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked,
                        this, nullptr );
            win->setPickingMode( ccGLWindowInterface::DEFAULT_PICKING );
        }

        removeMeasureHighlight();

        m_measureState = 0;

        // Palauta napit normaaliksi
        if ( m_measureNearButton )
        {
            m_measureNearButton->blockSignals( true );
            m_measureNearButton->setChecked( false );
            m_measureNearButton->setText( "Mittaa" );
            m_measureNearButton->blockSignals( false );
        }
        if ( m_measureFarButton )
        {
            m_measureFarButton->blockSignals( true );
            m_measureFarButton->setChecked( false );
            m_measureFarButton->setText( "Mittaa" );
            m_measureFarButton->blockSignals( false );
        }
    }

    // ----------------------------------------------------------------
    // removeLineHighlights
    // ----------------------------------------------------------------

    void MaastoDialog::removeLineHighlights()
    {
        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();

        if ( m_linePoint1Highlight )
        {
            m_appInterface->removeFromDB( m_linePoint1Highlight );
            m_linePoint1Highlight = nullptr;
        }
        if ( m_linePoint2Highlight )
        {
            m_appInterface->removeFromDB( m_linePoint2Highlight );
            m_linePoint2Highlight = nullptr;
        }
        if ( win )
            win->redraw();
    }

    // ----------------------------------------------------------------
    // startLinePicking
    // ----------------------------------------------------------------

    void MaastoDialog::startLinePicking()
    {
        // Pysäytä mittaustyökalu jos käynnissä (molemmat käyttävät POINT_PICKING)
        if ( m_measureState > 0 )
            stopMeasure();

        removeLineHighlights();
        m_linePickState = 1;

        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
        if ( !win )
        {
            stopLinePicking();
            return;
        }

        win->setPickingMode( ccGLWindowInterface::POINT_PICKING );

        connect( win->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked,
            this, [this, win]( ccHObject *entity, unsigned /*itemIdx*/,
                               int /*x*/, int /*y*/,
                               const CCVector3 &P, const CCVector3d & /*uvw*/ )
            {
                Q_UNUSED( entity )

                // Luo highlight-dot valitulle pisteelle (sama tyyli kuin mittaus)
                auto makeDot = [&]( const CCVector3 &pt ) -> ccHObject*
                {
                    ccPointCloud *dot = new ccPointCloud( "LinePoint" );
                    dot->reserve( 1 );
                    dot->addPoint( pt );
                    dot->setColor( ccColor::Rgb(
                        static_cast<ColorCompType>( m_measurePointColor.red() ),
                        static_cast<ColorCompType>( m_measurePointColor.green() ),
                        static_cast<ColorCompType>( m_measurePointColor.blue() ) ) );
                    dot->showColors( true );
                    dot->showSF( false );
                    dot->setPointSize( static_cast<unsigned>( m_measurePointSize ) );
                    m_appInterface->addToDB( dot, false, true, false );
                    return dot;
                };

                if ( m_linePickState == 1 )
                {
                    // Ensimmäinen piste
                    m_lineP1 = P;
                    if ( m_linePoint1Highlight )
                        m_appInterface->removeFromDB( m_linePoint1Highlight );
                    m_linePoint1Highlight = makeDot( P );
                    m_linePickState = 2;

                    m_appInterface->dispToConsole(
                        QString( "MaastoPlugin: viivan 1. piste valittu (%1, %2, %3)" )
                            .arg( P.x, 0, 'f', 3 )
                            .arg( P.y, 0, 'f', 3 )
                            .arg( P.z, 0, 'f', 3 ),
                        ccMainAppInterface::STD_CONSOLE_MESSAGE );

                    win->redraw();
                }
                else if ( m_linePickState == 2 )
                {
                    // Toinen piste — viiva valmis
                    m_lineP2 = P;
                    if ( m_linePoint2Highlight )
                        m_appInterface->removeFromDB( m_linePoint2Highlight );
                    m_linePoint2Highlight = makeDot( P );

                    m_appInterface->dispToConsole(
                        QString( "MaastoPlugin: viivan 2. piste valittu (%1, %2, %3)" )
                            .arg( P.x, 0, 'f', 3 )
                            .arg( P.y, 0, 'f', 3 )
                            .arg( P.z, 0, 'f', 3 ),
                        ccMainAppInterface::STD_CONSOLE_MESSAGE );

                    win->redraw();

                    // Rakenna viivakappale ja laske pisteet
                    processLine();

                    // Poista väliaikaiset highlight-dotit (mesh jää näkyviin)
                    removeLineHighlights();

                    // Aloita uusi viiva automaattisesti
                    m_linePickState = 1;
                }
            },
            Qt::UniqueConnection
        );
    }

    // ----------------------------------------------------------------
    // stopLinePicking
    // ----------------------------------------------------------------

    void MaastoDialog::stopLinePicking()
    {
        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
        if ( win )
        {
            disconnect( win->signalEmitter(), &ccGLWindowSignalEmitter::itemPicked,
                        this, nullptr );
            win->setPickingMode( ccGLWindowInterface::DEFAULT_PICKING );
        }

        removeLineHighlights();
        m_linePickState = 0;
    }

    // ----------------------------------------------------------------
    // processLine
    // ----------------------------------------------------------------

    void MaastoDialog::processLine()
    {
        if ( !m_cloud )
            return;

        // Hae akseli ja paksuus UI:sta
        const QString axisStr = m_lineAxisComboBox ? m_lineAxisComboBox->currentText() : "Z";
        const char    axis    = axisStr.isEmpty() ? 'Z' : axisStr.at( 0 ).toLatin1();
        const double  thickness = m_lineThicknessSpinBox
                                  ? m_lineThicknessSpinBox->value()
                                  : 1.0;

        // Mahdollisesti laajennetaan viiva bounding boxin rajoille
        CCVector3 p1 = m_lineP1;
        CCVector3 p2 = m_lineP2;
        if ( m_extendLineToBBoxCheckBox && m_extendLineToBBoxCheckBox->isChecked() )
            extendLineToBBox( p1, p2 );

        // Päivitä m_lineP1/P2 (laajennettu tai alkuperäinen) → copyLineRight käyttää näitä
        m_lineP1 = p1;
        m_lineP2 = p2;

        // Rakenna boksi-mesh (kokonaan oikealle puolelle viivasta)
        ccMesh *mesh = VolumeBuilder::buildFromLine( p1, p2, axis, thickness, 10000.0 );
        if ( mesh )
        {
            m_appInterface->addToDB( mesh );
            m_meshObjects.push_back( mesh );
        }
        else
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: viiva-kappaleen luonti epäonnistui",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
        }

        // Kerää pisteet jotka ovat viivan oikealla puolella paksuuden verran
        std::vector<unsigned> indices;
        VolumeBuilder::highlightPointsInsideLineVolume(
            p1, p2, axis, m_cloud, thickness, &indices );

        if ( !indices.empty() )
        {
            for ( unsigned idx : indices )
                m_indexHitCount[idx]++;
        }
        else
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: viivan sisällä ei pisteitä — kokeile suurempaa paksuutta",
                ccMainAppInterface::STD_CONSOLE_MESSAGE );
        }

        // Aktivoi "Kopioi oikealle" -nappi
        if ( m_copyLineRightButton )
            m_copyLineRightButton->setEnabled( true );

        refreshHighlights();
    }

    // ----------------------------------------------------------------
    // extendLineToBBox
    // Laajentaa viivan (p1→p2) pistepilven bounding boxin rajoille
    // viivan suunnassa käyttäen ray-AABB slab-intersection -algoritmia.
    // ----------------------------------------------------------------

    void MaastoDialog::extendLineToBBox( CCVector3& p1, CCVector3& p2 ) const
    {
        if ( !m_cloud )
            return;

        ccBBox bb = m_cloud->getOwnBB();
        if ( !bb.isValid() )
            return;

        const CCVector3d p1d( static_cast<double>( p1.x ),
                              static_cast<double>( p1.y ),
                              static_cast<double>( p1.z ) );
        const CCVector3d p2d( static_cast<double>( p2.x ),
                              static_cast<double>( p2.y ),
                              static_cast<double>( p2.z ) );

        const CCVector3d lineVec = p2d - p1d;
        const double lineLen = lineVec.norm();
        if ( lineLen < 1e-10 )
            return;

        const CCVector3d dir = lineVec / lineLen;

        const CCVector3d bbMin( static_cast<double>( bb.minCorner().x ),
                                static_cast<double>( bb.minCorner().y ),
                                static_cast<double>( bb.minCorner().z ) );
        const CCVector3d bbMax( static_cast<double>( bb.maxCorner().x ),
                                static_cast<double>( bb.maxCorner().y ),
                                static_cast<double>( bb.maxCorner().z ) );

        // Ray-AABB slab intersection
        // Säde: p1d + t * dir
        // Etsitään t_min (taaksepäin) ja t_max (eteenpäin) jotka kattavat koko bb:n
        double tMin = -1e18;
        double tMax =  1e18;

        for ( int i = 0; i < 3; ++i )
        {
            if ( std::abs( dir.u[i] ) < 1e-10 )
            {
                // Säde on lähes yhdensuuntainen tämän akselin kanssa
                // Jos p1 on bounding boxin ulkopuolella tällä akselilla → ei leikkausta
                if ( p1d.u[i] < bbMin.u[i] || p1d.u[i] > bbMax.u[i] )
                    return;  // ei leikkausta → jätetään alkuperäiset pisteet
            }
            else
            {
                const double t1 = ( bbMin.u[i] - p1d.u[i] ) / dir.u[i];
                const double t2 = ( bbMax.u[i] - p1d.u[i] ) / dir.u[i];
                tMin = std::max( tMin, std::min( t1, t2 ) );
                tMax = std::min( tMax, std::max( t1, t2 ) );
            }
        }

        if ( tMin > tMax )
            return;  // ei leikkausta → jätetään alkuperäiset pisteet

        // Uudet pisteet bounding boxin rajoilla
        const CCVector3d newP1d = p1d + dir * tMin;
        const CCVector3d newP2d = p1d + dir * tMax;

        p1 = CCVector3( static_cast<float>( newP1d.x ),
                        static_cast<float>( newP1d.y ),
                        static_cast<float>( newP1d.z ) );
        p2 = CCVector3( static_cast<float>( newP2d.x ),
                        static_cast<float>( newP2d.y ),
                        static_cast<float>( newP2d.z ) );
    }

    // ----------------------------------------------------------------
    // copyLineRight
    // ----------------------------------------------------------------

    void MaastoDialog::copyLineRight()
    {
        if ( !m_cloud )
            return;

        const QString axisStr = m_lineAxisComboBox ? m_lineAxisComboBox->currentText() : "Z";
        const char    axis    = axisStr.isEmpty() ? 'Z' : axisStr.at( 0 ).toLatin1();
        const double  thickness = m_lineThicknessSpinBox
                                  ? m_lineThicknessSpinBox->value()
                                  : 1.0;

        // Laske thickDir — sama kuin VolumeBuilder:ssa
        CCVector3d axisDir( 0.0, 0.0, 0.0 );
        if      ( axis == 'X' || axis == 'x' ) axisDir.x = 1.0;
        else if ( axis == 'Y' || axis == 'y' ) axisDir.y = 1.0;
        else                                   axisDir.z = 1.0;

        const CCVector3d p1d( static_cast<double>( m_lineP1.x ),
                              static_cast<double>( m_lineP1.y ),
                              static_cast<double>( m_lineP1.z ) );
        const CCVector3d p2d( static_cast<double>( m_lineP2.x ),
                              static_cast<double>( m_lineP2.y ),
                              static_cast<double>( m_lineP2.z ) );

        const CCVector3d lineVec = p2d - p1d;
        const double lineLen = lineVec.norm();
        if ( lineLen < 1e-10 )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: ei viivaa kopioitavaksi",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        const CCVector3d lineDirNorm = lineVec / lineLen;
        CCVector3d thickDir = lineDirNorm.cross( axisDir );
        const double thickDirLen = thickDir.norm();
        if ( thickDirLen < 1e-10 )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: viiva yhdensuuntainen akselin kanssa, kopiointia ei voi tehdä",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }
        thickDir /= thickDirLen;

        // Siirrä P1 ja P2 yhden paksuuden verran oikealle
        const CCVector3d shift = thickDir * thickness;
        const CCVector3d newP1d = p1d + shift;
        const CCVector3d newP2d = p2d + shift;

        const CCVector3 newP1( static_cast<float>( newP1d.x ),
                               static_cast<float>( newP1d.y ),
                               static_cast<float>( newP1d.z ) );
        const CCVector3 newP2( static_cast<float>( newP2d.x ),
                               static_cast<float>( newP2d.y ),
                               static_cast<float>( newP2d.z ) );

        // Rakenna uusi mesh siirretyillä pisteillä
        ccMesh *mesh = VolumeBuilder::buildFromLine( newP1, newP2, axis, thickness, 10000.0 );
        if ( mesh )
        {
            m_appInterface->addToDB( mesh );
            m_meshObjects.push_back( mesh );
        }
        else
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: kopiointi epäonnistui",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
        }

        // Kerää pisteet uuden muodon sisältä
        std::vector<unsigned> indices;
        VolumeBuilder::highlightPointsInsideLineVolume(
            newP1, newP2, axis, m_cloud, thickness, &indices );

        if ( !indices.empty() )
        {
            for ( unsigned idx : indices )
                m_indexHitCount[idx]++;
        }
        else
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: kopion sisällä ei pisteitä",
                ccMainAppInterface::STD_CONSOLE_MESSAGE );
        }

        // Päivitä m_lineP1/P2 uusiksi → seuraava kopioi jatkaa siitä
        m_lineP1 = newP1;
        m_lineP2 = newP2;

        refreshHighlights();
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
