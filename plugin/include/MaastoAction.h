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
        // Säilyttää combobox-valinnat jos samat kentät löytyvät uudesta pilvestä.
        void updateCloud( ccPointCloud *cloud );

    private:
        void populateComboBox( QComboBox *comboBox, const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );

        // Asettaa valitun scalar-kentän aktiiviseksi CloudComparen 3D-näkymässä.
        void applyColorField( const QString &fieldName );

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Combobox: scalar-kenttä jonka arvot näytetään listassa
        QComboBox          *m_valuesComboBox;

        // Combobox: scalar-kenttä jolla pisteet värjätään 3D-näkymässä
        QComboBox          *m_colorComboBox;

        QListWidget        *m_listWidget;

        // Estää updateCloud()-silmukan kun CC triggeroi onNewSelection() updateUI():n jälkeen
        bool                m_updatingCloud;
    };

    // Avaa dialogin tai nostaa sen etualalle jos jo auki.
    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
