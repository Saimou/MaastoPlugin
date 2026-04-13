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
        ~MaastoDialog() override;

        void updateCloud( ccPointCloud *cloud );
        bool isUpdatingCloud() const { return m_updatingCloud; }

    private:
        void populateComboBox( QComboBox *comboBox, const QString &keepField = QString() );
        void populateColorComboBox( const QString &keepField = QString() );
        void populateValueList( const QString &fieldName );
        void populateTargetClassComboBox( const QString &keepValue = QString() );
        void populateVisibilityList( const QString &fieldName );
        void applyColorField( const QString &fieldName );
        void applyVisibilityFilter();
        void resetVisibility();
        void performClassification();

        QSet<float> getCheckedValues() const;
        ccPointCloud* createFilteredHighlight( const QSet<float>& selectedValues );
        void removeHighlightObjects();
        void refreshHighlights();

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Scalar field -valitsin
        QComboBox          *m_valuesComboBox;

        // Luokat-lista (luokitteluun)
        QListWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;

        QComboBox          *m_targetClassComboBox;

        // Pisteiden väritys
        QComboBox          *m_colorComboBox;

        // Näkyvät luokat -lista (visibility mask)
        QListWidget        *m_visibilityListWidget;
        QPushButton        *m_selectAllVisButton;

        bool                m_updatingCloud;
        bool                m_updatingVisibility;  // estää signaalisilukat visibility-päivityksessä

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        std::vector<unsigned>   m_highlightedIndices;
        std::vector<ccHObject*> m_meshObjects;
        std::vector<ccHObject*> m_highlightObjects;
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
