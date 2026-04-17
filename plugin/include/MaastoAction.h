#pragma once

#include <QDialog>
#include <QComboBox>
#include <QTreeWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QColor>
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
        void applyShowFilter();
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

        // Poistaa kaikki piirretyt 3D-muodot ja highlight-pistepilvet
        void clearSelection();

        // "Näytä vain valinta" -tila
        void enableShowOnlyMode();
        void disableShowOnlyMode();
        void removeSelectionOnlyCloud();

        // Mittaus: piste valitaan 3D-ikkunasta
        // state: 0=ei mittausta, 1=odottaa pistettä, 2=piste valittu (odottaa hyväksyntää)
        void startMeasure( bool isNear );
        void stopMeasure();
        void removeMeasureHighlight();

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Polku ladatulle .ptc-tiedostolle (päivitetään loadPtcFile:ssa)
        QString             m_ptcFilePath;

        QComboBox          *m_valuesComboBox;
        QTreeWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;
        QPushButton        *m_showAllButton;

        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        QTreeWidget        *m_visibilityListWidget;
        QPushButton        *m_selectAllVisButton;

        bool                m_updatingCloud;
        bool                m_updatingVisibility;
        bool                m_updatingShow;
        bool                m_ptcColorsApplied;

        QMap<QString, bool> m_showStates;   // arvo → Show-tila (true=näkyvä)

        // Highlight-asetukset
        int                 m_highlightPointSize;
        QColor              m_highlightColor;

        // Mittauspisteen asetukset
        int                 m_measurePointSize;
        QColor              m_measurePointColor;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;
        QPushButton        *m_clearSelectionButton;
        QPushButton        *m_showOnlyButton;
        QPushButton        *m_fileButton;
        QSpinBox           *m_minPolygonCountSpinBox;

        QDoubleSpinBox     *m_nearDistSpinBox;
        QDoubleSpinBox     *m_farDistSpinBox;

        // Mittaa-napit (near ja far)
        QPushButton        *m_measureNearButton;
        QPushButton        *m_measureFarButton;

        // Mittaustila: 0=ei mittausta, 1=odottaa pistettä, 2=piste valittu
        int                 m_measureState;   // 0/1/2
        bool                m_measuringNear;  // true=near, false=far
        double              m_measuredX;      // valitun pisteen koordinaatti
        double              m_measuredY;
        double              m_measuredZ;
        ccHObject          *m_measureHighlight; // punainen väliaikainen piste

        QMap<int, ClassDefinition> m_classDefinitions;
        QMap<int, int>             m_classCounts;

        std::map<unsigned, int>   m_indexHitCount;
        std::vector<ccHObject*>   m_meshObjects;
        std::vector<ccHObject*>   m_highlightObjects;

        // "Näytä vain valinta" -tila
        bool                      m_showOnlyMode;       // onko tila päällä
        ccPointCloud             *m_selectionOnlyCloud; // väliaikainen pilvi "vain valinta" -tilaan
        std::vector<unsigned>     m_selectionIndices;   // valittujen pisteiden indeksit m_cloud:ssa
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
