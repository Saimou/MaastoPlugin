#pragma once

#include <QDialog>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QDoubleSpinBox>

class ccMainAppInterface;
class ccPointCloud;
class PolygonDrawer;

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

        // Palauttaa true jos päivitys on kesken — estää onNewSelection()-silmukan
        bool isUpdatingCloud() const { return m_updatingCloud; }

    private:
        void populateComboBox( QComboBox *comboBox, const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );
        void populateTargetClassComboBox( const QString &keepValue = QString() );

        // Asettaa valitun scalar-kentän aktiiviseksi CloudComparen 3D-näkymässä.
        void applyColorField( const QString &fieldName );

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Combobox: scalar-kenttä jonka arvot näytetään listassa
        QComboBox          *m_valuesComboBox;

        // Lista: uniikit arvot valitusta scalar-kentästä (checkbox-monivalinta)
        QListWidget        *m_listWidget;

        // Combobox: mihin luokkaan pisteet luokitellaan
        QComboBox          *m_targetClassComboBox;

        // Combobox: scalar-kenttä jolla pisteet värjätään 3D-näkymässä
        QComboBox          *m_colorComboBox;

        // Estää updateCloud()-silmukan kun CC triggeroi onNewSelection() updateUI():n jälkeen
        bool                m_updatingCloud;

        // Polygon-piirtotyökalu
        PolygonDrawer      *m_polygonDrawer;

        // Piirrä polygon -nappi (checkable)
        QPushButton        *m_polygonButton;

        // Etäisyysasetukset 3D-kappaleen etukanteen ja takakanteen
        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;
    };

    // Avaa dialogin tai nostaa sen etualalle jos jo auki.
    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
