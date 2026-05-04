#pragma once

#include <QDialog>
#include <QMap>
#include <QColor>

#include "ClassDefinition.h"

class QTableWidget;
class QPushButton;
class QDialogButtonBox;
class QSpinBox;
class QLineEdit;

namespace MaastoPlugin
{
    // ----------------------------------------------------------------
    // ClassEditorDialog
    // Mahdollistaa luokkamäärittelyn (ClassDefinition) muokkauksen ja
    // uusien luokkien lisäämisen. Ei tallenna .ptc-tiedostoon —
    // kutsuja tallentaa tarvittaessa onSavePtc():lla.
    // ----------------------------------------------------------------
    class ClassEditorDialog : public QDialog
    {
        Q_OBJECT

    public:
        // defs: nykyiset luokat (kopioidaan sisäisesti)
        explicit ClassEditorDialog( const QMap<int, ClassDefinition> &defs,
                                    QWidget *parent = nullptr );

        // Palauttaa muokatun / täydennetyn luokkamapin kun dialogi on hyväksytty
        QMap<int, ClassDefinition> classDefinitions() const;

    private slots:
        void onAddClass();
        void onCellDoubleClicked( int row, int col );
        void onValueChanged( int row );
        void validateAndUpdateOk();

    private:
        // Täyttää taulukon m_defs-sisällöstä
        void populateTable();

        // Lisää yksi rivi taulukkoon annetulla ClassDefinition:lla
        void addTableRow( const ClassDefinition &def );

        // Luo automaattisen värin — yrittää välttää jo käytössä olevia sävyjä
        QColor generateUniqueColor() const;

        // Kerää taulukosta ClassDefinition-mapin (tarkistaa konfliktit)
        // Palauttaa false jos on duplicate value -konflikti
        bool collectDefinitions( QMap<int, ClassDefinition> &out ) const;

        // Rakentaa väriswatch-pixmapin annetulla värillä
        static QPixmap colorSwatch( const QColor &c );

        QTableWidget      *m_table;
        QPushButton       *m_addButton;
        QDialogButtonBox  *m_buttonBox;

        // Alkuperäinen kopio (dialogin avauksen hetkellä)
        QMap<int, ClassDefinition> m_original;
    };

} // namespace MaastoPlugin
