#pragma once

#include <QDialog>
#include <QColor>
#include <QString>

class QSpinBox;
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
                                 QWidget       *parent = nullptr );

        int    pointSize()         const;
        QColor color()             const;
        int    measurePointSize()  const;
        QColor measurePointColor() const;

    signals:
        void ptcFileLoaded( const QString &filePath );

    private:
        QSpinBox    *m_pointSizeSpinBox;
        QPushButton *m_colorButton;
        QColor       m_color;
        QLabel      *m_ptcFileLabel;

        QSpinBox    *m_measurePointSizeSpinBox;
        QPushButton *m_measurePointColorButton;
        QColor       m_measurePointColor;

        void updateColorButton();
        void updateMeasureColorButton();
    };
}
