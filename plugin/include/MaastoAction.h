#pragma once

#include <QDialog>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSet>
#include <vector>

class ccMainAppInterface;
class ccPointCloud;
class ccHObject;
class PolygonDrawer;

namespace MaastoPlugin
{
    QStringList getScalarFieldNames( ccPointCloud *cloud );
    QStringList getScalarFieldValues( ccPointCloud *cloud, const QString &fieldName );

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
        void populateColorComboBox( const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );
        void populateTargetClassComboBox( const QString &keepValue = QString() );
        void applyColorField( const QString &fieldName );
        void performClassification();

        // Palauttaa ☑-tilan arvot Arvot-listasta
        QSet<float> getCheckedValues() const;

        // Luo highlight-pilven m_highlightedIndices:stä suodatettuna valituilla arvoilla
        ccPointCloud* createFilteredHighlight( const QSet<float>& selectedValues );

        // Poistaa kaikki highlight-pilvet DB:stä
        void removeHighlightObjects();

        // Päivittää highlight-pilvet vastaamaan nykyistä Arvot-valintaa
        void refreshHighlights();

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        QComboBox          *m_valuesComboBox;
        QListWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;
        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        bool                m_updatingCloud;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        // Kaikki prisman sisällä olevat pisteiden indeksit (suodattamaton)
        std::vector<unsigned>   m_highlightedIndices;

        // 3D-prismat — poistetaan luokittelun jälkeen
        std::vector<ccHObject*> m_meshObjects;

        // Highlight-pilvet — päivitetään kun Arvot-valinta muuttuu
        std::vector<ccHObject*> m_highlightObjects;
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
