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
                                    QWidget       *parent )
        : QDialog( parent )
        , m_pointSizeSpinBox( nullptr )
        , m_colorButton( nullptr )
        , m_color( color )
        , m_ptcFileLabel( nullptr )
        , m_measurePointSizeSpinBox( nullptr )
        , m_measurePointColorButton( nullptr )
        , m_measurePointColor( measurePointColor )
    {
        setWindowTitle( "Asetukset" );
        setMinimumWidth( 320 );

        QVBoxLayout *layout = new QVBoxLayout( this );

        // ── Luokkamäärittelytiedosto ──────────────────────────────
        layout->addWidget( new QLabel( "Luokkamäärittelytiedosto:", this ) );

        QPushButton *readFileButton = new QPushButton( "Read class definition file", this );
        layout->addWidget( readFileButton );

        m_ptcFileLabel = new QLabel( currentPtcPath, this );
        m_ptcFileLabel->setVisible( !currentPtcPath.isEmpty() );
        m_ptcFileLabel->setWordWrap( true );
        m_ptcFileLabel->setStyleSheet( "color: gray; font-size: 9pt;" );
        layout->addWidget( m_ptcFileLabel );

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
