#include "ClassEditorDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QColorDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QIcon>
#include <QColor>
#include <QSet>
#include <algorithm>

namespace MaastoPlugin
{
    // Sarakeindeksit
    static constexpr int COL_VALUE = 0;
    static constexpr int COL_COLOR = 1;
    static constexpr int COL_NAME  = 2;
    static constexpr int NUM_COLS  = 3;

    // ----------------------------------------------------------------
    // Apufunktio: värillinen 24×24-pixmap
    // ----------------------------------------------------------------
    QPixmap ClassEditorDialog::colorSwatch( const QColor &c )
    {
        const QColor col = c.isValid() ? c : QColor( 128, 128, 128 );
        QPixmap px( 24, 24 );
        px.fill( col );
        return px;
    }

    // ----------------------------------------------------------------
    // Konstruktori
    // ----------------------------------------------------------------
    ClassEditorDialog::ClassEditorDialog( const QMap<int, ClassDefinition> &defs,
                                          QWidget *parent )
        : QDialog( parent )
        , m_original( defs )
    {
        setWindowTitle( "Muokkaa luokkia" );
        setMinimumSize( 480, 400 );
        resize( 540, 520 );

        QVBoxLayout *mainLayout = new QVBoxLayout( this );
        mainLayout->setSpacing( 6 );
        mainLayout->setContentsMargins( 8, 8, 8, 8 );

        // --- Ohjeteksti ---
        QLabel *hint = new QLabel(
            "Kaksoisnapsauta väripalkkia vaihtaaksesi värin.\n"
            "Value-sarake: kirjoita haluamasi koodi.\n"
            "Tyhjennä nimi-kenttä = ei nimeä.", this );
        hint->setWordWrap( true );
        QFont hintFont = hint->font();
        hintFont.setPointSize( hintFont.pointSize() - 1 );
        hint->setFont( hintFont );
        hint->setStyleSheet( "color: #888;" );
        mainLayout->addWidget( hint );

        // --- Taulukko ---
        m_table = new QTableWidget( 0, NUM_COLS, this );
        m_table->setHorizontalHeaderLabels( { "Value", "Väri", "Nimi" } );
        m_table->horizontalHeader()->setSectionResizeMode( COL_VALUE, QHeaderView::ResizeToContents );
        m_table->horizontalHeader()->setSectionResizeMode( COL_COLOR, QHeaderView::Fixed );
        m_table->horizontalHeader()->resizeSection( COL_COLOR, 50 );
        m_table->horizontalHeader()->setSectionResizeMode( COL_NAME,  QHeaderView::Stretch );
        m_table->verticalHeader()->setDefaultSectionSize( 28 );
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_table->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked );
        m_table->setAlternatingRowColors( true );
        mainLayout->addWidget( m_table, 1 );

        // --- Lisää uusi luokka -nappi ---
        m_addButton = new QPushButton( "Lisää uusi luokka", this );
        m_addButton->setFixedHeight( 26 );
        mainLayout->addWidget( m_addButton );

        // --- OK / Peruuta ---
        m_buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        mainLayout->addWidget( m_buttonBox );

        // Täytä taulukko olemassaolevilla luokilla
        populateTable();

        // --- Signaalit ---
        connect( m_addButton, &QPushButton::clicked,
            this, &ClassEditorDialog::onAddClass );

        connect( m_table, &QTableWidget::cellDoubleClicked,
            this, &ClassEditorDialog::onCellDoubleClicked );

        connect( m_table, &QTableWidget::cellChanged,
            this, [this]( int row, int col )
            {
                if ( col == COL_VALUE )
                    onValueChanged( row );
                validateAndUpdateOk();
            } );

        connect( m_buttonBox, &QDialogButtonBox::accepted,
            this, &QDialog::accept );
        connect( m_buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject );

        validateAndUpdateOk();
    }

    // ----------------------------------------------------------------
    // populateTable — täyttää taulukon m_original-sisällöstä
    // ----------------------------------------------------------------
    void ClassEditorDialog::populateTable()
    {
        m_table->blockSignals( true );
        m_table->setRowCount( 0 );

        for ( auto it = m_original.cbegin(); it != m_original.cend(); ++it )
            addTableRow( it.value() );

        m_table->blockSignals( false );
    }

    // ----------------------------------------------------------------
    // addTableRow — lisää yhden rivin taulukkoon
    // ----------------------------------------------------------------
    void ClassEditorDialog::addTableRow( const ClassDefinition &def )
    {
        const int row = m_table->rowCount();
        m_table->insertRow( row );

        // Col 0: Value (muokattava kokonaisluku)
        QTableWidgetItem *valueItem = new QTableWidgetItem(
            QString::number( def.value ) );
        valueItem->setTextAlignment( Qt::AlignCenter );
        m_table->setItem( row, COL_VALUE, valueItem );

        // Col 1: Väriswatch (ei suoraan muokattava — kaksoisnapsautus avaa QColorDialog)
        const QColor color = def.color.isValid() ? def.color : QColor( 128, 128, 128 );
        QTableWidgetItem *colorItem = new QTableWidgetItem();
        colorItem->setIcon( QIcon( colorSwatch( color ) ) );
        colorItem->setData( Qt::UserRole, color );         // tallennetaan QColor UserRole:en
        colorItem->setToolTip( color.name() + " — kaksoisnapsauta vaihtaaksesi" );
        colorItem->setFlags( colorItem->flags() & ~Qt::ItemIsEditable ); // ei suoraan muokattava
        m_table->setItem( row, COL_COLOR, colorItem );

        // Col 2: Nimi (muokattava teksti)
        QTableWidgetItem *nameItem = new QTableWidgetItem( def.name );
        m_table->setItem( row, COL_NAME, nameItem );
    }

    // ----------------------------------------------------------------
    // onAddClass — lisää uuden rivin automaattiväreillä
    // ----------------------------------------------------------------
    void ClassEditorDialog::onAddClass()
    {
        // Etsi pienin positiivinen koodi, joka ei ole käytössä taulukossa
        QSet<int> usedValues;
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            const QTableWidgetItem *it = m_table->item( r, COL_VALUE );
            if ( it )
                usedValues.insert( it->text().toInt() );
        }

        int newValue = 1;
        while ( usedValues.contains( newValue ) )
            ++newValue;

        const QColor newColor = generateUniqueColor();

        ClassDefinition def;
        def.value = newValue;
        def.name  = "";
        def.color = newColor;

        m_table->blockSignals( true );
        addTableRow( def );
        m_table->blockSignals( false );

        // Scrollaa uuteen riviin ja aloita nimen muokkaus
        const int newRow = m_table->rowCount() - 1;
        m_table->scrollToItem( m_table->item( newRow, COL_NAME ) );
        m_table->setCurrentCell( newRow, COL_NAME );
        m_table->editItem( m_table->item( newRow, COL_NAME ) );

        validateAndUpdateOk();
    }

    // ----------------------------------------------------------------
    // onCellDoubleClicked — kaksoisnapsautus värisarakkeella → QColorDialog
    // ----------------------------------------------------------------
    void ClassEditorDialog::onCellDoubleClicked( int row, int col )
    {
        if ( col != COL_COLOR )
            return;

        QTableWidgetItem *colorItem = m_table->item( row, COL_COLOR );
        if ( !colorItem )
            return;

        const QColor current = colorItem->data( Qt::UserRole ).value<QColor>();

        const QColor chosen = QColorDialog::getColor(
            current, this, "Valitse väri" );

        if ( !chosen.isValid() )
            return;   // käyttäjä peruutti

        colorItem->setIcon( QIcon( colorSwatch( chosen ) ) );
        colorItem->setData( Qt::UserRole, chosen );
        colorItem->setToolTip( chosen.name() + " — kaksoisnapsauta vaihtaaksesi" );
    }

    // ----------------------------------------------------------------
    // onValueChanged — korostaa konfliktirivit punaisella
    // ----------------------------------------------------------------
    void ClassEditorDialog::onValueChanged( int /*row*/ )
    {
        // Kerää kaikki value-arvot
        QMap<int, QList<int>> valueToRows;
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            const QTableWidgetItem *it = m_table->item( r, COL_VALUE );
            if ( it )
                valueToRows[ it->text().toInt() ].append( r );
        }

        // Korosta duplikaatit punaisella, muut oletusväri
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            QTableWidgetItem *it = m_table->item( r, COL_VALUE );
            if ( !it ) continue;

            const bool duplicate = ( valueToRows.value( it->text().toInt() ).size() > 1 );
            it->setBackground( duplicate ? QColor( 255, 180, 180 ) : QColor() );
        }
    }

    // ----------------------------------------------------------------
    // validateAndUpdateOk — disabloi OK jos duplikaattikonflikteja
    // ----------------------------------------------------------------
    void ClassEditorDialog::validateAndUpdateOk()
    {
        QSet<int> seen;
        bool hasDuplicate = false;
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            const QTableWidgetItem *it = m_table->item( r, COL_VALUE );
            if ( !it ) continue;
            const int v = it->text().toInt();
            if ( seen.contains( v ) )
            {
                hasDuplicate = true;
                break;
            }
            seen.insert( v );
        }

        if ( QPushButton *ok = m_buttonBox->button( QDialogButtonBox::Ok ) )
            ok->setEnabled( !hasDuplicate );
    }

    // ----------------------------------------------------------------
    // classDefinitions — kerää tuloksen taulukosta
    // ----------------------------------------------------------------
    QMap<int, ClassDefinition> ClassEditorDialog::classDefinitions() const
    {
        QMap<int, ClassDefinition> result;
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            const QTableWidgetItem *valueItem = m_table->item( r, COL_VALUE );
            const QTableWidgetItem *colorItem = m_table->item( r, COL_COLOR );
            const QTableWidgetItem *nameItem  = m_table->item( r, COL_NAME );

            if ( !valueItem ) continue;

            ClassDefinition def;
            def.value = valueItem->text().toInt();
            def.name  = nameItem  ? nameItem->text()                           : QString();
            def.color = colorItem ? colorItem->data( Qt::UserRole ).value<QColor>() : QColor();

            result.insert( def.value, def );
        }
        return result;
    }

    // ----------------------------------------------------------------
    // generateUniqueColor — luo värin joka on kaukana jo käytetyistä
    // ----------------------------------------------------------------
    QColor ClassEditorDialog::generateUniqueColor() const
    {
        // Kerää jo käytössä olevat HSV-sävyt (hue 0–359)
        QList<int> usedHues;
        for ( int r = 0; r < m_table->rowCount(); ++r )
        {
            const QTableWidgetItem *it = m_table->item( r, COL_COLOR );
            if ( !it ) continue;
            const QColor c = it->data( Qt::UserRole ).value<QColor>();
            if ( c.isValid() && c.saturation() > 30 )
                usedHues.append( c.hsvHue() );
        }

        // Kultainen kulma -iteraatio — 137.5° väli tuottaa tasaisen jakauman
        // Kokeillaan enintään 36 kandidaattia ja valitaan kaukaisimmaksi jäävä
        int bestHue       = 0;
        int bestMinDist   = -1;

        for ( int i = 0; i < 36; ++i )
        {
            const int candidateHue = ( i * 137 ) % 360;

            int minDist = 360;
            for ( int h : usedHues )
            {
                const int d = std::abs( candidateHue - h );
                minDist = std::min( minDist, std::min( d, 360 - d ) );
            }

            if ( usedHues.isEmpty() )
            {
                bestHue = candidateHue;
                break;
            }

            if ( minDist > bestMinDist )
            {
                bestMinDist = minDist;
                bestHue     = candidateHue;
            }
        }

        // Kyllästys 200/255, kirkkaus 210/255 — selkeä, ei liian tumma/vaalea
        return QColor::fromHsv( bestHue, 200, 210 );
    }

} // namespace MaastoPlugin
