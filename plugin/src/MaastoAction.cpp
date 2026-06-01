#include "MaastoAction.h"
#include "PolygonDrawer.h"
#include "SettingsDialog.h"
#include "VolumeBuilder.h"
#include "ClassDefinition.h"

#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"
#include "ccGLWindowSignalEmitter.h"
#include "ccHObjectCaster.h"
#include "ccMesh.h"
#include "ccPointCloud.h"
#include "ccBBox.h"
#include "ccScalarField.h"
#include "ccColorScale.h"
#include "ccHObject.h"
#include "ccColorTypes.h"

#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
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
#include <QFileInfo>
#include <QRegularExpression>
#include <QCheckBox>
#include <QIcon>
#include <QMap>
#include <map>
#include <unordered_map>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QSplitter>

#include "ccPickingHub.h"
#include "ccPickingListener.h"
#include "ccGLWindowInterface.h"
#include "FileIOFilter.h"

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
            names << QString::fromStdString( cloud->getScalarFieldName( i ) );

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
        // Sulje MDI-ikkuna siististi (vain jos omistamme sen)
        if ( m_selectionGLWindow )
        {
            if ( m_selectionOnlyCloud )
                m_selectionGLWindow->removeFromOwnDB( m_selectionOnlyCloud );
            if ( m_selectionWindowIsOwned && m_selectionGLWidget )
            {
                // Poistetaan kopiot removeFromOwnDB+delete kautta ennen ikkunan tuhoamista
                clearSelectionHighlights();
                clearSelectionMeshes();
                clearSelectionSizeSubClouds();
                m_selectionWindowPrismOffset = 0;
                if ( m_selectionOnlyCloud )
                {
                    m_selectionOnlyCloud->setDisplay( nullptr );
                    delete m_selectionOnlyCloud;
                    m_selectionOnlyCloud = nullptr;
                }
                // Tyhjennetään OwnDB kokonaan varmuuden vuoksi
                if ( m_selectionGLWindow->getOwnDB() )
                    m_selectionGLWindow->getOwnDB()->removeAllChildren();
                // Suljetaan QMdiSubWindow ennen GL-ikkunan tuhoamista
                if ( QMdiSubWindow *sub = qobject_cast<QMdiSubWindow*>( m_selectionGLWidget->parent() ) )
                    sub->close();
                m_selectionGLWidget->hide();
                m_appInterface->destroyGLWindow( m_selectionGLWindow );
                m_selectionGLWidget = nullptr;
            }
            m_selectionGLWindow = nullptr;
        }
        removeSelectionOnlyCloud();

        // Poista highlight-pistepilvet DB-puusta
        removeHighlightObjects();

        // Poista kaikki 3D-prismat DB-puusta
        for ( ccHObject *obj : m_meshObjects )
            m_appInterface->removeFromDB( obj, true );
        m_meshObjects.clear();
        m_prismData.clear();

        // Poista pistekoko-sub-pilvet DB-puusta (pääpilven lapset)
        clearAllSizeSubClouds();

        // Palauta pistepilvi alkutilaan ennen tuhoamista
        removePtcColors();
        resetVisibility();
    }

    // ----------------------------------------------------------------
    // Point picking
    // ----------------------------------------------------------------

    void MaastoDialog::removePickMarker()
    {
        if ( m_pickMarker )
        {
            m_appInterface->removeFromDB( m_pickMarker, true );
            m_pickMarker = nullptr;
            ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
            if ( win )
                win->redraw();
        }
    }

    void MaastoDialog::onItemPicked( const ccPickingListener::PickedItem &pi )
    {
        if ( !pi.entity )
            return;

        // --- Koordinaatit ---
        const CCVector3 &P = pi.P3D;

        // --- Luokkakoodi ja -nimi ---
        QString classInfo = "—";
        if ( m_cloud && pi.entity == m_cloud )
        {
            // Etsi "Classification"-skalaarikentän indeksi
            int sfIdx = m_cloud->getScalarFieldIndexByName( "Classification" );
            if ( sfIdx < 0 )
            {
                // Kokeile myös aktiivisella skalaarikentällä jos nimeä ei löydy
                sfIdx = m_cloud->getCurrentDisplayedScalarFieldIndex();
            }
            if ( sfIdx >= 0 )
            {
                ScalarType val = m_cloud->getScalarField( sfIdx )->getValue( pi.itemIndex );
                int code = static_cast<int>( val );
                if ( m_classDefinitions.contains( code ) )
                    classInfo = QString( "%1 - %2" ).arg( code )
                                    .arg( m_classDefinitions[code].name );
                else
                    classInfo = QString::number( code );
            }
        }

        // --- Päivitä label ---
        if ( m_pickInfoLabel )
        {
            m_pickInfoLabel->setText(
                QString( "X: %1   Y: %2   Z: %3\nLuokka: %4" )
                    .arg( P.x, 0, 'f', 3 )
                    .arg( P.y, 0, 'f', 3 )
                    .arg( P.z, 0, 'f', 3 )
                    .arg( classInfo ) );
            m_pickInfoLabel->setVisible( true );
        }

        // --- Korostuspiste 3D:ssä ---
        removePickMarker();

        ccPointCloud *marker = new ccPointCloud( "pick_marker" );
        if ( marker->reserve( 1 ) )
        {
            marker->addPoint( P );
            marker->setGlobalShift( m_cloud ? m_cloud->getGlobalShift() : CCVector3d( 0, 0, 0 ) );
            marker->setGlobalScale( m_cloud ? m_cloud->getGlobalScale() : 1.0 );
            marker->showColors( false );
            marker->setPointSize( static_cast<unsigned>( m_highlightPointSize + 4 ) );
            marker->setTempColor( ccColor::Rgba( 255, 64, 0, 255 ) );
            m_appInterface->addToDB( marker );
            m_pickMarker = marker;
        }
        else
        {
            delete marker;
        }

        ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
        if ( win )
            win->redraw();
    }

    // ----------------------------------------------------------------

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
        , m_meshOpacity( 70 )
        , m_lastSaveDir( "" )
        , m_lastSettingsFilePath( "" )
        , m_polygonDrawer( new PolygonDrawer( appInterface, this ) )
        , m_polygonButton( nullptr )
        , m_highlightButton( nullptr )
        , m_clearSelectionButton( nullptr )
        , m_showOnlyButton( nullptr )
        , m_separateWindowCheckBox( nullptr )
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
        , m_lockViewMode( false )
        , m_view2Frozen( false )
        , m_selectionWindowIsOwned( false )
        , m_selectionOnlyCloud( nullptr )
        , m_selectionGLWindow( nullptr )
        , m_selectionGLWidget( nullptr )
        , m_workingGLWindow( nullptr )
        , m_preLockedPrismCount( 0 )
        , m_selectionWindowPrismOffset( 0 )
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
        , m_lastTargetCode( -1 )
        , m_saveButton( nullptr )
        , m_autoSaveCheckBox( nullptr )
        , m_pickingHub( nullptr )
        , m_pickPointButton( nullptr )
        , m_pickInfoLabel( nullptr )
        , m_pickMarker( nullptr )
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

        // --- Yläpalkki: Asetukset-nappi vasemmalla, Poimi piste sen oikealla ---
        m_pickingHub = appInterface->pickingHub();
        {
            QHBoxLayout *topRow = new QHBoxLayout();
            m_fileButton = new QPushButton( "Asetukset", this );
            m_fileButton->setFixedWidth( 80 );
            topRow->addWidget( m_fileButton );

            m_pickPointButton = new QPushButton( "Poimi piste", this );
            m_pickPointButton->setFixedWidth( 90 );
            m_pickPointButton->setCheckable( true );
            m_pickPointButton->setEnabled( m_pickingHub != nullptr );
            topRow->addWidget( m_pickPointButton );

            topRow->addStretch();

            m_autoSaveCheckBox = new QCheckBox( "Automaattitallennus", this );
            m_autoSaveCheckBox->setChecked( false );
            m_autoSaveCheckBox->setEnabled( false );
            topRow->addWidget( m_autoSaveCheckBox );

            m_saveButton = new QPushButton( "Tallenna", this );
            m_saveButton->setFixedWidth( 80 );
            m_saveButton->setEnabled( false );
            topRow->addWidget( m_saveButton );

            layout->addLayout( topRow );
        }

        connect( m_pickPointButton, &QPushButton::toggled, this,
            [this]( bool checked )
            {
                if ( !m_pickingHub ) return;
                if ( checked )
                {
                    if ( !m_pickingHub->addListener( this, /*exclusive=*/true,
                                                     /*autoStart=*/true,
                                                     ccGLWindowInterface::POINT_PICKING ) )
                    {
                        m_pickPointButton->blockSignals( true );
                        m_pickPointButton->setChecked( false );
                        m_pickPointButton->blockSignals( false );
                    }
                }
                else
                {
                    m_pickingHub->removeListener( this );
                    removePickMarker();
                    if ( m_pickInfoLabel )
                        m_pickInfoLabel->clear();
                }
            } );

        connect( m_saveButton, &QPushButton::clicked, this, [this]()
        {
            saveLasFile();
        } );

        connect( m_fileButton, &QPushButton::clicked, this, [this]()
        {
            const QString currentPath = m_ptcFilePath;

            // Oletushakemisto: PTC-tiedoston hakemisto jos asetettu, muuten m_lastSaveDir
            const QString defaultDir = !m_ptcFilePath.isEmpty()
                ? QFileInfo( m_ptcFilePath ).absolutePath()
                : ( !m_lastSaveDir.isEmpty() ? m_lastSaveDir : QDir::homePath() );

            SettingsDialog dlg( m_highlightPointSize, m_highlightColor, currentPath,
                                m_measurePointSize, m_measurePointColor,
                                m_meshOpacity,
                                m_nearDistSpinBox        ? m_nearDistSpinBox->value()        : 10.0,
                                m_farDistSpinBox         ? m_farDistSpinBox->value()         : 1000.0,
                                m_lineThicknessSpinBox   ? m_lineThicknessSpinBox->value()   : 1.0,
                                m_minPolygonCountSpinBox ? m_minPolygonCountSpinBox->value()  : 1,
                                m_lineAxisComboBox       ? m_lineAxisComboBox->currentText() : "Z",
                                defaultDir,
                                m_lastSettingsFilePath,
                                this );

            // Kun käyttäjä valitsee .ptc-tiedoston asetuksissa → lataa heti
            connect( &dlg, &SettingsDialog::ptcFileLoaded,
                this, [this]( const QString &path ) { loadPtcFile( path ); } );

            // Luokkamäärittelyn tallennus
            connect( &dlg, &SettingsDialog::savePtcRequested,
                this, &MaastoDialog::onSavePtc );

            // Tallennus- ja lataussignaalit
            connect( &dlg, &SettingsDialog::saveRequested,
                this, &MaastoDialog::onSaveSettings );
            connect( &dlg, &SettingsDialog::loadRequested,
                this, [this, &dlg]() { onLoadSettings( &dlg ); } );

            if ( dlg.exec() == QDialog::Accepted )
            {
                m_highlightPointSize = dlg.pointSize();
                m_highlightColor     = dlg.color();
                m_measurePointSize   = dlg.measurePointSize();
                m_measurePointColor  = dlg.measurePointColor();
                m_meshOpacity        = dlg.meshOpacity();
                refreshHighlights();
            }
        } );

        // --- Scalar field ---
        layout->addWidget( new QLabel( "Scalar field:", this ) );
        m_valuesComboBox = new QComboBox( this );
        layout->addWidget( m_valuesComboBox );

        // --- Splitter: "Luokittele luokista" + "Näkyvät luokat" ---
        QSplitter *listSplitter = new QSplitter( Qt::Vertical, this );
        listSplitter->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

        // Yläpaneeli: "Luokittele luokista"
        QWidget *topPane = new QWidget( listSplitter );
        QVBoxLayout *topPaneLayout = new QVBoxLayout( topPane );
        topPaneLayout->setContentsMargins( 0, 0, 0, 0 );
        topPaneLayout->setSpacing( 2 );
        {
            QHBoxLayout *arvoHeader = new QHBoxLayout();
            arvoHeader->addWidget( new QLabel( "Luokittele luokista:", topPane ) );
            m_selectAllButton = new QPushButton( "Valitse kaikki", topPane );
            m_selectAllButton->setCheckable( true );
            m_selectAllButton->setFixedHeight( 22 );
            arvoHeader->addWidget( m_selectAllButton );
            m_showAllButton = new QPushButton( "Hide all", topPane );
            m_showAllButton->setCheckable( true );
            m_showAllButton->setChecked( true );
            m_showAllButton->setFixedHeight( 22 );
            arvoHeader->addWidget( m_showAllButton );
            m_editClassesButton = new QPushButton( "Muokkaa", topPane );
            m_editClassesButton->setFixedHeight( 22 );
            m_editClassesButton->setEnabled( false ); // aktivoituu kun .ptc ladattu
            arvoHeader->addWidget( m_editClassesButton );
            topPaneLayout->addLayout( arvoHeader );
        }
        m_listWidget = new QTreeWidget( topPane );
        m_listWidget->setMinimumHeight( 60 );
        m_listWidget->setHeaderHidden( true );
        m_listWidget->setRootIsDecorated( false );
        topPaneLayout->addWidget( m_listWidget );
        listSplitter->addWidget( topPane );

        // Alapaneeli: "Näkyvät luokat"
        QWidget *botPane = new QWidget( listSplitter );
        QVBoxLayout *botPaneLayout = new QVBoxLayout( botPane );
        botPaneLayout->setContentsMargins( 0, 0, 0, 0 );
        botPaneLayout->setSpacing( 2 );
        {
            QHBoxLayout *visHeader = new QHBoxLayout();
            visHeader->addWidget( new QLabel( "Näkyvät luokat:", botPane ) );
            m_selectAllVisButton = new QPushButton( "Valitse kaikki", botPane );
            m_selectAllVisButton->setCheckable( true );
            m_selectAllVisButton->setChecked( true );
            m_selectAllVisButton->setFixedHeight( 22 );
            visHeader->addWidget( m_selectAllVisButton );
            botPaneLayout->addLayout( visHeader );
        }
        m_visibilityListWidget = new QTreeWidget( botPane );
        m_visibilityListWidget->setMinimumHeight( 60 );
        m_visibilityListWidget->setHeaderHidden( true );
        m_visibilityListWidget->setRootIsDecorated( false );
        botPaneLayout->addWidget( m_visibilityListWidget );
        listSplitter->addWidget( botPane );

        listSplitter->setSizes( { 200, 150 } );
        layout->addWidget( listSplitter );

        // --- Pick-info-label ---
        m_pickInfoLabel = new QLabel( this );
        m_pickInfoLabel->setAlignment( Qt::AlignLeft | Qt::AlignVCenter );
        m_pickInfoLabel->setWordWrap( true );
        m_pickInfoLabel->setStyleSheet( "QLabel { background: #1e1e1e; color: #d4d4d4;"
                                        " padding: 4px; border-radius: 3px; }" );
        m_pickInfoLabel->setVisible( false );
        layout->addWidget( m_pickInfoLabel );

        // --- Luokittele luokkaan ---
        layout->addWidget( new QLabel( "Luokittele luokkaan:", this ) );
        m_targetClassComboBox = new QComboBox( this );
        layout->addWidget( m_targetClassComboBox );

        // Tallenna valinta pysyvästi jäsenmuuttujaan aina kun se muuttuu
        connect( m_targetClassComboBox,
            QOverload<int>::of( &QComboBox::currentIndexChanged ),
            [this]( int idx )
            {
                if ( idx >= 0 )
                {
                    const QVariant d = m_targetClassComboBox->itemData( idx );
                    m_lastTargetCode = d.isValid() ? d.toInt() : idx;
                }
                // Päivitä korostukset — kohdeluokan pisteet suodatetaan pois
                if ( !m_indexHitCount.empty() )
                    refreshHighlights();
            } );

        // --- Pisteiden väritys (RGB + scalar-kentät) ---
        layout->addWidget( new QLabel( "Pisteiden väritys:", this ) );
        m_colorComboBox = new QComboBox( this );
        layout->addWidget( m_colorComboBox );

        // Päivitä arvolista ja kohdeluokka kun valuesComboBox muuttuu
        connect( m_valuesComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                populateValueList( fieldName );
                populateTargetClassComboBox( m_lastTargetCode );
            } );

        // Aseta värjäys + päivitä näkyvyyslista kun colorComboBox muuttuu
        connect( m_colorComboBox, &QComboBox::currentTextChanged,
            [this]( const QString &fieldName )
            {
                applyColorField( fieldName );
                populateVisibilityList( fieldName );
            } );

        // Muokkaa-nappi → avaa ClassEditorDialog
        connect( m_editClassesButton, &QPushButton::clicked,
            this, &MaastoDialog::onEditClasses );

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
                {
                    QTreeWidgetItem *it = m_listWidget->topLevelItem( i );
                    it->setCheckState( 0, state );
                    const int code = it->text( 0 ).toInt();
                    if ( checked )
                        m_checkedClassCodes.insert( code );
                    else
                        m_checkedClassCodes.remove( code );
                }
                m_listWidget->blockSignals( false );
                refreshHighlights();
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
                    // Value-sarakkeen muutos — päivitä pysyvä valintamuisti
                    const int code = item->text( 0 ).toInt();
                    if ( item->checkState( 0 ) == Qt::Checked )
                        m_checkedClassCodes.insert( code );
                    else
                        m_checkedClassCodes.remove( code );

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

        // --- Välilehdet: Polygon ja Viiva ---
        m_polygonButton = new QPushButton( "Piirrä polygon", this );
        m_polygonButton->setCheckable( true );

        m_drawLineButton = new QPushButton( "Piirrä viiva", this );
        m_drawLineButton->setCheckable( true );

        m_lineAxisComboBox = new QComboBox( this );
        m_lineAxisComboBox->addItem( "Z" );
        m_lineAxisComboBox->addItem( "X" );
        m_lineAxisComboBox->addItem( "Y" );
        m_lineAxisComboBox->setFixedWidth( 48 );

        m_copyLineRightButton = new QPushButton( "Kopioi oikealle", this );
        m_copyLineRightButton->setEnabled( false );  // disabloitu kunnes viiva on piirretty

        QTabWidget *toolTabs = new QTabWidget( this );

        // Tab 1: Polygon
        QWidget *polygonTab = new QWidget();
        QVBoxLayout *polygonTabLayout = new QVBoxLayout( polygonTab );
        polygonTabLayout->setContentsMargins( 4, 4, 4, 4 );
        polygonTabLayout->addWidget( m_polygonButton );
        polygonTabLayout->addStretch();
        toolTabs->addTab( polygonTab, "Polygon" );

        // Tab 2: Viiva
        m_lineThicknessSpinBox = new QDoubleSpinBox( this );
        m_lineThicknessSpinBox->setRange( 0.01, 9999.0 );
        m_lineThicknessSpinBox->setSingleStep( 0.1 );
        m_lineThicknessSpinBox->setValue( 1.0 );
        m_lineThicknessSpinBox->setSuffix( " m" );
        m_lineThicknessSpinBox->setDecimals( 2 );

        m_extendLineToBBoxCheckBox = new QCheckBox( "Jatka viivan pituutta", this );
        m_extendLineToBBoxCheckBox->setChecked( false );

        QWidget *viivaTab = new QWidget();
        QVBoxLayout *viivaTabLayout = new QVBoxLayout( viivaTab );
        viivaTabLayout->setContentsMargins( 4, 4, 4, 4 );

        QHBoxLayout *viivaToolRow = new QHBoxLayout();
        viivaToolRow->addWidget( m_drawLineButton );
        viivaToolRow->addWidget( m_lineAxisComboBox );
        viivaToolRow->addWidget( m_copyLineRightButton );
        viivaToolRow->addStretch();
        viivaTabLayout->addLayout( viivaToolRow );

        QFormLayout *viivaForm = new QFormLayout();
        viivaForm->setContentsMargins( 0, 4, 0, 0 );
        {
            QWidget     *thickContainer = new QWidget( this );
            QHBoxLayout *thickRow       = new QHBoxLayout( thickContainer );
            thickRow->setContentsMargins( 0, 0, 0, 0 );
            thickRow->addWidget( m_lineThicknessSpinBox );
            thickRow->addWidget( m_extendLineToBBoxCheckBox );
            viivaForm->addRow( "Viivatyökalun paksuus:", thickContainer );
        }
        viivaTabLayout->addLayout( viivaForm );
        viivaTabLayout->addStretch();
        toolTabs->addTab( viivaTab, "Viiva" );

        // Tab 3: Etäisyyksien määrittely
        m_nearDistSpinBox = new QDoubleSpinBox( this );
        m_nearDistSpinBox->setRange( 0.1, 9999.0 );
        m_nearDistSpinBox->setSingleStep( 0.5 );
        m_nearDistSpinBox->setValue( 10.0 );
        m_nearDistSpinBox->setSuffix( " m" );
        m_nearDistSpinBox->setDecimals( 1 );

        m_measureNearButton = new QPushButton( "Mittaa", this );
        m_measureNearButton->setCheckable( true );
        m_measureNearButton->setFixedWidth( 60 );

        m_farDistSpinBox = new QDoubleSpinBox( this );
        m_farDistSpinBox->setRange( 0.1, 9999.0 );
        m_farDistSpinBox->setSingleStep( 0.5 );
        m_farDistSpinBox->setValue( 1000.0 );
        m_farDistSpinBox->setSuffix( " m" );
        m_farDistSpinBox->setDecimals( 1 );

        m_measureFarButton = new QPushButton( "Mittaa", this );
        m_measureFarButton->setCheckable( true );
        m_measureFarButton->setFixedWidth( 60 );

        QWidget *etaisyysTab = new QWidget();
        QVBoxLayout *etaisyysTabLayout = new QVBoxLayout( etaisyysTab );
        etaisyysTabLayout->setContentsMargins( 4, 4, 4, 4 );

        QFormLayout *distForm = new QFormLayout();
        distForm->setContentsMargins( 0, 0, 0, 0 );
        {
            QWidget     *nearContainer = new QWidget( this );
            QHBoxLayout *nearRow       = new QHBoxLayout( nearContainer );
            nearRow->setContentsMargins( 0, 0, 0, 0 );
            nearRow->addWidget( m_nearDistSpinBox );
            nearRow->addWidget( m_measureNearButton );
            distForm->addRow( "Lähin etäisyys:", nearContainer );
        }
        {
            QWidget     *farContainer = new QWidget( this );
            QHBoxLayout *farRow       = new QHBoxLayout( farContainer );
            farRow->setContentsMargins( 0, 0, 0, 0 );
            farRow->addWidget( m_farDistSpinBox );
            farRow->addWidget( m_measureFarButton );
            distForm->addRow( "Pisin etäisyys:", farContainer );
        }
        etaisyysTabLayout->addLayout( distForm );
        etaisyysTabLayout->addStretch();
        toolTabs->addTab( etaisyysTab, "Etäisyyksien määrittely" );

        layout->addWidget( toolTabs );

        // --- Napirivi muut työkalut ---
        QHBoxLayout *buttonRow = new QHBoxLayout();

        // Vasen sarake: Näytä valinta + Korosta/Poista valinta
        QVBoxLayout *polygonCol = new QVBoxLayout();

        {
            QHBoxLayout *showLockRow = new QHBoxLayout();
            showLockRow->setContentsMargins( 0, 0, 0, 0 );

            m_showOnlyButton = new QPushButton( "Näytä valinta", this );
            m_showOnlyButton->setCheckable( true );
            m_showOnlyButton->setEnabled( false );   // disabloitu kunnes on valinta
            showLockRow->addWidget( m_showOnlyButton );

            m_separateWindowCheckBox = new QCheckBox( "näytä erillisessä ikkunassa", this );
            m_separateWindowCheckBox->setChecked( false );
            showLockRow->addWidget( m_separateWindowCheckBox );

            showLockRow->addStretch();
            polygonCol->addLayout( showLockRow );
        }

        {
            QHBoxLayout *selRow = new QHBoxLayout();
            selRow->setContentsMargins( 0, 0, 0, 0 );

            m_highlightButton = new QPushButton( "Korosta valinta", this );
            m_highlightButton->setCheckable( true );
            m_highlightButton->setChecked( true );
            selRow->addWidget( m_highlightButton );

            m_clearSelectionButton = new QPushButton( "Poista valinta", this );
            selRow->addWidget( m_clearSelectionButton );

            polygonCol->addLayout( selRow );
        }

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
            polyCountRow->addWidget( new QLabel( "Valintamuotojen minimimäärä:", this ) );
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

        // "Näytä valinta" -toggle
        connect( m_showOnlyButton, &QPushButton::toggled, this, [this]( bool checked )
        {
            const bool separateWindow = m_separateWindowCheckBox && m_separateWindowCheckBox->isChecked();

            if ( checked )
            {
                // Otetaan snapshot nykyisestä valinnasta
                m_preLockedHitCount   = m_indexHitCount;
                m_preLockedPrismCount = m_meshObjects.size();
                m_lockedIndices = std::unordered_set<unsigned>(
                    m_selectionIndices.begin(), m_selectionIndices.end() );

                // Jos View 2 on jo auki, tyhjennetään sen OwnDB suoraan
                if ( m_selectionWindowIsOwned && m_selectionGLWindow )
                {
                    clearSelectionHighlights();
                    clearSelectionMeshes();
                    clearSelectionSizeSubClouds();
                    removeSelectionOnlyCloud();
                }

                // Avaa View 2 -ikkuna uudelle valinnalle
                enableShowOnlyMode( /*resetCamera=*/true );

                // Korostetaan lukitun joukon pisteet ja synkataan View 2:een
                removeHighlightObjects();
                buildHighlightFromIndices( std::vector<unsigned>(
                    m_lockedIndices.begin(), m_lockedIndices.end() ) );
                syncHighlightsToSelectionWindow();

                if ( separateWindow )
                {
                    // Erillinen ikkuna: nappi ei jää pohjaan, View 2 jäädytetään
                    // View 1 jatkuu vapaasti ilman lukitusta
                    m_view2Frozen  = true;
                    m_showOnlyMode = false;
                    m_lockViewMode = false;
                    m_lockedIndices.clear();
                    m_preLockedHitCount.clear();
                    m_preLockedPrismCount = 0;
                    m_showOnlyButton->blockSignals( true );
                    m_showOnlyButton->setChecked( false );
                    m_showOnlyButton->blockSignals( false );
                }
                else
                {
                    // View 1: nappi jää pohjaan, näkymä lukitaan
                    m_showOnlyMode = true;
                    m_lockViewMode = true;
                }

                m_cloud->prepareDisplayForRefresh_recursive();
                m_appInterface->refreshAll();
            }
            else
            {
                // Nappi nousee — vain View 1 -tila (separateWindow=false)
                // Puretaan lukitus ja poistetaan lukituksen aikana lisätyt prismat
                for ( size_t i = m_preLockedPrismCount; i < m_meshObjects.size(); ++i )
                    m_appInterface->removeFromDB( m_meshObjects[i], true );
                if ( m_meshObjects.size() > m_preLockedPrismCount )
                {
                    m_meshObjects.erase(
                        m_meshObjects.begin() + static_cast<ptrdiff_t>( m_preLockedPrismCount ),
                        m_meshObjects.end() );
                    m_prismData.erase(
                        m_prismData.begin() + static_cast<ptrdiff_t>( m_preLockedPrismCount ),
                        m_prismData.end() );
                }
                m_indexHitCount = m_preLockedHitCount;
                m_lockedIndices.clear();
                m_preLockedHitCount.clear();
                m_preLockedPrismCount = 0;
                m_lockViewMode = false;

                disableShowOnlyMode();
                refreshHighlights();
            }
        } );

        // Korosta valinta: päällä/pois highlight-pilven näyttäminen
        connect( m_highlightButton, &QPushButton::toggled, this, [this]( bool )
        {
            refreshHighlights();
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
            // Käytetään piirtoikkunaa (ei getActiveGLWindow() joka saattaa palauttaa
            // väärän ikkunan tai nullptr kun piirtotila on jo pois päältä)
            ccGLWindowInterface *win = m_polygonDrawer->drawnInWindow();
            if ( !win )
                win = m_appInterface->getActiveGLWindow();
            if ( win && !m_polygonDrawer->getClosedVertices().empty() )
            {
                const ccBBox clipBBox = ( m_cloud != nullptr )
                                        ? m_cloud->getOwnBB()
                                        : ccBBox();
                ccMesh *mesh = VolumeBuilder::build(
                    m_polygonDrawer->getClosedVertices(),
                    win,
                    clipBBox,
                    m_nearDistSpinBox->value(),
                    m_farDistSpinBox->value(),
                    m_meshOpacity
                );
                if ( mesh )
                {
                    // Varmistetaan että prisma renderöityy View 1:ssä — ei View 2:ssa
                    // (addToDB asettaisi displayksi aktiivisen ikkunan joka saattaa olla View 2)
                    if ( m_cloud && m_cloud->getDisplay() )
                        mesh->setDisplay( m_cloud->getDisplay() );

                    m_appInterface->addToDB( mesh );
                    m_meshObjects.push_back( mesh );

                    // Jos View 2 on auki, "Näytä valinta" on aktiivinen ja tämä prisma on
                    // rajan jälkeen, lisätään kopio View 2:een
                    if ( m_selectionGLWindow && m_selectionWindowIsOwned
                         && m_showOnlyMode
                         && m_meshObjects.size() > m_selectionWindowPrismOffset )
                    {
                        ccMesh *copy = mesh->cloneMesh();
                        if ( copy )
                        {
                            copy->setDisplay( m_selectionGLWindow );
                            m_selectionGLWindow->addToOwnDB( copy, true );
                            m_selectionMeshObjects.push_back( copy );
                            m_selectionGLWindow->redraw();
                        }
                    }

                    // Tallenna prisman data uudelleenlaskentaa varten
                    // (insideIndices täytetään alla lasketuista indekseistä)
                    m_prismData.push_back( { m_polygonDrawer->getClosedVertices(),
                                             m_nearDistSpinBox->value(),
                                             m_farDistSpinBox->value(),
                                             {} } );

                    // Kerää prisman sisällä olevien pisteiden indeksit
                    if ( m_cloud != nullptr )
                    {
                        std::vector<unsigned> indices;
                        VolumeBuilder::highlightPointsInsideVolume(
                            m_polygonDrawer->getClosedVertices(),
                            win,
                            m_cloud,
                            clipBBox,
                            m_nearDistSpinBox->value(),
                            m_farDistSpinBox->value(),
                            &indices
                        );
                        if ( !indices.empty() )
                        {
                            // Tallenna piirtohetken indeksit rakenteeseen
                            m_prismData.back().insideIndices = indices;
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

                    // Jos View 2 on jäädytetty, synkronoidaan highlight-kopiot View 2:een
                    // riippumatta siitä piirrettiinkö polygon View 1:ssä vai View 2:ssa
                    if ( m_view2Frozen )
                        syncHighlightsToSelectionWindow();
                }
                else
                {
                    m_appInterface->dispToConsole(
                        "MaastoPlugin: 3D-kappaleen luonti epäonnistui",
                        ccMainAppInterface::WRN_CONSOLE_MESSAGE );
                }
            }

            // Sammuta polygon-nappi — käyttäjä painaa uudelleen aloittaakseen uuden
            m_polygonDrawer->stopDrawing();
            m_polygonButton->blockSignals( true );
            m_polygonButton->setChecked( false );
            m_polygonButton->blockSignals( false );
        } );

        // Kun dialogi suljetaan → poista pick-kuuntelija automaattisesti
        connect( this, &QDialog::finished, this, [this]()
        {
            if ( m_pickingHub )
                m_pickingHub->removeListener( this );
            removePickMarker();
        } );
    }

    void MaastoDialog::updateCloud( ccPointCloud *cloud )
    {
        if ( m_updatingCloud )
            return;
        m_updatingCloud = true;

        // Palauta vanha pilvi alkutilaan ennen vaihtoa
        // Jos "Näytä vain valinta" on päällä, pura tila ensin (palauttaa m_cloud näkyviin)
        // Poikkeus: jos sama pilvi tulee sisään (esim. luokittelun jälkeen updateUI()-kutsu),
        // ei pureta showOnly/lukitustilaa — pilvi ei vaihdu, tila säilyy.
        if ( m_showOnlyMode )
        {
            if ( cloud == m_cloud )
            {
                m_updatingCloud = false;
                return;
            }
            m_lockViewMode = false;
            m_lockedIndices.clear();
            m_preLockedHitCount.clear();
            m_preLockedPrismCount = 0;
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

        // Jos pilvi vaihtuu, nollaa luokkamääritykset, polkunäyttö ja pistekoko-sub-pilvet
        if ( m_cloud != cloud )
        {
            clearAllSizeSubClouds();
            m_classPointSizes.clear();
            m_classDefinitions.clear();
            m_classCounts.clear();
            m_ptcFilePath = "";
        }

        QString keepValues  = m_valuesComboBox->currentText();
        QString keepColor   = m_colorComboBox->currentText();

        m_cloud = cloud;

        // Automaattinen .ptc-lataus jos ei ladattu
        if ( m_classDefinitions.isEmpty() )
            tryAutoLoadPtcFile();

        populateComboBox( m_valuesComboBox, keepValues );
        populateTargetClassComboBox( m_lastTargetCode );
        populateColorComboBox( keepColor );

        // Päivitä Tallenna-napin ja automaattitallennus-checkboxin tila
        if ( m_saveButton )
        {
            const QString path = resolveCloudFilePath( m_cloud );
            const QString lc   = path.toLower();
            const bool canSave = !path.isEmpty()
                                 && ( lc.endsWith( ".las" ) || lc.endsWith( ".laz" ) );
            m_saveButton->setEnabled( canSave );
            if ( m_autoSaveCheckBox )
                m_autoSaveCheckBox->setEnabled( canSave );
        }

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
            populateTargetClassComboBox( m_lastTargetCode );
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

        // Päivitä pysyvä valintamuisti listasta ennen tyhjennystä
        // (käyttäjä on saattanut muuttaa valintoja suoraan listasta ilman signaaleja)
        for ( int i = 0; i < m_listWidget->topLevelItemCount(); ++i )
        {
            const QTreeWidgetItem *it = m_listWidget->topLevelItem( i );
            const int code = it->text( 0 ).toInt();
            if ( it->checkState( 0 ) == Qt::Checked )
                m_checkedClassCodes.insert( code );
            else
                m_checkedClassCodes.remove( code );
        }

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

        QStringList values = getScalarFieldValues( m_cloud, fieldName );

        // Jos Classification-kenttä on valittu ja luokkamäärittely on ladattu,
        // lisätään mapista löytyvät luokat jotka eivät esiinny pistepilvessä (count=0)
        const bool isClassifField = ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 );
        if ( isClassifField && !m_classDefinitions.isEmpty() )
        {
            QSet<int> existingVals;
            for ( const QString &v : values )
                existingVals.insert( v.toInt() );

            for ( int key : m_classDefinitions.keys() )
            {
                if ( !existingVals.contains( key ) )
                    values.append( QString::number( key ) );
            }

            // Järjestä numerisesti
            std::sort( values.begin(), values.end(),
                []( const QString &a, const QString &b )
                { return a.toInt() < b.toInt(); } );
        }

        // Näytetään Name + Color sarakkeet jos Scalar field on Classification ja .ptc on ladattu
        const bool isClassif = ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
                               && !m_classDefinitions.isEmpty();

        // Count-sarake näytetään aina kun pilvi on valittu
        const bool hasCloud = ( m_cloud != nullptr ) && !values.isEmpty();

        // Sarakeindeksit:
        //   isClassif: Value=0, Show=1, Name=2, Color=3, Koko=4, Count=5
        //   muu:       Value=0, Show=1, Count=2
        if ( hasCloud )
        {
            if ( isClassif )
            {
                // Value | Show | Name | Color | Koko | Count
                m_listWidget->setColumnCount( 6 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Show", "Name", "Color", "Koko", "Count" } );
                m_listWidget->header()->setStretchLastSection( false );
                m_listWidget->header()->setSectionResizeMode( QHeaderView::Interactive );
                m_listWidget->header()->resizeSection( 0, 50 );
                m_listWidget->header()->resizeSection( 1, 40 );
                m_listWidget->header()->resizeSection( 2, 120 );
                m_listWidget->header()->resizeSection( 3, 40 );
                m_listWidget->header()->resizeSection( 4, 65 );
                m_listWidget->header()->resizeSection( 5, 55 );
            }
            else
            {
                // Value | Show | Count
                m_listWidget->setColumnCount( 3 );
                m_listWidget->setHeaderHidden( false );
                m_listWidget->setHeaderLabels( { "Value", "Show", "Count" } );
                m_listWidget->header()->setStretchLastSection( false );
                m_listWidget->header()->setSectionResizeMode( QHeaderView::Interactive );
                m_listWidget->header()->resizeSection( 0, 120 );
                m_listWidget->header()->resizeSection( 1, 40 );
                m_listWidget->header()->resizeSection( 2, 55 );
            }
        }
        else
        {
            m_listWidget->setColumnCount( 1 );
            m_listWidget->setHeaderHidden( true );
        }

        // Lisää tuntemattomat luokkakoodit m_classDefinitions:iin tmp-nimellä
        // jotta ClassEditorDialog näkee ne ja käyttäjä voi muokata niitä.
        if ( isClassif )
        {
            for ( const QString &val : values )
            {
                const int code = val.toInt();
                if ( !m_classDefinitions.contains( code ) )
                {
                    ClassDefinition tmpDef;
                    tmpDef.value = code;
                    tmpDef.name  = QString( "tmp_%1" ).arg( code );
                    tmpDef.color = QColor( 128, 128, 128 );
                    m_classDefinitions.insert( code, tmpDef );
                }
            }
        }

        for ( const QString &val : values )
        {
            QTreeWidgetItem *item = new QTreeWidgetItem( m_listWidget );

            // Value-sarake (col 0): checkbox luokittelua varten
            // Jos m_checkedClassCodes on tyhjä (ensimmäinen täyttö), kaikki oletuksena Checked
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            const bool checked = m_checkedClassCodes.isEmpty()
                                 ? true
                                 : m_checkedClassCodes.contains( val.toInt() );
            item->setCheckState( 0, checked ? Qt::Checked : Qt::Unchecked );
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

            // Koko-sarake (col 4): SpinBox kun isClassif
            if ( isClassif && hasCloud )
            {
                QSpinBox *sb = new QSpinBox( m_listWidget );
                sb->setMinimum( 0 );
                sb->setMaximum( 16 );
                sb->setSpecialValueText( "Def" );
                sb->setValue( m_classPointSizes.value( intVal, 0 ) );
                sb->setFrame( false );
                // Tallennetaan classCode SpinBoxiin jotta signaali löytää sen
                sb->setProperty( "classCode", intVal );
                m_listWidget->setItemWidget( item, 4, sb );

                connect( sb, QOverload<int>::of( &QSpinBox::valueChanged ),
                         this, [this, intVal]( int newSize )
                         {
                             m_classPointSizes[intVal] = newSize;
                             applyClassPointSize( intVal, newSize );
                         } );
            }

            // Count-sarake: oikea indeksi riippuu isClassif:stä
            if ( hasCloud )
            {
                const int countCol = isClassif ? 5 : 2;
                const int count = m_classCounts.value( intVal, 0 );
                item->setText( countCol, QString::number( count ) );
                item->setTextAlignment( countCol, Qt::AlignRight | Qt::AlignVCenter );
            }
        }

        m_listWidget->blockSignals( false );

        // Jos listassa on valittuja arvoja (palautettu edellisestä tilasta),
        // päivitä korostus — itemChanged ei laukea signaalien ollessa blokattuna
        refreshHighlights();

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

    void MaastoDialog::populateTargetClassComboBox( int keepCode )
    {
        m_targetClassComboBox->blockSignals( true );
        m_targetClassComboBox->clear();

        const QString fieldName = m_valuesComboBox->currentText();
        QStringList values = getScalarFieldValues( m_cloud, fieldName );

        // Jos Classification-kenttä on valittu ja luokkamäärittely on ladattu,
        // lisätään myös mapista löytyvät luokat joilla ei ole pisteitä (count=0)
        const bool isClassif = ( fieldName.compare( "Classification", Qt::CaseInsensitive ) == 0 )
                               && !m_classDefinitions.isEmpty();
        if ( isClassif )
        {
            QSet<int> existingVals;
            for ( const QString &v : values )
                existingVals.insert( v.toInt() );

            for ( int key : m_classDefinitions.keys() )
            {
                if ( !existingVals.contains( key ) )
                    values.append( QString::number( key ) );
            }

            // Järjestä numerisesti
            std::sort( values.begin(), values.end(),
                []( const QString &a, const QString &b )
                { return a.toInt() < b.toInt(); } );
        }

        for ( const QString &v : values )
        {
            if ( isClassif )
            {
                const int code = v.toInt();
                const QString name = m_classDefinitions.contains( code )
                                     ? m_classDefinitions[code].name
                                     : QString( "tmp_%1" ).arg( code );
                const QString label = QString( "%1 - %2" ).arg( code ).arg( name );
                m_targetClassComboBox->addItem( label, code );
            }
            else
            {
                m_targetClassComboBox->addItem( v );
            }
        }
        m_targetClassComboBox->blockSignals( false );

        if ( values.isEmpty() )
            return;

        // Säilytä edellinen valinta luokkakoodin perusteella
        int keepIdx = -1;
        if ( keepCode >= 0 )
        {
            keepIdx = isClassif
                      ? m_targetClassComboBox->findData( keepCode )
                      : m_targetClassComboBox->findText( QString::number( keepCode ) );
        }
        // blockSignals on jo false — setCurrentIndex triggeröi currentIndexChanged
        // joka päivittää m_lastTargetCode oikeaan arvoon
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
            if ( !m_cloud->hasColors() )
                return;  // ei alkuperäistä RGB-dataa — ei tehdä mitään
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

        // --- Tallenna alkuperäinen vertex-RGB ennen ylikirjoitusta (vain kerran) ---
        if ( !m_ptcColorsApplied )
        {
            m_hadColorsBeforePtc = m_cloud->hasColors();
            m_savedColors.clear();
            if ( m_hadColorsBeforePtc )
            {
                const unsigned ptCount = m_cloud->size();
                m_savedColors.reserve( static_cast<int>( ptCount ) );
                for ( unsigned p = 0; p < ptCount; ++p )
                {
                    const ccColor::Rgba &c = m_cloud->getPointColor( p );
                    m_savedColors.append( ccColor::Rgb( c.r, c.g, c.b ) );
                }
            }
        }

        // --- Varmista että RGB-taulukko on olemassa ---
        if ( !m_cloud->hasColors() )
        {
            if ( !m_cloud->resizeTheRGBTable( false ) )
                return;
        }

        // --- Kirjoita .ptc-värit suoraan vertex-taulukkoon ---
        // Tuntemattomat koodit saavat harmaan värin.
        const unsigned ptCount = m_cloud->size();
        for ( unsigned p = 0; p < ptCount; ++p )
        {
            const int code = static_cast<int>( sf->getValue( p ) );
            QColor qcol;
            if ( m_classDefinitions.contains( code ) && m_classDefinitions[code].color.isValid() )
                qcol = m_classDefinitions[code].color;
            else
                qcol = QColor( 128, 128, 128 );

            m_cloud->setPointColor( p, ccColor::Rgb(
                static_cast<unsigned char>( qcol.red() ),
                static_cast<unsigned char>( qcol.green() ),
                static_cast<unsigned char>( qcol.blue() ) ) );
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

        if ( m_hadColorsBeforePtc && !m_savedColors.isEmpty() )
        {
            // Palautetaan alkuperäiset vertex-RGB-värit
            const unsigned ptCount = m_cloud->size();
            for ( unsigned p = 0; p < ptCount && p < static_cast<unsigned>( m_savedColors.size() ); ++p )
                m_cloud->setPointColor( p, m_savedColors[p] );
            m_cloud->showColors( true );
        }
        else
        {
            // Pistepilvessä ei ollut RGB-taulukkoa alun perin
            m_cloud->unallocateColors();
            m_cloud->showColors( false );
        }

        m_savedColors.clear();
        m_hadColorsBeforePtc = false;
        m_cloud->prepareDisplayForRefresh();
        m_ptcColorsApplied = false;
    }

    // ----------------------------------------------------------------
    // Luokkakohtaiset pistekoko-sub-pilvet
    // ----------------------------------------------------------------

    void MaastoDialog::removeClassSizeSubCloud( int classCode )
    {
        if ( !m_sizeSubClouds.contains( classCode ) )
            return;

        ccPointCloud *sub = m_sizeSubClouds[classCode];
        if ( m_cloud && m_cloud->getChildrenNumber() > 0 )
        {
            // Etsi ja poista lapsista
            for ( unsigned i = 0; i < m_cloud->getChildrenNumber(); ++i )
            {
                if ( m_cloud->getChild( i ) == sub )
                {
                    m_cloud->removeChild( i );
                    break;
                }
            }
        }
        // DP_PARENT_OF_OTHER huolehtii poistosta, mutta varmistetaan
        m_sizeSubClouds.remove( classCode );
    }

    void MaastoDialog::clearAllSizeSubClouds()
    {
        if ( !m_cloud )
        {
            // Pilvi poistettu — tyhjennä pelkästään map
            for ( ccPointCloud *sub : m_sizeSubClouds )
                delete sub;
            m_sizeSubClouds.clear();
            return;
        }

        // Poista lapset käänteisessä järjestyksessä (turvallisempaa)
        for ( int code : m_sizeSubClouds.keys() )
            removeClassSizeSubCloud( code );

        m_sizeSubClouds.clear();
    }

    void MaastoDialog::applyClassPointSize( int classCode, int size )
    {
        if ( !m_cloud )
            return;

        if ( size == 0 )
        {
            // Palautetaan default — poistetaan sub-pilvi jos olemassa
            removeClassSizeSubCloud( classCode );
            m_appInterface->refreshAll();
            return;
        }

        // Poistetaan vanha sub-pilvi ensin
        removeClassSizeSubCloud( classCode );

        // Haetaan Classification-kenttä
        const int sfIdx = m_cloud->getScalarFieldIndexByName( "Classification" );
        if ( sfIdx < 0 )
            return;

        CCCoreLib::ScalarField *sf = m_cloud->getScalarField( sfIdx );
        if ( !sf )
            return;

        // Kerää pisteiden indeksit joiden luokka == classCode
        std::vector<unsigned> indices;
        indices.reserve( 1024 );
        const float targetVal = static_cast<float>( classCode );
        for ( unsigned i = 0; i < m_cloud->size(); ++i )
        {
            if ( sf->getValue( i ) == targetVal )
                indices.push_back( i );
        }

        if ( indices.empty() )
            return;

        // Luo sub-pilvi
        CCCoreLib::ReferenceCloud refCloud( m_cloud );
        refCloud.reserve( static_cast<unsigned>( indices.size() ) );
        for ( unsigned idx : indices )
            refCloud.addPointIndex( idx );

        ccPointCloud *sub = m_cloud->partialClone( &refCloud );
        if ( !sub )
            return;

        sub->setName( QString( "SizeSubCloud_%1" ).arg( classCode ) );
        sub->setPointSize( static_cast<unsigned>( size ) );

        // Väritä kiinteällä luokan värillä jos määritelty
        if ( m_classDefinitions.contains( classCode )
             && m_classDefinitions[classCode].color.isValid() )
        {
            const QColor col = m_classDefinitions[classCode].color;
            const ccColor::Rgb rgb( static_cast<unsigned char>( col.red() ),
                                    static_cast<unsigned char>( col.green() ),
                                    static_cast<unsigned char>( col.blue() ) );
            sub->setColor( rgb );
            sub->showColors( true );
            sub->showSF( false );
        }
        else
        {
            // Ei väriä — näytä SF:n mukaan kuten pääpilvi
            sub->showSF( true );
        }

        sub->setVisible( true );

        // Lisää pääpilven lapseksi (DP_PARENT_OF_OTHER = CC omistaa muistin)
        m_cloud->addChild( sub, ccHObject::DP_PARENT_OF_OTHER );
        m_sizeSubClouds[classCode] = sub;

        m_cloud->prepareDisplayForRefresh_recursive();
        m_appInterface->refreshAll();

        // Päivitä View 2 jos auki
        refreshSelectionWindow();
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

        const QVariant targetData = m_targetClassComboBox->currentData();
        const ScalarType targetValue = static_cast<ScalarType>(
            targetData.isValid() ? targetData.toFloat() : targetStr.toFloat() );

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

        if ( count == 0 )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: luokittelumuodon sisällä ei ole yhtään pistettä valittuna. "
                "Muuta Polygonien vähimmäismäärää tai lähtöluokkia.",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        sf->computeMinAndMax();
        m_cloud->prepareDisplayForRefresh();

        // Prismat jäävät DB:hen — poista vain highlight-pilvet
        removeHighlightObjects();

        // Laske pisteindeksit uudelleen kaikista prismoista
        // Käytetään piirtohetkellä tallennettuja indeksejä — ei kamera-riippuvuutta
        m_indexHitCount.clear();
        for ( const PrismData &pd : m_prismData )
        {
            for ( unsigned idx : pd.insideIndices )
                m_indexHitCount[idx]++;
        }

        // Lukitustilassa päivitetään myös snapshot — jotta lukituksen vapautus
        // palauttaa luokittelun jälkeisen tilan eikä vanhaa
        if ( m_lockViewMode )
            m_preLockedHitCount = m_indexHitCount;

        // Päivitä vertex-RGB ennen highlight-päivitystä — tärkeää lukitustilassa
        // koska enableShowOnlyMode() kopioi värit pääpilveltä selectionOnlyCloud:iin
        if ( m_ptcColorsApplied )
            applyPtcColors();

        // Päivitä korostukset uudelleenlaskettujen indeksien perusteella
        refreshHighlights();

        m_appInterface->dispToConsole(
            QString( "MaastoPlugin: luokiteltu %1 pistettä → %2" )
                .arg( count ).arg( targetStr ),
            ccMainAppInterface::STD_CONSOLE_MESSAGE );

        // Päivitä Count-sarake luokittelun jälkeen
        populateValueList( sfName );

        m_appInterface->updateUI();
        m_appInterface->refreshAll();

        // Automaattitallennus luokittelun jälkeen
        if ( m_autoSaveCheckBox && m_autoSaveCheckBox->isChecked() )
            saveLasFile();
    }

    // ----------------------------------------------------------------
    // clearSelection
    // ----------------------------------------------------------------

    void MaastoDialog::clearSelection()
    {
        // 1. Pysäytä polygon- ja viiva-piirto ENSIN — nollaa m_previousGLWindow
        // ennen kuin View 2:n ikkuna mahdollisesti tuhotaan (estää dangling pointer -kaatumisen)
        m_polygonDrawer->stopDrawing();
        m_polygonDrawer->clearCompletedPolygon();
        m_polygonButton->blockSignals( true );
        m_polygonButton->setChecked( false );
        m_polygonButton->blockSignals( false );
        stopLinePicking();

        // 2. Jos View 2 on jäädytetty (erillinen ikkuna -tila), sulje se
        if ( m_view2Frozen )
        {
            disableShowOnlyMode();  // poistaa View 2:n pilven ja ikkunan, nollaa m_view2Frozen
        }

        // 3. Jos "Näytä valinta" on päällä (View 1 -tila), pura tila
        if ( m_showOnlyMode )
        {
            m_lockViewMode = false;
            m_lockedIndices.clear();
            m_preLockedHitCount.clear();
            m_preLockedPrismCount = 0;
            m_showOnlyMode = false;
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
            // Pääpilvi oli piilotettu View 1:ssä — palautetaan näkyviin
            disableShowOnlyMode();
        }

        // 4. Poista kaikki prism-meshit DB:stä (myös View 2 -kopiot)
        clearSelectionMeshes();
        clearSelectionSizeSubClouds();
        removeSelectionOnlyCloud();   // poistaa vanhan valintapilven View 2:sta
        m_selectionWindowPrismOffset = 0;
        for ( ccHObject *obj : m_meshObjects )
            m_appInterface->removeFromDB( obj, true );
        m_meshObjects.clear();
        m_prismData.clear();

        // 5. Poista highlight-pistepilvet DB:stä
        removeHighlightObjects();

        // 6. Tyhjennä indeksit ja osuma-laskuri
        m_selectionIndices.clear();
        m_indexHitCount.clear();

        // 7. Disabloi "Näytä vain valinta" -nappi
        if ( m_showOnlyButton )
            m_showOnlyButton->setEnabled( false );

        // 9. Nollaa viiva-työkalu-napit
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
            // Poistetaan MDI-ikkunan omasta DB:stä ennen deletea
            if ( m_selectionGLWindow )
                m_selectionGLWindow->removeFromOwnDB( m_selectionOnlyCloud );
            delete m_selectionOnlyCloud;
            m_selectionOnlyCloud = nullptr;
        }
    }

    // ----------------------------------------------------------------
    // enableShowOnlyMode
    // ----------------------------------------------------------------

    void MaastoDialog::enableShowOnlyMode( bool resetCamera )
    {
        if ( !m_cloud )
            return;

        // Lähdeindeksit ovat aina m_lockedIndices (snapshot otettiin ennen kutsua)
        if ( m_lockedIndices.empty() )
            return;

        // Lähdeindeksit: m_lockedIndices, suodatettuna piilotettujen pisteiden varalta
        // (m_cloud:n visibility array asettaa POINT_HIDDEN pisteille jotka on piilotettu Show-filtterillä)
        std::vector<unsigned> lockedVec;
        {
            const bool hasVisArray = ( m_cloud->getTheVisibilityArray().size() == m_cloud->size() );
            lockedVec.reserve( m_lockedIndices.size() );
            for ( unsigned idx : m_lockedIndices )
            {
                if ( hasVisArray &&
                     m_cloud->getTheVisibilityArray()[idx] == CCCoreLib::POINT_HIDDEN )
                    continue;
                lockedVec.push_back( idx );
            }
        }
        const std::vector<unsigned> *srcIndices = &lockedVec;

        // 1. Poista vanha väliaikainen pilvi jos on
        removeSelectionOnlyCloud();

        // 2. Rakenna uusi valintapilvi srcIndices:stä
        const unsigned count = static_cast<unsigned>( srcIndices->size() );
        m_selectionOnlyCloud = new ccPointCloud( "SelectionOnly" );
        if ( count == 0 || !m_selectionOnlyCloud->reserve( count ) )
        {
            delete m_selectionOnlyCloud;
            m_selectionOnlyCloud = nullptr;
            if ( count == 0 )
                return; // kaikki piilotettu — näytetään tyhjä View 2
            return;
        }

        for ( unsigned idx : *srcIndices )
            m_selectionOnlyCloud->addPoint( *m_cloud->getPoint( idx ) );

        // 3. Kopioi visualisointi pääpilveltä
        if ( m_cloud->sfShown() )
        {
            const int srcSfIdx = m_cloud->getCurrentDisplayedScalarFieldIndex();
            ccScalarField *srcSf = static_cast<ccScalarField*>(
                m_cloud->getScalarField( srcSfIdx ) );

            if ( srcSf )
            {
                ccScalarField *dstSf = new ccScalarField( srcSf->getName() );
                dstSf->reserve( count );
                for ( unsigned idx : *srcIndices )
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
            if ( m_cloud->hasColors() && m_selectionOnlyCloud->reserveTheRGBTable() )
            {
                for ( unsigned idx : *srcIndices )
                    m_selectionOnlyCloud->addColor( m_cloud->getPointColor( idx ) );
                m_selectionOnlyCloud->showColors( true );
                m_selectionOnlyCloud->showSF( false );
            }
        }

        const bool separateWindow = m_separateWindowCheckBox && m_separateWindowCheckBox->isChecked();

        if ( separateWindow )
        {
            // --- Erillinen MDI-ikkuna (View 2) ---
            m_selectionWindowIsOwned = true;

            const bool windowAlreadyExisted = ( m_selectionGLWindow != nullptr );

            if ( !m_selectionGLWindow )
            {
                m_appInterface->createGLWindow( m_selectionGLWindow, m_selectionGLWidget );
                if ( !m_selectionGLWindow || !m_selectionGLWidget )
                {
                    delete m_selectionOnlyCloud;
                    m_selectionOnlyCloud = nullptr;
                    return;
                }

                // Lisätään MDI-alueelle
                QMainWindow *mainWin = m_appInterface->getMainWindow();
                QMdiArea *mdiArea = qobject_cast<QMdiArea*>( mainWin->centralWidget() );
                if ( mdiArea )
                {
                    QMdiSubWindow *sub = mdiArea->addSubWindow( m_selectionGLWidget );
                    sub->setAttribute( Qt::WA_DeleteOnClose, false );
                    sub->setWindowTitle( "Valinta" );
                }

                // Sulkemissignaali: käyttäjä sulkee MDI-ikkunan manuaalisesti
                connect( m_selectionGLWindow->signalEmitter(),
                         &ccGLWindowSignalEmitter::aboutToClose,
                         this, [this]( ccGLWindowInterface* )
                {
                    // Ensin puhdistetaan kopiot (ikkuna vielä olemassa tässä vaiheessa)
                    m_selectionHighlightObjects.clear(); // ikkuna tuhoutuu itse, ei tarvita removeFromOwnDB
                    m_selectionMeshObjects.clear();      // sama
                    m_selectionSizeSubClouds.clear();    // sama
                    m_selectionWindowPrismOffset = 0;
                    m_selectionGLWindow      = nullptr;
                    m_selectionGLWidget      = nullptr;
                    m_selectionOnlyCloud     = nullptr;
                    m_selectionWindowIsOwned = false;
                    // Palauta pääpilvi näkyviin
                    if ( m_cloud )
                    {
                        m_cloud->setVisible( true );
                        m_cloud->prepareDisplayForRefresh();
                    }
                    // Nollaa nappi
                    if ( m_showOnlyButton )
                    {
                        m_showOnlyButton->blockSignals( true );
                        m_showOnlyButton->setChecked( false );
                        m_showOnlyButton->blockSignals( false );
                    }
                    m_showOnlyMode = false;
                    m_lockViewMode = false;
                    m_view2Frozen  = false;
                    m_lockedIndices.clear();
                    m_preLockedHitCount.clear();
                    m_preLockedPrismCount = 0;
                    m_appInterface->refreshAll();
                } );

                // Poimi piste -signaali: välitetään onItemPicked()-callbackiin
                connect( m_selectionGLWindow->signalEmitter(),
                         &ccGLWindowSignalEmitter::itemPicked,
                         this, [this]( ccHObject* entity, unsigned subEntityID,
                                       int x, int y,
                                       const CCVector3& P, const CCVector3d& uvw )
                {
                    if ( !m_pickPointButton || !m_pickPointButton->isChecked() )
                        return;
                    ccPickingListener::PickedItem pi;
                    pi.entity     = entity;
                    pi.itemIndex  = subEntityID;
                    pi.clickPoint = QPoint( x, y );
                    pi.P3D        = P;
                    pi.uvw        = uvw;
                    onItemPicked( pi );
                } );

                // Aktivoidaan point-picking View 2 -ikkunaan
                m_selectionGLWindow->setPickingMode( ccGLWindowInterface::POINT_PICKING );

                // Tallennetaan prismaraja: kaikki tähän mennessä olevat prismat eivät näy View 2:ssa
                m_selectionWindowPrismOffset = m_meshObjects.size();
            }
            else
            {
                // Ikkuna jo olemassa — removeSelectionOnlyCloud() on jo poistanut vanhan pilven.
                // Päivitetään prismaraja vain jos clearSelection() ei ole jo nollannut sitä:
                // clearSelection() tyhjentää m_meshObjects -> size()==0 -> offset pysyy 0 -> kaikki
                // uuden valinnan prismat kopioidaan. Jos päivitetään sama valinta (ilman
                // clearSelection():ia), m_meshObjects ei ole tyhjä ja offset asetetaan oikein.
                if ( !m_meshObjects.empty() )
                    m_selectionWindowPrismOffset = m_meshObjects.size();
            }

            // Pääpilvi jää näkyviin View 1:ssä — valinta näytetään lisäksi erillisessä ikkunassa
            m_selectionOnlyCloud->setDisplay( m_selectionGLWindow );
            m_selectionGLWindow->addToOwnDB( m_selectionOnlyCloud, true );
            m_selectionGLWidget->show();
            // zoomGlobal ensimmäisellä kerralla tai kun uusi valinta ladataan (resetCamera=true)
            // Päivityksissä (show-filtterin muutos jne.) kameranäkymä säilytetään
            if ( !windowAlreadyExisted || resetCamera )
                m_selectionGLWindow->zoomGlobal();
            // Synkronoi prisma-kopiot View 2:een
            syncMeshesToSelectionWindow();
            m_selectionGLWindow->redraw();
        }
        else
        {
            // --- Sama ikkuna (View 1) ---
            m_selectionWindowIsOwned = false;
            m_selectionGLWidget      = nullptr;

            // Haetaan aktiivinen (View 1) ikkuna
            ccGLWindowInterface *win = m_appInterface->getActiveGLWindow();
            if ( !win )
            {
                delete m_selectionOnlyCloud;
                m_selectionOnlyCloud = nullptr;
                return;
            }

            m_selectionGLWindow = win;

            // Piilotetaan pääpilvi — valintapilvi korvaa sen
            m_cloud->setVisible( false );
            m_cloud->prepareDisplayForRefresh();

            m_selectionOnlyCloud->setDisplay( m_selectionGLWindow );
            m_selectionGLWindow->addToOwnDB( m_selectionOnlyCloud, true );
            m_selectionGLWindow->zoomGlobal();
            m_selectionGLWindow->redraw();
        }

        m_appInterface->refreshAll();
    }

    // ----------------------------------------------------------------
    // disableShowOnlyMode
    // ----------------------------------------------------------------

    void MaastoDialog::disableShowOnlyMode()
    {
        m_view2Frozen = false;

        if ( m_selectionGLWindow )
        {
            if ( m_selectionOnlyCloud )
                m_selectionGLWindow->removeFromOwnDB( m_selectionOnlyCloud );

            if ( m_selectionWindowIsOwned )
            {
                // MDI-ikkuna — tuhotaan.
                // Poistetaan kopiot removeFromOwnDB+delete kautta ennen ikkunan tuhoamista
                clearSelectionHighlights();
                clearSelectionMeshes();
                clearSelectionSizeSubClouds();
                m_selectionWindowPrismOffset = 0;
                // Deletoidaan valintapilvi ENNEN destroyGLWindow():ta
                if ( m_selectionOnlyCloud )
                {
                    m_selectionOnlyCloud->setDisplay( nullptr );
                    delete m_selectionOnlyCloud;
                    m_selectionOnlyCloud = nullptr;
                }
                // Tyhjennetään OwnDB kokonaan varmuuden vuoksi ennen ikkunan tuhoamista
                if ( m_selectionGLWindow->getOwnDB() )
                    m_selectionGLWindow->getOwnDB()->removeAllChildren();
                // Suljetaan QMdiSubWindow ennen GL-ikkunan tuhoamista
                if ( QMdiSubWindow *sub = qobject_cast<QMdiSubWindow*>( m_selectionGLWidget->parent() ) )
                    sub->close();
                m_selectionGLWidget->hide();
                m_appInterface->destroyGLWindow( m_selectionGLWindow );
                m_selectionGLWidget = nullptr;
            }
            // View 1 -tapauksessa ei tuhota ikkunaa — se on CC:n oma ikkuna

            m_selectionGLWindow      = nullptr;
            m_selectionWindowIsOwned = false;
        }

        // Poista väliaikainen pilvi jos vielä olemassa (View 1 -tapaus tai yllä ei deletoitu)
        removeSelectionOnlyCloud();

        // Palauta pääpilvi näkyviin
        if ( m_cloud )
        {
            m_cloud->setVisible( true );
            m_cloud->prepareDisplayForRefresh();
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
        // Lukituksen aikana hyväksytään vain lukitushetken snapshot-pisteet
        std::vector<unsigned> matchIndices;
        for ( const auto& kv : m_indexHitCount )
        {
            const unsigned idx   = kv.first;
            const int      hits  = kv.second;
            if ( hits < minHits || idx >= m_cloud->size() )
                continue;
            if ( m_lockViewMode && m_lockedIndices.count( idx ) == 0 )
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

        // Highlight-pisteiden koko: käytetään suurinta valittujen luokkien
        // luokkakohtaisesta koosta (m_classPointSizes), tai globaalia m_highlightPointSize
        int effectiveSize = m_highlightPointSize;
        for ( float v : selectedValues )
        {
            const int code = static_cast<int>( v );
            if ( m_classPointSizes.contains( code ) && m_classPointSizes[code] > 0 )
                effectiveSize = qMax( effectiveSize, m_classPointSizes[code] + 1 );
        }
        highlighted->setPointSize( static_cast<unsigned>( effectiveSize ) );

        return highlighted;
    }

    // ----------------------------------------------------------------
    // removeHighlightObjects
    // ----------------------------------------------------------------

    void MaastoDialog::removeHighlightObjects()
    {
        clearSelectionHighlights();
        for ( ccHObject *obj : m_highlightObjects )
            m_appInterface->removeFromDB( obj, true );
        m_highlightObjects.clear();
    }

    // ----------------------------------------------------------------
    // clearSelectionHighlights
    // Poistaa View 2 -ikkunan highlight-kopiot (m_selectionHighlightObjects).
    // Kutsutaan aina ennen uusia kopioita ja removeHighlightObjects():sta.
    // ----------------------------------------------------------------

    void MaastoDialog::clearSelectionHighlights()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned )
        {
            m_selectionHighlightObjects.clear();
            return;
        }
        for ( ccHObject *obj : m_selectionHighlightObjects )
        {
            m_selectionGLWindow->removeFromOwnDB( obj );
            delete obj;
        }
        m_selectionHighlightObjects.clear();
    }

    // ----------------------------------------------------------------
    // syncHighlightsToSelectionWindow
    // Luo kopiot kaikista m_highlightObjects-pilveistä View 2 -ikkunaan.
    // Kopioilla on sama geometria ja väri, mutta display = m_selectionGLWindow.
    // ----------------------------------------------------------------

    void MaastoDialog::syncHighlightsToSelectionWindow()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned )
            return;

        clearSelectionHighlights();

        for ( ccHObject *src : m_highlightObjects )
        {
            ccPointCloud *srcCloud = dynamic_cast<ccPointCloud*>( src );
            if ( !srcCloud )
                continue;

            ccPointCloud *copy = new ccPointCloud( srcCloud->getName() + "_v2" );
            if ( !copy->reserve( srcCloud->size() ) )
            {
                delete copy;
                continue;
            }

            // Kopioi pisteet
            for ( unsigned i = 0; i < srcCloud->size(); ++i )
                copy->addPoint( *srcCloud->getPoint( i ) );

            // Kopioi värit
            if ( srcCloud->hasColors() && copy->reserveTheRGBTable() )
            {
                for ( unsigned i = 0; i < srcCloud->size(); ++i )
                    copy->addColor( srcCloud->getPointColor( i ) );
                copy->showColors( true );
            }

            copy->setPointSize( srcCloud->getPointSize() );
            copy->setDisplay( m_selectionGLWindow );
            m_selectionGLWindow->addToOwnDB( copy, true );
            m_selectionHighlightObjects.push_back( copy );
        }

        m_selectionGLWindow->redraw();
    }

    // ----------------------------------------------------------------
    // clearSelectionMeshes
    // Poistaa View 2 -ikkunan prisma-kopiot (m_selectionMeshObjects).
    // ----------------------------------------------------------------

    void MaastoDialog::clearSelectionMeshes()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned )
        {
            m_selectionMeshObjects.clear();
            return;
        }
        for ( ccHObject *obj : m_selectionMeshObjects )
        {
            m_selectionGLWindow->removeFromOwnDB( obj );
            delete obj;
        }
        m_selectionMeshObjects.clear();
    }

    // ----------------------------------------------------------------
    // syncMeshesToSelectionWindow
    // Luo kopiot prismoista indeksistä m_selectionWindowPrismOffset alkaen
    // View 2 -ikkunaan (m_selectionMeshObjects).
    // ----------------------------------------------------------------

    void MaastoDialog::syncMeshesToSelectionWindow()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned )
            return;

        clearSelectionMeshes();

        for ( size_t i = m_selectionWindowPrismOffset; i < m_meshObjects.size(); ++i )
        {
            ccMesh *src = dynamic_cast<ccMesh*>( m_meshObjects[i] );
            if ( !src )
                continue;
            ccMesh *copy = src->cloneMesh();
            if ( !copy )
                continue;
            copy->setDisplay( m_selectionGLWindow );
            m_selectionGLWindow->addToOwnDB( copy, true );
            m_selectionMeshObjects.push_back( copy );
        }

        m_selectionGLWindow->redraw();
    }

    // ----------------------------------------------------------------
    // clearSelectionSizeSubClouds
    // Poistaa View 2 -ikkunan luokkakohtaiset pistekoko-sub-pilvet.
    // ----------------------------------------------------------------

    void MaastoDialog::clearSelectionSizeSubClouds()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned )
        {
            m_selectionSizeSubClouds.clear();
            return;
        }
        for ( ccHObject *obj : m_selectionSizeSubClouds )
        {
            m_selectionGLWindow->removeFromOwnDB( obj );
            delete obj;
        }
        m_selectionSizeSubClouds.clear();
    }

    // ----------------------------------------------------------------
    // syncSizeSubCloudsToSelectionWindow
    // Luo kopiot m_sizeSubClouds-pilveistä View 2 -ikkunaan, suodatettuna
    // m_lockedIndices:n pisteisiin.
    // ----------------------------------------------------------------

    void MaastoDialog::syncSizeSubCloudsToSelectionWindow()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned || !m_cloud )
            return;

        clearSelectionSizeSubClouds();

        if ( m_sizeSubClouds.isEmpty() )
            return;

        // Rakennetaan käänteinen hakutaulukko: pääpilven globaaliindeksi → selectionOnly-indeksi
        // m_lockedIndices sisältää pääpilven indeksit järjestyksessä jolla ne lisättiin
        // m_selectionOnlyCloud:iin (sama järjestys kuin enableShowOnlyMode():ssa)
        std::unordered_map<unsigned, unsigned> globalToLocal;
        {
            unsigned localIdx = 0;
            for ( unsigned globalIdx : m_lockedIndices )
                globalToLocal[globalIdx] = localIdx++;
        }

        const int sfIdx = m_cloud->getScalarFieldIndexByName( "Classification" );

        for ( auto it = m_sizeSubClouds.constBegin(); it != m_sizeSubClouds.constEnd(); ++it )
        {
            const int classCode = it.key();
            ccPointCloud *srcSub = it.value();
            if ( !srcSub )
                continue;

            // Kerää m_lockedIndices:stä ne pisteet joiden luokka == classCode
            std::vector<unsigned> localIndices;
            if ( sfIdx >= 0 )
            {
                ccScalarField *sf = static_cast<ccScalarField*>( m_cloud->getScalarField( sfIdx ) );
                const float targetVal = static_cast<float>( classCode );
                for ( unsigned globalIdx : m_lockedIndices )
                {
                    if ( sf->getValue( globalIdx ) == targetVal )
                    {
                        auto found = globalToLocal.find( globalIdx );
                        if ( found != globalToLocal.end() )
                            localIndices.push_back( found->second );
                    }
                }
            }

            if ( localIndices.empty() || !m_selectionOnlyCloud )
                continue;

            ccPointCloud *copy = new ccPointCloud(
                srcSub->getName() + "_v2" );
            if ( !copy->reserve( static_cast<unsigned>( localIndices.size() ) ) )
            {
                delete copy;
                continue;
            }

            // Kopioi pisteet m_selectionOnlyCloud:sta (ne ovat jo lokaalissa koordinaatistossa)
            for ( unsigned localIdx : localIndices )
                copy->addPoint( *m_selectionOnlyCloud->getPoint( localIdx ) );

            // Kopioi väri srcSub:lta (yhtenäinen luokkaväri tai SF)
            if ( srcSub->hasColors() && copy->reserveTheRGBTable() )
            {
                // Käytä srcSub:n ensimmäisen pisteen väriä (kaikki samaa luokkaa)
                if ( srcSub->size() > 0 )
                {
                    const ccColor::Rgba col = srcSub->getPointColor( 0 );
                    for ( unsigned i = 0; i < static_cast<unsigned>( localIndices.size() ); ++i )
                        copy->addColor( col );
                }
                copy->showColors( true );
                copy->showSF( false );
            }

            copy->setPointSize( srcSub->getPointSize() );
            copy->setVisible( srcSub->isVisible() );
            copy->setDisplay( m_selectionGLWindow );
            m_selectionGLWindow->addToOwnDB( copy, true );
            m_selectionSizeSubClouds.push_back( copy );
        }

        m_selectionGLWindow->redraw();
    }

    // ----------------------------------------------------------------
    // refreshSelectionWindow
    // Päivittää View 2 -ikkunan sisällön kun show/koko-tila muuttuu:
    // rakennetaan m_selectionOnlyCloud uudelleen ja synkronoidaan kaikki kopiot.
    // ----------------------------------------------------------------

    void MaastoDialog::refreshSelectionWindow()
    {
        if ( !m_selectionGLWindow || !m_selectionWindowIsOwned || !m_showOnlyMode )
            return;

        // EI rakenneta m_selectionOnlyCloud:ia uudelleen — View 2:n pistepilvi kiinnitetään
        // "Näytä valinta" -napin painamisen hetkeen. Value-valinnat, pistekoot ja
        // show-filtteri eivät muuta View 2:n pistepilveä.
        // Päivitetään vain highlight- ja sub-pilvi-kopiot.
        syncHighlightsToSelectionWindow();
        syncSizeSubCloudsToSelectionWindow();
    }

    // ----------------------------------------------------------------
    // buildHighlightFromIndices
    // Rakentaa highlight-pilven suoraan annetuista indekseistä korostusvärillä.
    // Käytetään lukituksen aktivoinnissa jolloin kaikki lukitun joukon pisteet
    // korostetaan ilman minHits/SF-suodatusta.
    // ----------------------------------------------------------------

    void MaastoDialog::buildHighlightFromIndices( const std::vector<unsigned>& indices )
    {
        if ( !m_cloud || indices.empty() )
            return;

        static int s_lockedCounter = 0;
        ++s_lockedCounter;

        ccPointCloud *highlighted = new ccPointCloud(
            QString( "Highlighted_Locked_%1" ).arg( s_lockedCounter ) );

        if ( !highlighted->reserve( static_cast<unsigned>( indices.size() ) ) )
        {
            delete highlighted;
            return;
        }

        for ( unsigned idx : indices )
            highlighted->addPoint( *m_cloud->getPoint( idx ) );

        // Korostusväri
        if ( highlighted->reserveTheRGBTable() )
        {
            const ccColor::Rgba col(
                static_cast<ColorCompType>( m_highlightColor.red() ),
                static_cast<ColorCompType>( m_highlightColor.green() ),
                static_cast<ColorCompType>( m_highlightColor.blue() ),
                ccColor::MAX );
            for ( std::size_t i = 0; i < indices.size(); ++i )
                highlighted->addColor( col );
            highlighted->showColors( true );
        }

        highlighted->setPointSize( static_cast<unsigned>( m_highlightPointSize ) );

        // Varmistetaan että highlight renderöityy View 1:ssä — ei View 2:ssa
        if ( m_cloud && m_cloud->getDisplay() )
            highlighted->setDisplay( m_cloud->getDisplay() );

        m_cloud->addChild( highlighted );
        m_appInterface->addToDB( highlighted, false, false, false, false );
        m_highlightObjects.push_back( highlighted );

        // Päivitä m_selectionIndices jotta enableShowOnlyMode tietää mitkä pisteet ovat valittuja
        m_selectionIndices = indices;
    }

    // ----------------------------------------------------------------
    // refreshHighlights
    // ----------------------------------------------------------------

    void MaastoDialog::refreshHighlights()
    {
        // Puretaan showOnly-tila ensin jotta voidaan rakentaa uudet pilvet
        // Lukitustilassa EI kutsuta disableShowOnlyMode() koska se palauttaisi
        // pääpilven näkyviin — poistetaan vain highlight-pilvet suoraan.
        // m_selectionOnlyCloud:ia EI poisteta — View 2:n pohjainen pistepilvi
        // (luokkaväritys) pysyy näkyvissä koko showOnlyMode-session ajan.
        if ( m_showOnlyMode )
        {
            if ( m_lockViewMode )
            {
                removeHighlightObjects();
            }
            else
            {
                disableShowOnlyMode();
                removeHighlightObjects();
            }
        }
        else
        {
            removeHighlightObjects();
        }

        if ( !m_cloud || m_indexHitCount.empty() )
        {
            if ( m_lockViewMode )
            {
                // Lukitustilassa: ei prismavalintaa, mutta säilytetään lukittu näkymä
                // selectionOnlyCloud rakennetaan m_lockedIndices:stä (highlight on tyhjä)
                enableShowOnlyMode();
                syncHighlightsToSelectionWindow();
                m_cloud->prepareDisplayForRefresh_recursive();
                m_appInterface->refreshAll();
                return;
            }
            if ( m_view2Frozen )
            {
                // View 2 jäädytetty — ei nollata tilaa, View 2 pysyy ennallaan
                m_cloud->prepareDisplayForRefresh_recursive();
                m_appInterface->refreshAll();
                return;
            }
            // Ei valintaa — disabloi napit ja nollaa tila
            m_selectionIndices.clear();
            m_lockViewMode = false;
            m_lockedIndices.clear();
            m_preLockedHitCount.clear();
            m_preLockedPrismCount = 0;
            if ( m_showOnlyButton )
            {
                m_showOnlyButton->blockSignals( true );
                m_showOnlyButton->setChecked( false );
                m_showOnlyButton->blockSignals( false );
                m_showOnlyButton->setEnabled( false );
            }
            m_showOnlyMode = false;
            m_cloud->prepareDisplayForRefresh_recursive();
            m_appInterface->refreshAll();
            return;
        }

        // Jos "Korosta valinta" -nappi on poissa päältä, ei luoda highlight-pilveä
        if ( m_highlightButton && !m_highlightButton->isChecked() )
        {
            if ( m_showOnlyButton )
                m_showOnlyButton->setEnabled( !m_indexHitCount.empty() );
            // View 2: poistetaan vain keltaiset highlight-kopiot — pistepilvi pysyy ennallaan
            syncHighlightsToSelectionWindow();  // tyhjentää kopiot (m_highlightObjects on tyhjä)
            m_cloud->prepareDisplayForRefresh_recursive();
            m_appInterface->refreshAll();
            return;
        }

        const QSet<float> selectedValues = getCheckedValues();
        ccPointCloud *highlighted = createFilteredHighlight( selectedValues );

        if ( highlighted )
        {
            // Varmistetaan että highlight renderöityy View 1:ssä — ei View 2:ssa
            // (addToDB asettaisi displayksi aktiivisen ikkunan joka saattaa olla View 2)
            if ( m_cloud && m_cloud->getDisplay() )
                highlighted->setDisplay( m_cloud->getDisplay() );

            m_cloud->addChild( highlighted );
            m_appInterface->addToDB(
                highlighted,
                false,  // updateZoom
                false,  // autoExpandDBTree
                false,  // checkDimensions
                false   // autoRedraw
            );
            m_highlightObjects.push_back( highlighted );

            // Aktivoi nappi kun valittuja pisteitä on — ei lukituksen aikana
            if ( m_showOnlyButton )
                m_showOnlyButton->setEnabled( !m_lockViewMode );

            // Päivitä View 2:n pohjainen pilvi uuden leikkauksen mukaan
            // (snapshot päivitetään — ei zoom-hyppyä)
            // Vain View 1 -lukitustilassa (m_lockViewMode). View 2 jäädytystilassa (m_view2Frozen)
            // pohjainen pilvi pysyy ennallaan.
            if ( m_lockViewMode && m_selectionWindowIsOwned && m_selectionGLWindow )
            {
                m_lockedIndices = std::unordered_set<unsigned>(
                    m_selectionIndices.begin(), m_selectionIndices.end() );
                clearSelectionHighlights();
                clearSelectionMeshes();
                clearSelectionSizeSubClouds();
                removeSelectionOnlyCloud();
                enableShowOnlyMode( false );
            }
        }
        else
        {
            if ( m_lockViewMode )
            {
                // Lukitustilassa: suodatus ei tuottanut pisteitä (esim. minHits liian suuri)
                // Säilytetään lukittu näkymä — selectionOnlyCloud rakennetaan m_lockedIndices:stä
                // highlight on tyhjä (ei korostusta)
                enableShowOnlyMode();
            }
            else if ( m_view2Frozen )
            {
                // View 2 jäädytetty: suodatus ei tuottanut pisteitä — ei nollata tilaa,
                // View 2 pysyy ennallaan (korostukset häviävät mutta pohjainen pilvi jää)
            }
            else
            {
                // createFilteredHighlight palautti nullptr — nollaa kaikki
                m_selectionIndices.clear();
                m_lockViewMode = false;
                m_lockedIndices.clear();
                m_preLockedHitCount.clear();
                m_preLockedPrismCount = 0;
                if ( m_showOnlyButton )
                {
                    m_showOnlyButton->blockSignals( true );
                    m_showOnlyButton->setChecked( false );
                    m_showOnlyButton->blockSignals( false );
                    m_showOnlyButton->setEnabled( false );
                }
                m_showOnlyMode = false;
            }
        }

        // View 2:n pistepilvi pysyy ennallaan — vain highlight-kopiot synkronoidaan
        // Jäädytystilassa (m_view2Frozen) View 2:ta ei päivitetä lainkaan
        if ( !m_view2Frozen )
            syncHighlightsToSelectionWindow();

        // Ei kutsuta updateUI():ta tässä — se triggeröisi onNewSelection() → updateCloud()
        // → populateColorComboBox() → applyColorField() → updateUI() silmukan
        // joka tyhjentäisi m_indexHitCount:n tai estäisi luokittelun
        m_cloud->prepareDisplayForRefresh_recursive();
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

        // Synkronoi sub-pilvien näkyvyys Show-tilan mukaan
        for ( auto it = m_sizeSubClouds.begin(); it != m_sizeSubClouds.end(); ++it )
        {
            const bool visible = !hiddenValues.contains( static_cast<float>( it.key() ) );
            it.value()->setVisible( visible );
            it.value()->prepareDisplayForRefresh();
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

        // Päivitä View 2 jos auki
        refreshSelectionWindow();
    }

    // ----------------------------------------------------------------
    // resetVisibility
    // ----------------------------------------------------------------

    void MaastoDialog::resetVisibility()
    {
        if ( m_cloud == nullptr )
            return;

        // Palauta kaikki sub-pilvet näkyviksi
        bool subCloudChanged = false;
        for ( auto it = m_sizeSubClouds.begin(); it != m_sizeSubClouds.end(); ++it )
        {
            it.value()->setVisible( true );
            it.value()->prepareDisplayForRefresh();
            subCloudChanged = true;
        }

        if ( m_cloud->isVisibilityTableInstantiated() )
        {
            m_cloud->unallocateVisibilityArray();
            m_cloud->prepareDisplayForRefresh();
            m_appInterface->refreshAll();
            refreshSelectionWindow();
        }
        else if ( subCloudChanged )
        {
            m_updatingCloud = true;
            m_appInterface->updateUI();
            m_updatingCloud = false;
            m_appInterface->refreshAll();
            refreshSelectionWindow();
        }
    }

    // ----------------------------------------------------------------
    // ----------------------------------------------------------------
    // onSaveSettings
    // ----------------------------------------------------------------

    void MaastoDialog::onSaveSettings()
    {
        // Oletushakemisto: PTC-tiedoston hakemisto jos asetettu
        const QString defaultDir = !m_ptcFilePath.isEmpty()
            ? QFileInfo( m_ptcFilePath ).absolutePath()
            : ( !m_lastSaveDir.isEmpty() ? m_lastSaveDir : QDir::homePath() );

        const QString path = QFileDialog::getSaveFileName(
            this,
            "Tallenna asetukset",
            defaultDir + "/MaastoPluginAsetukset.json",
            "JSON (*.json)" );

        if ( path.isEmpty() )
            return;

        QJsonObject root;

        // Highlight
        QJsonObject highlight;
        highlight["pointSize"] = m_highlightPointSize;
        highlight["color"]     = m_highlightColor.name();
        root["highlight"] = highlight;

        // Mittauspiste
        QJsonObject measurePt;
        measurePt["pointSize"] = m_measurePointSize;
        measurePt["color"]     = m_measurePointColor.name();
        root["measurePoint"] = measurePt;

        // 3D-kappaleet
        root["meshOpacity"] = m_meshOpacity;

        // Polygon-asetukset
        QJsonObject polygon;
        polygon["nearDist"] = m_nearDistSpinBox ? m_nearDistSpinBox->value() : 10.0;
        polygon["farDist"]  = m_farDistSpinBox  ? m_farDistSpinBox->value()  : 1000.0;
        root["polygon"] = polygon;

        // Viiva-asetukset
        QJsonObject line;
        line["thickness"] = m_lineThicknessSpinBox ? m_lineThicknessSpinBox->value() : 1.0;
        line["axis"]      = m_lineAxisComboBox ? m_lineAxisComboBox->currentText() : "Z";
        root["line"] = line;

        // PTC-tiedosto
        root["ptcFilePath"] = m_ptcFilePath;

        QFile file( path );
        if ( !file.open( QIODevice::WriteOnly ) )
        {
            QMessageBox::warning( this, "Virhe",
                QString( "Tiedoston kirjoittaminen epäonnistui:\n%1" ).arg( path ) );
            return;
        }
        file.write( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
        file.close();

        m_lastSaveDir          = QFileInfo( path ).absolutePath();
        m_lastSettingsFilePath = path;
    }

    // ----------------------------------------------------------------
    // onSavePtc
    // ----------------------------------------------------------------

    void MaastoDialog::onSavePtc()
    {
        if ( m_classDefinitions.isEmpty() )
        {
            QMessageBox::warning( this, "Virhe",
                "Luokkamäärittelytiedostoa ei ole ladattu.\n"
                "Lataa ensin .ptc-tiedosto asetuksista." );
            return;
        }

        // Oletushakemisto: ladatun .ptc-tiedoston kansio
        const QString defaultDir = !m_ptcFilePath.isEmpty()
            ? QFileInfo( m_ptcFilePath ).absolutePath()
            : ( !m_lastSaveDir.isEmpty() ? m_lastSaveDir : QDir::homePath() );

        const QString path = QFileDialog::getSaveFileName(
            this,
            "Tallenna luokkamäärittely",
            defaultDir + "/MaastoPlugin.ptc",
            "PTC files (*.ptc)" );

        if ( path.isEmpty() )
            return;

        QFile file( path );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        {
            QMessageBox::warning( this, "Virhe",
                QString( "Tiedoston kirjoittaminen epäonnistui:\n%1" ).arg( path ) );
            return;
        }

        QTextStream out( &file );

        // Kirjoitetaan jokainen luokka kahdella rivillä + tyhjä rivi väliin
        // Rivi 1: <koodi>\t\t<nimi>
        // Rivi 2: *\t\t<koodi>\t<R,G,B>
        const QList<int> keys = m_classDefinitions.keys(); // nouseva järjestys
        for ( int i = 0; i < keys.size(); ++i )
        {
            const ClassDefinition &def = m_classDefinitions[ keys[i] ];
            const QString name  = def.name.isEmpty() ? "" : def.name;
            const QColor  color = def.color.isValid() ? def.color : QColor( 0, 0, 0 );

            // Rivi 1: koodi \t\t nimi
            out << def.value << "\t\t" << name << "\n";

            // Rivi 2: * \t\t koodi \t R,G,B
            out << "*\t\t" << def.value << "\t"
                << color.red() << "," << color.green() << "," << color.blue() << "\n";

            // Tyhjä rivi luokkien väliin (paitsi viimeisen jälkeen)
            if ( i < keys.size() - 1 )
                out << "\n";
        }

        file.close();

        m_appInterface->dispToConsole(
            QString( "MaastoPlugin: Luokkamäärittely tallennettu: %1" ).arg( path ),
            ccMainAppInterface::STD_CONSOLE_MESSAGE );
    }

    // ----------------------------------------------------------------
    // onEditClasses
    // ----------------------------------------------------------------

    void MaastoDialog::onEditClasses()
    {
        ClassEditorDialog dlg( m_classDefinitions, this );
        if ( dlg.exec() != QDialog::Accepted )
            return;

        m_classDefinitions = dlg.classDefinitions();

        // Nappi pysyy aktiivisena (luokkamäärittely on nyt muistissa)
        if ( m_editClassesButton )
            m_editClassesButton->setEnabled( true );

        // Päivitä pääikkunan lista
        populateValueList( m_valuesComboBox->currentText() );

        // Päivitä 3D-värit jos Classification-väritys on käytössä
        const QString colorField = m_colorComboBox->currentText();
        if ( colorField.compare( "Classification", Qt::CaseInsensitive ) == 0 )
            applyPtcColors();
    }

    // ----------------------------------------------------------------
    // onLoadSettings
    // ----------------------------------------------------------------

    void MaastoDialog::onLoadSettings( SettingsDialog *dlg )
    {
        const QString defaultDir = !m_ptcFilePath.isEmpty()
            ? QFileInfo( m_ptcFilePath ).absolutePath()
            : ( !m_lastSaveDir.isEmpty() ? m_lastSaveDir : QDir::homePath() );

        const QString path = QFileDialog::getOpenFileName(
            this,
            "Lataa asetukset",
            defaultDir,
            "JSON (*.json)" );

        if ( path.isEmpty() )
            return;

        loadSettingsFromFile( path, dlg );
    }

    // ----------------------------------------------------------------
    // loadSettingsFromFile
    // ----------------------------------------------------------------

    void MaastoDialog::loadSettingsFromFile( const QString &path, SettingsDialog *dlg )
    {
        QFile file( path );
        if ( !file.open( QIODevice::ReadOnly ) )
        {
            QMessageBox::warning( this, "Virhe",
                QString( "Tiedoston avaaminen epäonnistui:\n%1" ).arg( path ) );
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
        file.close();

        if ( doc.isNull() || !doc.isObject() )
        {
            QMessageBox::warning( this, "Virhe", "Tiedosto ei ole kelvollinen JSON." );
            return;
        }

        const QJsonObject root = doc.object();

        // Highlight
        if ( root.contains( "highlight" ) )
        {
            const QJsonObject h = root["highlight"].toObject();
            if ( h.contains( "pointSize" ) )
                m_highlightPointSize = h["pointSize"].toInt( m_highlightPointSize );
            if ( h.contains( "color" ) )
            {
                QColor c( h["color"].toString() );
                if ( c.isValid() ) m_highlightColor = c;
            }
        }

        // Mittauspiste
        if ( root.contains( "measurePoint" ) )
        {
            const QJsonObject m = root["measurePoint"].toObject();
            if ( m.contains( "pointSize" ) )
                m_measurePointSize = m["pointSize"].toInt( m_measurePointSize );
            if ( m.contains( "color" ) )
            {
                QColor c( m["color"].toString() );
                if ( c.isValid() ) m_measurePointColor = c;
            }
        }

        // 3D-kappaleet
        if ( root.contains( "meshOpacity" ) )
            m_meshOpacity = root["meshOpacity"].toInt( m_meshOpacity );

        // Polygon-asetukset
        if ( root.contains( "polygon" ) )
        {
            const QJsonObject p = root["polygon"].toObject();
            if ( m_nearDistSpinBox && p.contains( "nearDist" ) )
                m_nearDistSpinBox->setValue( p["nearDist"].toDouble() );
            if ( m_farDistSpinBox && p.contains( "farDist" ) )
                m_farDistSpinBox->setValue( p["farDist"].toDouble() );
            if ( m_minPolygonCountSpinBox && p.contains( "minCount" ) )
                m_minPolygonCountSpinBox->setValue( p["minCount"].toInt() );
        }

        // Viiva-asetukset
        if ( root.contains( "line" ) )
        {
            const QJsonObject l = root["line"].toObject();
            if ( m_lineThicknessSpinBox && l.contains( "thickness" ) )
                m_lineThicknessSpinBox->setValue( l["thickness"].toDouble() );
            if ( m_lineAxisComboBox && l.contains( "axis" ) )
            {
                const int idx = m_lineAxisComboBox->findText( l["axis"].toString() );
                if ( idx >= 0 ) m_lineAxisComboBox->setCurrentIndex( idx );
            }
        }

        // PTC-tiedosto — lataa jos muuttui
        if ( root.contains( "ptcFilePath" ) )
        {
            const QString newPtc = root["ptcFilePath"].toString();
            if ( !newPtc.isEmpty() && newPtc != m_ptcFilePath )
                loadPtcFile( newPtc );
        }

        m_lastSaveDir          = QFileInfo( path ).absolutePath();
        m_lastSettingsFilePath = path;

        // Päivitä SettingsDialog:n widgetit ladatuilla arvoilla
        // (muuten OK-painallus ylikirjoittaisi juuri ladatut arvot vanhoilla)
        if ( dlg )
        {
            dlg->applyLoadedSettings( m_highlightPointSize, m_highlightColor,
                                      m_measurePointSize,   m_measurePointColor,
                                      m_meshOpacity );
            dlg->setLoadedSettingsPath( path );
        }

        // Päivitä highlight nykyisillä asetuksilla
        refreshHighlights();
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

        // Aktivoi Muokkaa-nappi nyt kun luokkamäärittely on ladattu
        if ( m_editClassesButton )
            m_editClassesButton->setEnabled( true );

        // Päivitä Luokat-lista
        populateValueList( m_valuesComboBox->currentText() );

        // Päivitä Luokittele luokkaan -combo nimillä
        populateTargetClassComboBox( m_lastTargetCode );

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

        // Lataa automaattisesti MaastoPluginAsetukset.json jos se löytyy samasta kansiosta
        const QString autoSettingsPath = dir.absoluteFilePath( "MaastoPluginAsetukset.json" );
        if ( QFile::exists( autoSettingsPath ) )
            loadSettingsFromFile( autoSettingsPath );
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

        // Tallennetaan ikkuna — stopMeasure irrottaa signaalin samasta ikkunasta
        m_workingGLWindow = win;

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
        // Irrota signaali siitä ikkunasta johon se kytkettiin
        ccGLWindowInterface *win = m_workingGLWindow;
        m_workingGLWindow = nullptr;
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

        // Tallennetaan ikkuna — stopLinePicking irrottaa signaalin samasta ikkunasta
        m_workingGLWindow = win;

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

                    // Sammuta viiva-nappi — käyttäjä painaa uudelleen aloittaakseen uuden
                    stopLinePicking();
                    m_drawLineButton->blockSignals( true );
                    m_drawLineButton->setChecked( false );
                    m_drawLineButton->blockSignals( false );
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
        // Irrota signaali siitä ikkunasta johon se kytkettiin
        ccGLWindowInterface *win = m_workingGLWindow;
        m_workingGLWindow = nullptr;
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

        // Hae bounding box akseli-rajaukseen
        ccBBox bb = m_cloud->getOwnBB();
        double axisMin = 0.0, axisMax = 0.0;
        if ( axis == 'X' || axis == 'x' ) { axisMin = bb.minCorner().x; axisMax = bb.maxCorner().x; }
        else if ( axis == 'Y' || axis == 'y' ) { axisMin = bb.minCorner().y; axisMax = bb.maxCorner().y; }
        else { axisMin = bb.minCorner().z; axisMax = bb.maxCorner().z; }

        // Rakenna boksi-mesh (kokonaan oikealle puolelle viivasta, BB:n rajoihin)
        ccMesh *mesh = VolumeBuilder::buildFromLine( p1, p2, axis, thickness, axisMin, axisMax, m_meshOpacity );
        if ( mesh )
        {
            m_appInterface->addToDB( mesh );
            m_meshObjects.push_back( mesh );

            // Jos View 2 on auki, "Näytä valinta" on aktiivinen ja tämä prisma on
            // rajan jälkeen, lisätään kopio View 2:een
            if ( m_selectionGLWindow && m_selectionWindowIsOwned
                 && m_showOnlyMode
                 && m_meshObjects.size() > m_selectionWindowPrismOffset )
            {
                ccMesh *copy = mesh->cloneMesh();
                if ( copy )
                {
                    copy->setDisplay( m_selectionGLWindow );
                    m_selectionGLWindow->addToOwnDB( copy, true );
                    m_selectionMeshObjects.push_back( copy );
                    m_selectionGLWindow->redraw();
                }
            }
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

        // Hae bounding box akseli-rajaukseen
        ccBBox bb2 = m_cloud->getOwnBB();
        double axisMin2 = 0.0, axisMax2 = 0.0;
        if ( axis == 'X' || axis == 'x' ) { axisMin2 = bb2.minCorner().x; axisMax2 = bb2.maxCorner().x; }
        else if ( axis == 'Y' || axis == 'y' ) { axisMin2 = bb2.minCorner().y; axisMax2 = bb2.maxCorner().y; }
        else { axisMin2 = bb2.minCorner().z; axisMax2 = bb2.maxCorner().z; }

        // Rakenna uusi mesh siirretyillä pisteillä, BB:n rajoihin
        ccMesh *mesh = VolumeBuilder::buildFromLine( newP1, newP2, axis, thickness, axisMin2, axisMax2, m_meshOpacity );
        if ( mesh )
        {
            m_appInterface->addToDB( mesh );
            m_meshObjects.push_back( mesh );

            // Jos View 2 on auki, "Näytä valinta" on aktiivinen ja tämä prisma on
            // rajan jälkeen, lisätään kopio View 2:een
            if ( m_selectionGLWindow && m_selectionWindowIsOwned
                 && m_showOnlyMode
                 && m_meshObjects.size() > m_selectionWindowPrismOffset )
            {
                ccMesh *copy = mesh->cloneMesh();
                if ( copy )
                {
                    copy->setDisplay( m_selectionGLWindow );
                    m_selectionGLWindow->addToOwnDB( copy, true );
                    m_selectionMeshObjects.push_back( copy );
                    m_selectionGLWindow->redraw();
                }
            }
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
    // resolveCloudFilePath
    // ----------------------------------------------------------------

    QString MaastoDialog::resolveCloudFilePath( ccPointCloud *cloud ) const
    {
        if ( !cloud )
            return {};

        // Parent-objektin nimi on muotoa "tiedosto.las (/polku/kansioon)"
        ccHObject *parent = cloud->getParent();
        if ( !parent )
            return {};

        const QString fullName = parent->getName();

        // Hae hakemistopolku sulkujen sisältä
        const QRegularExpression re( R"(\((.+)\)$)" );
        const QRegularExpressionMatch match = re.match( fullName );
        if ( !match.hasMatch() )
            return {};

        const QString dirPath  = match.captured( 1 );
        // Tiedostonimi on kaikki ennen " ("-osuutta
        const int sepIdx = fullName.lastIndexOf( " (" );
        if ( sepIdx <= 0 )
            return {};
        const QString fileName = fullName.left( sepIdx );

        return QDir( dirPath ).absoluteFilePath( fileName );
    }

    // ----------------------------------------------------------------
    // saveLasFile
    // ----------------------------------------------------------------

    void MaastoDialog::saveLasFile()
    {
        if ( !m_cloud )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: ei aktiivista pistepilveä",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        const QString filePath = resolveCloudFilePath( m_cloud );
        if ( filePath.isEmpty() )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: tiedostopolkua ei voitu selvittää — avaa tiedosto ensin",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        const QString lc = filePath.toLower();
        if ( !lc.endsWith( ".las" ) && !lc.endsWith( ".laz" ) )
        {
            m_appInterface->dispToConsole(
                "MaastoPlugin: tallennus tukee vain .las/.laz-tiedostoja",
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        if ( !QFile::exists( filePath ) )
        {
            m_appInterface->dispToConsole(
                QString( "MaastoPlugin: tiedostoa ei löydy: %1" ).arg( filePath ),
                ccMainAppInterface::WRN_CONSOLE_MESSAGE );
            return;
        }

        // Hiljaiseen tallennukseen: ei dialogia, kaikki scalar fieldit mukaan automaattisesti
        // (alwaysDisplaySaveDialog = false aktivoi komentorivimoodin LasIOFilter:ssa)
        FileIOFilter::SaveParameters params;
        params.alwaysDisplaySaveDialog = false;
        params.parentWidget            = nullptr;

        const CC_FILE_ERROR err = FileIOFilter::SaveToFile(
            m_cloud,
            filePath,
            params,
            "LAS file (*.las *.laz)" );

        if ( err == CC_FERR_NO_ERROR )
        {
            m_appInterface->dispToConsole(
                QString( "MaastoPlugin: tallennettu → %1" ).arg( filePath ),
                ccMainAppInterface::STD_CONSOLE_MESSAGE );
        }
        else
        {
            m_appInterface->dispToConsole(
                QString( "MaastoPlugin: tallennus epäonnistui (virhekoodi %1): %2" )
                    .arg( static_cast<int>( err ) ).arg( filePath ),
                ccMainAppInterface::ERR_CONSOLE_MESSAGE );
        }
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
