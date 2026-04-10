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

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        QComboBox          *m_valuesComboBox;
        QListWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;   // toggle: Valitse kaikki / Poista valinnat
        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        bool                m_updatingCloud;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        std::vector<unsigned>   m_highlightedIndices;
        std::vector<ccHObject*> m_volumeObjects;
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
