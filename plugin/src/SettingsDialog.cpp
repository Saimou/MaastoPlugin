#include "SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QPixmap>
#include <QIcon>
#include <QFrame>

namespace MaastoPlugin
{
    SettingsDialog::SettingsDialog( int            pointSize,
                                    const QColor  &color,
                                    const QString &currentPtcPath,
                                    int            measurePointSize,
                                    const QColor  &measurePointColor,
                                    int            meshOpacity,
                                    double         nearDist,
                                    double         farDist,
                                    double         lineThickness,
                                    int            minPolyCount,
                                    const QString &lineAxis,
                                    const QString &defaultSaveDir,
                                    const QString &currentSettingsPath,
                                    QWidget       *parent )
        : QDialog( parent )
        , m_pointSizeSpinBox( nullptr )
        , m_colorButton( nullptr )
        , m_color( color )
        , m_ptcFileLabel( nullptr )
        , m_measurePointSizeSpinBox( nullptr )
        , m_measurePointColorButton( nullptr )
        , m_measurePointColor( measurePointColor )
        , m_meshOpacitySpinBox( nullptr )
        , m_settingsFileLabel( nullptr )
    {
        // Hiljenna käyttämättömät parametrit (tallennus/lataus hoidetaan MaastoAction:ssa)
        Q_UNUSED( nearDist )
        Q_UNUSED( farDist )
        Q_UNUSED( lineThickness )
        Q_UNUSED( minPolyCount )
        Q_UNUSED( lineAxis )
        Q_UNUSED( defaultSaveDir )

        setWindowTitle( "Asetukset" );
        setMinimumWidth( 320 );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // ── Luokkamäärittelytiedosto ──────────────────────────────
        layout->addWidget( new QLabel( "Luokkamäärittelytiedosto:", this ) );

        {
            QHBoxLayout *ptcRow = new QHBoxLayout();
            ptcRow->setContentsMargins( 0, 0, 0, 0 );

            QPushButton *savePtcButton = new QPushButton( "Tallenna luokkamäärittely", this );
            QPushButton *readFileButton = new QPushButton( "Read class definition file", this );

            ptcRow->addWidget( savePtcButton );
            ptcRow->addWidget( readFileButton );
            layout->addLayout( ptcRow );

            m_ptcFileLabel = new QLabel( currentPtcPath, this );
            m_ptcFileLabel->setVisible( !currentPtcPath.isEmpty() );
            m_ptcFileLabel->setWordWrap( true );
            m_ptcFileLabel->setStyleSheet( "color: gray; font-size: 9pt;" );
            layout->addWidget( m_ptcFileLabel );

            connect( savePtcButton, &QPushButton::clicked, this, [this]()
            {
                emit savePtcRequested();
            } );

            connect( readFileButton, &QPushButton::clicked, this, [this]()
            {
                const QString file = QFileDialog::getOpenFileName(
                    this, "Open class definition", "",
                    "PTC files (*.ptc);;All files (*.*)" );
                if ( !file.isEmpty() )
                {
                    m_ptcFileLabel->setText( file );
                    m_ptcFileLabel->setVisible( true );
                    emit ptcFileLoaded( file );
                }
            } );
        }

        // ── Erotin ───────────────────────────────────────────────
        {
            QFrame *sep = new QFrame( this );
            sep->setFrameShape( QFrame::HLine );
            sep->setFrameShadow( QFrame::Sunken );
            layout->addWidget( sep );
        }

        // ── Highlight-asetukset ───────────────────────────────────
        {
            QFormLayout *form = new QFormLayout();
            form->setContentsMargins( 0, 4, 0, 8 );

            m_pointSizeSpinBox = new QSpinBox( this );
            m_pointSizeSpinBox->setRange( 1, 20 );
            m_pointSizeSpinBox->setValue( pointSize );
            m_pointSizeSpinBox->setSuffix( " px" );
            form->addRow( "Highlight-pisteiden koko:", m_pointSizeSpinBox );

            m_colorButton = new QPushButton( this );
            m_colorButton->setFixedWidth( 60 );
            updateColorButton();
            connect( m_colorButton, &QPushButton::clicked, this, [this]()
            {
                QColor chosen = QColorDialog::getColor( m_color, this, "Valitse highlight-väri" );
                if ( chosen.isValid() )
                {
                    m_color = chosen;
                    updateColorButton();
                }
            } );
            form->addRow( "Highlight-pisteiden väri:", m_colorButton );

            layout->addLayout( form );
        }

        // ── Erotin ───────────────────────────────────────────────
        {
            QFrame *sep2 = new QFrame( this );
            sep2->setFrameShape( QFrame::HLine );
            sep2->setFrameShadow( QFrame::Sunken );
            layout->addWidget( sep2 );
        }

        // ── Mittauspiste-asetukset ────────────────────────────────
        {
            QFormLayout *form2 = new QFormLayout();
            form2->setContentsMargins( 0, 4, 0, 8 );

            m_measurePointSizeSpinBox = new QSpinBox( this );
            m_measurePointSizeSpinBox->setRange( 1, 30 );
            m_measurePointSizeSpinBox->setValue( measurePointSize );
            m_measurePointSizeSpinBox->setSuffix( " px" );
            form2->addRow( "Mittauspisteen koko:", m_measurePointSizeSpinBox );

            m_measurePointColorButton = new QPushButton( this );
            m_measurePointColorButton->setFixedWidth( 60 );
            updateMeasureColorButton();
            connect( m_measurePointColorButton, &QPushButton::clicked, this, [this]()
            {
                QColor chosen = QColorDialog::getColor(
                    m_measurePointColor, this, "Valitse mittauspisteen väri" );
                if ( chosen.isValid() )
                {
                    m_measurePointColor = chosen;
                    updateMeasureColorButton();
                }
            } );
            form2->addRow( "Mittauspisteen väri:", m_measurePointColorButton );

            layout->addLayout( form2 );
        }

        // ── Erotin ───────────────────────────────────────────────
        {
            QFrame *sep3 = new QFrame( this );
            sep3->setFrameShape( QFrame::HLine );
            sep3->setFrameShadow( QFrame::Sunken );
            layout->addWidget( sep3 );
        }

        // ── 3D-kappaleet ──────────────────────────────────────────
        {
            QFormLayout *form3 = new QFormLayout();
            form3->setContentsMargins( 0, 4, 0, 8 );

            m_meshOpacitySpinBox = new QSpinBox( this );
            m_meshOpacitySpinBox->setRange( 0, 100 );
            m_meshOpacitySpinBox->setValue( meshOpacity );
            m_meshOpacitySpinBox->setSuffix( " %" );
            form3->addRow( "3D-kappaleiden läpinäkyvyys:", m_meshOpacitySpinBox );

            layout->addLayout( form3 );
        }

        // ── Erotin ───────────────────────────────────────────────
        {
            QFrame *sep4 = new QFrame( this );
            sep4->setFrameShape( QFrame::HLine );
            sep4->setFrameShadow( QFrame::Sunken );
            layout->addWidget( sep4 );
        }

        // ── Asetustiedosto ────────────────────────────────────────
        {
            QHBoxLayout *fileRow = new QHBoxLayout();
            fileRow->setContentsMargins( 0, 4, 0, 8 );

            QPushButton *saveButton = new QPushButton( "Tallenna asetukset", this );
            QPushButton *loadButton = new QPushButton( "Lataa asetukset", this );

            connect( saveButton, &QPushButton::clicked, this, [this]()
            {
                emit saveRequested();
            } );
            connect( loadButton, &QPushButton::clicked, this, [this]()
            {
                emit loadRequested();
            } );

            fileRow->addWidget( saveButton );
            fileRow->addWidget( loadButton );
            layout->addLayout( fileRow );

            m_settingsFileLabel = new QLabel( currentSettingsPath, this );
            m_settingsFileLabel->setVisible( !currentSettingsPath.isEmpty() );
            m_settingsFileLabel->setWordWrap( true );
            m_settingsFileLabel->setStyleSheet( "color: gray; font-size: 9pt;" );
            layout->addWidget( m_settingsFileLabel );
        }

        // ── OK / Cancel ───────────────────────────────────────────
        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        layout->addWidget( buttons );
    }

    int SettingsDialog::pointSize() const
    {
        return m_pointSizeSpinBox->value();
    }

    QColor SettingsDialog::color() const
    {
        return m_color;
    }

    int SettingsDialog::measurePointSize() const
    {
        return m_measurePointSizeSpinBox->value();
    }

    QColor SettingsDialog::measurePointColor() const
    {
        return m_measurePointColor;
    }

    int SettingsDialog::meshOpacity() const
    {
        return m_meshOpacitySpinBox->value();
    }

    void SettingsDialog::applyLoadedSettings( int           pointSize,
                                              const QColor &color,
                                              int           measurePointSize,
                                              const QColor &measureColor,
                                              int           meshOpacity )
    {
        if ( m_pointSizeSpinBox )
            m_pointSizeSpinBox->setValue( pointSize );

        if ( color.isValid() )
        {
            m_color = color;
            updateColorButton();
        }

        if ( m_measurePointSizeSpinBox )
            m_measurePointSizeSpinBox->setValue( measurePointSize );

        if ( measureColor.isValid() )
        {
            m_measurePointColor = measureColor;
            updateMeasureColorButton();
        }

        if ( m_meshOpacitySpinBox )
            m_meshOpacitySpinBox->setValue( meshOpacity );
    }

    void SettingsDialog::setLoadedSettingsPath( const QString &path )
    {
        if ( m_settingsFileLabel )
        {
            m_settingsFileLabel->setText( path );
            m_settingsFileLabel->setVisible( !path.isEmpty() );
        }
    }

    void SettingsDialog::updateColorButton()
    {
        QPixmap px( 40, 16 );
        px.fill( m_color );
        m_colorButton->setIcon( QIcon( px ) );
        m_colorButton->setIconSize( px.size() );
        m_colorButton->setText( "" );
    }

    void SettingsDialog::updateMeasureColorButton()
    {
        QPixmap px( 40, 16 );
        px.fill( m_measurePointColor );
        m_measurePointColorButton->setIcon( QIcon( px ) );
        m_measurePointColorButton->setIconSize( px.size() );
        m_measurePointColorButton->setText( "" );
    }

} // namespace MaastoPlugin
