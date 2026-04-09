#pragma once

#include <QDialog>
#include <QComboBox>
#include <QListWidget>

class ccMainAppInterface;
class ccPointCloud;

namespace MaastoPlugin
{
    // Hakee scalar-kenttien nimet pistepilveltä aakkosjärjestyksessä.
    QStringList getScalarFieldNames( ccPointCloud *cloud );

    // Hakee uniikit arvot scalar-kentästä lajiteltuna.
    QStringList getScalarFieldValues( ccPointCloud *cloud, const QString &fieldName );

    // Ei-modaali dialogi joka päivittyy kun DB-puun valinta muuttuu.
    class MaastoDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit MaastoDialog( ccMainAppInterface *appInterface, QWidget *parent = nullptr );
        ~MaastoDialog() override = default;

        // Päivittää dialogin uuden pistepilven mukaan.
        // Säilyttää combobox-valinnan jos sama kenttä löytyy uudesta pilvestä.
        void updateCloud( ccPointCloud *cloud );

    private:
        void populateComboBox( const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;
        QComboBox          *m_comboBox;
        QListWidget        *m_listWidget;
    };

    // Avaa dialogin tai nostaa sen etualalle jos jo auki.
    // Palauttaa osoittimen dialogiin (omistajuus siirtyy kutsujalle / Qt parent-mekanismille).
    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
