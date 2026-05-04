#pragma once

#include <QDialog>
#include <QColor>
#include <QString>

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QLabel;

namespace MaastoPlugin
{
    class SettingsDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit SettingsDialog( int            pointSize,
                                 const QColor  &color,
                                 const QString &currentPtcPath,
                                 int            measurePointSize,
                                 const QColor  &measurePointColor,
                                 int            meshOpacity,
                                 double         nearDist,
                                 double         farDist,
                                 double         lineThickness,
                                 int            minPolyCount,
                                 const QString &lineAxis,
                                 const QString &defaultSaveDir,
                                 const QString &currentSettingsPath = QString(),
                                 QWidget       *parent = nullptr );

        int     pointSize()         const;
        QColor  color()             const;
        int     measurePointSize()  const;
        QColor  measurePointColor() const;
        int     meshOpacity()       const;

        // Päivittää dialogin widgetit ladatuilla arvoilla
        void applyLoadedSettings( int           pointSize,
                                  const QColor &color,
                                  int           measurePointSize,
                                  const QColor &measureColor,
                                  int           meshOpacity );

        // Näyttää ladatun asetustiedoston polun dialogissa
        void setLoadedSettingsPath( const QString &path );

    signals:
        void ptcFileLoaded( const QString &filePath );
        void savePtcRequested();
        void saveRequested();
        void loadRequested();

    private:
        QSpinBox       *m_pointSizeSpinBox;
        QPushButton    *m_colorButton;
        QColor          m_color;
        QLabel         *m_ptcFileLabel;

        QSpinBox       *m_measurePointSizeSpinBox;
        QPushButton    *m_measurePointColorButton;
        QColor          m_measurePointColor;

        QSpinBox       *m_meshOpacitySpinBox;

        QLabel         *m_settingsFileLabel;

        void updateColorButton();
        void updateMeasureColorButton();
    };
}
