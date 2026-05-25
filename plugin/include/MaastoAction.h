#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QTreeWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QColor>
#include <QSet>
#include <QMap>
#include <QVector>
#include <vector>
#include <map>
#include <unordered_set>

#include "ClassDefinition.h"
#include "ClassEditorDialog.h"
#include "CCGeom.h"
#include "ccColorTypes.h"
#include "ccPickingListener.h"
#include "PolygonDrawer.h"

class ccMainAppInterface;
class ccGLWindowInterface;
class ccPickingHub;
class ccPointCloud;
class ccHObject;

namespace MaastoPlugin
{
    QStringList getScalarFieldNames( ccPointCloud *cloud );
    QStringList getScalarFieldValues( ccPointCloud *cloud, const QString &fieldName );

    class SettingsDialog;

    class MaastoDialog : public QDialog, public ccPickingListener
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
        void populateTargetClassComboBox( int keepCode = -1 );
        void populateVisibilityList( const QString &fieldName );
        void applyColorField( const QString &fieldName );
        void applyVisibilityFilter();
        void applyShowFilter();
        void resetVisibility();
        void performClassification();

        QSet<float> getCheckedValues() const;
        ccPointCloud* createFilteredHighlight( const QSet<float>& selectedValues );
        void buildHighlightFromIndices( const std::vector<unsigned>& indices );
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

        // Viiva-työkalu: pisteet valitaan pistepoiminnalla kuten mittaus
        // pickState: 0=ei aktiivinen, 1=odottaa 1.pistettä, 2=odottaa 2.pistettä
        void startLinePicking();
        void stopLinePicking();
        void removeLineHighlights();
        void processLine();
        void copyLineRight();
        void extendLineToBBox( CCVector3& p1, CCVector3& p2 ) const;

        // Asetusten tallennus / lataus JSON-tiedostoon
        void onSaveSettings();
        void onLoadSettings( MaastoPlugin::SettingsDialog *dlg );

        // Tallentaa luokkamäärittelyn .ptc-tiedostoon
        void onSavePtc();

        // Avaa ClassEditorDialog luokkien muokkaukseen
        void onEditClasses();

        // Luo tai poistaa sub-pilvi luokalle kun pistekoko muuttuu
        void applyClassPointSize( int classCode, int size );
        void removeClassSizeSubCloud( int classCode );
        void clearAllSizeSubClouds();

        // ccPickingListener — kutsutaan kun käyttäjä klikkaa 3D-näkymässä
        void onItemPicked( const ccPickingListener::PickedItem &pi ) override;

        // Poistaa aiemman pick-korostuspisteen
        void removePickMarker();

        // Lataa asetukset suoraan tiedostopolusta (ei tiedostoselaindialogeja)
        // dlg: jos != nullptr, päivitetään myös dialogin widgetit
        void loadSettingsFromFile( const QString &path,
                                   MaastoPlugin::SettingsDialog *dlg = nullptr );

        // Tallentaa aktiivisen .las/.laz-pilven olemassa olevan tiedoston päälle
        void saveLasFile();

        // Palauttaa pistepilven alkuperäisen tiedostopolun parent-nimen perusteella
        // tai tyhjän merkkijonon jos polkua ei voida selvittää
        QString resolveCloudFilePath( ccPointCloud *cloud ) const;

        ccMainAppInterface *m_appInterface;
        ccPointCloud       *m_cloud;

        // Polku ladatulle .ptc-tiedostolle (päivitetään loadPtcFile:ssa)
        QString             m_ptcFilePath;

        // Viimeksi käytetty tallennushakemisto asetuksille
        QString             m_lastSaveDir;

        // Viimeksi ladattu/tallennettu asetustiedostopolku
        QString             m_lastSettingsFilePath;

        QComboBox          *m_valuesComboBox;
        QTreeWidget        *m_listWidget;
        QPushButton        *m_selectAllButton;
        QPushButton        *m_showAllButton;
        QPushButton        *m_editClassesButton;

        QComboBox          *m_targetClassComboBox;
        QComboBox          *m_colorComboBox;

        QTreeWidget        *m_visibilityListWidget;
        QPushButton        *m_selectAllVisButton;

        bool                m_updatingCloud;
        bool                m_updatingVisibility;
        bool                m_updatingShow;
        bool                m_ptcColorsApplied;
        bool                m_hadColorsBeforePtc;  // oliko RGB-taulukko olemassa ennen ptc-väritystä
        QVector<ccColor::Rgb> m_savedColors;        // alkuperäiset vertex-RGB-värit

        QMap<QString, bool> m_showStates;   // arvo → Show-tila (true=näkyvä)

        // Pysyvät valinnat: säilyvät listan uudelleentäytön yli
        QSet<int>           m_checkedClassCodes;  // Luokittele luokista -lista
        int                 m_lastTargetCode;     // Luokittele luokkaan -combo (luokkakoodi, -1 = ei asetettu)

        // Highlight-asetukset
        int                 m_highlightPointSize;
        QColor              m_highlightColor;

        // Mittauspisteen asetukset
        int                 m_measurePointSize;
        QColor              m_measurePointColor;

        // 3D-kappaleiden läpinäkyvyys (0=täysin peittävä, 100=täysin läpinäkyvä)
        int                 m_meshOpacity;

        PolygonDrawer      *m_polygonDrawer;
        QPushButton        *m_polygonButton;
        QPushButton        *m_highlightButton;
        QPushButton        *m_clearSelectionButton;
        QPushButton        *m_showOnlyButton;
        QPushButton        *m_lockViewButton;
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

        // Luokkakohtaiset pistekoot (0 = käytä globaalia oletusta)
        QMap<int, int>             m_classPointSizes;
        // Luokkakohtaiset sub-pilvet (luokkakoodi → väliaikainen ccPointCloud*)
        QMap<int, ccPointCloud*>   m_sizeSubClouds;

        // Prismojen tallennusrakenne uudelleenlaskentaa varten
        struct PrismData
        {
            std::vector<PolygonDrawer::Point2D> vertices;
            double nearDist;
            double farDist;
            std::vector<unsigned> insideIndices; // piirtohetken oikeat pisteindeksit
        };

        std::map<unsigned, int>   m_indexHitCount;
        std::vector<ccHObject*>   m_meshObjects;
        std::vector<PrismData>    m_prismData;
        std::vector<ccHObject*>   m_highlightObjects;

        // "Näytä vain valinta" -tila
        bool                          m_showOnlyMode;           // onko tila päällä
        bool                          m_lockViewMode;           // onko näkymä lukittu
        ccPointCloud                 *m_selectionOnlyCloud;     // väliaikainen pilvi "vain valinta" -tilaan
        ccGLWindowInterface          *m_selectionGLWindow;      // MDI 3D-ikkuna leikkaukselle
        QWidget                      *m_selectionGLWidget;      // MDI-ikkunan Qt-widget
        std::vector<unsigned>         m_selectionIndices;       // valittujen pisteiden indeksit m_cloud:ssa

        // Ikkuna johon mittaus/viiva-työkalu on kytketty (tallennetaan start-hetkellä)
        ccGLWindowInterface          *m_workingGLWindow;

        // "Lukitse näkymä" -tila
        std::unordered_set<unsigned>  m_lockedIndices;          // snapshot lukitushetkellä
        std::map<unsigned, int>       m_preLockedHitCount;      // m_indexHitCount ennen lukitusta
        size_t                        m_preLockedPrismCount;    // prismojen määrä ennen lukitusta

        // Tallenna-nappi
        QPushButton        *m_saveButton;

        // Point picking
        ccPickingHub       *m_pickingHub;
        QPushButton        *m_pickPointButton;
        QLabel             *m_pickInfoLabel;
        ccHObject          *m_pickMarker;            // väliaikainen korostuspiste

        // Viiva-työkalu
        QPushButton        *m_drawLineButton;        // "Piirrä viiva" checkable toggle
        QComboBox          *m_lineAxisComboBox;      // "X" / "Y" / "Z"
        QDoubleSpinBox     *m_lineThicknessSpinBox;  // paksuus metreissä, default 1.0
        QPushButton        *m_copyLineRightButton;   // "Kopioi oikealle"
        QCheckBox          *m_extendLineToBBoxCheckBox; // "Jatka viivan pituutta"

        int                 m_linePickState;          // 0=ei aktiivinen, 1=odottaa P1, 2=odottaa P2
        CCVector3           m_lineP1;                 // 1. valittu piste 3D-maailmassa
        CCVector3           m_lineP2;                 // 2. valittu piste 3D-maailmassa
        ccHObject          *m_linePoint1Highlight;   // highlight-dot P1:lle
        ccHObject          *m_linePoint2Highlight;   // highlight-dot P2:lle
    };

    MaastoDialog *openDialog( ccMainAppInterface *appInterface, ccPointCloud *cloud );
}
