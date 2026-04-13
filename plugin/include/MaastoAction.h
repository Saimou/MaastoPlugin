#pragma once

#include <QDialog>
#include <QComboBox>
#include <QTreeWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSet>
#include <QMap>
#include <vector>
#include <map>

#include "ClassDefinition.h"

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

        void computeClassCounts( const QString &fieldName );
        void applyPtcColors();
        void removePtcColors();

        // Yrittää ladata .ptc-tiedoston automaattisesti pistepilven kansiosta
        void tryAutoLoadPtcFile();

        // Lataa .ptc-tiedoston ja päivittää UI:n
        void loadPtcFile( const QString &filePath );

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Read class definition file -nappi ja polkunäyttö
        QPushButton        *m_readFileButton;
        QLabel             *m_ptcFileLabel;

        QComboBox          *m_valuesComboBox;
        QTreeWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;

        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        QTreeWidget        *m_visibilityListWidget;
        QPushButton        *m_selectAllVisButton;

        bool                m_updatingCloud;
        bool                m_updatingVisibility;
        bool                m_ptcColorsApplied;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;
        QCheckBox          *m_dualPolygonCheckbox;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        QMap<int, ClassDefinition> m_classDefinitions;
        QMap<int, int>             m_classCounts;

        std::map<unsigned, int> m_indexHitCount;
        std::vector<ccHObject*> m_meshObjects;
        std::vector<ccHObject*> m_highlightObjects;
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
