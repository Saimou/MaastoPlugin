#pragma once

#include <QDialog>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <vector>

class ccMainAppInterface;
class ccPointCloud;
class ccHObject;
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

        void updateCloud( ccPointCloud *cloud );

        bool isUpdatingCloud() const { return m_updatingCloud; }

    private:
        void populateComboBox( QComboBox *comboBox, const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );
        void populateTargetClassComboBox( const QString &keepValue = QString() );
        void applyColorField( const QString &fieldName );

        // Luokittelee prisman sisällä olevat pisteet joilla on valittu arvo
        void performClassification();

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        QComboBox          *m_valuesComboBox;
        QListWidget        *m_listWidget;
        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        bool                m_updatingCloud;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        // Prisman sisällä olevien pisteiden alkuperäiset indeksit pistepilvessä
        std::vector<unsigned>   m_highlightedIndices;

        // Kaikki luodut 3D-kappaleet ja highlight-pilvet — poistetaan luokittelun jälkeen
        std::vector<ccHObject*> m_volumeObjects;
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
